# Demo 06 · Infrarrojos: el robot como mando a distancia

La base del Stack-chan tiene un **receptor de infrarrojos** (IRM-56384, pin G10) y un **LED emisor** (pin G5), los dos a 38 kHz: la frecuencia de casi todos los mandos de tele, aire acondicionado o equipo de música. En esta demo:

1. **Lee mandos.** Apuntas un mando al robot y pulsas un botón. La pantalla muestra el protocolo (NEC, Sony, Samsung, RC5…), la dirección, la orden y el código.
2. **Les da color.** Cada botón pinta la pantalla y los LEDs de la base de un color. Los números del mando Samsung tienen el suyo y salen en grande (1 rojo, 2 naranja, 3 amarillo…); el resto de botones de cualquier mando, un color sacado de su código, siempre el mismo.
3. **Los repite.** El robot se queda con el último botón. Tocas su pantalla y lo reenvía por su LED: ahora el robot es el mando.

```
mando ))) 38 kHz ))) receptor G10 ──> CoreS3: decodifica ──> pantalla y monitor serie
                                         │
tele  ((( 38 kHz ((( emisor G5   <───────┘ al tocar la pantalla, repite el último código
```

**Cómo funciona un mando.** El LED del mando parpadea 38 000 veces por segundo, a ráfagas. La duración de las ráfagas y de los silencios codifica unos y ceros. Cada fabricante tiene su protocolo: en NEC, por ejemplo, van 32 bits con una dirección (qué aparato) y una orden (qué botón). El receptor filtra todo lo que no sea 38 kHz, así que la luz del sol o de las lámparas apenas le afecta.

## Necesitas

- **El robot.** No hace falta Wi-Fi.
- **Un mando a distancia** de infrarrojos: el de la tele, el del aire, el de un ventilador… Los mandos por Bluetooth o radio, como muchos de las teles modernas, no sirven. Si al pulsar un botón ves parpadear el LED del mando a través de la cámara del móvil, es de infrarrojos.

## Paso a paso

1. **Conecta el robot por USB-C y ejecuta:**
   ```bash
   demos/06-infrarrojos/run.sh          # Windows: demos\06-infrarrojos\run.bat
   ```
   Compila el firmware, lo sube al robot y deja abierto el monitor serie, que también se guarda en `logs/`.
2. **Apunta el mando a la base del robot**, a menos de 2 o 3 m, y pulsa un botón. El robot pita, la pantalla y los LEDs de la base se ponen del color de ese botón, y abajo sale el protocolo y el código. Pulsa los números del 0 al 9.
3. **Toca la pantalla del robot** con la base apuntando al aparato: el robot repite el último botón y los LEDs dan un destello morado. Prueba con el botón de encender de la tele.
4. **Monitor serie:** cada botón sale también como código fuente, listo para copiar a tu programa:
   ```
   Protocol  : NEC
   Code      : 0x20DF10EF (32 Bits)
   uint16_t rawData[71] = {9024, 4462,  598, 520, ...};
   uint32_t address = 0x4;
   uint32_t command = 0x8;
   uint64_t data = 0x20DF10EF;
   ```

## Qué puede salir

| En pantalla | Qué significa |
|---|---|
| `NEC`, `SONY`, `SAMSUNG`, `RC5`… | Protocolo conocido: se ven la dirección y la orden, y se reenvía tal cual. |
| *(repetición)* | Mantienes el botón pulsado: el mando manda un aviso corto de «sigo pulsado». El robot no lo guarda. |
| *Aire acondicionado: N bits* | Los aires mandan todo su estado de golpe (temperatura, modo, ventilador). El monitor serie lo traduce, por ejemplo `Power: On, Mode: Cool, Temp: 24C`. |
| `UNKNOWN` (N pulsos) | Protocolo que la librería no conoce. Se guardan los pulsos en bruto y se repiten pulso a pulso, y casi siempre funciona igual. |
| No sale nada | Acércate más, apunta a la base y no al CoreS3, y comprueba que el mando es de infrarrojos. |

## Dónde programar

`firmware/src/main.cpp` usa la librería [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) (`crankyoldgit/IRremoteESP8266@2.9.0`), que conoce más de 100 protocolos.

| Parte | Para qué |
|---|---|
| `IRrecv s_rx(...)`, `s_rx.decode()` | Recibir. `FRAME_GAP_MS` es el silencio que da por terminado un mensaje: 15 ms basta para las teles, los aires necesitan 50. |
| `BOTONES[]` | Botones con nombre y color propio. Vienen los números del mando Samsung; añade los tuyos copiando el protocolo y el código del monitor serie: `{ NEC, 0x20DF10EF, "ON", 255, 0, 0 },`. |
| `colorFromCode()` | El color de los botones que no están en la tabla. |
| `learn()` | Guardar el último código: valor y bits, el estado entero si es un aire, o los pulsos en bruto si es desconocido. |
| `sendLearned()` | Enviar. Apaga el receptor mientras emite para no oírse a sí mismo. |

## Para seguir

- **Un mando para el robot:** asigna botones del mando de la tele a acciones del robot (en `loop()`, compara `s_res.command`) y mueve la cabeza con `servoInit()` y `servoSetTarget()` de `StackChanHW`.
- **Mando universal por voz:** junta esta demo con la 02, para que «apaga la tele» envíe el código guardado.
- **Guardar varios botones:** una lista de códigos con nombre en vez de solo el último, y las tres zonas táctiles de la base para elegir cuál enviar.
- **Controlar el aire:** con `IRac` se fabrica el mensaje desde cero (`ac.next.degrees = 24; ac.sendAc();`) sin haberlo aprendido antes.
