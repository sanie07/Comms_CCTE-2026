/**
 * @file    app.c
 * @brief   Application top-level for STM32_test4_DRA — DRA818 handshake only.
 *
 * Purpose
 * -------
 * This project is a minimal verification test for the DRA818V hardware
 * connection.  It:
 *   1. Powers on the DRA818V (ENA pin HIGH).
 *   2. Reconfigures USART1 to 9600 baud.
 *   3. Sends AT+DMOCONNECT and checks for "+DMOCONNECT:0" in the reply.
 *   4. Stores the result (DRA818_OK / DRA818_ERR) in static state.
 *
 * No audio, no PTT, no AX.25 packet encoding/decoding is performed.
 * The while(1) loop does nothing but monitor the squelch GPIO so that
 * a debugger or logic analyser can observe the channel-busy state.
 *
 * Architecture note
 * -----------------
 * The App_Init / App_Run split mirrors STM32_test5_DRA so that main.c
 * does not need to change if this project is upgraded to full TX/RX later.
 */

#include "app.h"
#include "dra818.h"
#include "spi.h"
#include "main.h"
/* ================================================================
 * Module-level state
 * ================================================================ */

/** Result of the last DRA818_Init() call — DRA818_OK or DRA818_ERR. */
static int s_handshakeResult = DRA818_ERR;
static uint8_t s_spiStatusMsg = 0; 
static uint8_t s_dummyRx = 0;

/* ================================================================
 * App_Init — call once after all MX_*_Init() calls
 * ================================================================ */

void App_Init(void)
{
    /* Power on the DRA818V and attempt the AT+DMOCONNECT handshake.
     * DRA818_Init() will:
     *   - Assert ENA (PA6 HIGH)  →  module powers on
     *   - Wait 1 s for boot
     *   - Reconfigure USART1 to 9600 baud
     *   - Send "AT+DMOCONNECT\r\n" and verify "+DMOCONNECT:0" reply
     *   - Retry once on failure (first byte may be garbled after boot)
     */
    s_handshakeResult = DRA818_Init();

    // Map the result to our SPI status byte
    s_spiStatusMsg = (s_handshakeResult == DRA818_OK) ? 1 : 2;
    
    // TEMPORARY TEST:
    //s_spiStatusMsg = 1; 
    HAL_SPI_TransmitReceive_IT(&hspi1, &s_spiStatusMsg, &s_dummyRx, 1);
}

/* ================================================================
 * App_Run — call repeatedly from main() while(1)
 * ================================================================ */

void App_Run(void)
{
    /*
     * Handshake-only build: nothing to do in the loop.
     *
     * The DRA818_IsChannelBusy() call below is kept as a placeholder
     * so the compiler exercises the squelch-pin read path.  The result
     * is intentionally discarded — remove (void) when adding real logic.
     */
    (void)DRA818_IsChannelBusy();
}

/* ================================================================
 * SPI / GPIO Synchronization Callbacks
 * ================================================================ */

 /**
 * @brief EXTI Callback for the GPIO CS Pin
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SPI1_CS_Pin) 
    {
        /* ONLY execute when CS goes HIGH (Master is done reading) */
        if (HAL_GPIO_ReadPin(SPI1_CS_GPIO_Port, SPI1_CS_Pin) == GPIO_PIN_SET) 
        {
            /* 1. Abort to clear any line noise and reset the shift register */
            HAL_SPI_Abort(&hspi1);
            
            /* 2. Preload the buffer for the NEXT time the ESP32 reads */
            HAL_SPI_TransmitReceive_IT(&hspi1, &s_spiStatusMsg, &s_dummyRx, 1);
        }
    }
}