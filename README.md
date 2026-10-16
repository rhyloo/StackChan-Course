# Stack-chan VR

Mira por los ojos de Stack-chan con unas Google Cardboard y mueve su cabeza girando la tuya o con un mando de PS4.

```
Robot (CoreS3) --UDP 5600, JPEG--> Portátil (relay) --UDP 5601, JPEG--> Móvil (app Stack-chan VR)
Robot (servos) <-- posición ------ Portátil (relay) <-- control/sensores -- Móvil
```

- **Robot** (`firmware/`): M5Stack CoreS3 + cámara GC0308 + base Stack-chan K151 con dos servos SCS0009. Se conecta a la Wi-Fi, envía la cámara al portátil y mueve la cabeza según lo que le llega.
- **Portátil** (`relay/`): pasarela en Python. Reenvía el vídeo al móvil, si quieres lo procesa antes (visión por computador, grabar, mejorar la imagen) y pasa los movimientos del móvil al robot. Es donde se programa durante el curso.
- **Móvil**: la app Android [Stack-chan VR](https://github.com/rhyloo/StackChan-VR).

Los tres tienen que estar en la **misma red Wi-Fi de 2,4 GHz**.

## Instalación (una vez, con el ordenador recién encendido)

El script instala **todo**, aunque nunca hayas programado un microcontrolador: Git, Python, Visual Studio Code con PlatformIO, la toolchain del ESP32-S3 (unos 600 MB, mejor en casa antes del curso), los permisos del puerto USB y el entorno del proyecto. Al final te pide la Wi-Fi del robot y comprueba que el firmware compila.

**Linux (Ubuntu/Debian):**
```bash
sudo apt install -y git
git clone https://github.com/rhyloo/StachChan-Course.git stackchan-vr-robot
cd stackchan-vr-robot
./setup.sh
```
Si te añade al grupo `dialout`, cierra sesión y vuelve a entrar.

**Windows 10/11:** instala [Git](https://git-scm.com/download/win), clona el repo (o descárgalo en ZIP desde GitHub) y haz **doble clic en `setup.bat`**. Si Windows pregunta por el cortafuegos al arrancar la pasarela, marca *Redes privadas* y permite el acceso.

La contraseña de la Wi-Fi se guarda en `firmware/include/wifi_secrets.h`, que nunca se sube al repo. Para cambiarla: `./setup.sh --wifi` (Linux) o `setup.bat -Wifi` (Windows).

### Programar con VS Code

Abre `stackchan-vr.code-workspace` (`code stackchan-vr.code-workspace`). La extensión PlatformIO detecta el proyecto `firmware/`: la barra inferior tiene los botones ✓ (compilar), → (flashear) y 🔌 (monitor serie). La primera vez PlatformIO tarda unos minutos en prepararse.

Para trabajar con un asistente de IA en la terminal, por ejemplo Claude Code: `curl -fsSL https://claude.ai/install.sh | bash` (Linux) o `irm https://claude.ai/install.ps1 | iex` (PowerShell).

## Uso

1. **Flashea el robot** (por USB-C, una vez, o cuando cambies el firmware o la Wi-Fi): `./tools/flash.sh` en Linux, `tools\flash.bat` en Windows, o el botón → de PlatformIO.
   En la pantalla del robot verás su IP, su nombre (`stackchan-XXXX`) y si ha encontrado el portátil.
2. **Arranca la pasarela** en el portátil (en Windows, `run_relay.bat` en vez de `./run_relay.sh`):
   ```bash
   ./run_relay.sh                 # reenvía el vídeo tal cual, con la mínima latencia
   ./run_relay.sh --show          # y además lo ves en una ventana del portátil
   ./run_relay.sh --enhance       # más contraste (la GC0308 es oscura en interiores)
   ./run_relay.sh --record videos # graba lo que se ve
   ```
   Si hay varios robots en la red, el portátil se empareja con el primero que oye. Para elegir uno: `./run_relay.sh --robot stackchan-XXXX`.
3. **Abre la app** en el móvil: el portátil aparece solo en la lista. Pulsa *Enter VR*, mete el móvil en las Cardboard y mira alrededor.

¿No tienes el robot a mano? `.venv/bin/python tools/fake_robot.py` simula uno con una imagen de prueba.

## Dónde programar

| Fichero | Para qué |
|---|---|
| `relay/processing.py` | Procesar cada fotograma antes de enviarlo al móvil (OpenCV): detección de caras, filtros, superponer información… |
| `relay/hooks.py` | Reaccionar a botones del mando, sensores y eventos del móvil, y mandar órdenes al robot (`tone`, `leds`, `text`, `home`) o al móvil (`hud`, `vibrate`, `say`). |
| `firmware/src/` | El robot: `link.cpp` (red), `board.cpp` (servos y LEDs), `camera.cpp`, `main.cpp` (pantalla). |

Sensores del móvil: `./run_relay.sh --sensors accelerometer=20,light=2 --events shake,touch` y activa *Share with laptop* en la pantalla *Sensors* de la app.

## Problemas frecuentes

| Síntoma | Solución |
|---|---|
| El robot se queda en «WiFi: conectando…» | La red tiene que ser de 2,4 GHz. Revisa el nombre y la contraseña con `./setup.sh --wifi` y vuelve a flashear. |
| El robot dice «Buscando el portátil» | El relay no está arrancado, el portátil está en otra red, o el cortafuegos bloquea UDP 5600-5601 (`sudo ufw allow 5600:5601/udp`). Algunas redes (eduroam, de invitados) aíslan a los clientes entre sí: usa un router o el móvil como punto de acceso. |
| La app no encuentra el portátil | Lo mismo: misma red y UDP 5601 abierto. También puedes escribir la IP del portátil a mano en la app. |
| El script de flasheo no encuentra el puerto | Prueba otro cable USB-C (algunos solo cargan). Si sigue sin aparecer, mantén pulsado el botón de reset del CoreS3 unos 3 s hasta que se encienda el LED verde y reintenta. |
| La imagen es muy oscura | `--enhance` en el relay, y luz en la habitación. |

Monitor serie del robot: `.venv/bin/pio device monitor -d firmware` (o el botón 🔌 de PlatformIO) y escribe `info`.

## Más

- [docs/ROBOT.md](docs/ROBOT.md): el robot por dentro (componentes, pines, servos, límites, recuperación). Las hojas de datos, esquemas y manuales están en `docs/hardware/`.
- [PROTOCOL.md](PROTOCOL.md): todos los mensajes UDP.
- App del móvil: [rhyloo/StackChan-VR](https://github.com/rhyloo/StackChan-VR).
