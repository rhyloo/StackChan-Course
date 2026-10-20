# Curso Stack-chan

Demos para aprender a programar [Stack-chan](docs/ROBOT.md), un robot de escritorio de M5Stack con un ESP32-S3, cámara, pantalla y dos servos para mover la cabeza. Cada demo está en su propia carpeta, con su README, y se puede hacer sin las demás.

| Demo | Qué hace | Necesitas |
|---|---|---|
| [01 · VR con Cardboard](demos/01-vr-cardboard/) | Ves por los ojos del robot con unas Cardboard y mueves su cabeza girando la tuya o con un mando de PS4. El portátil puede procesar la imagen (OpenCV). | Robot, portátil, móvil Android con la app [Stack-chan VR](https://github.com/rhyloo/StackChan-VR), Cardboard. Opcional: mando de PS4. |
| [02 · Voz con Gemini](demos/02-voz-gemini/) | La demo 01 más voz: le hablas al robot («activa la visión nocturna», «¿qué ves?», «mira a la izquierda») y Gemini Live entiende, responde por el altavoz del robot y ejecuta funciones. | Lo de la demo 01 (el móvil es opcional) y una [clave de Gemini](https://aistudio.google.com/apikey). |
| [03 · Balizas ESP-NOW](demos/03-espnow-balizas/) | Tres ESP32 hacen de balizas por ESP-NOW. El robot mide su señal, sabe cuál tiene más cerca y el portátil estima su posición en un mapa. | Robot, 3 placas ESP32 DevKit, portátil. |
| [04 · Lector NFC](demos/04-nfc/) | Acercas una tarjeta o pegatina NFC a la base y el robot enseña en pantalla su número de serie, su tipo y lo que lleva escrito (texto, URL…). | Robot y etiquetas NFC (NTAG, MIFARE). Opcional: móvil con NFC para escribirlas. |
| [05 · Tarjetas NFC por voz](demos/05-nfc-voz/) | Le dices lo que quieres guardar («escribe en la tarjeta: …»), acercas la tarjeta y el robot lo escribe. También te lee lo que pone. | Robot, portátil, tarjetas NTAG y una [clave de Gemini](https://aistudio.google.com/apikey). |
| [06 · Infrarrojos](demos/06-infrarrojos/) | Apuntas un mando a distancia al robot y ves el código de cada botón; tocas la pantalla y el robot lo repite, como un mando universal. | Robot y un mando de infrarrojos (tele, aire…). |
| [07 · Fotomatón](demos/07-fotomaton/) | Tocas la pantalla, cuenta atrás de 3 o 5 s y foto. Eliges un filtro (Ghibli, Gaming y Cómic con IA; Boceto, Píxel u Original) y la imprime en una impresora térmica MX10 por Bluetooth. | Robot, portátil, impresora MX10 y, para los filtros con IA, una [clave de Gemini](https://aistudio.google.com/apikey). |
| [08 · Servo SG90](demos/08-servo-sg90/) | Conectas un micro-servo SG90 al PORT.B y lo mueves desde la pantalla: barra, 0°/90°/180° o barrido. | Robot, un servo SG90 y un cable Grove. |

```
demos/                 una carpeta por demo: firmware, código del portátil y README
lib/StackChanHW/       C++ compartido: servos, LEDs, batería y cámara del robot
lib/StackChanLink/     C++ compartido: enlace UDP robot <-> portátil
lib/python/stackchan/  Python compartido: protocolo y pasarela base
config/                tu Wi-Fi (wifi_secrets.h: la crea setup y nunca se sube)
docs/                  el robot por dentro, el entorno de desarrollo y las hojas de datos
tools/                 flashear cualquier firmware
```

## Instalación (una vez)

El script deja el ordenador listo aunque nunca hayas programado un microcontrolador:
- Git, Python y Visual Studio Code con PlatformIO;
- los compiladores de ESP32 y ESP32-S3 (cerca de 1 GB, mejor en casa antes del curso);
- los permisos del puerto USB y las librerías de Python.

Al final te pide la Wi-Fi y comprueba que todos los firmwares compilan.

**Linux (Ubuntu/Debian):**
```bash
sudo apt install -y git
git clone https://github.com/rhyloo/StachChan-Course.git stackchan-curso
cd stackchan-curso
./setup.sh
```
Si te añade al grupo `dialout`, cierra sesión y vuelve a entrar.

**Windows 10/11:**
1. Instala [Git](https://git-scm.com/download/win).
2. Clona el repo, o descárgalo en ZIP desde GitHub.
3. Haz **doble clic en `setup.bat`**.

Después, abre el curso en VS Code: `code stackchan-curso.code-workspace`. Lo que hay que saber de VS Code, PlatformIO y el puerto USB está en [docs/ENTORNO.md](docs/ENTORNO.md).

**Wi-Fi.** El robot, el portátil y el móvil tienen que estar en la **misma red Wi-Fi de 2,4 GHz**.
- No sirven las redes que aíslan a los clientes entre sí, como eduroam o las de invitados. Usa un router propio o el móvil como punto de acceso.
- Para cambiar de red: `./setup.sh --wifi` (o `setup.bat -Wifi`) y vuelve a flashear.

## Usar una demo

Conecta el robot por USB-C y ejecuta el `run.sh` de la demo (en Windows, `run.bat`). Comprueba que todo está listo, compila, sube el firmware al robot y arranca lo que la demo necesita en el portátil:

```bash
./demo.sh                          # menú: eliges la demo por su número (Windows: demo.bat)
demos/02-voz-gemini/run.sh         # o directamente la que quieras
demos/02-voz-gemini/run.sh --sin-flashear   # sin volver a subir el firmware
```

## Flashear a mano

```bash
./tools/flash.sh                                    # lista los firmwares
./tools/flash.sh demos/01-vr-cardboard/firmware     # robot
./tools/flash.sh demos/03-espnow-balizas/baliza --env baliza2 /dev/ttyUSB0
```

En Windows es `tools\flash.bat` con los mismos argumentos, salvo que se escribe `-Entorno baliza2` en vez de `--env baliza2`. También puedes usar los botones de PlatformIO en VS Code.

## Documentación

- [docs/ROBOT.md](docs/ROBOT.md): el robot por dentro (componentes, pines, servos, límites, recuperación). Hojas de datos, esquemas y manuales en [docs/hardware/](docs/hardware/).
- [docs/ENTORNO.md](docs/ENTORNO.md): VS Code, PlatformIO, monitor serie, asistentes de IA y problemas con el USB.
- [demos/01-vr-cardboard/PROTOCOL.md](demos/01-vr-cardboard/PROTOCOL.md): los mensajes UDP entre robot, portátil y móvil.
