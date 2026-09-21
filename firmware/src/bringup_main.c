#include "tests.h"

#if defined(HOT_WAND_BRINGUP_BUILD) && HOT_WAND_BRINGUP_BUILD

int main(void)
{
    test_run();

    /* A selected bring-up test is expected not to return. Keep an inert
     * fallback here so an accidentally returning test cannot enter firmware
     * application code that is intentionally absent from this image. */
    for (;;)
    {
    }
}

#endif
