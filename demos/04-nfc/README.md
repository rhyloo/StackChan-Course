# Demo 04 · Lector NFC

La base del Stack-chan lleva un chip NFC **ST25R3916** de ST ([datasheet](../../docs/hardware/datasheets/STMicroelectronics_ST25R3916_NFC_Datasheet.pdf)), en el bus I2C interno (dirección `0x50`). Es un lector **y escritor** completo: lee y escribe etiquetas, y hasta puede hacerse pasar por una (emulación). En esta demo el robot solo **lee**. Acercas una tarjeta o una pegatina NFC, el robot pita, pone los LEDs de la base en verde y enseña en pantalla lo que lleva dentro.

```
 etiqueta NFC  )))  13,56 MHz  (((  ST25R3916 (base) ──I2C──> CoreS3 ──> pantalla
                                                                       └─> monitor serie
```

**NFC** (*Near Field Communication*) es radio a 13,56 MHz que solo alcanza unos centímetros. El lector crea un campo magnético que alimenta la etiqueta, así que la etiqueta no necesita pila. Las etiquetas guardan sus datos en **NDEF**, un formato estándar con uno o varios registros: un texto, una URL, un contacto…

## Necesitas

- **El robot.** No hace falta Wi-Fi ni portátil, salvo para flashearlo.
- **Etiquetas NFC-A** (ISO 14443A): pegatinas o tarjetas NTAG213/215/216, MIFARE Ultralight, Classic o DESFire. Muchas tarjetas de transporte, de hotel o de acceso son de estos tipos.
- Opcional: un móvil con NFC y la app **NFC Tools** para escribir en las etiquetas tus propios textos o URL.

## Paso a paso

1. **Conecta el robot por USB-C y ejecuta:**
   ```bash
   demos/04-nfc/run.sh          # Windows: demos\04-nfc\run.bat
   ```
   Compila el firmware, lo sube al robot y deja abierto el monitor serie, que también se guarda en `logs/`.
2. En la pantalla sale *«NFC: acerca una etiqueta»*.
3. **Acerca la etiqueta a la base**, a uno o dos centímetros, y déjala quieta un momento. La pantalla enseña:
   - el **UID** (el número de serie de la etiqueta);
   - el **tipo** y la **memoria** que tiene;
   - los **registros NDEF** uno a uno: su tipo (`T` = texto, `U` = URL…) y lo que llevan.
4. Si la etiqueta no lleva NDEF, la pantalla lo dice y el **monitor serie** muestra el volcado de su memoria, página a página.
5. Para leer la misma etiqueta otra vez, apártala y vuelve a acercarla. Un toque en la pantalla vuelve a la pantalla de espera.

## Qué puede salir

| En pantalla | Qué significa |
|---|---|
| Registros `T` / `U` | Etiqueta con NDEF: texto o URL, ya decodificados. |
| *Sin datos NDEF* | Etiqueta vacía o con un formato propio (tarjetas de acceso). Mira el monitor serie. |
| *MIFARE Classic: sus sectores van con clave* | Sin la clave del sector no se pueden leer sus datos. El volcado lo intenta con la clave por defecto. |
| *No se pudo identificar* | La etiqueta se movió o estaba en el borde del campo. Acércala más y déjala quieta. |
| *No encuentro el lector* | El ST25R3916 no responde por I2C. Comprueba que el CoreS3 está bien encajado en la base. |

## Cómo funciona

`firmware/src/main.cpp` usa la librería oficial [M5Unit-NFC](https://github.com/m5stack/M5Unit-NFC) (`m5stack/M5Unit-NFC@0.1.2`). Lleva el driver del ST25R3916 y las capas NFC-A y NDEF.

- `boardInit()` (de `lib/StackChanHW`) alimenta la base y `UnitNFC` se registra en `M5.In_I2C`, el bus interno.
- Cada 200 ms, `NFCLayerA::detect()` busca etiquetas.
- `identify()` averigua el modelo exacto y `ndefRead()` lee el mensaje NDEF.
- `deactivate()` deja la etiqueta en reposo, así no se vuelve a leer mientras sigue encima.

## Para seguir

- **Escribir:** `NFCLayerA::ndefWrite()` graba un mensaje NDEF. El ejemplo `examples/UnitUnified/NFCA/NDEF` de la librería escribe texto y una URL. Ojo: pasar una MIFARE Ultralight a NDEF no tiene vuelta atrás, y en las DESFire borra lo que hubiera.
- **Otros estándares:** el chip también lee NFC-B, NFC-F (FeliCa) y NFC-V (ISO 15693), con las clases `NFCLayerB`, `NFCLayerF` y `NFCLayerV`.
- **Que el robot reaccione:** según el texto de la etiqueta, mover la cabeza, cambiar los LEDs (sin pasar de `LED_MAX_LEVEL`) o decir algo.
