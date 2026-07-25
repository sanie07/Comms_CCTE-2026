/*#include "aprs_tx.h"
#include "sine_lut.h"
#include <string.h>
#include <limits.h>

#define AUDIO_SAMPLE_RATE 9600
#define BAUD_RATE 1200
#define SAMPLES_PER_BIT (AUDIO_SAMPLE_RATE / BAUD_RATE)

#define FREQ_MARK  1200
#define FREQ_SPACE 2200

#define PHASE_INC_MARK  ((uint32_t)((FREQ_MARK * 4294967296.0) / AUDIO_SAMPLE_RATE))
#define PHASE_INC_SPACE ((uint32_t)((FREQ_SPACE * 4294967296.0) / AUDIO_SAMPLE_RATE))

#define PING_PONG_SAMPLES 128
#define TOTAL_SAMPLES (PING_PONG_SAMPLES * 2)

static QueueHandle_t aprs_tx_queue;
static TaskHandle_t aprs_task_handle;
SemaphoreHandle_t rf_power_mutex;

static DAC_HandleTypeDef *aprs_dac;
static TIM_HandleTypeDef *aprs_tim;
static UART_HandleTypeDef *aprs_uart;

static uint8_t dma_buffer[TOTAL_SAMPLES]; // We use 8-bit DAC or 12-bit? User DAC config is left default, 12-bit right aligned but dac buffer size 8-bit? Wait, DAC output is PA10. The sample is 8 bit or 12 bit? Let's use 8-bit DAC: HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t*)dma_buffer, TOTAL_SAMPLES, DAC_ALIGN_8B_R).

typedef enum {
    STATE_IDLE,
    STATE_PREAMBLE,
    STATE_DATA,
    STATE_FCS,
    STATE_POSTAMBLE,
    STATE_DONE
} StreamState_t;

static uint8_t current_frame[APRS_MAX_PAYLOAD_LEN + 30];
static uint16_t frame_len = 0;
static uint16_t current_fcs = 0xFFFF;

static StreamState_t tx_state = STATE_IDLE;
static int preamble_cnt = 30;
static int postamble_cnt = 2;
static int byte_idx = 0;
static int bit_idx = 0;
static bool bit_stuffing = false;
static int ones_cnt = 0;

static uint32_t phase_acc = 0;
static uint32_t current_phase_inc = PHASE_INC_MARK;
static int sample_cnt = 0;
static uint8_t current_nrzi = 1; // 1 = initial phase, 0 = changed phase

static APRS_State_t global_aprs_state = APRS_STATE_IDLE;

static void crc_update(uint8_t data) {
    current_fcs ^= data;
    for (int i = 0; i < 8; i++) {
        if (current_fcs & 1)
            current_fcs = (current_fcs >> 1) ^ 0x8408;
        else
            current_fcs >>= 1;
    }
}

static void build_callsign(uint8_t *buf, const char *callsign, uint8_t ssid, bool is_last) {
    int i;
    for (i = 0; i < 6; i++) {
        if (*callsign && *callsign != '-') {
            buf[i] = (*callsign++) << 1;
        } else {
            buf[i] = ' ' << 1;
        }
    }
    buf[6] = (ssid << 1) | 0x60 | (is_last ? 0x01 : 0x00);
}

static uint8_t get_next_bit(void) {
    if (bit_stuffing && ones_cnt == 5) {
        ones_cnt = 0;
        return 0; // Stuffed bit
    }

    uint8_t bit = 1;

    switch (tx_state) {
        case STATE_PREAMBLE:
            bit = (0x7E >> bit_idx) & 1;
            bit_idx++;
            if (bit_idx == 8) {
                bit_idx = 0;
                preamble_cnt--;
                if (preamble_cnt == 0) {
                    tx_state = STATE_DATA;
                    byte_idx = 0;
                    bit_stuffing = true;
                    ones_cnt = 0;
                }
            }
            break;

        case STATE_DATA:
            bit = (current_frame[byte_idx] >> bit_idx) & 1;
            bit_idx++;
            if (bit_idx == 8) {
                bit_idx = 0;
                byte_idx++;
                if (byte_idx == frame_len) {
                    tx_state = STATE_FCS;
                    byte_idx = 0;
                    current_fcs ^= 0xFFFF; // Invert before sending
                }
            }
            break;

        case STATE_FCS:
            bit = (current_fcs >> bit_idx) & 1;
            bit_idx++;
            if (bit_idx == 16) {
                tx_state = STATE_POSTAMBLE;
                bit_idx = 0;
                bit_stuffing = false;
            }
            break;

        case STATE_POSTAMBLE:
            bit = (0x7E >> bit_idx) & 1;
            bit_idx++;
            if (bit_idx == 8) {
                bit_idx = 0;
                postamble_cnt--;
                if (postamble_cnt == 0) {
                    tx_state = STATE_DONE;
                }
            }
            break;
            
        case STATE_DONE:
        case STATE_IDLE:
        default:
            return 1;
    }

    if (bit_stuffing) {
        if (bit == 1) ones_cnt++;
        else ones_cnt = 0;
    }

    return bit;
}

static void fill_audio_buffer(uint8_t *buf, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        if (sample_cnt == 0) {
            if (tx_state != STATE_DONE) {
                uint8_t bit = get_next_bit();
                if (bit == 0) {
                    current_nrzi ^= 1; // Toggle frequency on 0
                }
                current_phase_inc = current_nrzi ? PHASE_INC_MARK : PHASE_INC_SPACE;
                sample_cnt = SAMPLES_PER_BIT;
            } else {
                current_phase_inc = PHASE_INC_MARK; // Tone during tail
            }
        }

        phase_acc += current_phase_inc;
        uint32_t lut_idx = phase_acc >> 23; // 32 - 9 = 23 (for 512 elements)
        buf[i] = sine_table[lut_idx & 0x1FF];
        
        if (sample_cnt > 0) {
            sample_cnt--;
        }
    }
}

static void setup_frame(APRS_Packet_t *pkt) {
    frame_len = 0;
    current_fcs = 0xFFFF;
    
    // Hardcode address lengths for now. (Dest, Src, WIDE1-1)
    build_callsign(&current_frame[frame_len], pkt->callsign_dst, 0, false);
    frame_len += 7;
    build_callsign(&current_frame[frame_len], pkt->callsign_src, 0, false);
    frame_len += 7;
    
    // Path (simplified)
    if (strlen(pkt->path) > 0) {
        build_callsign(&current_frame[frame_len], pkt->path, 1, true); // Assuming WIDE1-1
        frame_len += 7;
    } else {
        current_frame[frame_len - 1] |= 0x01; // Set last bit of previous
    }

    // Control & PID
    current_frame[frame_len++] = 0x03; // UI
    current_frame[frame_len++] = 0xF0; // No Layer 3

    // Payload
    memcpy(&current_frame[frame_len], pkt->payload, pkt->payload_len);
    frame_len += pkt->payload_len;

    // Calculate CRC
    for (int i = 0; i < frame_len; i++) {
        crc_update(current_frame[i]);
    }

    tx_state = STATE_PREAMBLE;
    preamble_cnt = 30;
    postamble_cnt = 2;
    byte_idx = 0;
    bit_idx = 0;
    bit_stuffing = false;
    ones_cnt = 0;
    sample_cnt = 0;
    phase_acc = 0;
    current_nrzi = 1;
}

static void aprs_task_func(void *arg) {
    APRS_Packet_t pkt;
    
    while (1) {
        global_aprs_state = APRS_STATE_IDLE;
        if (xQueueReceive(aprs_tx_queue, &pkt, portMAX_DELAY) == pdTRUE) {
            global_aprs_state = APRS_STATE_WAITING_PTT;
            
            xSemaphoreTake(rf_power_mutex, portMAX_DELAY);
            
            // AT Commands initialization to DRA if needed
            // char at_cmd[] = "AT+DMOSETGROUP=0,144.3900,144.3900,0000,0,0000\r\n";
            // HAL_UART_Transmit(aprs_uart, (uint8_t*)at_cmd, strlen(at_cmd), 1000);
            
            // Enable PTT
            HAL_GPIO_WritePin(STM_TO_DRA_PTT_GPIO_Port, STM_TO_DRA_PTT_Pin, GPIO_PIN_SET);
            vTaskDelay(pdMS_TO_TICKS(150)); // RF Settle
            
            global_aprs_state = APRS_STATE_TX_PREAMBLE;
            setup_frame(&pkt);
            
            // Pre-fill both halves
            fill_audio_buffer(&dma_buffer[0], PING_PONG_SAMPLES);
            fill_audio_buffer(&dma_buffer[PING_PONG_SAMPLES], PING_PONG_SAMPLES);
            
            // Start Timer and DMA
            HAL_TIM_Base_Start(aprs_tim);
            HAL_DAC_Start_DMA(aprs_dac, DAC_CHANNEL_1, (uint32_t*)dma_buffer, TOTAL_SAMPLES, DAC_ALIGN_8B_R);
            
            while (tx_state != STATE_DONE) {
                uint32_t notified_value;
                if (xTaskNotifyWait(0, ULONG_MAX, &notified_value, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (tx_state == STATE_DATA) global_aprs_state = APRS_STATE_TX_DATA;
                    else if (tx_state == STATE_POSTAMBLE) global_aprs_state = APRS_STATE_TX_POSTAMBLE;
                    
                    if (notified_value == 1) { // Half
                        fill_audio_buffer(&dma_buffer[0], PING_PONG_SAMPLES);
                    } else if (notified_value == 2) { // Full
                        fill_audio_buffer(&dma_buffer[PING_PONG_SAMPLES], PING_PONG_SAMPLES);
                    }
                }
            }
            
            global_aprs_state = APRS_STATE_TX_TAIL;
            vTaskDelay(pdMS_TO_TICKS(10)); // Flush audio
            
            HAL_DAC_Stop_DMA(aprs_dac, DAC_CHANNEL_1);
            HAL_TIM_Base_Stop(aprs_tim);
            
            HAL_GPIO_WritePin(STM_TO_DRA_PTT_GPIO_Port, STM_TO_DRA_PTT_Pin, GPIO_PIN_RESET);
            
            xSemaphoreGive(rf_power_mutex);
        }
    }
}

BaseType_t APRS_Init(DAC_HandleTypeDef *hdac, TIM_HandleTypeDef *htim, UART_HandleTypeDef *huart, SemaphoreHandle_t rf_mutex) {
    aprs_dac = hdac;
    aprs_tim = htim;
    aprs_uart = huart;
    rf_power_mutex = rf_mutex;
    
    aprs_tx_queue = xQueueCreate(5, sizeof(APRS_Packet_t));
    if (!aprs_tx_queue) return pdFAIL;
    
    return xTaskCreate(aprs_task_func, "APRS_TX_Task", 512, NULL, osPriorityNormal, &aprs_task_handle);
}

BaseType_t APRS_SendPacket_RTOS(const char *callsign_src, const char *callsign_dst, const char *path, const uint8_t *payload, uint16_t len, TickType_t xTicksToWait) {
    if (!aprs_tx_queue) return pdFAIL;
    
    APRS_Packet_t pkt;
    memset(&pkt, 0, sizeof(APRS_Packet_t));
    strncpy(pkt.callsign_src, callsign_src, sizeof(pkt.callsign_src)-1);
    strncpy(pkt.callsign_dst, callsign_dst, sizeof(pkt.callsign_dst)-1);
    if (path) strncpy(pkt.path, path, sizeof(pkt.path)-1);
    
    uint16_t clen = len > APRS_MAX_PAYLOAD_LEN ? APRS_MAX_PAYLOAD_LEN : len;
    memcpy(pkt.payload, payload, clen);
    pkt.payload_len = clen;
    
    return xQueueSend(aprs_tx_queue, &pkt, xTicksToWait);
}

APRS_State_t APRS_GetState(void) {
    return global_aprs_state;
}

void APRS_DMA_HalfTransfer_ISR(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (aprs_task_handle) {
        xTaskNotifyFromISR(aprs_task_handle, 1, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void APRS_DMA_FullTransfer_ISR(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (aprs_task_handle) {
        xTaskNotifyFromISR(aprs_task_handle, 2, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac) {
    if (hdac->Instance == DAC) {
        APRS_DMA_HalfTransfer_ISR();
    }
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac) {
    if (hdac->Instance == DAC) {
        APRS_DMA_FullTransfer_ISR();
    }
}*/
