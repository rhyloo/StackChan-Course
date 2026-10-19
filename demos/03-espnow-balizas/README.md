# Demo 03 · Balizas ESP-NOW

Tres placas ESP32 hacen de balizas y emiten un mensaje [ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/network/esp_now.html) diez veces por segundo. El Stack-chan las escucha y mide con qué intensidad le llega cada una (RSSI). Con eso:
- **sabe qué baliza tiene más cerca:** la muestra en pantalla, pone los LEDs de la base de su color y le pide por ESP-NOW que encienda su LED;
- **el portátil estima su posición** en un mapa (trilateración).

```
 baliza 1 ))) ESP-NOW (((                          ┌───────────────┐
 baliza 2 ))) ESP-NOW ((( ──>  Stack-chan  ──UDP──>│ portátil:     │
 baliza 3 ))) ESP-NOW (((      (receptor)  5602    │ mapa con la   │
            <── "enciende tu LED" ─┘               │ posición      │
                                                   └───────────────┘
```

**ESP-NOW** es un protocolo de Espressif para que dos ESP32 se manden mensajes cortos (hasta 250 bytes) directamente, sin router ni conexión previa, con muy poca latencia. Sirve para mandos, sensores y enjambres de robots.

## Necesitas

- **El robot**, que hace de receptor.
- **3 placas ESP32 DevKit** clásicas (ESP32-WROOM), una por baliza, alimentadas por USB o con una batería USB.
- **El portátil.**

## Paso a paso

1. **Flashea las tres balizas.** El script te pide que conectes cada placa, una detrás de otra:
   ```bash
   demos/03-espnow-balizas/run.sh balizas      # Windows: demos\03-espnow-balizas\run.bat balizas
   ```
   A mano sería `./tools/flash.sh demos/03-espnow-balizas/baliza --env baliza1` (y `baliza2`, `baliza3`).
   En el monitor verás `Baliza 1 lista: canal 1`. El LED azul da un destello por segundo.
2. **Conecta el robot y ejecuta** `demos/03-espnow-balizas/run.sh` (Windows: `run.bat`). Sube el receptor al robot y abre el mapa del paso 5; ciérralo con Ctrl+C para hacer antes los pasos 3 y 4.
3. **Coloca las balizas** formando un triángulo grande, de 3 a 5 m de lado, en alto y sin objetos metálicos alrededor. Mide sus posiciones en metros y escríbelas en `portatil/balizas.json`.
4. **Calibra.** Pon el robot a 1 m de una baliza, sin nada en medio, y ejecuta:
   ```bash
   .venv/bin/python demos/03-espnow-balizas/portatil/posicion.py --calibrar 1
   ```
5. **Mira el mapa:**
   ```bash
   .venv/bin/python demos/03-espnow-balizas/portatil/posicion.py          # --texto para verlo sin ventana
   ```
   Mueve el robot por la habitación. El círculo de cada baliza es la distancia estimada; el punto blanco, la posición.

## Cómo funciona

**Mismo canal.** ESP-NOW no necesita router, pero emisor y receptor tienen que estar en el **mismo canal** de radio. El robot está conectado a la Wi-Fi, que fija su canal. Por eso cada baliza busca esa red al arrancar (con el nombre de `config/wifi_secrets.h`) y se pone en su canal, aunque no se conecta a ella.

**Del RSSI a la distancia**, con el modelo log-distancia:

```
rssi = rssi_1m − 10·n·log10(d)      →      d = 10^((rssi_1m − rssi) / (10·n))
```
- `rssi_1m` es lo que se mide a 1 m (se calibra).
- `n` es el exponente de pérdidas: 2 en espacio libre, de 2,5 a 4 en interiores.

**Trilateración.** Con tres distancias, se busca el punto cuyas distancias a las tres balizas se parecen más a las medidas (mínimos cuadrados, método de Gauss-Newton).

**El RSSI del callback.** En Arduino-ESP32 2.x, el callback de ESP-NOW no trae el RSSI. El receptor activa también el modo promiscuo, ve la trama 802.11 completa y saca el RSSI de su cabecera.

**Expectativas realistas.** En interiores la señal rebota en paredes, muebles y personas, y tu propio cuerpo la atenúa. Espera errores de 1 a 3 m en la posición. Lo fiable es «qué baliza tengo más cerca».

Ideas para mejorar la precisión:
- más balizas;
- un filtro de Kalman;
- calibrar `n`;
- un mapa de huellas (*fingerprinting*): medir el RSSI en puntos conocidos y comparar.

## Dónde programar

| Fichero | Para qué |
|---|---|
| `espnow_msg.h` | Los mensajes ESP-NOW (baliza → robot y robot → baliza). Añade tipos nuevos aquí. |
| `baliza/src/main.cpp` | La baliza: qué emite y qué hace al recibir órdenes |
| `receptor/src/main.cpp` | El robot: suavizado del RSSI, pantalla, LEDs, órdenes a las balizas. Idea: girar la cabeza hacia la baliza más cercana con `servoSetTarget()` de `StackChanHW`. |
| `portatil/posicion.py` | Modelo de distancia, trilateración y mapa |
| `portatil/balizas.json` | Posición de las balizas y calibración |

## Problemas frecuentes

| Síntoma | Solución |
|---|---|
| La baliza repite «No encuentro la red» | La Wi-Fi de `config/wifi_secrets.h` tiene que estar al alcance y ser de 2,4 GHz. Si la cambias, reflashea las balizas. |
| El robot dice «esperando…» en todas | La baliza y el robot están en canales distintos: reinicia la baliza después de que el robot se conecte a la Wi-Fi. |
| El mapa no recibe nada | El portátil y el robot tienen que estar en la misma red, con UDP 5602 abierto en el cortafuegos. |
| La posición salta mucho | Es el RSSI. Sube el suavizado (`RSSI_ALPHA` en el receptor, `0.7/0.3` en `posicion.py`) o recalibra. |
