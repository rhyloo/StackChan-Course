# Protocolo Stack-chan VR

Todo es UDP en la red local, en orden de bytes **big-endian**. Cada datagrama empieza con la misma cabecera de 4 bytes y mide como máximo **1200 bytes** (así nunca se fragmenta en IP).

| Offset | Tamaño | Campo | Valor |
|---|---|---|---|
| 0 | 2 | magic | `0x53 0x56` ("SV") |
| 2 | 1 | versión | `1` |
| 3 | 1 | tipo | ver tablas |

Hay dos enlaces:

- **Móvil ↔ portátil**, puerto **5601** del portátil. Lo define la app ([rhyloo/StackChan-VR](https://github.com/rhyloo/StackChan-VR), `net/Protocol.kt`): no lo cambies sin cambiar la app.
- **Robot ↔ portátil**, puerto **5600** en los dos lados. Es nuestro: `firmware/src/link.cpp` y `relay/protocol.py` deben cambiar a la vez.

## Robot ↔ portátil (5600)

| Tipo | Nombre | Sentido | Contenido tras la cabecera |
|---|---|---|---|
| `0x20` | HELLO | robot → portátil | JSON `{"name":"stackchan-4F48","fw":"1.0.0","ip":"192.168.0.24","limits":{"pan":[-2.2689,2.7925],"tilt":[-0.05,0.48]}}`. Cada 1 s: a `255.255.255.255:5600` mientras no tiene portátil y directamente al portátil cuando ya lo tiene. |
| `0x21` | ACK | portátil → robot | Vacío. Respuesta a cada HELLO del robot emparejado. |
| `0x22` | VIDEO | robot → portátil | Igual que VIDEO_CHUNK del móvil: `frameId` u32, `chunkIndex` u16, `chunkCount` u16, `captureMs` u32 y hasta 1184 bytes de JPEG. |
| `0x23` | TELEMETRY | robot → portátil | JSON a 2 Hz: `batt_v`, `batt_pct`, `pan`, `tilt` (rad), `fps`, `jpeg_bytes`, `rssi`, `heap`, `psram`, `send_errors`, `quality`. |
| `0x24` | POSE | portátil → robot | Binario: `seq` u32, `pan` f32, `tilt` f32 (radianes, IEEE-754 big-endian). |
| `0x25` | CMD | portátil → robot | Texto ASCII: `home`, `torque_on`, `torque_off`, `quality <5-95>`, `tone <Hz> <ms>`, `leds <r> <g> <b>`, `text <mensaje>`. |

Reglas:

- **Emparejamiento.** El robot se empareja con el primer portátil que le manda ACK, POSE o CMD, e ignora a los demás mientras ese siga hablándole (3 s). El portátil se empareja con el robot de `--robot` o, si no se indica, con el primero que oye. Así varios equipos comparten la Wi-Fi del aula.
- El robot solo envía vídeo mientras el portátil le ha hablado en los últimos 3 s.
- POSE con `seq` antiguo o repetido se descarta, salvo tras 2 s sin POSE (porque el relay reinicia el contador al arrancar).
- El robot limita pan y tilt a sus límites y aplica un limitador de velocidad (unos 94°/s en pan y 47°/s en tilt), porque los saltos bruscos provocan picos de corriente que reinician la base.
- La GC0308 no codifica JPEG por hardware: el robot captura RGB565 QVGA (320×240) y lo comprime con `frame2jpg`.

## Móvil ↔ portátil (5601)

Resumen; el detalle está en la especificación de la app.

| Tipo | Nombre | Sentido |
|---|---|---|
| `0x13` | DISCOVER | móvil → broadcast, cada 1 s mientras busca |
| `0x04` | ANNOUNCE | portátil → móvil: `{"name","proto":1,"limits"}` |
| `0x10` | HELLO | móvil → portátil, cada 1 s; sin HELLO durante 3 s el portátil deja de enviarle |
| `0x01` | VIDEO_CHUNK | portátil → móvil |
| `0x02` | TELEMETRY | portátil → móvil, 2 Hz: `robot_online`, `batt_v`, `batt_pct`, `pan`, `tilt`, `robot_fps`, `relay_fps`, `limits` |
| `0x12` / `0x03` | PING / PONG | latencia (RTT) |
| `0x11` | CONTROL | móvil → portátil, hasta 20 Hz: `{"seq","mode","pan","tilt"}` → se convierte en POSE |
| `0x14` | BUTTON | móvil → portátil: botones del mando → `hooks.on_button` |
| `0x15` | COMMAND | móvil → portátil: `home` → CMD `home` |
| `0x05` | SUBSCRIBE | portátil → móvil: qué sensores quiere y a qué frecuencia |
| `0x16`–`0x19` | SENSOR_DATA, DEVICE_INFO, EVENT, BLE_SCAN | móvil → portátil → `hooks.on_sensors` / `hooks.on_event` |
| `0x06` | ACTUATE | portátil → móvil: `hud_message`, `vibrate`, `torch`, `tone`, `say`, `device_info` |
