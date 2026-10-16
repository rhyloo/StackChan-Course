#!/usr/bin/env bash
# Instala TODO lo necesario para programar Stack-chan en Linux (Ubuntu/Debian),
# partiendo de un ordenador sin nada: Git, Python, VS Code + PlatformIO, la
# toolchain del ESP32-S3, permisos del puerto USB y el entorno del proyecto.
#
#     ./setup.sh               instalación completa (pide sudo para lo del sistema)
#     ./setup.sh --no-system   sin sudo: solo el entorno del proyecto (.venv, toolchain, Wi-Fi)
#     ./setup.sh --wifi        solo vuelve a pedir la Wi-Fi del robot
#     ./setup.sh --flash       al final, flashea el robot conectado por USB
#
# Se puede ejecutar las veces que haga falta: lo ya instalado se salta.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(pwd)
VENV="$ROOT/.venv"
SECRETS="$ROOT/firmware/include/wifi_secrets.h"
UDEV_RULES=/etc/udev/rules.d/99-platformio-udev.rules
UDEV_URL=https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules

bold() { printf '\n\033[1m%s\033[0m\n' "$*"; }
ok()   { printf '  \033[32m✔\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
die()  { printf '  \033[31m✘ %s\033[0m\n' "$*"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

ask_wifi() {
    bold "Wi-Fi del robot"
    echo "  El robot, el portátil y el móvil tienen que estar en la MISMA red Wi-Fi de 2,4 GHz"
    echo "  (el ESP32-S3 no ve redes de 5 GHz)."
    local ssid="${WIFI_SSID:-}" pass="${WIFI_PASS:-}"
    [ -n "$ssid" ] || read -r -p "  Nombre de la red (SSID): " ssid
    [ -n "$pass" ] || { read -r -s -p "  Contraseña (no se muestra): " pass; echo; }
    [ -n "$ssid" ] || die "El SSID no puede estar vacío"
    WIFI_SSID="$ssid" WIFI_PASS="$pass" "$VENV/bin/python" - "$SECRETS" <<'PY'
import os, sys
def c(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
with open(sys.argv[1], "w") as f:
    f.write("// Generado por setup. NO lo subas al repo (está en .gitignore).\n#pragma once\n")
    f.write(f"#define WIFI_SSID {c(os.environ['WIFI_SSID'])}\n#define WIFI_PASS {c(os.environ['WIFI_PASS'])}\n")
PY
    chmod 600 "$SECRETS"
    ok "Guardada en firmware/include/wifi_secrets.h"
}

SYSTEM=1 FLASH=0 WIFI_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --no-system) SYSTEM=0 ;;
        --flash) FLASH=1 ;;
        --wifi) WIFI_ONLY=1 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) die "Opción desconocida: $arg (usa --help)" ;;
    esac
done

if [ "$WIFI_ONLY" = 1 ]; then
    [ -x "$VENV/bin/python" ] || die "Primero ejecuta ./setup.sh"
    ask_wifi
    exit 0
fi

[ "$(uname)" = Linux ] || die "Este script es para Linux. En Windows usa setup.ps1 (ver README)."

# ---------------------------------------------------------------------------
if [ "$SYSTEM" = 1 ]; then
    bold "1/7 Paquetes del sistema (apt)"
    have apt-get || die "No encuentro apt-get. Instala a mano: git python3 python3-venv python3-pip curl, y vuelve con --no-system"
    PKGS=(git curl python3 python3-venv python3-pip)
    MISSING=()
    for p in "${PKGS[@]}"; do dpkg -s "$p" >/dev/null 2>&1 || MISSING+=("$p"); done
    if [ ${#MISSING[@]} -gt 0 ]; then
        echo "  Instalando: ${MISSING[*]} (te pedirá tu contraseña de usuario)"
        sudo apt-get update -qq
        sudo apt-get install -y -qq "${MISSING[@]}"
    fi
    ok "git, curl, python3, venv y pip"

    bold "2/7 Permisos del puerto USB del robot"
    # El CoreS3 usa el USB nativo del ESP32-S3 (aparece como /dev/ttyACM0): no
    # necesita driver, pero sí permiso para abrir el puerto.
    if [ ! -f "$UDEV_RULES" ]; then
        curl -fsSL "$UDEV_URL" | sudo tee "$UDEV_RULES" >/dev/null
        sudo udevadm control --reload-rules && sudo udevadm trigger
    fi
    ok "Reglas udev de PlatformIO"
    if id -nG "$USER" | grep -qw dialout; then
        ok "Tu usuario está en el grupo dialout"
    else
        sudo usermod -aG dialout "$USER"
        warn "Te he añadido al grupo dialout: CIERRA SESIÓN y vuelve a entrar para que tenga efecto."
    fi
    if dpkg -s brltty >/dev/null 2>&1; then
        # brltty (lector braille) secuestra algunos puertos serie USB en Ubuntu.
        warn "brltty está instalado y a veces se apropia del puerto USB. Si el robot no aparece: sudo apt remove brltty"
    fi

    bold "3/7 Visual Studio Code"
    if have code; then
        ok "Ya instalado ($(code --version 2>/dev/null | head -1))"
    elif have snap; then
        sudo snap install code --classic
        ok "Instalado con snap"
    else
        # Repositorio oficial de Microsoft.
        sudo apt-get install -y -qq wget gpg apt-transport-https
        wget -qO- https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor | sudo tee /usr/share/keyrings/microsoft.gpg >/dev/null
        echo "deb [arch=amd64,arm64,armhf signed-by=/usr/share/keyrings/microsoft.gpg] https://packages.microsoft.com/repos/code stable main" \
            | sudo tee /etc/apt/sources.list.d/vscode.list >/dev/null
        sudo apt-get update -qq && sudo apt-get install -y -qq code
        ok "Instalado desde el repositorio de Microsoft"
    fi
    if have code; then
        for ext in platformio.platformio-ide ms-python.python; do
            code --list-extensions 2>/dev/null | grep -qi "^$ext$" || code --install-extension "$ext" --force >/dev/null 2>&1 || true
        done
        ok "Extensiones: PlatformIO IDE y Python"
    fi

    if have ufw && sudo ufw status 2>/dev/null | grep -q 'Status: active'; then
        sudo ufw allow 5600:5601/udp >/dev/null
        ok "Cortafuegos: abiertos UDP 5600-5601 (robot y móvil)"
    fi
fi

# ---------------------------------------------------------------------------
bold "4/7 Entorno de Python del proyecto (.venv)"
python3 -c 'import sys; sys.exit(sys.version_info < (3, 9))' || die "Necesitas Python 3.9 o superior (tienes $(python3 --version))"
python3 -c 'import venv, ensurepip' 2>/dev/null || die "Falta python3-venv: sudo apt install python3-venv"
[ -x "$VENV/bin/python" ] || python3 -m venv "$VENV"
"$VENV/bin/python" -m pip install --quiet --upgrade pip
"$VENV/bin/python" -m pip install --quiet -r requirements.txt
ok "$("$VENV/bin/pip" list 2>/dev/null | grep -i -E '^(platformio|opencv-python|numpy) ' | awk '{printf "%s %s  ", $1, $2}')"

bold "5/7 Toolchain del ESP32-S3 (descarga ~600 MB la primera vez)"
"$VENV/bin/pio" pkg install --project-dir firmware >/dev/null
ok "Compilador, framework Arduino-ESP32 y M5Unified"

bold "6/7 Wi-Fi del robot"
if [ -f "$SECRETS" ]; then
    ok "Ya configurada. Para cambiarla: ./setup.sh --wifi"
else
    ask_wifi
fi

bold "7/7 Comprobación"
"$VENV/bin/python" -m unittest discover -s relay/tests >/dev/null 2>&1 && ok "Tests de la pasarela" || warn "Fallan los tests: .venv/bin/python -m unittest discover -s relay/tests -v"
"$VENV/bin/pio" run --project-dir firmware --silent >/dev/null && ok "El firmware compila" || die "El firmware no compila: .venv/bin/pio run -d firmware"
if ls /dev/ttyACM* >/dev/null 2>&1; then ok "Robot detectado en $(ls /dev/ttyACM* | head -1)"; else warn "No veo el robot por USB (normal si no está conectado)"; fi

if [ "$FLASH" = 1 ]; then ./tools/flash.sh; fi

bold "¡Listo!"
cat <<MSG
  Abrir el proyecto en VS Code:  code stackchan-vr.code-workspace
  Flashear el robot (USB-C):      ./tools/flash.sh
  Arrancar la pasarela:           ./run_relay.sh --show
  Probar sin robot:               .venv/bin/python tools/fake_robot.py
  Asistente de IA (opcional):     curl -fsSL https://claude.ai/install.sh | bash   (Claude Code)
MSG
