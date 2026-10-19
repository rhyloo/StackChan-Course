# Protocolo UDP de las demos

Todo es UDP en la red local, en orden de bytes **big-endian**. Cada datagrama empieza con la misma cabecera de 4 bytes y mide como máximo **1200 bytes** (así nunca se fragmenta en IP).

| Offset | Tamaño | Campo | Valor |
|---|---|---|---|
| 0 | 2 | magic | `0x53 0x56` ("SV") |
| 2 | 1 | versión | `1` |
| 3 | 1 | tipo | ver tablas |

Hay dos enlaces:

- **Móvil ↔ portátil**, puerto **5601** del portátil. Lo define la app ([rhyloo/StackChan-VR](https://github.com/rhyloo/StackChan-VR), `net/Protocol.kt`): no lo cambies sin cambiar la app.
- **Robot ↔ portátil**, puerto **5600** en los dos lados. Es nuestro: `lib/StackChanLink/src/link.cpp` (robot) y `lib/python/stackchan/protocol.py` (portátil) deben cambiar a la vez.
- **Demo 03**: el receptor envía por broadcast a UDP **5602** (ver el final).

## Robot ↔ portátil (5600)

| Tipo | Nombre | Sentido | Contenido tras la cabecera |
|---|---|---|---|
| `0x20` | HELLO | robot → portátil | JSON `{"name":"stackchan-4F48","fw":"1.0.0","ip":"192.168.0.24","limits":{"pan":[-2.2689,2.7925],"tilt":[-0.05,0.48]}}`. Cada 1 s: a `255.255.255.255:5600` mientras no tiene portátil y directamente al portátil cuando ya lo tiene. |
| `0x21` | ACK | portátil → robot | 1 byte de flags; bit 0 = alguien está mirando (móvil conectado, `--show` o `--record`). Respuesta a cada HELLO del robot emparejado, y en cuanto cambian los flags. Un ACK vacío significa «envía vídeo siempre». |
| `0x22` | VIDEO | robot → portátil | Igual que VIDEO_CHUNK del móvil: `frameId` u32, `chunkIndex` u16, `chunkCount` u16, `captureMs` u32 y hasta 1184 bytes de JPEG. |
| `0x23` | TELEMETRY | robot → portátil | JSON a 2 Hz: `batt_v`, `batt_pct`, `current_ma` (INA226; negativo = cargando), `video` (0/1), `pan`, `tilt` (rad), `fps`, `jpeg_bytes`, `rssi`, `heap`, `psram`, `send_errors`, `quality`. |
| `0x24` | POSE | portátil → robot | Binario: `seq` u32, `pan` f32, `tilt` f32 (radianes, IEEE-754 big-endian). |
| `0x25` | CMD | portátil → robot | Texto ASCII: `home`, `torque_on`, `torque_off`, `quality <5-95>`, `tone <Hz> <ms>`, `leds <r> <g> <b>`, `text <mensaje>`, `brightness <0-255>`, `wifi_sleep <0/1>`. Demo 02: `mic on`, `mic off`. Demo 05: `nfc write <texto UTF-8>`, `nfc read`, `nfc cancel`. Demo 07: `photo ack <id>`, `photo status <texto>`, `photo error <texto>`. |
| `0x26` | AUDIO_IN | robot → portátil | Demo 02. `seq` u32 + PCM 16 bits little-endian, 16 kHz mono, 20 ms (640 bytes). |
| `0x27` | AUDIO_OUT | portátil → robot | Demo 02. `seq` u32 + PCM 16 bits little-endian, 24 kHz mono, hasta 1180 bytes. El robot lo acumula 200 ms antes de reproducir. |
| `0x28` | NFC | robot → portátil | Demo 05. JSON. Tarjeta leída: `{"event":"tag","ok","uid","type","ndef","text"}`. Resultado de `nfc write`: `{"event":"write","ok","uid","text","error"}`. |
| `0x29` | PHOTO | robot → portátil | Demo 07. Igual que VIDEO: `photoId` u32, `chunkIndex` u16, `chunkCount` u16, `captureMs` u32 y hasta 1184 bytes de JPEG. El portátil confirma con `photo ack <id>`; si no, el robot la reenvía (3 intentos). |
| `0x2A` | PHOTO_EVENT | robot → portátil | Demo 07. JSON: `{"event":"filter","photo","name"}`, `{"event":"bitmap_ok","job"}`, `{"event":"printed","ok","error"}`. |
| `0x2B` | PRINT_BITMAP | portátil → robot | Demo 07. `job` u32, `chunkIndex` u16, `chunkCount` u16, `width` u16 (384), `height` u16 y hasta 1000 bytes de filas de 1 bit (48 bytes por fila, bit menos significativo primero, 1 = negro). El portátil reenvía hasta que llega `bitmap_ok`. |

Reglas:

- **Emparejamiento.** El robot se empareja con el primer portátil que le manda ACK, POSE o CMD, e ignora a los demás mientras ese siga hablándole (3 s). El portátil se empareja con el robot de `--robot` o, si no se indica, con el primero que oye. Así varios equipos comparten la Wi-Fi del aula.
- El robot solo envía vídeo mientras el portátil le ha hablado en los últimos 3 s **y** el último ACK dice que alguien mira (ahorra batería).
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

## Demo 03: balizas (5602)

El receptor (robot) envía 5 veces por segundo, por broadcast a UDP 5602, un datagrama con la cabecera común, tipo `0x30`, y JSON:

```json
{"robot":"DC:54:75:..","t":123456,"beacons":[{"id":1,"rssi":-61.4,"raw":-63,"n":812}]}
```
`rssi` es la media exponencial; `raw`, la última medida; `n`, los paquetes recibidos de esa baliza. Los mensajes ESP-NOW entre balizas y robot están en `demos/03-espnow-balizas/espnow_msg.h`.
