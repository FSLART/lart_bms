/*
 * fan_management.c
 *
 *  Created on: Oct 6, 2025
 *      Author: jpser
 */
#include "fan_management.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------- Internal helpers ------- */

static int cmp_desc(const void *a, const void *b) {
	const float fa = *(const float*) a;
	const float fb = *(const float*) b;
	/* Place NaNs at the end (coldest) */
	const int a_nan = !(fa == fa);
	const int b_nan = !(fb == fb);
	if (a_nan && b_nan)
		return 0;
	if (a_nan)
		return 1;
	if (b_nan)
		return -1;
	return (fb > fa) - (fb < fa); // descending
}

static int cmp_asc(const void *a, const void *b) {
	const float fa = *(const float*) a;
	const float fb = *(const float*) b;
	const int a_nan = !(fa == fa);
	const int b_nan = !(fb == fb);
	if (a_nan && b_nan)
		return 0;
	if (a_nan)
		return 1;
	if (b_nan)
		return -1;
	return (fa > fb) - (fa < fb); // ascending
}

/* ------- PID implementation ------- */

void PID_Init(pid_ctrl_t *pid, float Kp, float Ki, float Kd, float Ts_sec, float out_min, float out_max) {
	if (!pid)
		return;
	pid->Kp = Kp;
	pid->Ki = Ki;
	pid->Kd = Kd;
	pid->Ts = (Ts_sec > 0.f) ? Ts_sec : 0.1f;
	pid->setpoint = 0.f;
	pid->integrator = 0.f;
	pid->prev_error = 0.f;
	pid->prev_measurement = 0.f;
	pid->out_min = out_min;
	pid->out_max = out_max;
	if (pid->out_max < pid->out_min) {
		float t = pid->out_max;
		pid->out_max = pid->out_min;
		pid->out_min = t;
	}
}

void PID_SetTunings(pid_ctrl_t *pid, float Kp, float Ki, float Kd) {
	if (!pid)
		return;
	pid->Kp = Kp;
	pid->Ki = Ki;
	pid->Kd = Kd;
}

/* Ziegler–Nichols conversions. Ku: ultimate gain, Tu: oscillation period (seconds) */
void PID_FromZieglerNichols(pid_ctrl_t *pid, float Ku, float Tu, zn_style_t style) {
	if (!pid)
		return;
	if (Ku <= 0.f || Tu <= 0.f)
		return;

	float Kp = 0.f, Ki = 0.f, Kd = 0.f;
	switch (style) {
	case ZN_P:
		Kp = 0.5f * Ku;
		Ki = 0.f;
		Kd = 0.f;
		break;
	case ZN_PI: {
		Kp = 0.45f * Ku;
		const float Ti = 0.83f * Tu;
		Ki = (Ti > 0.f) ? (Kp / Ti) : 0.f;
		Kd = 0.f;
	}
		break;
	case ZN_PID_CLASSIC:
	default: {
		/* Classic: Kp=0.6Ku, Ti=0.5Tu, Td=0.125Tu */
		Kp = 0.60f * Ku;
		const float Ti = 0.50f * Tu;
		const float Td = 0.125f * Tu;
		Ki = (Ti > 0.f) ? (Kp / Ti) : 0.f;
		Kd = Kp * Td;
	}
		break;
	}
	PID_SetTunings(pid, Kp, Ki, Kd);
}

/* Discrete PID (positional form) with simple anti-windup via clamped integral.
 u = Kp*e + Ki*Ts*sum(e) + Kd*(meas_prev - meas)/Ts  (derivative on measurement)
 Output is clamped to [out_min, out_max]. */
float PID_Update(pid_ctrl_t *pid, float measurement) {
	if (!pid)
		return 0.f;

	/* Sanitize measurement */
	if (!(measurement == measurement) || !isfinite(measurement)) {
		measurement = pid->setpoint; // neutralize crazy input
	}

	const float e = pid->setpoint - measurement;

	/* Proportional */
	const float P = pid->Kp * e;

	/* Derivative on measurement (noise-friendly) */
	const float dmeas = (measurement - pid->prev_measurement);
	const float D = (pid->Kd > 0.f && pid->Ts > 0.f) ? (-pid->Kd * dmeas / pid->Ts) : 0.f;

	/* Integral with basic anti-windup (clamp later using available headroom) */
	pid->integrator += pid->Ki * pid->Ts * e;

	/* Pre-saturation sum */
	float u = P + pid->integrator + D;

	/* Saturation */
	if (u > pid->out_max)
		u = pid->out_max;
	if (u < pid->out_min)
		u = pid->out_min;

	/* Anti-windup: back-calculate clamp by ensuring integrator stays within output range */
	const float u_PI = P + pid->integrator; // exclude D (not stored)
	if (u_PI > pid->out_max)
		pid->integrator = pid->out_max - P;
	else if (u_PI < pid->out_min)
		pid->integrator = pid->out_min - P;

	pid->prev_error = e;
	pid->prev_measurement = measurement;
	return u;
}

/* ------- Temperature aggregation ------- */

float FanMgr_ComputeControlTemp(const float *temps, size_t num_temps, temp_ctrl_method_t method, size_t top_n) {
	if (!temps || num_temps == 0)
		return 0.f;

	/* Copy into a scratch buffer we can sort */
	float *buf = (float*) malloc(num_temps * sizeof(float));
	if (!buf)
		return temps[0];
	memcpy(buf, temps, num_temps * sizeof(float));

	float result = 0.f;

	switch (method) {
	case TEMP_CTRL_MEAN_OF_TOP_N: {
		if (top_n == 0 || top_n > num_temps)
			top_n = num_temps;
		qsort(buf, num_temps, sizeof(float), cmp_desc);
		double sum = 0.0;
		size_t count = 0;
		for (size_t i = 0; i < top_n; ++i) {
			if (!(buf[i] == buf[i]))
				continue; // skip NaN
			sum += buf[i];
			count++;
		}
		result = (count > 0) ? (float) (sum / (double) count) : 0.f;
	}
		break;

	case TEMP_CTRL_MAX_OF_ALL: {
		qsort(buf, num_temps, sizeof(float), cmp_desc);
		result = buf[0];
		if (!(result == result))
			result = 0.f; // NaN guard
	}
		break;

	case TEMP_CTRL_MEDIAN_OF_ALL:
	default: {
		qsort(buf, num_temps, sizeof(float), cmp_asc);
		/* Remove trailing NaNs */
		size_t valid = num_temps;
		while (valid > 0 && !(buf[valid - 1] == buf[valid - 1]))
			valid--;
		if (valid == 0)
			result = 0.f;
		else if ((valid & 1u) == 1u)
			result = buf[valid / 2];
		else
			result = 0.5f * (buf[valid / 2 - 1] + buf[valid / 2]);
	}
		break;
	}

	free(buf);
	return result;
}

uint8_t FanMgr_UpdatePWM(pid_ctrl_t *pid, const float *temps, size_t num_temps, temp_ctrl_method_t method, size_t top_n) {
	const float ctrl_temp = FanMgr_ComputeControlTemp(temps, num_temps, method, top_n);
	const float u = PID_Update(pid, ctrl_temp);
	return FanMgr_ClampPWM(u);
}
