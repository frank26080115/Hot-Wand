/*
 * USB-serial hardware test commands.
 *
 * No test is active at boot, so normal Hot Wand operation continues while the
 * CLI waits for a complete command. Direct-output tests permanently take
 * ownership of loop() until reset. Simulation commands instead override an
 * input and leave the normal application running.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "testing_cli.h"

#include <Arduino.h>
#include <SerialCommands.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "power.h"
#include "rfgen.h"

// -----------------------------------------------------------------------------
// Test CLI
// -----------------------------------------------------------------------------

namespace
{
constexpr size_t   kCliBufferSize          = 32;
constexpr uint32_t kVoltageReportIntervalMs = 1000;

char cliBuffer[kCliBufferSize];
// Terminate on LF and treat CR as argument whitespace. This accepts terminals
// that send either LF or CRLF instead of requiring one exact line ending.
SerialCommands testingCli(&Serial, cliBuffer, sizeof(cliBuffer), "\n", " \r\t");

bool     g_testingActive           = false;
bool     g_voltageReportingEnabled = false;
bool     g_simulatedModeEnabled     = false;
bool     g_simulatedVoltageEnabled  = false;
bool     g_cliReadyReported         = false;
uint8_t  g_simulatedMode            = 0;
uint32_t g_simulatedVoltageMv       = 0;
uint32_t g_lastVoltageReportMs     = 0;

static void set_power_command(SerialCommands* sender);
static void enable_voltage_command(SerialCommands* sender);
static void simulate_mode_command(SerialCommands* sender);
static void simulate_voltage_command(SerialCommands* sender);
static void unrecognized_command(SerialCommands* sender, const char* command);
static void start_testing(SerialCommands* sender);
static void report_voltage(uint32_t currentTimeMs, uint32_t voltageMv);
static const char* simulated_mode_name(uint8_t modeNumber);

SerialCommand powerCommand("power", set_power_command);
SerialCommand voltageCommand("voltage", enable_voltage_command);
SerialCommand simulateModeCommand("simmode", simulate_mode_command);
SerialCommand simulateVoltageCommand("simvoltage", simulate_voltage_command);

/*
 * To add another test:
 *  1. Write a SerialCommands callback and create a persistent SerialCommand.
 *  2. Register that command in cli_init().
 *  3. For an exclusive test, call start_testing() and service any ongoing work
 *     from testing_task() without blocking.
 *  4. For a simulation, set an override flag/value and provide a getter that
 *     the production input function checks before reading its physical pin.
 *
 * Do not clear g_testingActive. Test mode is deliberately sticky so normal
 * soldering-iron control can never resume behind a running hardware test.
 */
} // namespace

// -----------------------------------------------------------------------------
// Public Interface
// -----------------------------------------------------------------------------

void cli_init()
{
    testingCli.AddCommand(&powerCommand);
    testingCli.AddCommand(&voltageCommand);
    testingCli.AddCommand(&simulateModeCommand);
    testingCli.AddCommand(&simulateVoltageCommand);
    testingCli.SetDefaultHandler(unrecognized_command);
}

bool testing_task()
{
    // Wait until input proves that a host has opened the USB CDC port before
    // printing the banner; setup-time USB output can be lost during enumeration.
    if (!g_cliReadyReported && (Serial.available() > 0))
    {
        Serial.println("OK: test CLI ready; line endings LF or CRLF");
        g_cliReadyReported = true;
    }

    // ReadSerial() is non-blocking and dispatches complete LF-terminated
    // commands. A CR from CRLF is discarded as token whitespace.
    const SERIAL_COMMANDS_ERRORS readResult = testingCli.ReadSerial();
    if (readResult == SERIAL_COMMANDS_ERROR_BUFFER_FULL)
    {
        Serial.println("ERROR: command is too long");
    }

    if (!g_testingActive)
    {
        return false;
    }

    const uint32_t currentTimeMs = millis();
    if (g_voltageReportingEnabled)
    {
        // Keep the ADC filter sampling at its normal cadence; only the serial
        // report is throttled to once per second.
        const uint32_t voltageMv = pwrmgt_read_voltage_mv();
        if (static_cast<uint32_t>(currentTimeMs - g_lastVoltageReportMs) >= kVoltageReportIntervalMs)
        {
            g_lastVoltageReportMs = currentTimeMs;
            report_voltage(currentTimeMs, voltageMv);
        }
    }

    return true;
}

bool testing_get_simulated_mode(uint8_t* modeNumber)
{
    if (!g_simulatedModeEnabled || (modeNumber == nullptr))
    {
        return false;
    }

    *modeNumber = g_simulatedMode;
    return true;
}

bool testing_get_simulated_voltage_mv(uint32_t* voltageMv)
{
    if (!g_simulatedVoltageEnabled || (voltageMv == nullptr))
    {
        return false;
    }

    *voltageMv = g_simulatedVoltageMv;
    return true;
}

// -----------------------------------------------------------------------------
// Command Handlers
// -----------------------------------------------------------------------------

namespace
{
static void set_power_command(SerialCommands* sender)
{
    char* argument = sender->Next();
    if ((argument == nullptr) || (sender->Next() != nullptr))
    {
        sender->GetSerial()->println("ERROR: usage: power <percent>");
        return;
    }

    errno          = 0;
    char* end      = nullptr;
    const long requestedPower = strtol(argument, &end, 10);
    if ((errno == ERANGE) || (end == argument) || (*end != '\0'))
    {
        sender->GetSerial()->println("ERROR: power must be an integer");
        return;
    }

    // Preserve rfgen_set() behavior: negative requests become off, while values
    // over 100 reach rfgen_set() as a representable out-of-range power request
    // and are clamped there.
    const uint8_t powerPercent =
        (requestedPower < 0) ? 0
                             : ((requestedPower > UCHAR_MAX) ? UCHAR_MAX : static_cast<uint8_t>(requestedPower));

    start_testing(sender);
    rfgen_set(powerPercent);

    sender->GetSerial()->print("OK: RF power requested: ");
    sender->GetSerial()->print(requestedPower < 0 ? 0 : requestedPower);
    sender->GetSerial()->println('%');
}

static void enable_voltage_command(SerialCommands* sender)
{
    if (sender->Next() != nullptr)
    {
        sender->GetSerial()->println("ERROR: usage: voltage");
        return;
    }

    start_testing(sender);
    g_voltageReportingEnabled = true;
    g_lastVoltageReportMs     = millis();
    sender->GetSerial()->println("OK: ADC voltage reporting enabled at 1 second intervals");
}

static void simulate_mode_command(SerialCommands* sender)
{
    char* argument = sender->Next();
    if ((argument == nullptr) || (sender->Next() != nullptr))
    {
        sender->GetSerial()->println("ERROR: usage: simmode <0=ECO, 1=NORMAL, 2=SPORT>");
        return;
    }

    errno          = 0;
    char* end      = nullptr;
    const long modeNumber = strtol(argument, &end, 10);
    if ((errno == ERANGE) || (end == argument) || (*end != '\0') || (modeNumber < 0) || (modeNumber > 2))
    {
        sender->GetSerial()->println("ERROR: mode must be 0 (ECO), 1 (NORMAL), or 2 (SPORT)");
        return;
    }

    g_simulatedMode        = static_cast<uint8_t>(modeNumber);
    g_simulatedModeEnabled = true;

    sender->GetSerial()->print("OK: mode input overridden until reset: ");
    sender->GetSerial()->print(g_simulatedMode);
    sender->GetSerial()->print(" (");
    sender->GetSerial()->print(simulated_mode_name(g_simulatedMode));
    sender->GetSerial()->println(')');
}

static void simulate_voltage_command(SerialCommands* sender)
{
    char* argument = sender->Next();
    if ((argument == nullptr) || (sender->Next() != nullptr))
    {
        sender->GetSerial()->println("ERROR: usage: simvoltage <millivolts>");
        return;
    }

    errno              = 0;
    char* end          = nullptr;
    const unsigned long voltageMv = strtoul(argument, &end, 10);
    if ((errno == ERANGE) || (end == argument) || (*end != '\0') || (*argument == '-'))
    {
        sender->GetSerial()->println("ERROR: voltage must be a non-negative integer in millivolts");
        return;
    }

    g_simulatedVoltageMv      = static_cast<uint32_t>(voltageMv);
    g_simulatedVoltageEnabled = true;

    sender->GetSerial()->print("OK: voltage input overridden until reset: ");
    sender->GetSerial()->print(g_simulatedVoltageMv);
    sender->GetSerial()->println(" mV");
}

static void unrecognized_command(SerialCommands* sender, const char* command)
{
    sender->GetSerial()->print("ERROR: unknown test command: ");
    sender->GetSerial()->println(command);
    sender->GetSerial()->println("Commands: power <percent>, voltage, simmode <0-2>, simvoltage <mV>");
}

static void start_testing(SerialCommands* sender)
{
    if (g_testingActive)
    {
        return;
    }

    g_testingActive = true;
    sender->GetSerial()->println("Test mode active; reset the board to resume normal operation");
}

static void report_voltage(uint32_t currentTimeMs, uint32_t voltageMv)
{
    Serial.print(currentTimeMs);
    Serial.print(" ms: voltage=");
    Serial.print(voltageMv);
    Serial.println(" mV");
}

static const char* simulated_mode_name(uint8_t modeNumber)
{
    switch (modeNumber)
    {
    case 0:
        return "ECO";

    case 2:
        return "SPORT";

    case 1:
    default:
        return "NORMAL";
    }
}
} // namespace
