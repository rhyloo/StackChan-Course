# Demo 02 · Voz con Gemini

La demo 01 más voz. Le hablas al robot y [Gemini Live](https://ai.google.dev/gemini-api/docs/live) te entiende, te responde por el altavoz del robot y ejecuta funciones: cambiar el filtro de la imagen, mover la cabeza, los LEDs, guardar fotos… Mientras hablas también ve lo que ve la cámara, así que le puedes preguntar «¿qué ves?».

```
micro del robot --AUDIO_IN 16 kHz--> pasarela --> Gemini Live --> voz --AUDIO_OUT 24 kHz--> altavoz del robot
                                        ^                    └-> llamada a función: activar_filtro("nocturna")
cámara del robot --JPEG------------------┘ (1 fotograma por segundo mientras hablas)
```

**Por qué Gemini corre en el portátil y no en el robot**, como hacía el proyecto Mika:
- **El robot ya está ocupado** comprimiendo y enviando vídeo. Abrir además la conexión cifrada con Gemini le quitaría CPU y Wi-Fi.
- **El audio pesa poco**: unos 32 KB/s viajan por el mismo enlace UDP que el vídeo.
- **La clave de Gemini se queda en el portátil**, no grabada en el robot.
- **Programar las funciones en Python es mucho más fácil** que en C++.

## Necesitas

- Lo de la demo 01. El móvil y las gafas son **opcionales**: sin ellos funciona como un robot con el que hablas.
- **Una clave de la API de Gemini**: créala gratis en https://aistudio.google.com/apikey y guárdala en tu terminal:
  ```bash
  export GEMINI_API_KEY="tu-clave"        # Windows (PowerShell): $env:GEMINI_API_KEY="tu-clave"
  ```
  Para no repetirlo cada vez, añádela a `~/.bashrc`. **Nunca la subas a git.**

## Paso a paso

1. **Flashea el robot:**
   ```bash
   ./tools/flash.sh demos/02-voz-gemini/firmware
   ```
2. **Arranca la pasarela:**
   ```bash
   demos/02-voz-gemini/run_relay.sh       # Windows: demos\02-voz-gemini\run_relay.bat
   ```
   Espera a ver `Gemini: listo`.
3. **Habla.** Hay tres formas, y todas hacen lo mismo:
   - **Mando de PS4:** mantén pulsado **L1** mientras hablas, como un walkie-talkie.
   - **Pantalla del robot:** tócala para empezar y otra vez para terminar. La boca se pone roja mientras escucha.
   - **Terminal de la pasarela:** pulsa **Enter** para empezar y otra vez para terminar.

Prueba con:
- «Activa la visión nocturna»
- «Pon el filtro de caricatura»
- «Quita el filtro»
- «¿Qué ves?»
- «Mira a la izquierda»
- «Pon los LEDs en azul»
- «Haz una foto»

Las fotos se guardan en `fotos/`.

### Opciones

Además de todas las de la demo 01:

| Opción | Qué hace |
|---|---|
| `--voz Kore` | Otra voz: Puck (por defecto), Charon, Kore, Fenrir, Aoede… |
| `--modelo …` | Otro modelo de Gemini Live |
| `--sin-vision` | No enviar imágenes a Gemini |

## Dónde programar

| Fichero | Para qué |
|---|---|
| `relay/voice.py` | **Las funciones que Gemini puede usar** (`functions()`) y su personalidad (`INSTRUCCIONES`). Para darle una habilidad nueva, añade una entrada con su descripción: Gemini decide solo cuándo usarla. |
| `relay/processing.py` | Los filtros. Añade una función a `FILTROS` y Gemini ya puede activarla por su nombre. |
| `relay/hooks.py` | Qué botón del mando sirve para hablar |
| `firmware/src/audio.cpp` | Micrófono y altavoz del robot |

## Cómo funciona

- **Turnos.** El turno empieza cuando llega audio del robot y termina tras 300 ms sin audio. La pasarela se lo indica a Gemini (`activity_start` / `activity_end`), así que no hace falta detectar silencios.
- **Micrófono o altavoz, no los dos.** En el CoreS3 comparten el mismo bus I2S. Si hablas mientras el robot responde, el robot se calla y te escucha.
- **Llamadas a funciones.** Gemini no ejecuta nada por sí mismo: pide «llama a `activar_filtro` con `filtro=nocturna`». La pasarela lo ejecuta y le devuelve el resultado, y Gemini lo usa para responder («Visión nocturna activada»).
- **Sesiones.** Con vídeo, una sesión de Gemini Live dura poco. Se comprime el contexto para alargarla, y si se corta la pasarela reconecta sola.

## Problemas frecuentes

| Síntoma | Solución |
|---|---|
| `Falta la clave de Gemini` | `export GEMINI_API_KEY=...` en la misma terminal |
| La boca se pone roja pero Gemini no contesta | Mira la terminal: si no aparece `Tú: …`, el audio no llega. Comprueba que el robot tiene el firmware de **esta** demo. |
| No se oye la respuesta | El volumen está en `audio.cpp` (`setVolume`). La respuesta solo suena cuando el micrófono está cerrado. |
| La cabeza vuelve a su sitio tras «mira a la izquierda» | En las gafas está activo el modo de control *Head*: manda la cabeza de la persona. Cambia a *View only* con □. |
