#include "button_bsp.h"
#include "multi_button.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern int global_adjust_output_volume(int delta);


EventGroupHandle_t key_groups;
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
static QueueHandle_t s_key_actions;
static QueueHandle_t s_touch_action;
#endif
struct Button Button_Up;            // Application button
#define Button_Up_KEY 4             // The actual GPIO
#define Button_Up_id 1              // The ID of the key
#define Button_Up_active 0          // active level

struct Button Button_Function;      
#define Button_Function_KEY 5    
#define Button_Function_id 2     
#define Button_Function_active 0    

struct Button Button_Down;    
#define Button_Down_KEY 6    
#define Button_Down_id 3      
#define Button_Down_active 0   

struct Button Boot;   
#define Boot_KEY 0       
#define Boot_id 4        
#define Boot_active 0    


static void button_press_event(void* btn);
static bool set_key_group_bits(uint8_t button_id, PressEvent event);
static volatile int s_forced_key_code = -1;
static int64_t s_last_volume_up_adjust_us = 0;
static int64_t s_last_volume_down_adjust_us = 0;

#define VOLUME_ADJUST_STEP 5
#define VOLUME_ADJUST_INTERVAL_US 120000

static void handle_global_volume_event(EventBits_t even)
{
  const int64_t now_us = esp_timer_get_time();

  if (get_bit_button(even, 5)) {
    global_adjust_output_volume(VOLUME_ADJUST_STEP);
    s_last_volume_up_adjust_us = now_us;
    return;
  }

  if (get_bit_button(even, 6)) {
    if (now_us - s_last_volume_up_adjust_us >= VOLUME_ADJUST_INTERVAL_US) {
      global_adjust_output_volume(VOLUME_ADJUST_STEP);
      s_last_volume_up_adjust_us = now_us;
    }
    return;
  }

  if (get_bit_button(even, 19)) {
    global_adjust_output_volume(-VOLUME_ADJUST_STEP);
    s_last_volume_down_adjust_us = now_us;
    return;
  }

  if (get_bit_button(even, 20)) {
    if (now_us - s_last_volume_down_adjust_us >= VOLUME_ADJUST_INTERVAL_US) {
      global_adjust_output_volume(-VOLUME_ADJUST_STEP);
      s_last_volume_down_adjust_us = now_us;
    }
    return;
  }
}

static void clock_task_callback(void *arg)
{
  button_ticks();              // Status callback
}
static uint8_t read_button_GPIO(uint8_t button_id)   // Return the GPIO level
{
	switch (button_id)
    {
        case Button_Up_id:
        return gpio_get_level(Button_Up_KEY);
        case Button_Function_id:
        return gpio_get_level(Button_Function_KEY);
        case Button_Down_id:
        return gpio_get_level(Button_Down_KEY);
        case Boot_id:
        return gpio_get_level(Boot_KEY);
        default:
        break;
    }
    return 1;
}

// GPIO initialization
static void gpio_init(void)
{
  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_INPUT;
  gpio_conf.pin_bit_mask = ((uint64_t)0x01<<Button_Up_KEY) | ((uint64_t)0x01<<Button_Function_KEY) | ((uint64_t)0x01<<Button_Down_KEY) | ((uint64_t)0x01<<Boot_KEY);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;

  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

}

void button_event_init(void)
{
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
  if (s_key_actions == NULL) {
    s_key_actions = xQueueCreate(32, sizeof(int));
    configASSERT(s_key_actions != NULL);
  }
  if (s_touch_action == NULL) {
    s_touch_action = xQueueCreate(1, sizeof(int));
    configASSERT(s_touch_action != NULL);
  }
#endif
  if (key_groups == NULL) {
    key_groups = xEventGroupCreate();
    configASSERT(key_groups != NULL);
  }
}

// Key initialization
void button_Init(void)
{
  button_event_init();
  gpio_init();
  button_init(&Button_Up, read_button_GPIO, Button_Up_active , Button_Up_id);              // Initialize the object callback function trigger level key ID
  button_attach(&Button_Up,SINGLE_CLICK,button_press_event);           //Click on the event
  button_attach(&Button_Up,DOUBLE_CLICK,button_press_event);           //Double-click the event
  button_attach(&Button_Up,PRESS_DOWN,button_press_event);             //Press the event
  button_attach(&Button_Up,PRESS_UP,button_press_event);               //Bouncing incident
  button_attach(&Button_Up,PRESS_REPEAT,button_press_event);           //Press the event repeatedly
  button_attach(&Button_Up,LONG_PRESS_START,button_press_event);       //Long press to trigger once
  button_attach(&Button_Up,LONG_PRESS_HOLD,button_press_event);        //Long press to keep triggering

  button_init(&Button_Function, read_button_GPIO, Button_Function_active , Button_Function_id);            
  button_attach(&Button_Function,SINGLE_CLICK,button_press_event);           
  button_attach(&Button_Function,DOUBLE_CLICK,button_press_event);        
  button_attach(&Button_Function,PRESS_DOWN,button_press_event);         
  button_attach(&Button_Function,PRESS_UP,button_press_event);       
  button_attach(&Button_Function,PRESS_REPEAT,button_press_event);      
  button_attach(&Button_Function,LONG_PRESS_START,button_press_event);   
  button_attach(&Button_Function,LONG_PRESS_HOLD,button_press_event);   

  button_init(&Button_Down, read_button_GPIO, Button_Down_active , Button_Down_id);         
  button_attach(&Button_Down,SINGLE_CLICK,button_press_event);     
  button_attach(&Button_Down,DOUBLE_CLICK,button_press_event);   
  button_attach(&Button_Down,PRESS_DOWN,button_press_event);      
  button_attach(&Button_Down,PRESS_UP,button_press_event);       
  button_attach(&Button_Down,PRESS_REPEAT,button_press_event);     
  button_attach(&Button_Down,LONG_PRESS_START,button_press_event);    
  button_attach(&Button_Down,LONG_PRESS_HOLD,button_press_event);     

  button_init(&Boot, read_button_GPIO, Boot_active , Boot_id);        
  button_attach(&Boot,SINGLE_CLICK,button_press_event);  
  button_attach(&Boot,DOUBLE_CLICK,button_press_event);  
  button_attach(&Boot,PRESS_DOWN,button_press_event);    
  button_attach(&Boot,PRESS_UP,button_press_event);     
  button_attach(&Boot,PRESS_REPEAT,button_press_event);   
  button_attach(&Boot,LONG_PRESS_START,button_press_event);   
  button_attach(&Boot,LONG_PRESS_HOLD,button_press_event);   

  const esp_timer_create_args_t clock_tick_timer_args = 
  {
    .callback = &clock_task_callback,
    .name = "clock_task",
    .arg = NULL,
  };
  esp_timer_handle_t clock_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&clock_tick_timer_args, &clock_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(clock_tick_timer, 1000 * 5));  // 5ms
  button_start(&Button_Up); // Start button
  button_start(&Button_Function); 
  button_start(&Button_Down);
  button_start(&Boot);
}

// button press event
static void button_press_event(void* btn)
{
  struct Button *user_button = (struct Button *)btn;
  PressEvent event = get_button_event(user_button);
  uint8_t buttonID = user_button->button_id;
  (void)set_key_group_bits(buttonID, event);
}

void button_mcp_set_event(uint8_t button_id, PressEvent event)
{
  if (key_groups == NULL) {
    return;
  }
  (void)set_key_group_bits(button_id, event);
}

void button_mcp_set_event_code(uint8_t button_id, uint8_t event)
{
  if (event >= number_of_event) {
    return;
  }
  button_mcp_set_event(button_id, (PressEvent)event);
}

void button_mcp_force_key_code(int key_code)
{
  s_forced_key_code = key_code;
}

bool button_touch_post_key_code(int key_code)
{
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
  if (s_touch_action == NULL || (key_code != 0 && key_code != 7 &&
      key_code != 8 && key_code != 12 && key_code != 14)) return false;
  bool ok = xQueueOverwrite(s_touch_action, &key_code) == pdTRUE;
  if (ok) ESP_LOGI("KEY_QUEUE", "Latest touch action=%d", key_code);
  return ok;
#else
  (void)key_code;
  return false;
#endif
}

static int fetch_forced_key_code(void)
{
  int code = s_forced_key_code;
  if (code >= 0) {
    s_forced_key_code = -1;
  }
  return code;
}

static bool set_key_group_bits(uint8_t button_id, PressEvent event)
{
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
  // Edge events are diagnostic only: pages act on clicks and long presses.
  // A queue retains these actions while the UI is occupied with slow e-paper.
  int code = -1;
  if (button_id < 1 || button_id > 4) return false;
  const int base = (button_id - 1) * 7;
  if (event == SINGLE_CLICK) code = base;
  else if (event == DOUBLE_CLICK) code = base + 1;
  else if (event == LONG_PRESS_START) code = button_id == 4 ? 23 : base + 5;
  else if (event == LONG_PRESS_HOLD) code = button_id == 4 ? -1 : base + 6;
  if (code < 0 || s_key_actions == NULL) return false;
  if (xQueueSend(s_key_actions, &code, 0) != pdTRUE) {
    ESP_LOGW("KEY_QUEUE", "Queue full; action %d not queued", code);
    return false;
  }
  if (event != LONG_PRESS_HOLD) ESP_LOGI("KEY_QUEUE", "Queued action=%d", code);
  return true;
#endif
  switch (event)
  {
    case SINGLE_CLICK:
    {
      switch (button_id)
      {
        case Button_Up_id:
          xEventGroupSetBits(key_groups, set_bit_button(0));
          return true;
        case Button_Function_id:
          xEventGroupSetBits(key_groups, set_bit_button(7));
          return true;
        case Button_Down_id:
          xEventGroupSetBits(key_groups, set_bit_button(14));
          return true;
        case Boot_id:
          xEventGroupSetBits(key_groups, set_bit_button(21));
          return true;
      }
      return false;
    }
    case DOUBLE_CLICK:
    {
      switch (button_id)
      {
        case Button_Up_id:
          xEventGroupSetBits(key_groups, set_bit_button(1));
          return true;
        case Button_Function_id:
          xEventGroupSetBits(key_groups, set_bit_button(8));
          return true;
        case Button_Down_id:
          xEventGroupSetBits(key_groups, set_bit_button(15));
          return true;
        case Boot_id:
          xEventGroupSetBits(key_groups, set_bit_button(22));
          return true;
      }
      return false;
    }
    case PRESS_DOWN:
    {
      switch (button_id)
      {
        case Button_Up_id:
          xEventGroupSetBits(key_groups, set_bit_button(2));
          return true;
        case Button_Function_id:
          xEventGroupSetBits(key_groups, set_bit_button(9));
          return true;
        case Button_Down_id:
          xEventGroupSetBits(key_groups, set_bit_button(16));
          return true;
      }
      return false;
    }
    case PRESS_UP:
    {
      switch (button_id)
      {
        case Button_Up_id:
          xEventGroupSetBits(key_groups, set_bit_button(3));
          return true;
        case Button_Function_id:
          xEventGroupSetBits(key_groups, set_bit_button(10));
          return true;
        case Button_Down_id:
          xEventGroupSetBits(key_groups, set_bit_button(17));
          return true;
      }
      return false;
    }
    case PRESS_REPEAT:
    {
      switch (button_id)
      {
        case Button_Up_id:
          xEventGroupSetBits(key_groups, set_bit_button(4));
          return true;
        case Button_Function_id:
          xEventGroupSetBits(key_groups, set_bit_button(11));
          return true;
        case Button_Down_id:
          xEventGroupSetBits(key_groups, set_bit_button(18));
          return true;
      }
      return false;
    }
    case LONG_PRESS_START:
    {
      switch (button_id)
      {
        case Button_Up_id:
          xEventGroupSetBits(key_groups, set_bit_button(5));
          return true;
        case Button_Function_id:
          xEventGroupSetBits(key_groups, set_bit_button(12));
          return true;
        case Button_Down_id:
          xEventGroupSetBits(key_groups, set_bit_button(19));
          return true;
        case Boot_id:
          xEventGroupSetBits(key_groups, set_bit_button(23));
          return true;
      }
      return false;
    }
    case LONG_PRESS_HOLD:
    {
      switch (button_id)
      {
        case Button_Up_id:
          xEventGroupSetBits(key_groups, set_bit_button(6));
          return true;
        case Button_Function_id:
          xEventGroupSetBits(key_groups, set_bit_button(13));
          return true;
        case Button_Down_id:
          xEventGroupSetBits(key_groups, set_bit_button(20));
          return true;
        case Boot_id:
          xEventGroupSetBits(key_groups, set_bit_button(23));
          return true;
      }
      return false;
    }
    default:
      return false;
  }
}

/**
 * @brief  Wait for the key press event and return the corresponding event code
 * 
 * @param timeout The timeout period for waiting for the event
 *          If it is portMAX_DELAY, keep waiting
 *          If it is 0, it does not wait and returns directly
 *          If it is pdMS_TO_TICKS(3000), wait for 3 seconds
 * @return int 
 */
int wait_key_event_and_return_code(TickType_t timeout)
{
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    if (s_key_actions == NULL) return -1;
    const TickType_t started = xTaskGetTickCount();
    while (true) {
      int code = fetch_forced_key_code();
      if (code >= 0) return code;
      TickType_t wait = pdMS_TO_TICKS(20);
      if (timeout != portMAX_DELAY) {
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) wait = 0;
        else if (timeout - elapsed < wait) wait = timeout - elapsed;
      }
      if (xQueueReceive(s_touch_action, &code, 0) == pdTRUE ||
          xQueueReceive(s_key_actions, &code, wait) == pdTRUE) {
        handle_global_volume_event(set_bit_button(code));
        if (code == 5 || code == 6 || code == 19 || code == 20) continue;
        ESP_LOGI("KEY_QUEUE", "Consumed action=%d task=%s", code, pcTaskGetName(NULL));
        return code;
      }
      if (timeout != portMAX_DELAY && xTaskGetTickCount() - started >= timeout) return -1;
    }
#endif
    if (key_groups == NULL) {
      return -1;
    }

    if (timeout == 0) {
      int forced = fetch_forced_key_code();
      if (forced >= 0) {
        return forced;
      }
      EventBits_t even = xEventGroupWaitBits(key_groups, set_bit_all, pdTRUE, pdFALSE, 0);
      handle_global_volume_event(even);
      if(get_bit_button(even, 0))     return 0; // Button_Up Click
      if(get_bit_button(even, 1))     return 1; // Button_Up Double-click
      if(get_bit_button(even, 2))     return 2; // Button_Up Press
      if(get_bit_button(even, 3))     return 3; // Button_Up Bounce up
      if(get_bit_button(even, 4))     return 4; // Button_Up Press repeatedly
      if(get_bit_button(even, 7))     return 7; // Button_Function Click
      if(get_bit_button(even, 8))     return 8; // Button_Function Double-click
      if(get_bit_button(even, 9))     return 9; // Button_Function Press
      if(get_bit_button(even, 10))     return 10; // Button_Function Bounce up
      if(get_bit_button(even, 11))     return 11; // Button_Function Press repeatedly
      if(get_bit_button(even, 12))     return 12; // Button_Function Button_Up Long press to trigger once
      if(get_bit_button(even, 13))     return 13; // Button_Function Button_Up Long press to keep triggering
      if(get_bit_button(even, 14))     return 14; // Button_Down Click
      if(get_bit_button(even, 15))     return 15; // Button_Down Double-click
      if(get_bit_button(even, 16))     return 16; // Button_Down Press
      if(get_bit_button(even, 17))     return 17; // Button_Down Bounce up
      if(get_bit_button(even, 18))     return 18; // Button_Down Press repeatedly
      if(get_bit_button(even, 21))     return 21; // Boot Click
      if(get_bit_button(even, 22))     return 22; // Boot Double-click
      if(get_bit_button(even, 23))     return 23; // Button_Up Long press to keep triggering
      return -1;
    }

    const TickType_t kSliceTicks = pdMS_TO_TICKS(20);
    TickType_t elapsed = 0;
    while (1) {
      int forced = fetch_forced_key_code();
      if (forced >= 0) {
        return forced;
      }

      TickType_t wait_ticks = kSliceTicks;
      if (timeout != portMAX_DELAY) {
        if (elapsed >= timeout) {
          return -1;
        }
        TickType_t remain = timeout - elapsed;
        wait_ticks = (remain < kSliceTicks) ? remain : kSliceTicks;
      }

      EventBits_t even = xEventGroupWaitBits(key_groups, set_bit_all, pdTRUE, pdFALSE, wait_ticks);
      handle_global_volume_event(even);
      if(get_bit_button(even, 0))     return 0; // Button_Up Click
      if(get_bit_button(even, 1))     return 1; // Button_Up Double-click
      if(get_bit_button(even, 2))     return 2; // Button_Up Press
      if(get_bit_button(even, 3))     return 3; // Button_Up Bounce up
      if(get_bit_button(even, 4))     return 4; // Button_Up Press repeatedly

      if(get_bit_button(even, 7))     return 7; // Button_Function Click
      if(get_bit_button(even, 8))     return 8; // Button_Function Double-click
      if(get_bit_button(even, 9))     return 9; // Button_Function Press
      if(get_bit_button(even, 10))     return 10; // Button_Function Bounce up
      if(get_bit_button(even, 11))     return 11; // Button_Function Press repeatedly
      if(get_bit_button(even, 12))     return 12; // Button_Function Button_Up Long press to trigger once
      if(get_bit_button(even, 13))     return 13; // Button_Function Button_Up Long press to keep triggering

      if(get_bit_button(even, 14))     return 14; // Button_Down Click
      if(get_bit_button(even, 15))     return 15; // Button_Down Double-click
      if(get_bit_button(even, 16))     return 16; // Button_Down Press
      if(get_bit_button(even, 17))     return 17; // Button_Down Bounce up
      if(get_bit_button(even, 18))     return 18; // Button_Down Press repeatedly

      // Only 24 bits can be used
      if(get_bit_button(even, 21))     return 21; // Boot Click
      if(get_bit_button(even, 22))     return 22; // Boot Double-click
      if(get_bit_button(even, 23))     return 23; // Button_Up Long press to keep triggering

      if (timeout != portMAX_DELAY) {
        elapsed += wait_ticks;
      }
    }
}




/*
事件:
SINGLE_CLICK :单击
DOUBLE_CLICK :双击
PRESS_DOWN :按下
PRESS_UP :弹起事件
PRESS_REPEAT :重复按下
LONG_PRESS_START :长按触发一次
LONG_PRESS_HOLD :长按一直触发
*/
