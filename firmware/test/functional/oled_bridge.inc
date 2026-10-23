/* Included after the unmodified OLED_SetDimMode body extracted from oled.c.
 * This isolates one public function without emulating the I2C register block. */
static OLED_Handle         handle;
static uint32_t            commands, contrast, fail_transport;
__declspec(dllexport) void dim_reset(void)
{
    handle.initialized = 1;
    handle.dimmed      = 0;
    commands = contrast = fail_transport = 0;
}
__declspec(dllexport) uint32_t dim_set(uint32_t enabled)
{
    return OLED_SetDimMode(&handle, enabled != 0);
}
__declspec(dllexport) void dim_fail(uint32_t failure)
{
    fail_transport = failure;
}
__declspec(dllexport) uint32_t dim_value(uint32_t selector)
{
    return selector == 0 ? handle.dimmed : selector == 1 ? contrast : commands;
}
void u8x8_SetContrast(u8x8_t* graphics, uint8_t value)
{
    ++commands;
    contrast = value;
    if (fail_transport)
    {
        handle.transport_ok = 0;
    }
}
