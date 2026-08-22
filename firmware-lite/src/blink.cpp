/*
 * Non-blocking status LED pattern playback.
 *
 * Patterns encode long pulses, short pulses, and explicit pauses
 * A null terminator loops each row without storing or calculating row lengths
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "blink.h"

#include <Arduino.h>

#include "hotwandlite.h"

#if defined(HOT_WAND_TARGET_XIAO_ESP32S3) || defined(HOT_WAND_TARGET_XIAO_ESP32C3)
#include <driver/gpio.h>
#endif

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

namespace
{
constexpr uint32_t kShortPulseMs = 100;
constexpr uint32_t kLongPulseMs  = 500;
constexpr uint32_t kPauseMs      = 100;

constexpr uint8_t kVoltageLevelCount = 2;
constexpr uint8_t kPowerLevelCount   = 3;
constexpr uint8_t kPatternCount      = kVoltageLevelCount * kPowerLevelCount;

constexpr char kLongPulse  = 'L';
constexpr char kShortPulse = 'S';
constexpr char kPause      = 'P';
constexpr char kTerminator = '\0';

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

struct BlinkPattern
{
    blink_voltage_t voltage;
    blink_power_t   power;
    char            commands[8];
};

enum class PlaybackState : uint8_t
{
    LoadCommand,
    PulseOn,
    Pause,
};

// -----------------------------------------------------------------------------
// Pattern Table
// -----------------------------------------------------------------------------

constexpr BlinkPattern kPatterns[kPatternCount] = {
    {BLINK_VOLTAGE_LOW,  BLINK_POWER_ECO,    "SPPPPPP"},
    {BLINK_VOLTAGE_LOW,  BLINK_POWER_NORMAL, "SSPPPP" },
    {BLINK_VOLTAGE_LOW,  BLINK_POWER_SPORT,  "SSSPP"  },
    {BLINK_VOLTAGE_HIGH, BLINK_POWER_ECO,    "LSPPP"  },
    {BLINK_VOLTAGE_HIGH, BLINK_POWER_NORMAL, "LSSPP"  },
    {BLINK_VOLTAGE_HIGH, BLINK_POWER_SPORT,  "LPLP"   },
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

bool          g_initialized       = false;
bool          g_enabled           = true;
bool          g_patternSelected   = false;
uint8_t       g_patternIndex      = 0;
uint8_t       g_playbackIndex     = 0;
uint32_t      g_lastTransitionMs  = 0;
uint32_t      g_currentDurationMs = 0;
PlaybackState g_playbackState     = PlaybackState::LoadCommand;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void start_next_command(uint32_t currentTimeMs);
static void restart_pattern();
static void led_on();
static void led_off();
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void blink_init(void)
{
    // Keep available LEDs dark until power management applies the initial status.
    digitalWrite(BLINK_LED_PIN, LOW);
#ifdef BLINK_XIAOBUILTIN_LED_PIN
    digitalWrite(BLINK_XIAOBUILTIN_LED_PIN, HIGH);
#endif
#if defined(HOT_WAND_TARGET_XIAO_SAMD21)
    pinMode(BLINK_LED_PIN, OUTPUT);
    // High drive sources up to 7 mA while meeting VOH; the PA06 GPIO cluster
    // has a 14 mA maximum aggregate source current.
    PORT->Group[g_APinDescription[BLINK_LED_PIN].ulPort]
        .PINCFG[g_APinDescription[BLINK_LED_PIN].ulPin]
        .bit.DRVSTR = 1;
#elif defined(HOT_WAND_TARGET_XIAO_RP2040) || defined(HOT_WAND_TARGET_WAVESHARE_RP2040_ZERO)
    // Select the 8 mA drive characteristic; total current sourced by all GPIO
    // and QSPI pins must remain below 50 mA.
    pinMode(BLINK_LED_PIN, OUTPUT_8MA);
#elif defined(HOT_WAND_TARGET_XIAO_ESP32S3) || defined(HOT_WAND_TARGET_XIAO_ESP32C3)
    pinMode(BLINK_LED_PIN, OUTPUT);
    // Strongest drive is characterized at about 40 mA source current while
    // meeting VOH; it is a drive characteristic, not a current limiter.
    gpio_set_drive_capability(static_cast<gpio_num_t>(BLINK_LED_PIN), GPIO_DRIVE_CAP_3);
#endif
#ifdef BLINK_XIAOBUILTIN_LED_PIN
    pinMode(BLINK_XIAOBUILTIN_LED_PIN, OUTPUT);
#endif

    g_initialized = true;
    if (g_patternSelected)
    {
        restart_pattern();
    }
}

void blink_task(void)
{
    // No pattern is valid until pwrmgt_task() applies the boot inputs.
    if (!g_initialized || !g_enabled || !g_patternSelected)
    {
        return;
    }

    const uint32_t currentTimeMs = millis();

    if (g_playbackState == PlaybackState::LoadCommand)
    {
        start_next_command(currentTimeMs);
        return;
    }

    // Unsigned subtraction keeps elapsed-time checks correct across millis() rollover.
    if (static_cast<uint32_t>(currentTimeMs - g_lastTransitionMs) < g_currentDurationMs)
    {
        return;
    }

    if (g_playbackState == PlaybackState::PulseOn)
    {
        // Every long or short pulse gets this mandatory off interval.
        led_off();
        g_lastTransitionMs  = currentTimeMs;
        g_currentDurationMs = kPauseMs;
        g_playbackState     = PlaybackState::Pause;
        return;
    }

    start_next_command(currentTimeMs);
}

void blink_set_enabled(bool enabled)
{
    if (enabled == g_enabled)
    {
        return;
    }

    g_enabled = enabled;
    if (!g_initialized)
    {
        return;
    }

    if (!g_enabled)
    {
        led_off();
        return;
    }

    if (g_patternSelected)
    {
        restart_pattern();
    }
}

void blink_set_pattern(blink_voltage_t voltage, blink_power_t power)
{
    const uint8_t voltageIndex = static_cast<uint8_t>(voltage);
    const uint8_t powerIndex   = static_cast<uint8_t>(power);
    if ((voltageIndex >= kVoltageLevelCount) || (powerIndex >= kPowerLevelCount))
    {
        return;
    }

    // The table stores the three power modes consecutively for each voltage range.
    const uint8_t patternIndex = static_cast<uint8_t>((voltageIndex * kPowerLevelCount) + powerIndex);
    if (g_patternSelected && (patternIndex == g_patternIndex))
    {
        return;
    }

    g_patternIndex    = patternIndex;
    g_patternSelected = true;
    if (g_initialized)
    {
        restart_pattern();
    }
    else
    {
        g_playbackIndex = 0;
        g_playbackState = PlaybackState::LoadCommand;
    }
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

namespace
{
static void start_next_command(uint32_t currentTimeMs)
{
    char command = kPatterns[g_patternIndex].commands[g_playbackIndex++];
    if (command == kTerminator)
    {
        // Every row is non-empty, so its first command is valid after looping.
        g_playbackIndex = 0;
        command         = kPatterns[g_patternIndex].commands[g_playbackIndex++];
    }

    g_lastTransitionMs = currentTimeMs;

    if (command == kLongPulse)
    {
        led_on();
        g_currentDurationMs = kLongPulseMs;
        g_playbackState     = PlaybackState::PulseOn;
    }
    else if (command == kShortPulse)
    {
        led_on();
        g_currentDurationMs = kShortPulseMs;
        g_playbackState     = PlaybackState::PulseOn;
    }
    else
    {
        // The table contains only L, S, P, and a trailing terminator.
        led_off();
        g_currentDurationMs = kPauseMs;
        g_playbackState     = PlaybackState::Pause;
    }
}

static void restart_pattern()
{
    // A status change always restarts at a known low output and row index zero.
    g_playbackIndex     = 0;
    g_currentDurationMs = 0;
    g_playbackState     = PlaybackState::LoadCommand;
    g_lastTransitionMs  = millis();
    led_off();
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

static void led_on()
{
    digitalWrite(BLINK_LED_PIN, HIGH);
#ifdef BLINK_XIAOBUILTIN_LED_PIN
    digitalWrite(BLINK_XIAOBUILTIN_LED_PIN, LOW);
#endif
}

static void led_off()
{
    digitalWrite(BLINK_LED_PIN, LOW);
#ifdef BLINK_XIAOBUILTIN_LED_PIN
    digitalWrite(BLINK_XIAOBUILTIN_LED_PIN, HIGH);
#endif
}
} // namespace
