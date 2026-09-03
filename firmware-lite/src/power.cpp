/*
 * Input monitoring and power-state management.
 *
 * RF remains off for at least 500 ms after boot. After the initial
 * state is
 * applied, voltage-range and jumper changes must remain stable before taking
 * effect.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "power.h"

#include <Arduino.h>

#include "blink.h"
#include "hotwandlite.h"
#include "rfgen.h"
#include "testing_cli.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

namespace
{
/*
 * Keep the divider constants synchronized with the resistor values in the
 * schematic. The conversion reconstructs the input voltage from their ratio.
 * SAMD21 and RP2040 targets use their nominal analog supply as ADC full scale;
 * ESP32 targets use the core's calibrated millivolt reading.
 */
constexpr uint32_t kAdcReferenceMv    = 3300;
constexpr uint32_t kAdcMaximumReading = 4095;
constexpr uint32_t kDividerUpperOhms  = 100000;
constexpr uint32_t kDividerLowerOhms  = 4700;

/*
 * A centered 500 mV hysteresis band prevents chatter around the nominal 22 V
 * boundary: Low rises at 22.25 V and
 * High falls at 21.75 V.
 */
constexpr uint32_t kVoltageThresholdMv   = 22000;
constexpr uint32_t kVoltageHysteresisMv  = 500;
constexpr uint32_t kLowToHighThresholdMv = kVoltageThresholdMv + (kVoltageHysteresisMv / 2);
constexpr uint32_t kHighToLowThresholdMv = kVoltageThresholdMv - (kVoltageHysteresisMv / 2);

constexpr uint32_t kMinimumRfStartDelayMs      = 500;
constexpr uint32_t kChangeSettleTimeMs         = 2000;
constexpr uint32_t kReportIntervalMs           = 1000;
constexpr uint32_t kPowerSwitchSampleMs        = 100;
constexpr uint32_t kVoltageSampleIntervalMs    = 50;
constexpr uint32_t kVoltageFilterDivisor       = 8;
constexpr uint32_t kNormalRfTrackingIntervalMs = 5000;

/*
 * RF power mappings are kept here for easy product tuning. Normal mode uses
 * the two voltage/power points to define its linear slope.
 */
constexpr uint8_t  kEcoRfPowerPercent              = 70;
constexpr uint8_t  kSportRfPowerPercent            = 100;
constexpr uint32_t kNormalFullPowerVoltageMv       = 21000;
constexpr uint8_t  kNormalFullPowerPercent         = 100;
constexpr uint32_t kNormalReferenceVoltageMv       = 36000;
constexpr uint8_t  kNormalReferencePowerPercent    = 56;

static_assert(kNormalFullPowerVoltageMv < kNormalReferenceVoltageMv, "Normal power voltage range is invalid");
static_assert(kNormalFullPowerPercent > kNormalReferencePowerPercent, "Normal power must fall as voltage rises");
static_assert(kNormalReferencePowerPercent >= RFGEN_MINIMUM_POWER_PERCENT, "Normal reference power is below RF minimum");

constexpr uint32_t normal_power_drop(uint32_t voltageMv)
{
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(voltageMv - kNormalFullPowerVoltageMv) *
             (kNormalFullPowerPercent - kNormalReferencePowerPercent) +
         ((kNormalReferenceVoltageMv - kNormalFullPowerVoltageMv) / 2u)) /
        (kNormalReferenceVoltageMv - kNormalFullPowerVoltageMv));
}

constexpr uint8_t normal_power_percent(uint32_t voltageMv)
{
    return (voltageMv <= kNormalFullPowerVoltageMv)
               ? kNormalFullPowerPercent
               : ((normal_power_drop(voltageMv) >= (kNormalFullPowerPercent - RFGEN_MINIMUM_POWER_PERCENT))
                      ? RFGEN_MINIMUM_POWER_PERCENT
                      : static_cast<uint8_t>(kNormalFullPowerPercent - normal_power_drop(voltageMv)));
}

static_assert(normal_power_percent(21000) == 100, "Normal mode must be full power at 21 V");
static_assert(normal_power_percent(28500) == 78, "Normal mode midpoint is incorrect");
static_assert(normal_power_percent(36000) == 56, "Normal mode must be 56 percent at 36 V");
static_assert(normal_power_percent(40000) == 44, "Normal mode must remain linear above 36 V");

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

bool g_hardwareInitialized      = false;
bool g_inputStateInitialized    = false;
bool g_appliedStateValid        = false;
bool g_powerSwitchSampled       = false;
bool g_voltageFilterInitialized = false;
bool g_shutdownRequested        = false;
bool g_shutdownApplied          = false;

VoltageRange g_voltageRange        = VoltageRange::Low;
VoltageRange g_appliedVoltageRange = VoltageRange::Low;
PowerMode    g_powerMode           = PowerMode::Normal;
PowerMode    g_appliedPowerMode    = PowerMode::Normal;

uint32_t g_voltageChangedMs         = 0;
uint32_t g_powerChangedMs           = 0;
uint32_t g_lastReportMs             = 0;
uint32_t g_lastPowerSwitchSampleMs = 0;
uint32_t g_lastVoltageSampleMs      = 0;
uint32_t g_filteredVoltageMv        = 0;
uint32_t g_lastNormalRfTrackingMs   = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void            apply_state(uint32_t currentTimeMs, uint32_t voltageMv);
static VoltageRange    update_voltage_range(VoltageRange currentRange, uint32_t voltageMv);
static VoltageRange    initial_voltage_range(uint32_t voltageMv);
static void            initialize_hardware();
static void            sample_power_switch(uint32_t currentTimeMs);
static PowerMode       read_power_mode();
static uint8_t         power_percent(PowerMode powerMode, uint32_t voltageMv);
static blink_voltage_t voltage_to_blink_mode(VoltageRange voltageRange);
static blink_power_t   power_to_blink_mode(PowerMode powerMode);
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
    sample_power_switch(currentTimeMs);
    if (g_shutdownRequested)
    {
        if (!g_shutdownApplied)
        {
            rfgen_set(0);
            blink_set_enabled(false);
            g_appliedStateValid = false;
            g_shutdownApplied = true;
        }
        return;
    }

    if (g_shutdownApplied)
    {
        blink_set_enabled(true);
        g_shutdownApplied = false;
    }

    const uint32_t  voltageMv     = pwrmgt_read_voltage_mv();
    const PowerMode powerMode     = read_power_mode();

    report_readings(currentTimeMs, voltageMv, powerMode);

    if (!g_inputStateInitialized)
    {
        g_voltageRange          = initial_voltage_range(voltageMv);
        g_powerMode             = powerMode;
        g_voltageChangedMs      = currentTimeMs;
        g_powerChangedMs        = currentTimeMs;
        g_inputStateInitialized = true;
    }
    else
    {
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
    }

    if (!g_appliedStateValid)
    {
        // millis() is measured from boot, so this is an RF-start interlock rather
        // than a debounce interval for the initial input readings.
        if (currentTimeMs < kMinimumRfStartDelayMs)
        {
            return;
        }

        apply_state(currentTimeMs, voltageMv);
        return;
    }

    const bool voltageIsStable = static_cast<uint32_t>(currentTimeMs - g_voltageChangedMs) >= kChangeSettleTimeMs;
    const bool powerIsStable   = static_cast<uint32_t>(currentTimeMs - g_powerChangedMs) >= kChangeSettleTimeMs;

    // In Normal mode the RF target follows the filtered battery voltage, but
    // no faster than the battery can realistically require. Mode changes and
    // initial startup are still applied immediately by apply_state().
    if (powerIsStable && (g_powerMode == PowerMode::Normal) && (g_powerMode == g_appliedPowerMode) &&
        (static_cast<uint32_t>(currentTimeMs - g_lastNormalRfTrackingMs) >= kNormalRfTrackingIntervalMs))
    {
        rfgen_set(power_percent(g_powerMode, voltageMv));
        g_lastNormalRfTrackingMs = currentTimeMs;
    }

    if (!voltageIsStable || !powerIsStable)
    {
        return;
    }

    if ((g_voltageRange != g_appliedVoltageRange) || (g_powerMode != g_appliedPowerMode))
    {
        apply_state(currentTimeMs, voltageMv);
    }
}

uint32_t pwrmgt_read_voltage_mv(void)
{
    uint32_t simulatedVoltageMv = 0;
    if (testing_get_simulated_voltage_mv(&simulatedVoltageMv))
    {
        return simulatedVoltageMv;
    }

    initialize_hardware();

    const uint32_t currentTimeMs = millis();
    if (g_voltageFilterInitialized &&
        (static_cast<uint32_t>(currentTimeMs - g_lastVoltageSampleMs) < kVoltageSampleIntervalMs))
    {
        return g_filteredVoltageMv;
    }

    constexpr uint32_t kDividerTotalOhms      = kDividerUpperOhms + kDividerLowerOhms;
#if defined(HOT_WAND_TARGET_XIAO_ESP32S3) || defined(HOT_WAND_TARGET_ESP32C3)
    const uint32_t     adcVoltageMv          = static_cast<uint32_t>(analogReadMilliVolts(ADC_PIN));
    const uint64_t     conversionNumerator   = static_cast<uint64_t>(adcVoltageMv) * kDividerTotalOhms;
    constexpr uint32_t kConversionDenominator = kDividerLowerOhms;
#else
    const uint32_t     adcReading             = static_cast<uint32_t>(analogRead(ADC_PIN));
    constexpr uint32_t kConversionDenominator = kAdcMaximumReading * kDividerLowerOhms;
    const uint64_t     conversionNumerator    = static_cast<uint64_t>(adcReading) * kAdcReferenceMv * kDividerTotalOhms;
#endif

    // Add half the denominator so the integer conversion rounds to nearest mV.
    const uint32_t rawVoltageMv =
        static_cast<uint32_t>((conversionNumerator + (kConversionDenominator / 2)) / kConversionDenominator);

    g_lastVoltageSampleMs = currentTimeMs;
    if (!g_voltageFilterInitialized)
    {
        // Seed from the first reading so the startup interlock is not spent
        // waiting for a filter initialized at zero to charge.
        g_filteredVoltageMv        = rawVoltageMv;
        g_voltageFilterInitialized = true;
    }
    else
    {
        // Light first-order low-pass filter: 7/8 previous + 1/8 new.
        const uint64_t filteredNumerator =
            (static_cast<uint64_t>(g_filteredVoltageMv) * (kVoltageFilterDivisor - 1u)) + rawVoltageMv;
        g_filteredVoltageMv =
            static_cast<uint32_t>((filteredNumerator + (kVoltageFilterDivisor / 2u)) / kVoltageFilterDivisor);
    }

    return g_filteredVoltageMv;
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

namespace
{
static void apply_state(uint32_t currentTimeMs, uint32_t voltageMv)
{
    // A voltage-range-only change affects the status indication immediately,
    // but Normal-mode RF voltage tracking remains on its five-second cadence.
    if (!g_appliedStateValid || (g_powerMode != g_appliedPowerMode))
    {
        rfgen_set(power_percent(g_powerMode, voltageMv));
        if (g_powerMode == PowerMode::Normal)
        {
            g_lastNormalRfTrackingMs = currentTimeMs;
        }
    }
    blink_set_pattern(voltage_to_blink_mode(g_voltageRange), power_to_blink_mode(g_powerMode));

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

    // The power switch grounds this input to request shutdown.
    pinMode(POWER_SWITCH_PIN, INPUT_PULLUP);

    // Keep the active-high fan control off until the power switch is sampled.
    // Write the safe level before enabling the output to avoid a startup pulse.
    digitalWrite(FAN_CONTROL_PIN, LOW);
    pinMode(FAN_CONTROL_PIN, OUTPUT);

    // The PCB connects two alternative module pads to the voltage-sense net.
    // Only ADC_PIN may interact with it. INPUT disables the other pad's output
    // driver, and writing LOW while it is an input explicitly disables any
    // Arduino-style internal pull-up that may previously have been enabled.
    pinMode(ADC_UNUSED_PIN, INPUT);
    digitalWrite(ADC_UNUSED_PIN, LOW);
    pinMode(ADC_PIN, INPUT);

#if defined(HOT_WAND_TARGET_XIAO_SAMD21)
    // The SAMD21 core requires an explicit selection of its 3.3 V supply.
    analogReference(AR_DEFAULT);
#elif defined(HOT_WAND_TARGET_RP2040)
    // RP2040 ADC reference selection is fixed in hardware; this Arduino core
    // intentionally has no analogReference() API.
#elif defined(HOT_WAND_TARGET_XIAO_ESP32S3) || defined(HOT_WAND_TARGET_ESP32C3)
    // Arduino-ESP32 supplies calibrated millivolt readings. Maximum attenuation
    // is required because the existing divider approaches the 3.3 V rail.
#else
#error "Unsupported target for power-management ADC setup"
#endif
    analogReadResolution(12);
#if defined(HOT_WAND_TARGET_XIAO_ESP32S3) || defined(HOT_WAND_TARGET_ESP32C3)
    analogSetPinAttenuation(ADC_PIN, ADC_11db);
#endif

    g_hardwareInitialized = true;
}

static void sample_power_switch(uint32_t currentTimeMs)
{
    if (g_powerSwitchSampled &&
        (static_cast<uint32_t>(currentTimeMs - g_lastPowerSwitchSampleMs) < kPowerSwitchSampleMs))
    {
        return;
    }

    g_lastPowerSwitchSampleMs = currentTimeMs;
    g_shutdownRequested       = (digitalRead(POWER_SWITCH_PIN) == LOW);
    digitalWrite(FAN_CONTROL_PIN, g_shutdownRequested ? LOW : HIGH);
    g_powerSwitchSampled      = true;
}

static PowerMode read_power_mode()
{
    uint8_t simulatedMode = 0;
    if (testing_get_simulated_mode(&simulatedMode))
    {
        // The CLI accepts only the three values represented by PowerMode.
        return static_cast<PowerMode>(simulatedMode);
    }

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

static uint8_t power_percent(PowerMode powerMode, uint32_t voltageMv)
{
    switch (powerMode)
    {
    case PowerMode::Eco:
        return kEcoRfPowerPercent;

    case PowerMode::Sport:
        return kSportRfPowerPercent;

    case PowerMode::Normal:
    default:
        return normal_power_percent(voltageMv);
    }
}

static blink_voltage_t voltage_to_blink_mode(VoltageRange voltageRange)
{
    return (voltageRange == VoltageRange::High) ? BLINK_VOLTAGE_HIGH : BLINK_VOLTAGE_LOW;
}

static blink_power_t power_to_blink_mode(PowerMode powerMode)
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
