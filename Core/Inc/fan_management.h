/*
 * fan_management.h
 *
 *  Created on: Oct 6, 2025
 *      Author: jpser
 */

#ifndef INC_FAN_MANAGEMENT_H_
#define INC_FAN_MANAGEMENT_H_

#include "main.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	TEMP_CTRL_MEAN_OF_TOP_N = 0,   // average of the hottest N sensors
	TEMP_CTRL_MEDIAN_OF_ALL = 1,   // median of all sensors
	TEMP_CTRL_MAX_OF_ALL = 2    // absolute hottest sensor
} temp_ctrl_method_t;

/* Classic Ziegler–Nichols PID styles */
typedef enum {
	ZN_PID_CLASSIC = 0,        // Kp=0.6Ku, Ti=0.5Tu, Td=0.125Tu
	ZN_PI,                     // Kp=0.45Ku, Ti=0.83Tu
	ZN_P                      // Kp=0.5Ku
} zn_style_t;

typedef struct {
	float Kp, Ki, Kd;          // tunable
	float setpoint;            // target temperature (°C)
	float Ts;                  // sample time (seconds)

	/* Internal state */
	float integrator;
	float prev_error;
	float prev_measurement;

	/* Output clamp for anti-windup */
	float out_min;             // e.g. 0.0f
	float out_max;             // e.g. 255.0f
} pid_ctrl_t;

/* -------- PID API -------- */
void PID_Init(pid_ctrl_t *pid, float Kp, float Ki, float Kd, float Ts_sec, float out_min, float out_max);

void PID_SetTunings(pid_ctrl_t *pid, float Kp, float Ki, float Kd);
void PID_FromZieglerNichols(pid_ctrl_t *pid, float Ku, float Tu, zn_style_t style);

/* Runs one PID step. Returns clamped output in the same units as out_min/out_max (0..255). */
float PID_Update(pid_ctrl_t *pid, float measurement);

/* -------- Temperature aggregation --------
 Provide a flat array of temperatures (°C), length = num_temps.
 For MEAN_OF_TOP_N pass top_n >= 1 (and <= num_temps). Ignored for other methods. */
float FanMgr_ComputeControlTemp(const float *temps, size_t num_temps, temp_ctrl_method_t method, size_t top_n);

/* High-level helper: compute control temperature then compute PWM using the PID.
 Returns 0..255 (uint8_t). */
uint8_t FanMgr_UpdatePWM(pid_ctrl_t *pid, const float *temps, size_t num_temps, temp_ctrl_method_t method, size_t top_n);

/* Utility: hard clamp to [0,255] and sanitize NaNs/Infs. */
static inline uint8_t FanMgr_ClampPWM(float u) {
	if (!(u == u))
		return 0; // NaN -> 0
	if (u < 0.0f)
		return 0;
	if (u > 255.0f)
		return 255;
	return (uint8_t) (u + 0.5f);
}

#ifdef __cplusplus
}
#endif

#endif /* INC_FAN_MANAGEMENT_H_ */
