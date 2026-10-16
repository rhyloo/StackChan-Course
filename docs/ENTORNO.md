# El entorno de desarrollo

Todo esto lo instala `setup.sh` (Linux) o `setup.bat` (Windows). Aquí se explica qué es cada cosa y qué hacer cuando algo falla.

## Las piezas

| Pieza | Para qué |
|---|---|
| **PlatformIO** | Compila el C++ de los firmwares, descarga el compilador y las librerías que pide cada `platformio.ini`, flashea por USB y abre el monitor serie. Está en dos sitios: la extensión de VS Code (botones) y `.venv/bin/pio` (línea de comandos). Los dos comparten `~/.platformio`. |
| **Arduino-ESP32 2.0.17** | El "framework": `setup()`, `loop()`, `WiFi`, `Serial`… sobre ESP-IDF 4.4. Lo fija `platform = espressif32@7.1.2` en cada `platformio.ini`, así todos compilamos lo mismo. |
| **M5Unified** | Librería de M5Stack para la pantalla, el táctil, el altavoz, los micrófonos, la batería y el IMU del CoreS3. |
| **`lib/`** | Código nuestro compartido por las demos: `StackChanHW` (servos, LEDs, batería, cámara) y `StackChanLink` (UDP con el portátil). Los `platformio.ini` lo incluyen con `lib_extra_dirs`. |
| **`.venv/`** | Entorno virtual de Python con PlatformIO, OpenCV, NumPy y google-genai, para no mezclar con el Python del sistema. Los scripts `run_relay` lo usan solos; a mano: `.venv/bin/python` (Windows: `.venv\Scripts\python`). |

## VS Code

1. Abre `stackchan-curso.code-workspace` (menú *File → Open Workspace from File*, o `code stackchan-curso.code-workspace`).
2. La primera vez, PlatformIO tarda unos minutos en prepararse (barra inferior).
3. Cada firmware aparece como una carpeta del workspace. En la barra inferior, elige el proyecto (el icono de carpeta) y usa:

| Botón | Hace |
|---|---|
| ✓ | Compilar |
| → | Compilar y flashear |
| 🔌 | Monitor serie |
| 🗑 | Borrar lo compilado (si algo raro pasa al cambiar de librerías) |

En el monitor serie, el robot acepta `info` (estado de la red y el vídeo) y `home` (centrar la cabeza).

## Asistentes de IA

Trabajar con un asistente en la terminal ayuda mucho con un proyecto así: le puedes pedir que lea `docs/ROBOT.md` antes de tocar los servos. Por ejemplo, Claude Code:

```bash
curl -fsSL https://claude.ai/install.sh | bash        # Linux
irm https://claude.ai/install.ps1 | iex               # Windows (PowerShell)
```

Y luego `claude` dentro de la carpeta del curso. Cualquier otro asistente (Gemini CLI, Antigravity, Copilot…) vale igual.

## El puerto USB

| | Robot (CoreS3) | ESP32 DevKit |
|---|---|---|
| Chip USB | El propio ESP32-S3 (USB nativo) | Un puente CP2102 o CH340 |
| Linux | `/dev/ttyACM0` | `/dev/ttyUSB0` |
| Windows | `COMx` ("Dispositivo serie USB") | `COMx` ("Silicon Labs CP210x" o "USB-SERIAL CH340") |
| Driver en Windows | Se instala solo (con Internet) | [CP210x](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) o [CH340](https://www.wch-ic.com/downloads/CH341SER_EXE.html) si no aparece el COM |

Si no aparece el puerto:
1. **Cable.** Muchos cables USB-C solo cargan. Prueba otro.
2. **Linux, permisos.** `groups` debe incluir `dialout` (cierra sesión tras añadirte). Si el puerto aparece y desaparece, `sudo apt remove brltty`.
3. **CoreS3, modo descarga.** El USB del CoreS3 lo crea el propio programa: si el firmware se cuelga, desaparece. Mantén pulsado **RST** ~3 s hasta que el LED se ponga **verde** y flashea. Después, pulsa RST para arrancar.
4. **DevKit.** Si se queda en `Connecting...`, mantén pulsado **BOOT** mientras empieza a flashear.
5. **Windows.** Si en el Administrador de dispositivos sale "USB JTAG/serial debug unit" sin COM, falta el driver: [guía de Espressif](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-usb-driver.html).

## Cortafuegos

Las demos usan UDP 5600 (robot), 5601 (móvil) y 5602 (balizas).
- **Linux:** `setup.sh` los abre si usas `ufw`.
- **Windows:** la primera vez que arranques una pasarela, permite el acceso en *Redes privadas*. Tu Wi-Fi tiene que estar marcada como red privada.
