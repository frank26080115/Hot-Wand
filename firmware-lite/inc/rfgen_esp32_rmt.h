#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace rfgen_esp32_rmt
{
bool start(const uint32_t* periodTable, uint16_t periodCount);
bool change(const uint32_t* periodTable, uint16_t periodCount);
void stop();
} // namespace rfgen_esp32_rmt
