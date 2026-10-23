#pragma once

/* Deterministic stand-in for the factory-calibrated VREFINT conversion. Tests validate the
 * injected channel/cadence, not ST calibration accuracy. */
#define LL_ADC_RESOLUTION_10B                                0
#define __LL_ADC_CALC_VREFANALOG_VOLTAGE(sample, resolution) (3300 * 372 / (sample))
