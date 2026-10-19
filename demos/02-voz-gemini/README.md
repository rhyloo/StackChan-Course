# Demo 02 · Voz con Gemini

La demo 01 más voz. Le hablas al robot y [Gemini Live](https://ai.google.dev/gemini-api/docs/live) te entiende, te responde por el altavoz del robot y ejecuta funciones: cambiar el filtro de la imagen, mover la cabeza, los LEDs, guardar fotos… Al empezar a hablar ve lo que ve la cámara, así que le puedes preguntar «¿qué ves?».

```
micro del robot --AUDIO_IN 16 kHz--> pasarela --> Gemini Live --> voz --AUDIO_OUT 24 kHz--> altavoz del robot
                                        ^                    └-> llamada a función: activar_filtro("nocturna")
cámara del robot --JPEG------------------┘ (un fotograma al empezar cada turno)
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

1. **Conecta el robot por USB-C y ejecuta:**
   ```bash
   demos/02-voz-gemini/run.sh       # Windows: demos\02-voz-gemini\run.bat
   ```
   Compila el firmware, lo sube al robot y arranca la pasarela. Espera a ver `Gemini: listo`.
2. **La próxima vez**, si no has cambiado el firmware: `run.sh --sin-flashear` (o `run_relay.sh`, que solo arranca la pasarela).
3. **Habla normal**, como con una persona. El robot te escucha siempre que él no está hablando (boca con un círculo verde), y Gemini detecta solo cuándo has terminado (medio segundo de silencio).
   - **Para que deje de escucharte**, toca su pantalla, pulsa L1 en el mando o Enter en la terminal. Lo mismo para reactivarlo.
   - **Mientras el robot habla no te oye.** El micrófono y el altavoz comparten el mismo bus de audio del CoreS3, así que no se le puede interrumpir.
   - **Modo walkie-talkie:** `run.sh --pulsar`. Hablas solo mientras mantienes L1, o entre dos toques de pantalla o dos Enter. Va mejor en sitios ruidosos.

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
| `--pulsar` | Pulsar para hablar en vez de manos libres |

## Dónde programar

| Fichero | Para qué |
|---|---|
| `relay/voice.py` | **Las funciones que Gemini puede usar** (`functions()`) y su personalidad (`INSTRUCCIONES`). Para darle una habilidad nueva, añade una entrada con su descripción: Gemini decide solo cuándo usarla. |
| `relay/processing.py` | Los filtros. Añade una función a `FILTROS` y Gemini ya puede activarla por su nombre. |
| `relay/hooks.py` | Qué botón del mando sirve para hablar |
| `firmware/src/audio.cpp` | Micrófono y altavoz del robot |

## Cómo funciona

- **Turnos.** En manos libres, el robot envía su micrófono siempre que no habla, y la detección de voz de Gemini decide cuándo empiezas y terminas. Tiene sensibilidad baja al inicio, para que el ruido no la dispare, y da el turno por acabado tras 500 ms de silencio (`SILENCE_MS` en `voice.py`). Con `--pulsar`, el turno lo marca la pasarela: empieza cuando llega audio y termina tras 300 ms sin audio (`activity_start` / `activity_end`).
- **Imágenes.** En manos libres, Gemini recibe una imagen cada 3 s; con `--pulsar`, una al empezar cada turno. Cada imagen se queda en el contexto de la sesión y frena las respuestas siguientes: medido, unos 100-200 ms con una por segundo.
- **Consumo de la API.** En manos libres el audio va a Gemini todo el rato (silencios incluidos), así que gasta más cuota que pulsar para hablar.
- **Micrófono o altavoz, no los dos.** En el CoreS3 comparten el mismo bus I2S. Si hablas mientras el robot responde, el robot se calla y te escucha.
- **Llamadas a funciones.** Gemini no ejecuta nada por sí mismo: pide «llama a `activar_filtro` con `filtro=nocturna`». La pasarela lo ejecuta y le devuelve el resultado, y Gemini lo usa para responder («Visión nocturna activada»).
- **Sesiones.** Con vídeo, una sesión de Gemini Live dura poco. Se comprime el contexto para alargarla, y si se corta la pasarela reconecta sola.

## Problemas frecuentes

| Síntoma | Solución |
|---|---|
| `Falta la clave de Gemini` | `export GEMINI_API_KEY=...` en la misma terminal |
| Responde a ruidos o a conversaciones de fondo | Silencia el micro (toque, L1 o Enter) o usa `--pulsar` |
| La boca escucha (círculo verde) pero Gemini no contesta | Mira la terminal: si no aparece `Tú: …`, el audio no llega. Comprueba que el robot tiene el firmware de **esta** demo. |
| No se oye la respuesta | El volumen está en `audio.cpp` (`setVolume`). La respuesta solo suena cuando el micrófono está cerrado. |
| La cabeza vuelve a su sitio unos segundos después de «mira a la izquierda» | Es lo esperado si las gafas controlan la cabeza (modo *Head*): la voz manda durante 10 s (`VOICE_HOLD_S` en `voice.py`) y después la cabeza vuelve a seguirte. Para mandar solo con la voz, pon la app en *View only* con □. |
