#include "radio_app.h"
#include <RadioLib.h>
#include "STM32CubeHal.h"

#ifndef LOW
#define LOW 0
#endif
#ifndef HIGH
#define HIGH 1
#endif

// 1. Instanciar la HAL propia y enlazar la Radio con los pines virtuales de STM32WL
static STM32CubeHAL hal(&hsubghz);
static STM32WLx_Cube_Module radio_mod(&hal);
static STM32WLx radio(&radio_mod);
static AX25Client ax25(&radio);

// 2. Array de pines para el control del RF Switch (1 solo pin real)
// Usamos solo el número de pin, ya que el STM32CubeHAL que creamos lo tiene mapeado a GPIOA internamente
static const uint32_t rfswitch_pins[] = {
    RF_CTRL_Pin, 
    RADIOLIB_NC,
    RADIOLIB_NC,
    RADIOLIB_NC,
    RADIOLIB_NC
};

// 3. Tabla de estados para el RF Switch (LOW = RX/IDLE, HIGH = TX)
static const Module::RfSwitchMode_t rfswitch_table[] = {
    { STM32WLx::MODE_IDLE,  { LOW  } }, // Reposo
    { STM32WLx::MODE_RX,    { LOW  } }, // Recepción
    { STM32WLx::MODE_TX_LP, { HIGH } }, // Transmisión Low Power
    { STM32WLx::MODE_TX_HP, { HIGH } }, // Transmisión High Power
    END_OF_MODE_TABLE,
};

extern "C" void radio_app_init(void) {
    // Configurar RF Switch con la tabla definida
    radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);

    // Inicializar GFSK (Frecuencia, Bitrate, DevFreq, RX BW, Power, Preamble)
    // 433.0 MHz, 1.2 kbps, 0.6 kHz, 10.4 kHz, 14 dBm, 16 bits
    int state = radio.beginFSK(433.0, 1.2, 0.6, 10.4, 14, 16, 0, false);
    
    if (state == RADIOLIB_ERR_NONE) {
        // Aplicar el Gaussian filter BT = 0.5 (shaping)
        radio.setDataShaping(RADIOLIB_SHAPING_0_5);
    } else {
        // Error de hardware/SPI (Revisar inicialización)
        while(1);
    }

    // Inicializar cliente AX.25 (Indicativo origen N0CALL, SSID 0)
    state = ax25.begin("N0CALL", 0);
    if (state != RADIOLIB_ERR_NONE) {
        // Error AX.25
        while(1);
    }
}

extern "C" void radio_app_send_ax25(const char* payload) {
    // Transmitir trama UI
    // Indicativo destino: "COMS", SSID 0
    ax25.transmit(payload, "COMS", 0);
}
