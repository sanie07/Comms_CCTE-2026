/*
 * radio_app.h
 *
 *  Created on: Jul 30, 2026
 *      Author: elias
 */

#ifndef CORE_INC_RADIO_APP_H_
#define CORE_INC_RADIO_APP_H_
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

// Funciones expuestas hacia C (main.c)
void radio_app_init(void);
void radio_app_send_ax25(const char* payload);

#ifdef __cplusplus
}
#endif

#endif /* CORE_INC_RADIO_APP_H_ */
