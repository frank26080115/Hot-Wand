/* Native flash model runs the actual journal and checksum implementations.
 * The aligned linker symbols bound a writable DLL page, never device flash. */
#include "../../src/nvm.c"
#include "../../src/miscutils.c"
#include "../../src/watchdog.c"

__asm__(".data\n.p2align 10\n.globl __nvm_page_start__\n__nvm_page_start__:\n"
        ".space 1024,255\n.globl __nvm_page_end__\n__nvm_page_end__:\n.text\n");

static uint32_t            failure, program_failure_at, program_calls, lock_calls, unfreeze_calls, refresh_calls;
static hotwand_setup_nvm_t settings;

__declspec(dllexport) void storage_reset(void)
{
    volatile uint8_t* bytes = (volatile uint8_t*)nvm_page_start();
    for (uint32_t index = 0; index < 1024; ++index)
    {
        bytes[index] = 255;
    }
    nvm_initialized = false;
    failure = program_calls = lock_calls = unfreeze_calls = refresh_calls = 0;
    nvm_init();
    nvm_apply_defaults(&settings);
}

__declspec(dllexport) uint32_t storage_save(uint32_t mode)
{
    settings.startup_power_level = mode;
    return nvm_save(&settings);
}

__declspec(dllexport) void storage_fail(uint32_t which)
{
    failure            = which;
    program_failure_at = program_calls + 2;
}
__declspec(dllexport) uint32_t storage_erase(void)
{
    return nvm_factory_reset();
}
__declspec(dllexport) uint32_t storage_read(void)
{
    hotwand_setup_nvm_t readback;
    return nvm_read(&readback) ? readback.startup_power_level : 255;
}
__declspec(dllexport) void storage_reboot(void)
{
    nvm_initialized = false;
    nvm_init();
}
__declspec(dllexport) uint32_t storage_value(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return program_calls;
    case 1:
        return lock_calls;
    case 2:
        return nvm_next_slot;
    case 3:
        return unfreeze_calls;
    case 4:
        return refresh_calls;
    default:
        return 0;
    }
}
__declspec(dllexport) uint32_t storage_watchdog(void)
{
    return watchdog_init();
}
__declspec(dllexport) void storage_feed(void)
{
    watchdog_feed();
}

/* Failures: unlock; erase status; erase error; erase readback; program status;
 * program readback; torn second halfword; watchdog startup. */
HAL_StatusTypeDef HAL_FLASH_Unlock(void)
{
    return failure == 1;
}
HAL_StatusTypeDef HAL_FLASH_Lock(void)
{
    ++lock_calls;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef* cfg, uint32_t* error)
{
    volatile uint8_t* bytes = (volatile uint8_t*)nvm_page_start();
    *error                  = failure == 3 ? 0 : UINT32_MAX;
    if (failure == 2)
    {
        return 1;
    }
    if (failure != 4)
    {
        for (uint32_t index = 0; index < 1024; ++index)
        {
            bytes[index] = 255;
        }
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t value)
{
    uint32_t           offset      = address - (uint32_t)nvm_page_start();
    volatile uint16_t* destination = (volatile uint16_t*)(nvm_page_start() + offset);
    ++program_calls;
    if ((failure == 5) || ((failure == 7) && (program_calls == program_failure_at)))
    {
        return 1;
    }
    if (failure != 6)
    {
        *destination &= value;
    }
    return HAL_OK;
}
void mock_unfreeze(void)
{
    ++unfreeze_calls;
}
HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef* handle)
{
    return failure == 8 || handle->Init.Prescaler != 64 || handle->Init.Reload != 2499;
}
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef* handle)
{
    ++refresh_calls;
    return HAL_OK;
}

/* LLVM may lower aggregate initialization in the included utilities to these
 * routines. Volatile destinations keep this no-CRT fixture self-contained. */
void* memset(void* destination, int value, size_t count)
{
    volatile uint8_t* bytes = destination;
    for (size_t index = 0; index < count; ++index)
    {
        bytes[index] = value;
    }
    return destination;
}
void* memcpy(void* destination, const void* source, size_t count)
{
    volatile uint8_t* bytes = destination;
    const uint8_t*    input = source;
    for (size_t index = 0; index < count; ++index)
    {
        bytes[index] = input[index];
    }
    return destination;
}
