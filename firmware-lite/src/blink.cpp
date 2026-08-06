/*
 * Non-blocking status LED pattern playback.
 *
 * Patterns encode long pulses, short pulses, and explicit pauses as
 * characters.
 * A null terminator loops each row without storing or calculating row lengths.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "blink.h"

#include <Arduino.h>

#include "hotwandlite_pins.h"

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
static void led_off();
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void blink_init(void)
{
    // Keep the LED dark until power management confirms the initial status.
    digitalWrite(BLINK_LED_PIN, LOW);
    pinMode(BLINK_LED_PIN, OUTPUT);

    g_initialized = true;
    if (g_patternSelected)
    {
        restart_pattern();
    }
}

void blink_task(void)
{
    // No pattern is valid until pwrmgt_task() confirms the boot inputs.
    if (!g_initialized || !g_patternSelected)
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
        digitalWrite(BLINK_LED_PIN, HIGH);
        g_currentDurationMs = kLongPulseMs;
        g_playbackState     = PlaybackState::PulseOn;
    }
    else if (command == kShortPulse)
    {
        digitalWrite(BLINK_LED_PIN, HIGH);
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

static void led_off()
{
    digitalWrite(BLINK_LED_PIN, LOW);
}
} // namespace
