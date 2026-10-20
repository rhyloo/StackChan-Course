# Demo 08 · Un servo SG90 en el conector Grove

Conectas un micro-servo **SG90** a uno de los conectores Grove del robot y lo mueves desde la pantalla táctil: arrastrando una barra, con botones de 0°, 90° y 180°, o en modo barrido de un extremo a otro. Sirve para añadirle al Stack-chan un brazo, una bandera, una tapa que se abre…

```
 pantalla táctil ──> ESP32-S3: pulso de 0,5 a 2,4 ms cada 20 ms ──G9 y G8 (PORT.B)──> SG90
```

**Cómo se controla un servo.** El servo recibe un pulso cada 20 ms (50 Hz). Lo que importa es **cuánto dura el pulso**: unos 0,5 ms lo llevan a 0°, 1,45 ms a 90° y 2,4 ms a 180°. Dentro tiene un motor, unos engranajes y un potenciómetro que mide dónde está: corrige solo hasta llegar al ángulo pedido. El ESP32 genera el pulso por hardware con su periférico **LEDC**, el que también se usa para regular LEDs (PWM).

## Necesitas

- **El robot.** No hace falta Wi-Fi ni portátil, salvo para flashearlo.
- **Un servo SG90** (o MG90S, que es igual pero con engranajes de metal).
- **Un cable Grove** (HY2.0-4P) con un extremo libre, o un cable Grove a pines Dupont.

## Conexiones

El robot tiene tres conectores Grove (HY2.0-4P). El servo va al **PORT.B**: sus pines G9 y G8 están libres, mientras que el PORT.A lleva un bus I2C y el PORT.C, una UART.

| Puerto | Negro | Rojo | Amarillo | Blanco |
|---|---|---|---|---|
| PORT.A | GND | 5 V | G2 | G1 |
| **PORT.B** | **GND (−)** | **5 V (+)** | **G9** | **G8** |
| PORT.C | GND | 5 V | G17 | G18 |

| Cable del SG90 | Qué es | PORT.B |
|---|---|---|
| **Negro** (o marrón) | Masa, GND | Negro (−) |
| **Rojo** | Alimentación, 5 V | Rojo (+) |
| **Azul** (o naranja o amarillo) | Señal | Amarillo (G9) o blanco (G8) |

El firmware saca el mismo pulso por G9 y por G8, así que la señal funciona en cualquiera de los dos. Comprobado con un SG90 real.

> ⚠️ Los colores del SG90 cambian según el fabricante. El rojo siempre es 5 V y el más oscuro (negro o marrón) es masa: **el que queda es la señal**. **Nunca pongas la señal en +5 V**.

## Paso a paso

1. **Con el robot apagado**, conecta el servo al PORT.B como en la tabla.
2. **Conecta el robot por USB-C y ejecuta:**
   ```bash
   demos/08-servo-sg90/run.sh          # Windows: demos\08-servo-sg90\run.bat
   ```
   Compila, sube el firmware y abre el monitor serie. Al arrancar, el servo va a 90°.
3. **Mueve el servo** desde la pantalla:
   - **arrastra la barra** para elegir el ángulo;
   - **0°, 90°, 180°:** van directos a esa posición;
   - **Barrido:** va y viene de un extremo a otro hasta que pulsas «Parar».

   El dial muestra en naranja dónde está el brazo, en azul a dónde va, y el ancho del pulso en microsegundos.
4. **Desde el ordenador**, con `.venv/bin/pio device monitor -d demos/08-servo-sg90/firmware`, escribe `angulo 45` y Enter.

## Qué puede pasar

| Síntoma | Causa y solución |
|---|---|
| No se mueve | Revisa que la señal vaya al amarillo o al blanco del **PORT.B**. Comprueba el 5 V: si el servo no hace ni un ruido, el + y el − no están bien. |
| Zumba o tiembla en los extremos | Ese SG90 no llega a 0° o a 180° y empuja contra su tope. Estrecha el pulso: `MIN_US = 600` y `MAX_US = 2300` en `main.cpp`. Algunos solo admiten de 1000 a 2000 µs. |
| Gira menos de 180° | Al revés que el anterior: amplía el rango (`MIN_US = 400`, `MAX_US = 2500`) con cuidado, sin que llegue a zumbar. |
| El robot se reinicia al moverlo | El servo atascado puede pedir 0,6 A y hundir los 5 V. No lo bloquees con la mano, y mejor sin LEDs a tope ni altavoz alto. |

## Cómo funciona

`firmware/src/main.cpp`:

| Parte | Para qué |
|---|---|
| `ledcSetup(0, 50, 14)` + `ledcAttachPin(9, 0)` y `(8, 0)` | PWM a 50 Hz con 14 bits: el periodo de 20 ms se divide en 16 384 pasos de 1,2 µs. Un canal puede mover varios pines: el pulso sale por G9 y G8. |
| `writeServo()` | Ángulo → microsegundos (`MIN_US`…`MAX_US`) → valor del LEDC. |
| `SPEED_DEG_S` | Limitador de velocidad: el servo avanza como mucho 180° por segundo, a pasos cada 20 ms. Los saltos bruscos piden picos de corriente, igual que con los servos de la cabeza. |
| `drawDial()` | El dial, dibujado en un sprite (memoria) y enviado de golpe a la pantalla para que no parpadee. |

## Para seguir

- **Más servos:** quita G8 de `SERVO_PINS` y úsalo con otro canal LEDC para un segundo servo.
- **Que reaccione:** que levante una bandera cuando le toques la cabeza, o junto con la demo 06, que se mueva con los botones del mando de la tele.
- **Por voz:** junto con la demo 02, una función de Gemini `mover_brazo(grados)` que mande `angulo N`.
