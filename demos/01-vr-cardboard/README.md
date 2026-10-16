# Demo 01 · VR con Cardboard

Ves por los ojos de Stack-chan con unas Google Cardboard y mueves su cabeza girando la tuya o con un mando de PS4.

```
Robot (cámara) --UDP 5600, JPEG--> Portátil (pasarela) --UDP 5601, JPEG--> Móvil (app Stack-chan VR)
Robot (servos) <-- posición ------ Portátil (pasarela) <-- control/sensores -- Móvil
```

**Por qué hay un portátil en medio.** Al ESP32-S3 le cuesta enviar vídeo a varios sitios y procesarlo. El portátil reenvía el vídeo y, si quieres, lo procesa antes con OpenCV: más contraste, detección de caras, grabación. Es donde se programa durante el curso.

## Necesitas

- **El robot.**
- **El portátil**, con el curso instalado (`setup.sh`).
- **Un móvil Android** con la app [Stack-chan VR](https://github.com/rhyloo/StackChan-VR) y unas Cardboard.
- **Opcional, un mando de PS4** emparejado con el **móvil** por Bluetooth (no con el robot: el ESP32-S3 solo tiene Bluetooth LE y el mando necesita Bluetooth clásico).

## Paso a paso

1. **Flashea el robot** (USB-C), desde la raíz del repo:
   ```bash
   ./tools/flash.sh demos/01-vr-cardboard/firmware
   ```
   En la pantalla verás su IP, su nombre (`stackchan-XXXX`) y «Buscando el portátil».
2. **Arranca la pasarela:**
   ```bash
   demos/01-vr-cardboard/run_relay.sh            # Windows: demos\01-vr-cardboard\run_relay.bat
   ```
   El robot pasa a «Portátil 192.168.x.x». Mientras nadie mira, el robot no envía vídeo, para ahorrar batería.
3. **Abre la app** en el móvil. El portátil aparece solo en la lista. Pulsa *Enter VR*, mete el móvil en las Cardboard y mira alrededor.

Sin móvil, `run_relay.sh --show` muestra el vídeo en una ventana del portátil. Sin robot, `.venv/bin/python demos/01-vr-cardboard/fake_robot.py` simula uno.

### Opciones de la pasarela

| Opción | Qué hace |
|---|---|
| `--show` | Muestra el vídeo en una ventana (con `q` se cierra) |
| `--enhance` | Más contraste: la cámara GC0308 es oscura en interiores |
| `--upscale 2` | Duplica la resolución antes de enviarla |
| `--record videos` | Graba en `videos/` lo que se ve |
| `--robot stackchan-XXXX` | Empareja solo con ese robot, si hay varios en la red |
| `--sensors accelerometer=20,light=2 --events shake,touch` | Pide sensores al móvil (en la app: *Sensors → Share with laptop*) |
| `--always-video` | El robot envía vídeo aunque nadie mire (gasta más batería) |

### Controles en las gafas

| Acción | Cómo |
|---|---|
| Recentrar | Tocar la pantalla (o el botón de las Cardboard), o ✕ en el mando |
| Mostrar/ocultar el HUD | Doble toque, o △ |
| Cambiar el modo de control | □. Modos: solo ver, cabeza, mando, cabeza + mando. |
| Salir | Pulsación larga, o ○ |
| Mover la cabeza con el mando | Joystick izquierdo; R2 para ir más despacio |

## Dónde programar

| Fichero | Para qué |
|---|---|
| `relay/processing.py` | Cada fotograma pasa por `process()` antes de ir al móvil: filtros, detección de caras, superponer datos… |
| `relay/hooks.py` | Reaccionar a botones del mando (L1, R1, flechas…), eventos (agitar el móvil, tocar la pantalla) y sensores, y mandar órdenes al robot (`tone`, `leds`, `text`, `home`) o al móvil (`hud`, `vibrate`, `say`) |
| `firmware/src/main.cpp` | La pantalla del robot y el bucle principal |
| `../../lib/StackChanLink/` | El enlace UDP con el portátil (compartido con la demo 02) |
| `../../lib/StackChanHW/` | Servos, LEDs, batería y cámara |

Todos los mensajes están en [PROTOCOL.md](PROTOCOL.md).

## Batería

La base tiene una batería de solo 550 mAh. Lo que más gasta es la radio Wi-Fi (sin ahorro de energía, para que el vídeo no tenga tirones), la cámara y la compresión JPEG. Por eso:
- **El vídeo se pausa** cuando no hay ningún móvil conectado, ni `--show` ni `--record`.
- **La línea de estado de la pasarela muestra el consumo real** (`mA`), medido con el INA226 de la base. Un valor negativo significa que está cargando.
- **Hay órdenes para medir cuánto ahorra cada cosa:** `brightness 0-255` (pantalla) y `wifi_sleep 1` (ahorro de Wi-Fi; añade latencia). Mándalas desde `hooks.py` con `robot.cmd("brightness 20")`.

Para clases largas, deja el robot conectado por USB-C.

## Problemas frecuentes

| Síntoma | Solución |
|---|---|
| El robot se queda en «WiFi: conectando…» | La red tiene que ser de 2,4 GHz. Revisa nombre y contraseña con `./setup.sh --wifi` y vuelve a flashear. |
| El robot dice «Buscando el portátil» | La pasarela no está arrancada, el portátil está en otra red, el cortafuegos bloquea UDP 5600-5601 ([docs/ENTORNO.md](../../docs/ENTORNO.md)), o la red aísla a los clientes. |
| La app no encuentra el portátil | Lo mismo. También puedes escribir la IP del portátil a mano en la app. |
| Dos pasarelas se pelean por el robot | El robot se queda con la primera que le habla. Cierra la otra (Ctrl+C) o usa `--robot`. |
| La imagen es muy oscura | `--enhance`, y más luz. |
