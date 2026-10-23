#include "../faults/storage_bridge.c"
__declspec(dllexport) uint32_t default_value(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return settings.startup_power_level;
    case 1:
        return settings.fan_mode;
    case 2:
        return settings.auto_sleep;
    case 3:
        return settings.auto_dim;
    case 4:
        return settings.idle_detect_thresh;
    case 5:
        return settings.batt_mode;
    case 6:
        return settings.show_splash;
    case 7:
        return settings.fan_sig_inv;
    case 8:
        return settings.input_v_calib;
    }
    return 255;
}
