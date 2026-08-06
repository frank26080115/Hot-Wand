/*
 * Input monitoring and confirmed power-state management.
 *
 * The task tracks voltage-range and jumper changes immediately, but delays
 * their effect until the inputs have remained stable for the required time.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "power.h"

#include <Arduino.h>

#include "blink.h"
#include "hotwandlite_pins.h"
#include "rfgen.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

namespace
{
/*
 * The schematic uses 4.7 kOhm above the ADC node and 470 Ohm below it, giving
 * an exact 11:1 input-to-ADC ratio.
 * AR_DEFAULT uses the 3.3 V analog supply as
 * the effective full-scale input on this SAMD21 Arduino core.
 */
constexpr uint32_t kAdcReferenceMv    = 3300;
constexpr uint32_t kAdcMaximumReading = 4095;
constexpr uint32_t kDividerUpperOhms  = 4700;
constexpr uint32_t kDividerLowerOhms  = 470;

/*
 * A centered 500 mV hysteresis band prevents chatter around the nominal 22 V
 * boundary: Low rises at 22.25 V and
 * High falls at 21.75 V.
 */
constexpr uint32_t kVoltageThresholdMv   = 22000;
constexpr uint32_t kVoltageHysteresisMv  = 500;
constexpr uint32_t kLowToHighThresholdMv = kVoltageThresholdMv + (kVoltageHysteresisMv / 2);
constexpr uint32_t kHighToLowThresholdMv = kVoltageThresholdMv - (kVoltageHysteresisMv / 2);

constexpr uint32_t kInitialSettleTimeMs = 500;
constexpr uint32_t kChangeSettleTimeMs  = 2000;
constexpr uint32_t kReportIntervalMs    = 1000;

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

enum class VoltageRange : uint8_t
{
    Low,
    High,
};

enum class PowerMode : uint8_t
{
    Eco,
    Normal,
    Sport,
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

bool g_hardwareInitialized   = false;
bool g_inputStateInitialized = false;
bool g_appliedStateValid     = false;

VoltageRange g_voltageRange        = VoltageRange::Low;
VoltageRange g_appliedVoltageRange = VoltageRange::Low;
PowerMode    g_powerMode           = PowerMode::Normal;
PowerMode    g_appliedPowerMode    = PowerMode::Normal;

uint32_t g_voltageChangedMs = 0;
uint32_t g_powerChangedMs   = 0;
uint32_t g_lastReportMs     = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void            apply_state();
static VoltageRange    update_voltage_range(VoltageRange currentRange, uint32_t voltageMv);
static VoltageRange    initial_voltage_range(uint32_t voltageMv);
static void            initialize_hardware();
static PowerMode       read_power_mode();
static uint8_t         power_percent(PowerMode powerMode);
static blink_voltage_t blink_voltage(VoltageRange voltageRange);
static blink_power_t   blink_power(PowerMode powerMode);
static void            report_readings(uint32_t currentTimeMs, uint32_t voltageMv, PowerMode powerMode);
static const char*     power_mode_name(PowerMode powerMode);
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void pwrmgt_task(void)
{
    initialize_hardware();

    const uint32_t  currentTimeMs = millis();
    const uint32_t  voltageMv     = pwrmgt_read_voltage_mv();
    const PowerMode powerMode     = read_power_mode();

    report_readings(currentTimeMs, voltageMv, powerMode);

    if (!g_inputStateInitialized)
    {
        // Boot starts a confirmation window; it does not apply this first sample.
        g_voltageRange          = initial_voltage_range(voltageMv);
        g_powerMode             = powerMode;
        g_voltageChangedMs      = currentTimeMs;
        g_powerChangedMs        = currentTimeMs;
        g_inputStateInitialized = true;
        return;
    }

    const VoltageRange voltageRange = update_voltage_range(g_voltageRange, voltageMv);
    if (voltageRange != g_voltageRange)
    {
        g_voltageRange     = voltageRange;
        g_voltageChangedMs = currentTimeMs;
    }

    if (powerMode != g_powerMode)
    {
        g_powerMode      = powerMode;
        g_powerChangedMs = currentTimeMs;
    }

    // The first configuration confirms quickly; later physical changes debounce longer.
    const uint32_t settleTimeMs    = g_appliedStateValid ? kChangeSettleTimeMs : kInitialSettleTimeMs;
    const bool     voltageIsStable = static_cast<uint32_t>(currentTimeMs - g_voltageChangedMs) >= settleTimeMs;
    const bool     powerIsStable   = static_cast<uint32_t>(currentTimeMs - g_powerChangedMs) >= settleTimeMs;
    if (!voltageIsStable || !powerIsStable)
    {
        return;
    }

    if (!g_appliedStateValid || (g_voltageRange != g_appliedVoltageRange) || (g_powerMode != g_appliedPowerMode))
    {
        apply_state();
    }
}

uint32_t pwrmgt_read_voltage_mv(void)
{
    initialize_hardware();

    const uint32_t     adcReading             = static_cast<uint32_t>(analogRead(ADC_PIN));
    constexpr uint32_t kDividerTotalOhms      = kDividerUpperOhms + kDividerLowerOhms;
    constexpr uint32_t kConversionDenominator = kAdcMaximumReading * kDividerLowerOhms;
    const uint64_t     conversionNumerator    = static_cast<uint64_t>(adcReading) * kAdcReferenceMv * kDividerTotalOhms;

    // Add half the denominator so the integer conversion rounds to nearest mV.
    return static_cast<uint32_t>((conversionNumerator + (kConversionDenominator / 2)) / kConversionDenominator);
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

namespace
{
static void apply_state()
{
    // RF power and the visible status always change as one confirmed state.
    rfgen_set(power_percent(g_powerMode));
    blink_set_pattern(blink_voltage(g_voltageRange), blink_power(g_powerMode));

    g_appliedVoltageRange = g_voltageRange;
    g_appliedPowerMode    = g_powerMode;
    g_appliedStateValid   = true;
}

static VoltageRange update_voltage_range(VoltageRange currentRange, uint32_t voltageMv)
{
    if ((currentRange == VoltageRange::Low) && (voltageMv > kLowToHighThresholdMv))
    {
        return VoltageRange::High;
    }
    if ((currentRange == VoltageRange::High) && (voltageMv < kHighToLowThresholdMv))
    {
        return VoltageRange::Low;
    }

    // Readings inside the hysteresis band retain the previous range.
    return currentRange;
}

static VoltageRange initial_voltage_range(uint32_t voltageMv)
{
    // Exactly 22 V is classified Low; only readings above it start High.
    return (voltageMv > kVoltageThresholdMv) ? VoltageRange::High : VoltageRange::Low;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static void initialize_hardware()
{
    if (g_hardwareInitialized)
    {
        return;
    }

    // Open jumpers read HIGH; a fitted jumper pulls its selector input LOW.
    pinMode(SEL2_PIN, INPUT_PULLUP);
    pinMode(SEL3_PIN, INPUT_PULLUP);
    pinMode(ADC_PIN, INPUT);

    analogReference(AR_DEFAULT);
    analogReadResolution(12);

    g_hardwareInitialized = true;
}

static PowerMode read_power_mode()
{
    // ECO wins if an invalid jumper arrangement pulls both inputs low.
    if (digitalRead(SEL2_PIN) == LOW)
    {
        return PowerMode::Eco;
    }
    if (digitalRead(SEL3_PIN) == LOW)
    {
        return PowerMode::Sport;
    }

    return PowerMode::Normal;
}

static uint8_t power_percent(PowerMode powerMode)
{
    switch (powerMode)
    {
    case PowerMode::Eco:
        return 33;

    case PowerMode::Sport:
        return 100;

    case PowerMode::Normal:
    default:
        return 66;
    }
}

static blink_voltage_t blink_voltage(VoltageRange voltageRange)
{
    return (voltageRange == VoltageRange::High) ? BLINK_VOLTAGE_HIGH : BLINK_VOLTAGE_LOW;
}

static blink_power_t blink_power(PowerMode powerMode)
{
    switch (powerMode)
    {
    case PowerMode::Eco:
        return BLINK_POWER_ECO;

    case PowerMode::Sport:
        return BLINK_POWER_SPORT;

    case PowerMode::Normal:
    default:
        return BLINK_POWER_NORMAL;
    }
}

// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------

static void report_readings(uint32_t currentTimeMs, uint32_t voltageMv, PowerMode powerMode)
{
    if (static_cast<uint32_t>(currentTimeMs - g_lastReportMs) < kReportIntervalMs)
    {
        return;
    }

    g_lastReportMs = currentTimeMs;
    Serial.print(currentTimeMs);
    Serial.print(" ms: voltage=");
    Serial.print(voltageMv);
    Serial.print(" mV, selection=");
    Serial.println(power_mode_name(powerMode));
}

static const char* power_mode_name(PowerMode powerMode)
{
    switch (powerMode)
    {
    case PowerMode::Eco:
        return "ECO";

    case PowerMode::Sport:
        return "SPORT";

    case PowerMode::Normal:
    default:
        return "NORMAL";
    }
}
} // namespace
