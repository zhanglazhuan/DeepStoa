#ifndef BUTTON_BSP_MCP_H
#define BUTTON_BSP_MCP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Button IDs (keep in sync with button_bsp.c)
#define BUTTON_BSP_ID_UP 1
#define BUTTON_BSP_ID_FUNCTION 2
#define BUTTON_BSP_ID_DOWN 3
#define BUTTON_BSP_ID_BOOT 4

// Event codes matching PressEvent in multi_button.h
#define BUTTON_BSP_EVENT_PRESS_DOWN 0
#define BUTTON_BSP_EVENT_PRESS_UP 1
#define BUTTON_BSP_EVENT_PRESS_REPEAT 2
#define BUTTON_BSP_EVENT_SINGLE_CLICK 3
#define BUTTON_BSP_EVENT_DOUBLE_CLICK 4
#define BUTTON_BSP_EVENT_LONG_PRESS_START 5
#define BUTTON_BSP_EVENT_LONG_PRESS_HOLD 6

// Initialize menu events without configuring legacy GPIO4/5/6.
void button_event_init(void);
void button_mcp_set_event_code(uint8_t button_id, uint8_t event);
// Slow display: coalesce touch input to the latest pending menu action.
bool button_touch_post_key_code(int key_code);

#ifdef __cplusplus
}
#endif

#endif
