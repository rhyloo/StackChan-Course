# Demo 05 · Escribir tarjetas NFC con la voz

Le dices al robot lo que quieres guardar, acercas una tarjeta NFC a su base y el robot lo escribe en ella. Junta la [demo 02](../02-voz-gemini/) (voz con Gemini Live) y la [demo 04](../04-nfc/) (lector NFC):

> «Escribe en la tarjeta: la llave del trastero está en el cajón azul.»
> *La pantalla se pone amarilla: «Acerca la tarjeta para escribir».* Acercas la tarjeta, pita…
> «¡Listo, ya está escrita!»

También lee: acerca una tarjeta y pregunta «¿qué pone en la tarjeta?».

```
micro --AUDIO_IN--> pasarela --> Gemini Live --> escribir_tarjeta("la llave del trastero...")
                       │                                 │
                       └── CMD "nfc write ..." ──> robot: espera la tarjeta, la escribe y la relee
                       <── NFC 0x28 {"event":"write","ok":true} ──┘
                                     Gemini <-- {"ok": true} --> «¡Listo!» --AUDIO_OUT--> altavoz
```

## Necesitas

- **El robot** y **el portátil** en la misma Wi-Fi de 2,4 GHz.
- **Una clave de Gemini**, como en la demo 02: créala en https://aistudio.google.com/apikey y
  ```bash
  export GEMINI_API_KEY="tu-clave"        # Windows (PowerShell): $env:GEMINI_API_KEY="tu-clave"
  ```
- **Tarjetas o pegatinas NTAG213/215/216**. Son las más baratas y las que mejor van; las nuevas ya vienen en formato NDEF.

## Paso a paso

1. Conecta el robot al portátil por USB y ejecuta **un solo comando**:
   ```bash
   demos/05-nfc-voz/run.sh          # Windows: demos\05-nfc-voz\run.bat
   ```
   Compila el firmware, lo sube al robot y arranca la pasarela. Cuando ponga `Gemini: listo`, ya puedes hablar. Para no volver a flashear: `run.sh --sin-flashear`.
2. Habla con el robot. Por ejemplo:
   - «Escribe en la tarjeta: hola, soy Stack-chan». Acerca la tarjeta en menos de 20 s y déjala quieta hasta que pite.
   - «¿Qué pone en la tarjeta?», con la tarjeta encima.
3. Comprueba con el móvil: con NFC activado, al acercarle la tarjeta muestra el texto. Con la app NFC Tools, en *Leer*, se ve el registro de texto.

Tocar la pantalla del robot silencia o reactiva el micro.

## Qué puede salir

| Mensaje | Qué significa |
|---|---|
| *No ha llegado ninguna tarjeta* | Pasaron 20 s sin tarjeta. Pídeselo otra vez. |
| *El texto no cabe en esta tarjeta* | Una NTAG213 guarda unos 130 caracteres; la pasarela recorta a 140 bytes. Usa una NTAG215/216 para textos largos. |
| *Es una MIFARE Classic / DESFire* | No se escriben: la Classic necesita claves y la DESFire habría que formatearla. Usa una NTAG. |
| *Lo escrito no coincide al releerlo* | La tarjeta se movió mientras se escribía. Repite sin moverla. |

## Cómo funciona

| Fichero | Para qué |
|---|---|
| `firmware/src/main.cpp` | El robot: lee cada tarjeta nueva y avisa a la pasarela; con `nfc write <texto>` espera una tarjeta, escribe un registro NDEF de texto (idioma `es`) y lo relee. El audio es `audio.cpp` de la demo 02, compilado tal cual. |
| `relay/nfc_voice.py` | Las funciones de Gemini `escribir_tarjeta` y `leer_tarjeta`. Hereda la voz de la demo 02 (`Voice`) y cambia sus instrucciones y funciones. |
| `relay/main.py` | Arranca la pasarela de la demo 02 con estas funciones. |

**La función espera de verdad.** `escribir_tarjeta` no devuelve nada a Gemini hasta que el robot ha escrito (o ha fallado), así que lo que te contesta es el resultado real. Mientras espera, la conversación sigue viva: `voice.py` ejecuta las funciones en un hilo aparte.

**Una tarjeta que ya está encima.** Tras leer una tarjeta, el robot la deja en reposo (HALT) para no leerla en bucle. Al pedirle que escriba o que lea, apaga y enciende el campo de radio: la tarjeta se reinicia y vuelve a contestar sin tener que apartarla.

**Mensajes nuevos del protocolo** (ver [PROTOCOL.md](../01-vr-cardboard/PROTOCOL.md)): las órdenes `nfc write <texto>`, `nfc read` y `nfc cancel`, y el tipo `0x28 NFC` (robot → portátil, JSON).

## Para seguir

- **Una URL** en vez de texto: `Record::setURIPayload("ejemplo.com", URIProtocol::HTTPS)`. El móvil la abre al acercarle la tarjeta.
- **Tarjetas que mandan:** una tarjeta con «modo fiesta» que, al leerla, cambie los LEDs. Mira `on_nfc` en `nfc_voice.py`.
- **Un diario:** que cada lectura se guarde con la fecha en un fichero del portátil.
