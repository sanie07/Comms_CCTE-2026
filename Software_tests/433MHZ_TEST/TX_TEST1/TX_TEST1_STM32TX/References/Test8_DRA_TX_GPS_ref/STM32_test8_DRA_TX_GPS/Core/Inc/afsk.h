/**
 * @file    afsk.h
 * @brief   Bell 202 AFSK modem — 1200 baud, 1200/2200 Hz tones.
 *
 * TX path  (driven by TIM2 ISR at 9600 Hz)
 * ─────────────────────────────────────────
 *  AX25_BuildTxFrame() pre-builds the complete NRZ bit-stream (with
 *  preamble flags, bit-stuffing, FCS, postamble flags) into a packed
 *  byte array, then calls AFSK_TX_Load().
 *
 *  Each TIM2 tick: phaseAcc += phaseInc, DAC ← sineTable[phaseAcc>>8].
 *  Every 8 ticks (= one 1200 baud symbol): fetch next NRZ bit from buffer,
 *  apply NRZI (0 = toggle tone, 1 = keep tone), update phaseInc.
 *
 *  Phase accumulator is 16-bit Q8 format:
 *    phaseInc_MARK  = 256 × 1200 / 9600 × 256 = 8 192   (exact)
 *    phaseInc_SPACE = 256 × 2200 / 9600 × 256 = 15 019  (~58.67 steps/tick)
 *  The DDS naturally handles the non-integer frequency.
 *
 * RX path  (driven by TIM2 ISR at 9600 Hz)
 * ─────────────────────────────────────────
 *  ADC samples are read via register-level polling (no HAL overhead).
 *  A sliding 8-sample correlator computes the L1-norm energy at 1200 Hz
 *  and 2200 Hz simultaneously.  Every 8 samples a hard symbol decision
 *  is made.  NRZI decode is applied and the result is exposed via the
 *  volatile pair (afsk_rx_bit, afsk_rx_bit_ready) for AX25_RxBit().
 */

#ifndef AFSK_H
#define AFSK_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Initialisation                                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief  Build the sine lookup table, start the DAC on PA10 (mid-point),
 *         configure ADC channel 7 (PA11), calibrate and arm the ADC.
 *         Must be called after MX_DAC_Init() and MX_ADC_Init().
 */
void AFSK_Init(void);

/* ------------------------------------------------------------------ */
/* TX API                                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief  Load a pre-built NRZ bit-stream into the modem TX buffer.
 * @param  bits      Packed byte array: bit 0 of byte 0 = first bit sent.
 * @param  bitCount  Total number of bits to transmit.
 */
void AFSK_TX_Load(const uint8_t *bits, uint16_t bitCount);

/** @brief  Enable the TX state machine (begin transmitting loaded bits). */
void AFSK_TX_Start(void);

/** @brief  Stop TX, return DAC to mid-point (silence). */
void AFSK_TX_Stop(void);

/** @brief  Returns true after the last bit of the loaded stream is sent. */
bool AFSK_TX_IsDone(void);

/** @brief  Transmit a continuous calibration tone until AFSK_ToneStop(). */
void AFSK_ToneStart(uint16_t hz);

/** @brief  Transmit alternating 1200/2200 Hz tones for the configured cycle count. */
void AFSK_CalAltStart(void);

/** @brief  Stop any calibration tone and return DAC to mid-point. */
void AFSK_ToneStop(void);

/* ------------------------------------------------------------------ */
/* ISR callback                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief  Must be called from the TIM2 period-elapsed ISR.
 *         Drives the TX DAC update and RX ADC sampling. During TX the
 *         ADC path still runs so PA10-to-PA11 loopback can decode frames.
 */
void AFSK_TimerTick(void);

/* ------------------------------------------------------------------ */
/* RX output (polled by app.c after each ISR tick)                    */
/* ------------------------------------------------------------------ */

/** Set to 1 by AFSK_TimerTick() when a new decoded bit is ready.
 *  App must clear it to 0 after reading afsk_rx_bit. */
extern volatile uint8_t afsk_rx_bit_ready;

/** The most recently decoded NRZ bit (0 or 1). Valid when
 *  afsk_rx_bit_ready == 1. */
extern volatile uint8_t afsk_rx_bit;

#endif /* AFSK_H */
