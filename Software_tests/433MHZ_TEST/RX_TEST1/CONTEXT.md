# RX_TEST1 — STM32WLE5 como receptor 433 MHz, ESP32 como transmisor

Prueba complementaria de `TX_TEST1`: se invierten los roles. El STM32WLE5CCU6 (radio SubGHz interna)
recibe y el ESP32-S3 + RA-02 (SX1278) transmite cada 5 s.

| Carpeta | Contenido |
|---|---|
| `TX_TEST1_STM32TRX/` | Proyecto STM32CubeIDE (mismo `.ioc`/pines que `TX_TEST1_STM32TX`). App RX en `SubGHz_Phy/App/subghz_phy_app.c`. |
| `TX_TEST1_ESP32TX/` | Proyecto ESP-IDF. Driver `sx1278_fsk.c` (mismo que el RX de TX_TEST1 + funciones TX). |
| `RX_TEST1_ESP32C6_SPI/` | Proyecto ESP-IDF (ESP32-C6). Maestro SPI que lee al STM32 y muestra los mensajes por USB. |

## Enlace SPI STM32 → ESP32-C6 (visor)

El STM32 es **esclavo** en SPI1 (como en `Test10_DRA_SPI`, ya probado en esta placa) y el ESP32-C6 es
**maestro**. Sin cambios de pines en el STM32.

| Señal | STM32 (placa) | ESP32-C6 |
|---|---|---|
| SCK | PA1 | GPIO6 |
| MOSI | PB5 | GPIO5 |
| MISO | PB4 | GPIO4 |
| CS (activo bajo, EXTI0) | PA0 | GPIO7 |
| GND | GND | GND (**obligatorio**) |

- Bus: Mode 0, MSB primero, 1 MHz. El C6 baja CS, espera 100 µs, manda cada byte con 20 µs de pausa y sube CS
  (mismo timing que Test10, para que el esclavo por interrupciones no pierda bytes). Lee cada 250 ms.
- Paquete fijo de **96 bytes** (`Core/Inc/spi_link.h` ⇄ `main/spi_link_proto.h`, deben coincidir):
  `0xAA`, status (`0x0A` hay mensajes / `0x0C` escuchando), `seq`, uptime, contadores OK/error,
  RSSI, CFO, FCS, bytes, remitente, texto del mensaje y checksum XOR.
- El STM32 re-arma la transferencia al terminar cada una; si un CS sube a mitad de paquete, resetea SPI1 y se
  resincroniza solo. La línea `[stat]` muestra `spi=` (transferencias) y `resync=`.
- El C6 imprime `MENSAJE #n (STM32 por SPI)` solo cuando cambia `seq`, y cada 5 s un `[spi] OK ...` o un
  diagnóstico (`Sin respuesta` = MISO fijo → cableado/GND/STM32 apagado).

## Parámetros de PHY (idénticos en ambos lados y a TX_TEST1)

| Parámetro | STM32 RX (`subghz_phy_app.h`) | ESP32 TX (`app_main.c`) |
|---|---|---|
| Frecuencia | `RF_FREQUENCY` 433 018 893 Hz | `RF_FREQUENCY_HZ` 433 018 893 Hz |
| Modulación | GFSK BT=0.5 | GFSK BT=0.5 |
| Bitrate | 1200 bps | 1200 bps |
| Desviación | 5 kHz | 5 kHz |
| Ancho de banda RX | 40 kHz → 46.9 kHz DSB (≈ 20.8 kHz SSB del SX1278) | — |
| Preámbulo | 8 bytes, detector 16 bits | 8 bytes (0x55) |
| Sync word | `C1 94 C1` | `C1 94 C1` |
| Longitud | variable (1 byte de longitud) | variable |
| CRC HW / whitening | OFF / OFF | OFF / OFF |
| Potencia TX | — | 16 dBm (PA_BOOST) |

Trama: AX.25 UI "Path A" (bytes crudos, sin NRZI ni bit-stuffing, FCS CRC-16 CCITT y 2× `0x7E` al final),
`FIUNA1-1 > CCTE-0`, info `Hola STM32! Soy el ESP32, msg #nnnnn` (36 bytes de mensaje, 56 en aire;
máximo 43 caracteres ASCII para no pasar los 63 bytes del FIFO del SX1278).

## Pines (sin cambios)

- STM32: PA9 `RF_CRL_TO_STM32` = switch RF BGS12WN6 (LOW = RX, lo maneja `RBI_ConfigRFSwitch`),
  SPI1 PA0/PA1/PB4/PB5, USART2 PA2/PA3 (va al GPS MAX-M10S, no a un USB), SWO en PB3.
- ESP32-S3: SCK 9, MISO 10, MOSI 11, CS 12, RST 13, DIO0 14, LED 21.

## Cómo probar

1. `idf.py -C TX_TEST1_ESP32TX build flash monitor`.
2. Flashear `TX_TEST1_STM32TRX` con el ST-LINK desde STM32CubeIDE (la placa no tiene USB-UART).
3. Visor principal: conectar el ESP32-C6 por SPI (tabla de arriba) y
   `idf.py -C RX_TEST1_ESP32C6_SPI set-target esp32c6 build flash monitor`.
   Alternativas sin el C6 (con el ST-LINK):

   **A. Consola SWV (texto completo, necesita ST-LINK con SWO y el pin PB3 cableado)**
   - *Debug Configurations → Debugger*: activar **Serial Wire Viewer (SWV)**, Core Clock = **48.0 MHz**.
   - Al entrar en debug: *Window → Show View → SWV → SWV ITM Data Console* → ⚙ → marcar **ITM port 0**
     → botón rojo **Start Trace** → Resume (F8).
   - Toda traza (`APP_PRINTF`) se copia a ITM en `vcom_Trace_DMA()` (`Core/Src/usart_if.c`).

   **B. Live Expressions (cualquier ST-LINK, solo SWD)**
   - En debug: *Window → Show View → Live Expressions* → agregar `RxMonitor`.
   - Campos: `total`, `ok`, `bad`, `rxErrors`, `bytes`, `msgBytes`, `rssiDbm`, `cfoKhz`, `fcsOk`,
     `from`, `message`. Se actualizan solos con el programa corriendo.
4. En la consola SWV debe verse `MENSAJE RECIBIDO #n`, `Hola! Estoy recibiendo datos de FIUNA1-1`, el mensaje,
   los bytes, RSSI/CFO, `FCS: OK` y el hex (igual al que imprime el ESP en `ENVIANDO MENSAJE #n`).

Nota: las trazas también siguen saliendo por USART2 hacia el RX del GPS (como en TX_TEST1). El u-blox
ignora ese texto porque no es NMEA ni UBX válido.
   Cada 10 s sale `[stat] ok=… fcs_fail=… err=… RSSI=…`.

Nota: el proyecto STM32 parte del esqueleto de `TX_TEST1_STM32TX` con las correcciones de la referencia
`test1_433` (RTC binario + IRQ de RTC/SUBGHZ, switch RF, DCDC off). Si se regenera con CubeMX, revisar
`rtc.c`, `stm32wlxx_it.c`, `subghz.c`, `radio_board_if.*` y los `FSK_*` de `subghz_phy_app.h`.
