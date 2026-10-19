/*
 * brain.c
 *
 *  Created on: Oct 20, 2025
 *      Author: jpser
 */

#include "main.h"
#include "brain.h"

#include "math.h"
#include "stdio.h"
#include <stdbool.h>

/* ================== LOCAL DEFINES / TYPES ================== */

//Heatbeat trigger flag
bool toggleHeartbeat = false;

// Brain INITILIAZATION
void brain_start(void) {

	// Watchdog Fault Acknoledge
	if (watchdog_flag & RCC_CSR_WWDGRSTF) {

	}


}

void brain_loop(void) {


}

// Timer interrupt callback


// Called each SysTick interrupt for HEARTBEAT LED
void HAL_SYSTICK_Callback(void) {

	heartbeat();
}

void heartbeat(void) {

	static uint16_t ticks = 0;
	static uint16_t period = 700;   // start period

	//toggleHeartbeat = false;
	if (++ticks >= period) {

		ticks = 0;

		toggleHeartbeat = true;

		// halve the period
		period >>= 1;

		// reset when reaching 0 or 1
		if (period < 1) {
			period = 1000;
		}
	}

}
