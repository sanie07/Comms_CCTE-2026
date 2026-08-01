#include "STM32CubeHal.h"
#include "main.h"

#ifndef LOW
#define LOW 0
#endif
#ifndef HIGH
#define HIGH 1
#endif

STM32CubeHAL::STM32CubeHAL(SUBGHZ_HandleTypeDef *hsubghz)
  : RadioLibHal(0, 1, 0, 1, 1, 2), _hsubghz(hsubghz) {
}

void STM32CubeHAL::pinMode(uint32_t pin, uint32_t mode) {
    if(pin >= RADIOLIB_STM32WL_VIRTUAL_PIN_NSS) return; 
    // Los pines de usuario ya están configurados por STM32CubeMX
}

void STM32CubeHAL::digitalWrite(uint32_t pin, uint32_t value) {
    switch (pin) {
        case RADIOLIB_STM32WL_VIRTUAL_PIN_NSS:
            if(value == LOW) {
                // LL_PWR_SelectSUBGHZSPI_NSS() equivalente:
                CLEAR_BIT(PWR->SUBGHZSPICR, PWR_SUBGHZSPICR_NSS);
            } else {
                // LL_PWR_UnselectSUBGHZSPI_NSS() equivalente:
                SET_BIT(PWR->SUBGHZSPICR, PWR_SUBGHZSPICR_NSS);
            }
            break;

        case RADIOLIB_STM32WL_VIRTUAL_PIN_RESET:
        case RADIOLIB_STM32WL_VIRTUAL_PIN_BUSY:
        case RADIOLIB_STM32WL_VIRTUAL_PIN_IRQ:
            // No aplicable para escritura
            break;

        default:
            // RF Switch manejado por RadioLib usando el pin
            if (pin == RF_CTRL_Pin) {
                HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, (value == HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            }
            break;
    }
}

uint32_t STM32CubeHAL::digitalRead(uint32_t pin) {
    switch (pin) {
        case RADIOLIB_STM32WL_VIRTUAL_PIN_BUSY:
            // Retorna HIGH si el flag RFBUSYS está encendido (radio ocupada)
            return (READ_BIT(PWR->SR2, PWR_SR2_RFBUSYS) != 0) ? HIGH : LOW;

        case RADIOLIB_STM32WL_VIRTUAL_PIN_IRQ:
            // Retorna estado pendiente de la IRQ
            return HAL_NVIC_GetPendingIRQ(SUBGHZ_Radio_IRQn) ? HIGH : LOW;

        case RADIOLIB_STM32WL_VIRTUAL_PIN_NSS:
            return (READ_BIT(PWR->SUBGHZSPICR, PWR_SUBGHZSPICR_NSS) != 0) ? HIGH : LOW;

        case RADIOLIB_STM32WL_VIRTUAL_PIN_RESET:
            return LOW;

        default:
            return LOW;
    }
}

void STM32CubeHAL::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
    // Si necesitas callbacks de interrupción por hardware, 
    // mappear llamadas desde HAL_SUBGHZ_IRQHandler en tu subghz.c
}

void STM32CubeHAL::detachInterrupt(uint32_t interruptNum) {
}

void STM32CubeHAL::delay(RadioLibTime_t ms) {
    HAL_Delay(ms);
}

void STM32CubeHAL::delayMicroseconds(RadioLibTime_t us) {
    uint32_t start = micros();
    while((micros() - start) < us);
}

RadioLibTime_t STM32CubeHAL::millis() {
    return HAL_GetTick();
}

RadioLibTime_t STM32CubeHAL::micros() {
    // Aproximación de microsegundos usando el SysTick
    uint32_t ms = HAL_GetTick();
    uint32_t st = SysTick->VAL;
    uint32_t reload = SysTick->LOAD;
    return (ms * 1000) + ((reload - st) * 1000) / reload;
}

long STM32CubeHAL::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) {
    return 0; // No implementado ni usado por STM32WLx 
}

void STM32CubeHAL::spiBegin() {
}

void STM32CubeHAL::spiBeginTransaction() {
}

void STM32CubeHAL::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    for(size_t i = 0; i < len; i++) {
        // Esperar a que el buffer TX esté vacío
        while ((SUBGHZSPI->SR & SPI_SR_TXE) == 0);
        
        uint8_t data = out ? out[i] : 0x00;
        *(__IO uint8_t *)&SUBGHZSPI->DR = data;
        
        // Esperar a que llegue un byte RX
        while ((SUBGHZSPI->SR & SPI_SR_RXNE) == 0);
        
        uint8_t rx = *(__IO uint8_t *)&SUBGHZSPI->DR;
        if(in) {
            in[i] = rx;
        }
    }
}

void STM32CubeHAL::spiEndTransaction() {
}

void STM32CubeHAL::spiEnd() {
}
