#include "gfsk_tx.h"
#include "main.h"

/* GFSK Modem Parameters */
#define RF_FREQUENCY                                433000000 // Default to 433 MHz, user can adjust
#define FSK_FDEV                                    50000     // 50 kHz
#define FSK_DATARATE                                50000     // 50 kbps
#define FSK_BANDWIDTH                               50000     // 50 kHz
#define FSK_PREAMBLE_LENGTH                         5         // Same as default
#define FSK_FIX_LENGTH_PAYLOAD_ON                   false

//extern SemaphoreHandle_t rf_power_mutex;

static RadioEvents_t RadioEvents;


void GFSK_Init(void)
{
    /* Initialize Radio Events */
    RadioEvents.TxDone = GFSK_TxDone_Callback;
    RadioEvents.TxTimeout = GFSK_TxTimeout_Callback;
    
    /* Initialize Radio driver */
    Radio.Init(&RadioEvents);

    /* Set Channel Frequency */
    Radio.SetChannel(RF_FREQUENCY);

    /* Configure TX parameters for FSK */
    Radio.SetTxConfig(MODEM_FSK, 14, FSK_FDEV, 0,
                      FSK_DATARATE, 0,
                      FSK_PREAMBLE_LENGTH, FSK_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, 0, 3000);
                      
    /* Ensure radio is asleep */
    Radio.Sleep();
}

/*BaseType_t GFSK_SendPacket_RTOS(uint8_t *payload, uint16_t size, TickType_t xTicksToWait)
{
    if (rf_power_mutex == NULL) {
        return pdFAIL;
    }

    /* Wait for the RF mutex to ensure APRS or other tasks are not transmitting
    if (xSemaphoreTake(rf_power_mutex, xTicksToWait) == pdTRUE) {
        
        /* 1. Turn on RF switch to route internal PA to antenna
#ifdef SWITCH_TO_CRL_Pin
        HAL_GPIO_WritePin(SWITCH_TO_CRL_GPIO_Port, SWITCH_TO_CRL_Pin, GPIO_PIN_SET);
#endif
        
        /* 2. Send the packet (Non-blocking, uses DMA/IRQ internally)
        Radio.Send(payload, size);
        
        /* Mutex will be released in the TxDone or TxTimeout callback
        return pdTRUE;
    }
    
    return pdFAIL; /* Timeout
}*/

void GFSK_TxDone_Callback(void)
{
    
    //Put radio back to sleep
    //Radio.Sleep();
    
    //Release Mutex so APRS or next GFSK packet can transmit
    /*if (rf_power_mutex != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(rf_power_mutex, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }*/
}

void GFSK_TxTimeout_Callback(void)
{
	/* Same recovery as TxDone */
    //GFSK_TxDone_Callback();
}
