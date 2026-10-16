# Stack-chan por dentro

Guía de referencia del robot para quien no lo ha visto nunca. Cada dato lleva su fuente en `docs/hardware/`. Lo que no se ha podido comprobar está marcado como **sin verificar**.

Stack-chan es un **M5Stack CoreS3** (el "cerebro" con pantalla y cámara) montado sobre la **base StackChan K151** de M5Stack, que añade dos servos para mover la cabeza, batería, LEDs, infrarrojos, NFC y un panel táctil.

| | |
|---|---|
| Tamaño | 54,0 × 70,5 × 61,5 mm |
| Peso | 187,2 g |
| Manual oficial | [M5Stack_StackChan_K151_User_Manual.pdf](hardware/manuals/M5Stack_StackChan_K151_User_Manual.pdf) |
| Planos | [StackChan_Mechanical_Model_Size.pdf](hardware/manuals/StackChan_Mechanical_Model_Size.pdf) |

```
            ┌────────────── CoreS3 (cabeza) ──────────────┐
            │ ESP32-S3 · pantalla 2" táctil · cámara GC0308│
            │ IMU 9 ejes · 2 micros · altavoz · luz/prox.  │
            └──────────────┬───────────────────────────────┘
                           │ M5-Bus (30 pines): I2C, UART servos, IR, batería, 5 V
            ┌──────────────┴──────── base K151 ────────────┐
            │ PY32L020 (I2C 0x6F): enciende servos, 12 LEDs │
            │ servo pan (ID 1) · servo tilt (ID 2) · 550 mAh │
            │ IR · NFC · táctil 3 zonas · INA226 (batería)   │
            └───────────────────────────────────────────────┘
```

## 1. CoreS3

Fuente: [M5Stack_CoreS3_User_Manual.pdf](hardware/manuals/M5Stack_CoreS3_User_Manual.pdf) y [esquemático](hardware/schematics/M5Stack_CoreS3_Schematic_v1.0.pdf).

| Componente | Detalle |
|---|---|
| Procesador | ESP32-S3: Xtensa LX7 de doble núcleo a 240 MHz, 512 KB de SRAM ([datasheet](hardware/datasheets/ESP32-S3_Datasheet_Espressif.pdf)) |
| Memoria | 16 MB de flash y 8 MB de PSRAM (Quad SPI) |
| Radio | Wi-Fi 2,4 GHz b/g/n (**no ve redes de 5 GHz**) y Bluetooth LE 5. No tiene Bluetooth clásico: por eso el mando de PS4 va emparejado al móvil. |
| Pantalla | 2,0" IPS, 320×240, controlador ILI9342C por SPI. Táctil capacitivo FT6336U. |
| Cámara | GC0308 de 0,3 MP (VGA 640×480 como máximo). **No comprime en JPEG por hardware**: entrega RGB565/YUV y el JPEG lo hace el procesador por software ([datasheet](hardware/datasheets/GalaxyCore_GC0308_Camera_Datasheet.pdf)). Usamos 320×240 a unos 12 fps. |
| Movimiento | IMU BMI270 (acelerómetro y giroscopio) + magnetómetro BMM150. Los imanes del robot molestan al magnetómetro. |
| Otros sensores | Luz y proximidad LTR-553, encima de la cámara |
| Audio | 2 micrófonos (códec ES7210) y altavoz de 1 W (amplificador AW88298) |
| Reloj | RTC BM8563 |
| Almacenamiento | Ranura microSD, hasta 16 GB |
| Energía | PMIC AXP2101 (raíles, carga y nivel de batería); expansor de E/S AW9523B (resets y habilitaciones) |
| USB | USB-C **nativo** del ESP32-S3 (VID:PID `303A:1001`). No hay chip USB-serie, así que no hacen falta drivers CP210x ni CH34x. |
| Tamaño | 54 × 54 × 15,5 mm |

### Botones

Están a los lados del CoreS3.

| Botón | Acción |
|---|---|
| **POWER** (izquierda, junto al USB-C) | Pulsación corta: encender. 6 s: apagar. |
| **RST** (abajo, junto a la microSD) | Pulsación corta: reiniciar. 3 s: modo descarga (ver apartado 6). |

No hay botones de usuario. Nuestro firmware usa la pantalla táctil: un toque centra la cabeza.

### Bus I2C interno (SDA G12, SCL G11)

Lo comparten el CoreS3 y la base:

| Dirección | Chip | | Dirección | Chip |
|---|---|---|---|---|
| 0x10 | BMM150 magnetómetro (bus auxiliar) | | 0x41 | INA226 monitor de batería (base) |
| 0x21 | GC0308 cámara (SCCB) | | 0x50 | ST25R3916 NFC (base) |
| 0x23 | LTR-553 luz/proximidad | | 0x51 | BM8563 RTC |
| 0x34 | AXP2101 PMIC | | 0x58 | AW9523B expansor |
| 0x36 | AW88298 amplificador | | 0x68 | Si12T táctil 3 zonas (base) |
| 0x38 | FT6336U táctil | | 0x69 | BMI270 IMU |
| 0x40 | ES7210 micrófonos | | 0x6F | PY32L020 coprocesador (base) |

### Pines útiles

| Uso | Pines |
|---|---|
| Servos (UART1, 1 Mbps) | TX G6, RX G7 |
| Infrarrojos | Emisor G5, receptor G10 (38 kHz) |
| Cámara | D0–D7 = G39, G40, G41, G42, G15, G16, G48, G47; PCLK G45; VSYNC G46; HREF G38. XCLK sale de un cristal del flex. RESET va en AW9523 P1_0. |
| Pantalla (SPI) | MOSI G37, SCK G36, CS G3, DC G35 |
| microSD | Mismo SPI, MISO G35, CS G4 |
| Audio (I2S) | MCLK G0, BCK G34, WS G33, DOUT G13, DIN G14 |
| Grove Port A (I2C) | SDA G2, SCL G1 |
| Grove Port B | G9, G8 |
| Grove Port C (UART) | TX G17, RX G18 |

Conector Grove: negro GND, rojo 5 V, amarillo y blanco señales. El mapa completo de pines del ESP32-S3 está en [ESP32-S3_Pinout_Reference.pdf](hardware/pinouts/ESP32-S3_Pinout_Reference.pdf).

## 2. Servos Feetech SCS0009

Uno mueve el giro horizontal (**pan**, ID 1) y otro el cabeceo (**tilt**, ID 2). Fuente: [Feetech_SCS0009_Servo_Datasheet.pdf](hardware/datasheets/Feetech_SCS0009_Servo_Datasheet.pdf).

| | a 4,8 V | a 6 V |
|---|---|---|
| **Par de bloqueo** | 1,89 kg·cm (0,185 N·m) | 2,3 kg·cm (0,226 N·m) |
| Par nominal | 0,65 kg·cm (0,064 N·m) | 0,75 kg·cm (0,074 N·m) |
| Velocidad en vacío | 0,125 s/60° (80 rpm) | 0,10 s/60° (100 rpm) |
| Corriente en vacío / nominal / bloqueo | 120 / 300 / 800 mA | 150 / 400 / 1000 mA |

En la base los servos van a **unos 5 V**, generados por un elevador TPS61088 desde la batería. Sus valores reales estarán entre las dos columnas (**sin verificar**).

**Características**
- Alimentación: 4–7,4 V.
- Engranajes de cobre y acero, reducción 1:416, holgura ≤ 0,5°.
- Peso: 13,2 g.
- Mide la posición con un **potenciómetro** de 330°, no con un encoder.
- Protecciones:
  - sobrecarga: si pasa del 80 % del par de bloqueo durante 2 s, corta el par;
  - tensión fuera del rango 4–9 V;
  - temperatura por encima de 70 °C.
- Rango en modo posición: 0–1023 pasos (unos 300°).

**Protocolo.** Serie TTL *half-duplex* por un solo hilo, 8N1, 1 Mbps. Paquete:

```
FF FF  ID  LEN  INSTR  PARAM...  CHK        LEN = nº de parámetros + 2
                                            CHK = ~(ID + LEN + INSTR + Σ PARAM) & 0xFF
```

Instrucciones:

| Código | Instrucción |
|---|---|
| `0x02` | Leer |
| `0x03` | Escribir |

Registros más usados (los valores de 2 bytes van con el **byte alto primero**):

| Registro | Uso |
|---|---|
| `0x05` | ID |
| `0x06` | Baudios |
| `0x28` | Activar par (1/0) |
| `0x2A` | Posición objetivo (2 bytes) |
| `0x2C` | Tiempo para llegar, en ms |
| `0x2E` | Velocidad |
| `0x38` | Posición actual (lectura) |

Implementación: `firmware/src/board.cpp` (`scsSend`, `scsWritePos`).

**Escala.** El datasheet da 3,41 pasos/°, pero el firmware oficial de Stack-chan y el nuestro usan **3,2 pasos/°** (183,35 pasos/rad). Cuál es exacta está **sin verificar**; la diferencia es de un 6 %.

## 3. Base K151

Fuente: manual K151 y esquemáticos en [hardware/schematics/](hardware/schematics/).

- **Batería de 550 mAh.** Va al pin BAT del M5-Bus, así que la carga el AXP2101 del CoreS3 al enchufar el USB-C (inferido del esquemático). Corriente de carga y autonomía: **sin verificar**.
- **INA226** (0x41) mide la corriente de la batería con un shunt de 10 mΩ.
- **Alimentación de los servos.** Un elevador TPS61088 genera 5 V ("VM") desde la batería. Solo se enciende cuando el PY32 activa `VM_EN`.
- **12 LEDs RGB** WS2812C en dos filas, gobernados por el PY32.
- **Infrarrojos**: emisor y receptor IRM-56384 (38 kHz).
- **NFC** ST25R3916.
- **Panel táctil** superior de 3 zonas (Si12T).
- Agujeros compatibles con LEGO.

### Coprocesador PY32L020 (I2C 0x6F)

Microcontrolador Cortex-M0+ que hace de expansor de E/S. Lo que usamos (`firmware/src/board.cpp`):

| Registro | Uso |
|---|---|
| `0x02` | Versión. Si es 0 o 0xFF, la base aún no ha arrancado. |
| `0x03`/`0x04` | Dirección de los pines (1 = salida) |
| `0x05`/`0x06` | Salida. **Bit 0 de `0x05` = VM_EN** (enciende los servos). |
| `0x09`–`0x0C` | Pull-up / pull-down |
| `0x13`/`0x14` | Tipo de salida: 1 = drenador abierto, 0 = push-pull. El pin 13 de los LEDs va en push-pull. |
| `0x24` | Bits 0–5: número de LEDs (12). Bit 6: refrescar. |
| `0x30`… | Color de cada LED: 2 bytes RGB565, byte bajo primero |

## 4. Límites de movimiento y calibración

Están en `firmware/include/config.h`. La app los recibe del robot y nunca pide valores fuera de ellos.

| Eje | Ángulo | Radianes | Pasos permitidos | Centro |
|---|---|---|---|---|
| Pan (ID 1) | −130° … +160° | −2,2689 … +2,7925 | 44 … 972 | 460 |
| Tilt (ID 2) | −2,8° … +27,5° | −0,050 … +0,480 | 611 … 708 | 620 |

**Por qué estos límites**
- **Pan no llega a ±180°.** Con el centro en el paso 460, girar −180° exigiría pasos negativos. Los extremos dejan margen respecto a los límites del registro (0 y 1023).
- **Tilt.** Por debajo de −2,8° la barbilla choca con la base. M5 recomienda mantener el servo vertical entre 5° y 85° en *su* referencia de ángulo, porque en los extremos puede bloquearse. Cómo se corresponde con la nuestra está **sin verificar**: no amplíes el tilt sin comprobarlo con el robot delante.
- **Velocidad limitada.** El bucle de servos va a 25 Hz y avanza como mucho unos 94°/s en pan y 47°/s en tilt. Los saltos bruscos provocan picos de corriente que reinician la base (*brownout*).
- **Centros.** 460 y 620 son los mismos que usa el firmware oficial de Stack-chan.

## 5. Software

| | |
|---|---|
| Entorno | PlatformIO, plataforma `espressif32@7.1.2` (`firmware/platformio.ini`) |
| Framework | Arduino-ESP32 2.0.17 (sobre ESP-IDF 4.4.7) |
| Librería | M5Unified 0.2.21: pantalla, táctil, altavoz, PMIC e IMU |
| Compilador | xtensa-esp32s3 GCC 8.4.0 |
| Velocidad de subida | 921 600 baudios (a 1,5 M falla en algunos Linux) |

**Alternativas**
- **Arduino IDE:** añade la URL de placas `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`, instala "M5Stack" y elige *M5CoreS3*.
- **ESP-IDF** y **UiFlow2:** también funcionan.

### Puerto USB en cada sistema

| Sistema | Puerto | Qué hace falta |
|---|---|---|
| Linux | `/dev/ttyACM0` | Estar en el grupo `dialout` y las reglas udev de PlatformIO (`setup.sh` lo hace). Si no aparece, desinstala `brltty`. |
| Windows 10/11 | `COMx` | El driver se instala solo, si hay Internet. Si en el Administrador de dispositivos sale dos veces "USB JTAG/serial debug unit" sin puerto COM, sigue la [guía de Espressif](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-usb-driver.html). |

El puerto USB lo crea el propio programa. Si el firmware se cuelga o entra en reposo profundo, el puerto desaparece: usa el modo descarga.

## 6. Recuperación

**Modo descarga** (cuando el ordenador no ve el robot o el firmware está colgado):
1. Conecta el USB-C. M5 recomienda el de la base, para que los motores no se muevan solos.
2. Mantén pulsado **RST** unos 3 s, hasta que el LED de al lado se ponga **verde**, y suelta.
3. Flashea normalmente (`./tools/flash.sh` o el botón → de PlatformIO).
4. Pulsa RST una vez para arrancar el programa nuevo.

**Volver al firmware de fábrica de M5:**
1. Instala [M5Burner](https://docs.m5stack.com/en/download).
2. Busca "StackChan" con el filtro *Only Official*.
3. Pulsa *Burn*.

## 7. Datos sin verificar

- Consumo del robot completo y autonomía con la batería de 550 mAh.
- Corriente de carga de la batería de la base.
- Par y velocidad reales de los servos a 5 V.
- Escala real del servo: 3,2 o 3,41 pasos/°.
- El manual llama al pan "rotación continua de 360°". En modo posición, que es el que usamos, el rango es 0–1023.
- Cómo se unen TX (G6) y RX (G7) en el único hilo de señal de los servos.
