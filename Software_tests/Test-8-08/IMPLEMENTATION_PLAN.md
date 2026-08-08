# Plan de Implementación: Librería APRS TX (AX.25 / AFSK 1200 baudios)

Este documento detalla el diseño arquitectónico y mapeo de recursos para la librería concurrente de transmisión APRS utilizando el DRA818V.

## Asignación de Recursos de Hardware
* **Salida Analógica AFSK (DAC):** `PA10` (Canal 1 del DAC - `STM_TO_MIC_Pin`).
* **Timer Disparador (Timer Trigger para el DAC):** `TIM2` (Configurado como base de tiempo para generar el Update Event `TRGO` a la frecuencia de muestreo del audio).
* **Controlador DMA:** `DMA1_Channel1` (o el canal disponible asignado a `DAC_CH1` por STM32) en modo **Circular**, tamaño de dato `Half-Word` (16 bits) hacia periférico (DAC).
* **Control PTT:** `PA6` (`STM_TO_DRA_PTT_Pin`).
* **Habilitación de DRA818V (Power):** *Nota:* No se encontró el macro explícito `STM32_TO_DRA_ENA` en `main.h`. Se utilizará un pin general o uno que asigne el usuario en la validación (se dejará pendiente o se usará `RF_CTRL_TO_STM_Pin` en `PA4` preventivamente).
* **UART para Comandos AT:** `USART1` (TX en `PB6`, RX en `PB7`).

## Arquitectura FreeRTOS
* **Cola de Mensajes (Message Queue):** `aprs_tx_queue`. Tamaño: 5 elementos. Contendrá una estructura con callsigns, ruta, payload y longitud.
* **Tarea de Transmisión (`APRS_TX_Task`):**
  * **Prioridad:** Normal.
  * **Stack Size:** `512` words (2048 bytes) para holgura durante el cálculo de FCS y buffers de audio.
* **Mutex de Energía (`RF_Power_Mutex`):** Un mutex global que debe ser adquirido antes del PTT para evitar concurrencia de consumo de energía con la radio Sub-GHz interna del STM32WL.
* **Notificaciones de Tarea (Task Notifications):** Se usará `vTaskNotifyGiveFromISR()` desde las interrupciones Half y Full del DMA para que la tarea `APRS_TX_Task` calcule y rellene el siguiente tramo del buffer de audio.

## Estructura de Buffers (Ping-Pong)
* Se usará un doble buffer lineal (Ping-Pong) de tamaño suficiente para alojar X muestras de audio.
* La ISR notificará a la tarea enviando un valor que indica si debe llenar la mitad "A" (Half) o la mitad "B" (Full).

## Open Questions

> [!WARNING]
> **1. Pin Enable (ENA) del DRA818V:** En la descripción indicaste la etiqueta `STM32_TO_DRA_ENA`, pero en `main.h` no encuentro dicho pin (solo `RF_CTRL_TO_STM_Pin` en `PA4`). ¿Debo agregar un pin nuevo, o omito el ENA y asumo que siempre está encendido?
> **2. Timer y DMA:** Propongo usar `TIM2` y `DMA1_Channel1` para el DAC, ya que `TIM1` se utiliza como Timebase en el HAL de este proyecto. ¿Estás de acuerdo con esta asignación?

## Tareas a Completar (Task List)

- [x] 1. Crear el archivo `aprs_tx.h` con tipos, enumeraciones, estructuras y prototipos requeridos.
- [x] 2. Implementar `aprs_tx.c` (Inicialización de cola, mutex, UART commands para DRA y el esqueleto de la tarea FreeRTOS).
- [x] 3. Implementar la máquina de estados AX.25 en `aprs_tx.c` (Cálculo del preámbulo, banderas, Bit Stuffing, CRC-16 y NRZI).
- [x] 4. Implementar la generación de la forma de onda (LUT AFSK) y el relleno del Ping-Pong buffer.
- [x] 5. Integrar llamadas `APRS_DMA_HalfTransfer_ISR` y `APRS_DMA_FullTransfer_ISR` dentro de `stm32wlxx_it.c`.
- [x] 6. Proveer el código de inicialización y configuración de `TIM2` y `DMA1` necesario para el funcionamiento del AFSK en DAC.
- [x] 7. Compilar autónomamente usando `cmake` y resolver advertencias o errores.
