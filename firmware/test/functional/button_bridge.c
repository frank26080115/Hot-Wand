#include "../button_modal/bridge.c"
__declspec(dllexport) uint32_t battery_guess_value(uint32_t voltage, uint32_t mode, uint32_t pessimistic)
{
    battery_guess_t guess;
    if (!battery_guess(voltage, mode, &guess))
    {
        return 0;
    }
    return pessimistic ? guess.pessimistic_cell_count : guess.optimistic_cell_count;
}
__declspec(dllexport) uint32_t battery_ok(void)
{
    return battery_check();
}
__declspec(dllexport) uint32_t button_shorts(void)
{
    return btn_get_consecutive_presses();
}
__declspec(dllexport) uint32_t button_event(uint32_t long_event)
{
    return long_event ? btn_has_long_press(true) : btn_has_short_press(true);
}
