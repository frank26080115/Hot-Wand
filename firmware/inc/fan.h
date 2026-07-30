#ifndef HOT_WAND_FAN_H
#define HOT_WAND_FAN_H

#ifdef __cplusplus
extern "C" {
#endif

void fan_init(void);
void fan_task(void);
void fan_on_wake(void);

#ifdef __cplusplus
}
#endif

#endif
