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
#if OLED_DIMMING_ENABLED
        return settings.auto_dim;
#else
        return 0;
#endif
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

__declspec(dllexport) uint32_t storage_idle_save(uint32_t profile)
{
    settings.idle_detect_thresh = profile;
    return nvm_save(&settings);
}
__declspec(dllexport) uint32_t storage_idle_read(void)
{
    hotwand_setup_nvm_t actual;
    return nvm_read(&actual) ? actual.idle_detect_thresh : 255;
}

// Build-independent packed-byte access verifies both on-flash layouts.
__declspec(dllexport) uint32_t storage_layout(void)
{
    return NVM_RECORD_MAGIC;
}
__declspec(dllexport) void storage_record(uint32_t slot, uint32_t magic, uint32_t first, uint32_t second)
{
    volatile uint8_t*   bytes  = (volatile uint8_t*)nvm_page_start() + slot * 6;
    hotwand_setup_nvm_t record = {0};
    uint8_t*            data   = (uint8_t*)&record;
    data[0]                    = magic;
    data[1]                    = first;
    data[2]                    = second;
    data[3]                    = 0x50;
    record.checksum            = nvm_checksum(&record);
    for (uint32_t i = 0; i < 6; ++i)
    {
        bytes[i] = data[i];
    }
}
__declspec(dllexport) uint32_t storage_byte(uint32_t index)
{
    return ((volatile uint8_t*)nvm_page_start())[index];
}
__declspec(dllexport) uint32_t storage_pending_erase(void)
{
    return nvm_erase_required;
}
__declspec(dllexport) uint32_t storage_reserved_save(void)
{
#if !OLED_DIMMING_ENABLED
    settings.rsvd_0 = 3;
#endif
    settings.rsvd_1 = 3;
    return nvm_save(&settings);
}

__declspec(dllexport) uint32_t storage_flags_save(void)
{
    settings.auto_sleep  = 3;
    settings.show_splash = settings.fan_sig_inv = 1;
#if OLED_DIMMING_ENABLED
    settings.auto_dim = 3;
#endif
    return nvm_save(&settings);
}
__declspec(dllexport) void storage_corrupt(uint32_t index, uint32_t value)
{
    ((volatile uint8_t*)nvm_page_start())[index] = value;
}
