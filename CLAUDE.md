# Curso Stack-chan: guía para asistentes de IA

Repositorio de un curso para programar Stack-chan, un M5Stack CoreS3 (ESP32-S3) sobre la base K151 con dos servos SCS0009. Lee `README.md` y el README de la demo con la que vayas a trabajar antes de tocar código. Para cualquier cosa del hardware (pines, I2C, servos, límites) la referencia es `docs/ROBOT.md`; las hojas de datos están en `docs/hardware/`.

## Idioma

- Habla con el usuario en español de España.
- READMEs, documentación y textos que ve la persona: en español.
- Identificadores y comentarios del código: en inglés.

## Estructura

| Ruta | Contenido |
|---|---|
| `demos/01-vr-cardboard/` | Cámara del robot → portátil → móvil (app [rhyloo/StackChan-VR](https://github.com/rhyloo/StackChan-VR), repo aparte) y pose de la cabeza de vuelta. |
| `demos/02-voz-gemini/` | La 01 más voz: micro del robot → pasarela → Gemini Live (llamadas a funciones) → altavoz del robot. |
| `demos/03-espnow-balizas/` | Balizas ESP-NOW en ESP32 DevKit, receptor en el robot y trilateración en el portátil. |
| `demos/04-nfc/` | Lector NFC: el ST25R3916 de la base lee etiquetas NFC-A y el robot enseña su contenido NDEF en pantalla. |
| `demos/05-nfc-voz/` | Demo 02 + 04: Gemini escribe por voz un texto NDEF en la tarjeta (`escribir_tarjeta`, `leer_tarjeta`). Reutiliza `audio.cpp` y `voice.py` de la demo 02. |
| `lib/StackChanHW/` | C++ compartido: servos, LEDs, INA226, cámara. Límites y calibración en `stackchan_limits.h`. |
| `lib/StackChanLink/` | C++ compartido: enlace UDP robot ↔ pasarela y sus puntos de extensión para las demos. |
| `lib/python/stackchan/` | Python compartido: protocolo y pasarela base. Cada demo aporta `hooks.py` y `processing.py`. |
| `config/wifi_secrets.h` | Wi-Fi. La genera `setup`, está en `.gitignore` y nunca se sube. |

El protocolo de todas las demos está en `demos/01-vr-cardboard/PROTOCOL.md`. Si cambias un mensaje del robot, cambia a la vez `lib/StackChanLink/src/link.cpp` y `lib/python/stackchan/protocol.py`. La parte móvil ↔ portátil (UDP 5601) la fija la app: no la cambies sin cambiar la app.

## Comandos

```bash
demos/05-nfc-voz/run.sh [--sin-flashear]                # una demo en un paso: compila, flashea y arranca el portátil
./setup.sh --no-system                                # entorno .venv + toolchains + compila todo
.venv/bin/pio run -d demos/02-voz-gemini/firmware     # compilar un firmware
./tools/flash.sh demos/02-voz-gemini/firmware --no-monitor   # flashear (sin args lista los firmwares)
./tools/flash.sh demos/03-espnow-balizas/baliza --env baliza2 /dev/ttyUSB0
.venv/bin/python -m unittest discover -s lib/python/tests     # tests del protocolo
demos/02-voz-gemini/run_relay.sh --debug              # pasarela; --debug guarda logs/relay-*.log
.venv/bin/python tools/serial_log.py demos/02-voz-gemini/firmware   # serie del robot → logs/robot-*.log
```

Plataforma fijada: `espressif32@7.1.2` (Arduino-ESP32 2.0.17, ESP-IDF 4.4.7), M5Unified 0.2.21.

## Reglas que no se ven en el código

- **La GC0308 no hace JPEG por hardware.** Captura en RGB565 y comprime con `frame2jpg`; `PIXFORMAT_JPEG` falla.
- **Respeta los límites de la cabeza** de `stackchan_limits.h` y el limitador de velocidad de los servos. Por debajo del tilt mínimo la barbilla choca con la base.
- **No subas los LEDs de la base por encima de `LED_MAX_LEVEL`.** A plena potencia, junto con el altavoz y la Wi-Fi, el PMIC cortó la alimentación del robot.
- **I2C y pantalla solo desde la tarea `loop()`.** Las tareas de red dejan órdenes en `PendingCommands`.
- **Reparto de núcleos:** el 0 lleva la red y la compresión de vídeo; el 1, servos, audio y pantalla.
- **El micro y el altavoz del CoreS3 comparten I2S:** el audio es half-duplex.
- **En modo *Head*, la app reenvía la pose cada 500 ms.** Para mover la cabeza por código, llama antes a `robot.pause_phone_control()`.
- **OpenCV está fijado a la 4.x.** La 5 quitó `CascadeClassifier`.
- **El robot se empareja con la primera pasarela que le habla.** Dos pasarelas abiertas se pelean por él.
- **Secretos:** nunca imprimas ni subas la contraseña de la Wi-Fi ni `GEMINI_API_KEY`.
- **Para cerrar procesos, usa su PID.** `pkill -f <patrón>` cierra también la propia shell que lo ejecuta.

## Forma de trabajar

Hay hardware real y una persona que lo prueba. Compila, flashea si el robot está conectado, haz como mucho una comprobación rápida y pásale los pasos para probarlo. No montes simulaciones largas. Después, lee los `logs/` antes de cambiar nada.
