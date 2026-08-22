#if defined(HOT_WAND_TARGET_XIAO_ESP32C3)

/* ESP32-C3 platform hooks; the RMT engine is shared with ESP32-S3. */

#include "rfgen_internal.h"

#include "rfgen_esp32_rmt.h"

bool rfgen_platform_start(const uint32_t* periodTable, uint16_t periodCount)
{
    return rfgen_esp32_rmt::start(periodTable, periodCount);
}

bool rfgen_platform_change(const uint32_t* periodTable, uint16_t periodCount)
{
    return rfgen_esp32_rmt::change(periodTable, periodCount);
}

void rfgen_platform_stop(void)
{
    rfgen_esp32_rmt::stop();
}

#endif
