#ifndef STM32CUBEHAL_H
#define STM32CUBEHAL_H

#include "RadioLib.h"
#include "subghz.h"

// Pines virtuales para manejar el radio interno del STM32WL
enum {
    RADIOLIB_STM32WL_VIRTUAL_PIN_NSS = 0xFF00,
    RADIOLIB_STM32WL_VIRTUAL_PIN_BUSY,
    RADIOLIB_STM32WL_VIRTUAL_PIN_IRQ,
    RADIOLIB_STM32WL_VIRTUAL_PIN_RESET,
};

class STM32CubeHAL : public RadioLibHal {
  public:
    STM32CubeHAL(SUBGHZ_HandleTypeDef *hsubghz);

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;

    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;

    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;

  private:
    SUBGHZ_HandleTypeDef *_hsubghz;
};

// Módulo especializado que enlaza los pines virtuales a RadioLib
class STM32WLx_Cube_Module : public Module {
  public:
    STM32WLx_Cube_Module(STM32CubeHAL* hal) : Module(
        hal,
        RADIOLIB_STM32WL_VIRTUAL_PIN_NSS,
        RADIOLIB_STM32WL_VIRTUAL_PIN_IRQ,
        RADIOLIB_STM32WL_VIRTUAL_PIN_RESET,
        RADIOLIB_STM32WL_VIRTUAL_PIN_BUSY
    ) {}
};

#endif
