# Demo 07 · Fotomatón con impresora térmica

El robot hace de fotomatón: tocas su pantalla, cuenta 3 o 5 segundos, hace la foto, eliges un filtro y la imprime en una **impresora térmica MX10** (una «cat printer» de papel térmico de 57 mm) por Bluetooth.

```
 1. vista previa ──toque──> 2. cuenta atrás 3/5 s ──> 3. ¡foto!
                                                          │ JPEG por Wi-Fi
                                                          v
 5. «Imprimir» ──Bluetooth LE──> MX10       portátil: 4. filtro (Gemini u OpenCV)
        ^                                              + paso a blanco y negro
        └──── imagen lista para imprimir (384 puntos) ─┘
```

| Filtro | Quién lo hace | Qué sale |
|---|---|---|
| **Ghibli** | Gemini (IA) | La foto redibujada como una película de animación de Ghibli. |
| **Gaming** | Gemini (IA) | La foto convertida en un videojuego retro de 16 bits, con barra de vida y puntuación. |
| **Cómic** | Gemini (IA) | Viñeta de cómic en blanco y negro, con tinta y tramas. |
| **Original** | Portátil | La foto tal cual. |
| **Boceto** | Portátil (OpenCV) | Dibujo a lápiz. |
| **Píxel** | Portátil | Pixel art de 64 puntos de ancho. |

**Por qué en blanco y negro.** El papel térmico se oscurece con calor: la impresora solo sabe poner un punto negro o no ponerlo. Los grises se simulan con más o menos puntos («tramado» de Floyd-Steinberg). La pantalla del robot enseña la imagen ya tramada: **lo que ves es exactamente lo que sale en el papel**.

## Necesitas

- **El robot** y **el portátil** en la misma Wi-Fi de 2,4 GHz.
- **Una impresora MX10** con papel y batería. Otras «cat printers» (GB01, GB02, GT01, MX05…) usan el mismo protocolo y deberían funcionar; la MXW01 no.
- **Para los filtros con IA, una clave de Gemini**: créala en https://aistudio.google.com/apikey y
  ```bash
  export GEMINI_API_KEY="tu-clave"        # Windows (PowerShell): $env:GEMINI_API_KEY="tu-clave"
  ```
  Sin clave funcionan Original, Boceto y Píxel.

## Paso a paso

1. **Enciende la impresora** y ponla cerca del robot.
2. **Conecta el robot por USB-C y ejecuta:**
   ```bash
   demos/07-fotomaton/run.sh          # Windows: demos\07-fotomaton\run.bat
   ```
   Compila el firmware, lo sube al robot y arranca la pasarela. En la pantalla del robot verás la cámara y «Portátil listo».
3. **Toca la pantalla:** la mitad izquierda para una cuenta atrás de **3 s**, la derecha para **5 s**. Pita cada segundo y, al llegar a cero, la pantalla y los LEDs dan un destello.
4. **Elige filtro.** Los de IA tardan unos 10 s. Puedes hacer **Nueva foto** si no te gusta.
5. **Mira el resultado** y pulsa:
   - **Imprimir:** busca la impresora por Bluetooth (la primera vez, unos 5 s), la imprime y desconecta;
   - **Otro filtro**, sobre la misma foto;
   - **Nueva foto**.

Todas las fotos se guardan en el portátil, en `fotos/`: la original (`.jpg`), la filtrada (`-ghibli.png`…) y la versión para imprimir (`-termica.png`).

## Qué puede salir

| Mensaje | Qué hacer |
|---|---|
| *Buscando el portátil (run.sh)…* | La pasarela no está en marcha o no estáis en la misma Wi-Fi. |
| *No encuentro la impresora* | Enciéndela y acércala. Debe anunciarse como `MX…` por Bluetooth LE. Si el móvil está conectado a ella, desconéctalo: solo admite una conexión. |
| *Se cortó la conexión con la impresora* | Casi siempre es batería baja: con poca carga la MX10 se apaga a mitad de imprimir. Cárgala. |
| *Gemini no ha devuelto imagen* | Gemini se ha negado o ha fallado. Prueba otro filtro u otra foto. |
| Impresión muy clara o muy oscura | Cambia `ENERGY` en `firmware/src/printer_mx10.cpp` (más alto = más oscuro) o el `0.8` de `_contraste()` en `relay/filtros.py`. |

## Cómo funciona

| Fichero | Para qué |
|---|---|
| `firmware/src/main.cpp` | Las pantallas (vista previa, cuenta atrás, menú, resultado), la foto (JPEG por UDP) y la recepción de la imagen. Los botones de filtro están en `FILTERS`. |
| `firmware/src/printer_mx10.cpp` | La impresora: Bluetooth LE (NimBLE), servicio `AE30`, escritura en `AE01`. Cada orden va en una trama `51 78 <orden> 00 <largo> <datos> <crc8> FF`; cada línea son 48 bytes (384 puntos). |
| `relay/filtros.py` | Los filtros: prompts de Gemini en `PROMPTS`, OpenCV para los locales, y `a_termica()`, que reduce a 384 puntos de ancho y trama. |
| `relay/fotomaton.py` | Recibe la foto, llama al filtro y devuelve la imagen al robot, repitiendo si se pierde algún trozo. |

**La impresora la maneja el robot.** El ESP32-S3 tiene Bluetooth LE, así que imprime sin depender del Bluetooth del portátil. La MX10 no avisa de cuándo puede recibir más datos durante una impresión: el robot le manda 200 bytes cada 20 ms, el ritmo al que imprime. Al terminar espera 3 s antes de desconectar, porque la impresora sigue quemando papel con lo que tiene en su memoria.

El protocolo de la MX10 y sus trucos (papel en blanco antes y después de la imagen, no usar la orden de avance, una conexión nueva por impresión) vienen de [s8n/cataclysm](https://github.com/s8n/cataclysm) (licencia MIT), que los probó en una MX10 real.

**Mensajes nuevos del protocolo** ([PROTOCOL.md](../01-vr-cardboard/PROTOCOL.md)): `0x29 PHOTO`, `0x2A PHOTO_EVENT`, `0x2B PRINT_BITMAP` y las órdenes `photo ack|status|error`.

## Para seguir

- **Un filtro nuevo:** añade su prompt a `PROMPTS` en `filtros.py` y su botón a `FILTERS` en `main.cpp`.
- **Un marco o texto** («Stack-chan · 2026») en la parte de abajo de la impresión, dibujado con OpenCV antes de tramar.
- **Tira de fotomatón:** cuatro fotos seguidas, una debajo de otra, en el mismo papel.
- **Por voz:** junta esta demo con la 02, para que «hazme una foto estilo Ghibli» lo haga todo.
