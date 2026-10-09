#include "application.h"
#include "button.h"
#include "button_bsp_mcp.h"
#include "ft6336u_touch.h"
#include "touch_gestures.h"
#include "audio/codecs/box_audio_codec.h"
#include "i2c_bsp.h"
#include "config.h"
#include "wifi_board.h"

#include "power_save_timer.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <functional>

#include "mcp_server.h"
#include "cJSON.h"
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>

#include "file_browser.h"
#include "page_clock.h"
#include "page_alarm.h"
#include "page_weather.h"
#include "page_network.h"
#include "page_audio.h"
#include "page_chat.h"
#include "page_fiction.h"
#include "page_todolist.h"
#include "page_mistakebook.h"
#include "page_pomodoro_timer.h"
#include "epaper_port.h"
#include "aw9523.h"

#include "freertos/event_groups.h"
#include "freertos/task.h"

#include <driver/i2c_master.h>
#include "led/gpio_led.h"

extern void esp_home(int selection, int Refresh_mode);
extern "C" void button_mcp_force_key_code(int key_code);
extern "C" TaskHandle_t get_user_task_handle(void);
extern int home_selection;
extern bool wifi_enable;

#define TAG "photopaint-v2"

class WavesharePhotoPaintV2 : public WifiBoard {
  private:
    enum UiEventBits : EventBits_t {
        kUiEventOpenHome      = BIT0,
        kUiEventOpenFile      = BIT1,
        kUiEventOpenClock     = BIT2,
        kUiEventOpenCalendar  = BIT3,
        kUiEventOpenAlarm     = BIT4,
        kUiEventOpenWeather   = BIT5,
        kUiEventOpenNetwork   = BIT6,
        kUiEventOpenAudio     = BIT7,
        kUiEventOpenFiction   = BIT8,
        kUiEventOpenTodolist  = BIT9,
        kUiEventOpenPomodoro  = BIT10,
        kUiEventOpenMistakebook = BIT11,
        kUiEventSetTimezone   = BIT12,
        kUiEventSetAlarm      = BIT13,
        kUiEventEnableWifi    = BIT14,
        kUiEventDisableWifi   = BIT15,
        kUiEventPlayTfAudio   = BIT16,
        kUiEventRecordAudio   = BIT17,
    };

    static constexpr EventBits_t kUiEventMask =
        kUiEventOpenHome | kUiEventOpenFile | kUiEventOpenClock | kUiEventOpenCalendar |
        kUiEventOpenAlarm | kUiEventOpenWeather | kUiEventOpenNetwork | kUiEventOpenAudio |
        kUiEventOpenFiction | kUiEventOpenTodolist | kUiEventOpenPomodoro | kUiEventOpenMistakebook | kUiEventSetTimezone |
        kUiEventSetAlarm | kUiEventEnableWifi | kUiEventDisableWifi | kUiEventPlayTfAudio |
        kUiEventRecordAudio;

    static constexpr EventBits_t kUiEventNavigationMask =
        kUiEventOpenHome | kUiEventOpenFile | kUiEventOpenClock | kUiEventOpenCalendar |
        kUiEventOpenAlarm | kUiEventOpenWeather | kUiEventOpenNetwork | kUiEventOpenAudio |
        kUiEventOpenFiction | kUiEventOpenTodolist | kUiEventOpenPomodoro | kUiEventOpenMistakebook;

    struct PendingAlarmConfig {
        int index = 0;
        int hour = 0;
        int minute = 0;
        bool enabled = true;
        bool save_now = true;
    };

    Button boot_button_;
    PowerSaveTimer *power_save_timer_;
    EventGroupHandle_t ui_event_group_ = nullptr;
    TaskHandle_t ui_task_handle_ = nullptr;
    TaskHandle_t key_sample_task_handle_ = nullptr;
    ft6336u_handle_t touch_ = {};
    TaskHandle_t touch_task_handle_ = nullptr;
    std::atomic<uint16_t> key_levels_{0};
    std::atomic<bool> ui_action_in_progress_{false};
    std::mutex ui_param_mutex_;
    int pending_timezone_index_ = 20;
    PendingAlarmConfig pending_alarm_config_;
    std::string pending_audio_filename_;
    uint32_t pending_record_seconds_ = 10;
    static constexpr UBaseType_t kUiTaskPriority = 2;

    // V2 hardware
    i2c_master_bus_handle_t ext_i2c_bus_ = nullptr;  // shared I2C: GPIO17/GPIO18
    GpioLed *code_light_ = nullptr;
    GpioLed *warm_light_ = nullptr;

    static WavesharePhotoPaintV2 *instance_;

    // --- Hardware init methods ---

    void InitializeExtI2c() {
        // V1.0 schematic: camera/touch and the board peripherals are all on
        // IIC_DATA/IIC_CLK (GPIO17/GPIO18). The bus is created during startup.
        ext_i2c_bus_ = i2c_bus_handle;
        if (ext_i2c_bus_ == nullptr) {
            ESP_LOGE(TAG, "Shared I2C bus has not been initialized");
            return;
        }

        // The shared bus already owns these pins. Reconfiguring them as GPIO
        // inputs here would disable the I2C outputs and their pull-ups.
        {
            ESP_LOGI(TAG, "Shared I2C GPIO levels: SDA(GP%d)=%d SCL(GP%d)=%d",
                     EXT_I2C_SDA_PIN, gpio_get_level((gpio_num_t)EXT_I2C_SDA_PIN),
                     EXT_I2C_SCL_PIN, gpio_get_level((gpio_num_t)EXT_I2C_SCL_PIN));
        }

        ESP_LOGI(TAG, "Using shared I2C bus (SDA=%d, SCL=%d)", EXT_I2C_SDA_PIN, EXT_I2C_SCL_PIN);
        // Full scan of EXT_I2C bus with verbose error for known addresses
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_LOGI(TAG, "=== Scanning schematic I2C bus (GPIO17/GPIO18) ===");
        int found_count = 0;
        for (uint8_t addr = 1; addr < 127; addr++) {
            esp_err_t r = i2c_master_probe(ext_i2c_bus_, addr, 100);
            if (r == ESP_OK) {
                ESP_LOGI(TAG, "  Device found at 0x%02X", addr);
                found_count++;
            } else if (addr == 0x18 || addr == 0x38 || addr == 0x40 ||
                       addr == 0x58 || addr == 0x6A || addr == 0x6B) {
                ESP_LOGW(TAG, "  No device at 0x%02X: %s (0x%X)", addr, esp_err_to_name(r), r);
            }
        }
        ESP_LOGI(TAG, "=== EXT_I2C scan complete: %d device(s) found ===", found_count);
    }

    void InitializeIoExpander() {
        esp_err_t ret = aw9523_init(ext_i2c_bus_, IO_EXPANDER_I2C_ADDR,
                                    IO_EXPANDER_RESET_GPIO);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "AW9523 init failed: %s", esp_err_to_name(ret));
            return;
        }

        ret = aw9523_set_direction(DRV_IO_EXP_OUTPUT_MASK, false);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "AW9523 set output direction failed: %s", esp_err_to_name(ret));
        }

        aw9523_write(IO_EXP_AUDIOCTR, false);
        aw9523_write(IO_EXP_EPD_CS | IO_EXP_EPD_DC |
                     IO_EXP_EPD_POWER, true);
        aw9523_write(IO_EXP_EPD_SCLK | IO_EXP_EPD_MOSI, false);

        ret = aw9523_set_direction(DRV_IO_EXP_INPUT_MASK, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "AW9523 set input direction failed: %s", esp_err_to_name(ret));
        }

        ESP_LOGI(TAG, "AW9523 initialized");

        // Scan again after enabling camera power
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "=== Re-scanning EXT_I2C after IO expander init ===");
        int post_found = 0;
        for (uint8_t addr = 1; addr < 127; addr++) {
            esp_err_t r = i2c_master_probe(ext_i2c_bus_, addr, 100);
            if (r == ESP_OK) {
                ESP_LOGI(TAG, "  Device found at 0x%02X", addr);
                post_found++;
            }
        }
        ESP_LOGI(TAG, "=== Post-init scan complete: %d device(s) found ===", post_found);
    }

    uint8_t IoExpanderGetLevel(uint16_t pin_mask) {
        uint16_t pin_val = 0;
        if (aw9523_read(pin_mask, &pin_val) != ESP_OK) return 0;
        return (uint8_t)(pin_val != 0);
    }

    void IoExpanderSetLevel(uint16_t pin_mask, uint8_t level) {
        aw9523_write(pin_mask, level != 0);
    }

    struct ExpanderButton {
        button_driver_t driver{};
        uint16_t pin;
        uint8_t id;
        const char *name;
        button_handle_t handle = nullptr;
    };

    void SampleKeys() {
        constexpr uint16_t mask = IO_EXP_KEY_UP | IO_EXP_KEY_DOWN |
                                  IO_EXP_KEY_FUNC | IO_EXP_POWER_KEY;
        uint16_t previous = 0xffff;
        unsigned errors = 0;
        int64_t last_heartbeat = 0;
        int64_t last_slow_warning = 0;
        while (true) {
            uint16_t levels = 0;
            const int64_t start = esp_timer_get_time();
            const esp_err_t ret = aw9523_read(mask, &levels);
            const int64_t now = esp_timer_get_time();
            if (ret == ESP_OK) {
                key_levels_.store(levels, std::memory_order_relaxed);
                if (levels != previous) {
                    ESP_LOGI(TAG, "KEY RAW mask=0x%04X UP=%d DOWN=%d CONFIRM=%d POWER=%d read_us=%lld",
                             levels, !!(levels & IO_EXP_KEY_UP), !!(levels & IO_EXP_KEY_DOWN),
                             !!(levels & IO_EXP_KEY_FUNC), !!(levels & IO_EXP_POWER_KEY),
                             static_cast<long long>(now - start));
                    previous = levels;
                }
                if (errors != 0) ESP_LOGI(TAG, "KEY I2C recovered after %u failures", errors);
                errors = 0;
            } else {
                // Do not leave a failed input stuck in the pressed state.
                key_levels_.store(0, std::memory_order_relaxed);
                if (++errors == 1 || errors % 100 == 0)
                    ESP_LOGW(TAG, "KEY I2C read failed: %s (%u failures)", esp_err_to_name(ret), errors);
            }
            if (now - start > 100000 && now - last_slow_warning > 5000000) {
                ESP_LOGW(TAG, "KEY sampling delayed %lld us", static_cast<long long>(now - start));
                last_slow_warning = now;
            }
            if (now - last_heartbeat >= 10000000) {
                ESP_LOGI(TAG, "KEY scanner alive mask=0x%04X errors=%u",
                         key_levels_.load(std::memory_order_relaxed), errors);
                last_heartbeat = now;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    static void OnExpanderButtonEvent(void *handle, void *arg) {
        auto *key = static_cast<ExpanderButton *>(arg);
        button_event_t event = iot_button_get_event(static_cast<button_handle_t>(handle));
        uint8_t code;
        switch (event) {
            case BUTTON_PRESS_DOWN: code = BUTTON_BSP_EVENT_PRESS_DOWN; break;
            case BUTTON_PRESS_UP: code = BUTTON_BSP_EVENT_PRESS_UP; break;
            case BUTTON_PRESS_REPEAT: code = BUTTON_BSP_EVENT_PRESS_REPEAT; break;
            case BUTTON_SINGLE_CLICK: code = BUTTON_BSP_EVENT_SINGLE_CLICK; break;
            case BUTTON_DOUBLE_CLICK: code = BUTTON_BSP_EVENT_DOUBLE_CLICK; break;
            case BUTTON_LONG_PRESS_START: code = BUTTON_BSP_EVENT_LONG_PRESS_START; break;
            case BUTTON_LONG_PRESS_HOLD: code = BUTTON_BSP_EVENT_LONG_PRESS_HOLD; break;
            default: return;
        }
        if (event != BUTTON_LONG_PRESS_HOLD) {
            ESP_LOGI(TAG, "KEY %s %s", key->name, iot_button_get_event_str(event));
        }
        button_mcp_set_event_code(key->id, code);
    }

    void RegisterExpanderButton(uint16_t pin, uint8_t id, const char *name) {
        // Context lives as long as the board; the button timer retains it.
        auto *key = new ExpanderButton;
        key->pin = pin;
        key->id = id;
        key->name = name;
        key->driver.enable_power_save = false;
        key->driver.get_key_level = [](button_driver_t *driver) -> uint8_t {
            auto *key = reinterpret_cast<ExpanderButton *>(driver);
            // The shared esp_timer task must never block on framebuffer I2C.
            return !!(instance_->key_levels_.load(std::memory_order_relaxed) & key->pin);
        };
        button_config_t cfg = {.long_press_time = 1000, .short_press_time = 300};
        ESP_ERROR_CHECK(iot_button_create(&cfg, &key->driver, &key->handle));
        const button_event_t events[] = {
            BUTTON_PRESS_DOWN, BUTTON_PRESS_UP, BUTTON_PRESS_REPEAT,
            BUTTON_SINGLE_CLICK, BUTTON_DOUBLE_CLICK,
            BUTTON_LONG_PRESS_START, BUTTON_LONG_PRESS_HOLD
        };
        for (auto event : events) {
            ESP_ERROR_CHECK(iot_button_register_cb(key->handle, event, nullptr,
                                                  OnExpanderButtonEvent, key));
        }
        ESP_LOGI(TAG, "KEY %s AW9523 mask=0x%04X active=HIGH", name, pin);
    }

    void InitializeButtons() {
        instance_ = this;
        button_event_init();
        ESP_ERROR_CHECK(aw9523_set_direction(IO_EXP_KEY_UP | IO_EXP_KEY_DOWN | IO_EXP_KEY_FUNC | IO_EXP_POWER_KEY, true));

        // Boot button on GPIO0 (native)
        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            app.Schedule([&app]() {
                auto state = app.GetDeviceState();
                const bool muted = app.IsUiMuted();
                if (!muted) {
                    app.SetUiMuted(true);
                    app.SetWakeWordEnabled(false);
                    app.InterruptConversationForLocalAudio();
                    return;
                }
                app.SetUiMuted(false);
                app.SetWakeWordEnabled(true);
                app.GetAudioService().EnableWakeWordDetection(true);
                app.ToggleChatState();
            });
        });

        // Active-high K3/K5/K4/K6 feed the menu event channel.
        RegisterExpanderButton(IO_EXP_KEY_UP, BUTTON_BSP_ID_UP, "UP");
        RegisterExpanderButton(IO_EXP_KEY_DOWN, BUTTON_BSP_ID_DOWN, "DOWN");
        RegisterExpanderButton(IO_EXP_KEY_FUNC, BUTTON_BSP_ID_FUNCTION, "CONFIRM");
        // K6 uses existing system-key events: click=21, double-click=shutdown, long-press=settings.
        RegisterExpanderButton(IO_EXP_POWER_KEY, BUTTON_BSP_ID_BOOT, "POWER");
        BaseType_t task_ret = xTaskCreate([](void *arg) {
            static_cast<WavesharePhotoPaintV2 *>(arg)->SampleKeys();
        }, "aw_key_sample", 3072, this, 4, &key_sample_task_handle_);
        ESP_ERROR_CHECK(task_ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
        boot_button_.OnDoubleClick([]() {
            button_mcp_set_event_code(BUTTON_BSP_ID_BOOT, BUTTON_BSP_EVENT_DOUBLE_CLICK);
        });
        boot_button_.OnLongPress([]() {
            button_mcp_set_event_code(BUTTON_BSP_ID_BOOT, BUTTON_BSP_EVENT_LONG_PRESS_START);
        });

        ESP_LOGI(TAG, "Buttons initialized according to V1.0 schematic");
    }

    void InitializeTouch() {
        // Poll the controller directly: INT is behind AW9523 and the module's
        // reset pin is not routed to an ESP32 GPIO on this board.
        if (!ft6336u_init(ext_i2c_bus_, TOUCH_I2C_ADDR, &touch_)) {
            ESP_LOGW(TAG, "Touch unavailable; hardware keys remain enabled");
            return;
        }
        if (xTaskCreate([](void *arg) {
                static_cast<WavesharePhotoPaintV2 *>(arg)->SampleTouch();
            }, "touch_sample", 4096, this, 3, &touch_task_handle_) != pdPASS) {
            ft6336u_deinit(&touch_);
            ESP_LOGE(TAG, "Failed to create touch task");
        }
    }

    void SampleTouch() {
        TouchGestures gestures;
        gestures.Cancel(); // A finger held during boot must first be released.
        unsigned errors = 0, previous_count = 0;
        int64_t last_report = 0;
        while (true) {
            const bool ok = ft6336u_read(&touch_);
            const int64_t ms = esp_timer_get_time() / 1000;
            if (!ok) {
                gestures.Cancel();
                if (++errors == 1 || errors % 20 == 0)
                    ESP_LOGW(TAG, "TOUCH read failed (%u); gesture cancelled", errors);
                vTaskDelay(pdMS_TO_TICKS(errors >= 5 ? 500 : 50));
                continue;
            }
            if (errors) ESP_LOGI(TAG, "TOUCH communication recovered");
            errors = 0;
            int x = touch_.points[0].x, y = touch_.points[0].y;
            if (TOUCH_SWAP_XY) { int temp = x; x = y; y = temp; }
            if (TOUCH_INVERT_X) x = -x;
            if (TOUCH_INVERT_Y) y = -y;
            if (touch_.num_points != previous_count ||
                (touch_.num_points && ms - last_report >= 250)) {
                ESP_LOGI(TAG, "TOUCH points=%u raw=(%u,%u) ui=(%d,%d)",
                    touch_.num_points, touch_.points[0].x, touch_.points[0].y, x, y);
                previous_count = touch_.num_points;
                last_report = ms;
            }
            const int code = gestures.Update(ms, touch_.num_points, x, y, touch_.points[0].id);
            if (code >= 0) {
                ESP_LOGI(TAG, "TOUCH gesture -> key=%d", code);
                if (!button_touch_post_key_code(code))
                    ESP_LOGW(TAG, "Touch action could not be queued");
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void InitializeLeds() {
        if (CODE_LIGHT_GPIO != GPIO_NUM_NC) {
            code_light_ = new GpioLed(CODE_LIGHT_GPIO);
        }
        if (WARM_LIGHT_GPIO != GPIO_NUM_NC) {
            warm_light_ = new GpioLed(WARM_LIGHT_GPIO);
        }
        ESP_LOGI(TAG, "LEDs initialized (CODE=%d, WARM=%d)", CODE_LIGHT_GPIO, WARM_LIGHT_GPIO);
    }

    // --- UI task framework (adapted from PhotoPainter V1) ---

    static void UiTaskEntry(void *arg) {
        auto *self = static_cast<WavesharePhotoPaintV2 *>(arg);
        self->UiTaskLoop();
        vTaskDelete(NULL);
    }

    static bool SuspendUserTask(TaskHandle_t *out_task) {
        TaskHandle_t user_task = get_user_task_handle();
        if (out_task != nullptr) *out_task = nullptr;
        if (!user_task || user_task == xTaskGetCurrentTaskHandle()) return false;
        vTaskSuspend(user_task);
        if (out_task != nullptr) *out_task = user_task;
        return true;
    }

    static void ResumeUserTask(TaskHandle_t task) {
        if (task) vTaskResume(task);
    }

    void ShowHomePage(bool full_refresh) {
        EPD_Init();
        esp_home(home_selection, full_refresh ? Global_refresh : Partial_refresh);
    }

    void OpenPageBySelection(int selection) {
        ui_action_in_progress_.store(true, std::memory_order_release);
        TaskHandle_t suspended_task = nullptr;
        const bool restore_chat_page = page_chat_is_active();
        SuspendUserTask(&suspended_task);

        home_selection = selection;
        EPD_Init();
        switch (selection) {
            case 0: file_browser_task(); break;
            case 1: page_clock_show(); break;
            case 2: page_calendar_show(); break;
            case 3: page_alarm_menu(); break;
            case 4: page_weather_city_select(); break;
            case 5: page_handle_network_key_event(); break;
            case 6: page_audio_main(); break;
            case 7: page_fiction_file(); break;
            case 8: page_todolist_show(); break;
            case 9: page_pomodoro_timer_show(); break;
            case 10: page_mistakebook_show(); break;
            default: break;
        }
        EventBits_t pending_nav = ui_event_group_ ? (xEventGroupGetBits(ui_event_group_) & kUiEventNavigationMask) : 0;
        if (pending_nav == 0) {
            if (restore_chat_page && suspended_task != nullptr) {
                home_selection = 1;
                page_chat_request_redraw();
                ResumeUserTask(suspended_task);
                suspended_task = nullptr;
            } else {
                ShowHomePage(false);
            }
        }
        ResumeUserTask(suspended_task);
        ui_action_in_progress_.store(false, std::memory_order_release);
    }

    void SetWifiEnabled(bool enable) {
        ui_action_in_progress_.store(true, std::memory_order_release);
        TaskHandle_t suspended_task = nullptr;
        SuspendUserTask(&suspended_task);
        wifi_set_enable(enable);
        wifi_enable = enable;
        ResumeUserTask(suspended_task);
        ui_action_in_progress_.store(false, std::memory_order_release);
    }

    void PlayTfAudio() {
        std::string filename;
        { std::lock_guard<std::mutex> lock(ui_param_mutex_); filename = pending_audio_filename_; }
        if (filename.empty()) return;
        ui_action_in_progress_.store(true, std::memory_order_release);
        TaskHandle_t suspended_task = nullptr;
        SuspendUserTask(&suspended_task);
        home_selection = 6;
        EPD_Init();
        page_audio_play_file(filename.c_str());
        ShowHomePage(false);
        ResumeUserTask(suspended_task);
        ui_action_in_progress_.store(false, std::memory_order_release);
    }

    void RecordAudio() {
        uint32_t duration = 0;
        { std::lock_guard<std::mutex> lock(ui_param_mutex_); duration = pending_record_seconds_; }
        if (duration == 0) return;
        ui_action_in_progress_.store(true, std::memory_order_release);
        TaskHandle_t suspended_task = nullptr;
        SuspendUserTask(&suspended_task);
        home_selection = 6;
        EPD_Init();
        page_audio_record_for_duration(duration);
        ShowHomePage(false);
        ResumeUserTask(suspended_task);
        ui_action_in_progress_.store(false, std::memory_order_release);
    }

    void UiTaskLoop() {
        ESP_LOGI(TAG, "MCP UI task loop started");
        while (ui_event_group_) {
            EventBits_t bits = xEventGroupWaitBits(ui_event_group_, kUiEventMask, pdTRUE, pdFALSE, portMAX_DELAY);
            if (bits & kUiEventOpenHome) ShowHomePage(true);
            if (bits & kUiEventOpenFile) OpenPageBySelection(0);
            if (bits & kUiEventOpenClock) OpenPageBySelection(1);
            if (bits & kUiEventOpenCalendar) OpenPageBySelection(2);
            if (bits & kUiEventOpenAlarm) OpenPageBySelection(3);
            if (bits & kUiEventOpenWeather) OpenPageBySelection(4);
            if (bits & kUiEventOpenNetwork) OpenPageBySelection(5);
            if (bits & kUiEventOpenAudio) OpenPageBySelection(6);
            if (bits & kUiEventOpenFiction) OpenPageBySelection(7);
            if (bits & kUiEventOpenTodolist) OpenPageBySelection(8);
            if (bits & kUiEventOpenPomodoro) OpenPageBySelection(9);
            if (bits & kUiEventOpenMistakebook) OpenPageBySelection(10);
            if (bits & kUiEventSetTimezone) {
                int idx;
                { std::lock_guard<std::mutex> lock(ui_param_mutex_); idx = pending_timezone_index_; }
                page_clock_set_timezone_index(idx);
            }
            if (bits & kUiEventSetAlarm) {
                PendingAlarmConfig cfg;
                { std::lock_guard<std::mutex> lock(ui_param_mutex_); cfg = pending_alarm_config_; }
                page_alarm_set_item(cfg.index, cfg.hour, cfg.minute, cfg.enabled, cfg.save_now);
            }
            if (bits & kUiEventEnableWifi) SetWifiEnabled(true);
            if (bits & kUiEventDisableWifi) SetWifiEnabled(false);
            if (bits & kUiEventPlayTfAudio) PlayTfAudio();
            if (bits & kUiEventRecordAudio) RecordAudio();
        }
    }

    void PostUiEvent(EventBits_t bits) {
        if (ui_action_in_progress_.load(std::memory_order_acquire)) {
            button_mcp_force_key_code(8);
        }
        if (ui_event_group_) xEventGroupSetBits(ui_event_group_, bits);
    }

    void InitializeUiTask() {
        ui_event_group_ = xEventGroupCreate();
        if (!ui_event_group_) {
            ESP_LOGE(TAG, "Failed to create UI event group");
            return;
        }
        if (xTaskCreate(UiTaskEntry, "mcp_ui_task", 12 * 1024, this, kUiTaskPriority, &ui_task_handle_) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create UI task");
        }
    }

    void InitializeTools() {
        auto &mcp = McpServer::GetInstance();

        mcp.AddTool("self.ui.open_file_browser", "打开文件浏览页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenFile); return std::string("已打开文件浏览页面"); });
        mcp.AddTool("self.ui.open_clock", "打开时钟页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenClock); return std::string("已打开时钟页面"); });
        mcp.AddTool("self.ui.open_calendar", "打开日历页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenCalendar); return std::string("已打开日历页面"); });
        mcp.AddTool("self.ui.open_alarm", "打开闹钟页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenAlarm); return std::string("已打开闹钟页面"); });
        mcp.AddTool("self.ui.open_weather_city_select", "打开天气城市选择页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenWeather); return std::string("已打开天气城市选择页面"); });
        mcp.AddTool("self.ui.open_network", "打开网络页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenNetwork); return std::string("已打开网络页面"); });
        mcp.AddTool("self.ui.open_audio", "打开音频页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenAudio); return std::string("已打开音频页面"); });
        mcp.AddTool("self.ui.open_fiction", "打开小说页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenFiction); return std::string("已打开小说页面"); });
        mcp.AddTool("self.ui.open_todolist", "打开待办页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenTodolist); return std::string("已打开待办页面"); });
        mcp.AddTool("self.ui.open_pomodoro", "打开番茄钟页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenPomodoro); return std::string("已打开番茄钟页面"); });
        mcp.AddTool("self.ui.open_mistakebook", "打开错题本页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenMistakebook); return std::string("已打开错题本页面"); });
        mcp.AddTool("self.ui.open_home", "返回主页面", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventOpenHome); return std::string("已返回主页面"); });

        mcp.AddTool("self.mistakebook.add_card",
            "添加一条错题卡片到本地错题本缓存",
            PropertyList({
                Property("subject", kPropertyTypeString),
                Property("title", kPropertyTypeString),
                Property("question_text", kPropertyTypeString),
                Property("student_answer", kPropertyTypeString),
                Property("correct_answer", kPropertyTypeString),
                Property("knowledge_point", kPropertyTypeString),
                Property("error_type", kPropertyTypeString),
                Property("explanation", kPropertyTypeString),
                Property("review_pending", kPropertyTypeInteger, 1, 0, 1)
            }),
            [](const PropertyList& p) -> ReturnValue {
                bool ok = mistakebook_add_item_from_mcp(
                    p["subject"].value<std::string>().c_str(),
                    p["title"].value<std::string>().c_str(),
                    p["question_text"].value<std::string>().c_str(),
                    p["student_answer"].value<std::string>().c_str(),
                    p["correct_answer"].value<std::string>().c_str(),
                    p["knowledge_point"].value<std::string>().c_str(),
                    p["error_type"].value<std::string>().c_str(),
                    p["explanation"].value<std::string>().c_str(),
                    p["review_pending"].value<int>());
                return ok ? std::string("已写入错题卡片") : std::string("写入错题卡片失败");
            });

        mcp.AddTool("self.clock.set_timezone", "设置时区",
            PropertyList({Property("timezone_index", kPropertyTypeInteger, 20, 0, 24)}),
            [this](const PropertyList& p) -> ReturnValue {
                int idx = p["timezone_index"].value<int>();
                { std::lock_guard<std::mutex> lock(ui_param_mutex_); pending_timezone_index_ = idx; }
                PostUiEvent(kUiEventSetTimezone);
                return std::string("已提交时区设置");
            });

        mcp.AddTool("self.alarm.set", "设置指定闹钟",
            PropertyList({
                Property("index", kPropertyTypeInteger, 0, 0, 5),
                Property("hour", kPropertyTypeInteger, 8, 0, 23),
                Property("minute", kPropertyTypeInteger, 0, 0, 59),
                Property("enabled", kPropertyTypeInteger, 1, 0, 1),
                Property("save_now", kPropertyTypeInteger, 1, 0, 1),
            }),
            [this](const PropertyList& p) -> ReturnValue {
                PendingAlarmConfig cfg;
                cfg.index = p["index"].value<int>();
                cfg.hour = p["hour"].value<int>();
                cfg.minute = p["minute"].value<int>();
                cfg.enabled = p["enabled"].value<int>() != 0;
                cfg.save_now = p["save_now"].value<int>() != 0;
                { std::lock_guard<std::mutex> lock(ui_param_mutex_); pending_alarm_config_ = cfg; }
                PostUiEvent(kUiEventSetAlarm);
                return std::string("已提交闹钟设置");
            });

        mcp.AddTool("self.network.enable_wifi", "开启 WiFi", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventEnableWifi); return std::string("已提交开启WiFi"); });
        mcp.AddTool("self.network.disable_wifi", "关闭 WiFi", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { PostUiEvent(kUiEventDisableWifi); return std::string("已提交关闭WiFi"); });

        mcp.AddTool("self.audio.play_tf_file", "播放 TF 卡音频",
            PropertyList({Property("filename", kPropertyTypeString)}),
            [this](const PropertyList& p) -> ReturnValue {
                auto filename = p["filename"].value<std::string>();
                if (filename.empty()) return std::string("filename 不能为空");
                { std::lock_guard<std::mutex> lock(ui_param_mutex_); pending_audio_filename_ = filename; }
                PostUiEvent(kUiEventPlayTfAudio);
                return std::string("已提交播放TF卡音频");
            });

        mcp.AddTool("self.audio.record", "录制音频",
            PropertyList({Property("duration_seconds", kPropertyTypeInteger, 10, 1, 3600)}),
            [this](const PropertyList& p) -> ReturnValue {
                int duration = p["duration_seconds"].value<int>();
                { std::lock_guard<std::mutex> lock(ui_param_mutex_); pending_record_seconds_ = (uint32_t)duration; }
                PostUiEvent(kUiEventRecordAudio);
                return std::string("已提交录音任务");
            });

        mcp.AddTool("self.fiction.append_note", "追加读书笔记",
            PropertyList({Property("thoughts", kPropertyTypeString)}),
            [](const PropertyList& p) -> ReturnValue {
                auto thoughts = p["thoughts"].value<std::string>();
                char result[192] = {0};
                if (!fiction_append_reading_note(thoughts.c_str(), result, sizeof(result)))
                    throw std::runtime_error(result[0] ? result : "追加读书笔记失败");
                return std::string(result);
            });

        mcp.AddTool("self.todo.add", "添加待办事项",
            PropertyList({
                Property("content", kPropertyTypeString),
                Property("type", kPropertyTypeInteger, 0, 0, 3),
                Property("remind_enabled", kPropertyTypeInteger, 0, 0, 1),
                Property("remind_month", kPropertyTypeInteger, 0, 0, 12),
                Property("remind_day", kPropertyTypeInteger, 0, 0, 31),
                Property("remind_hour", kPropertyTypeInteger, 0, 0, 23),
                Property("remind_minute", kPropertyTypeInteger, 0, 0, 59)
            }),
            [](const PropertyList& p) -> ReturnValue {
                return todolist_add_item_from_mcp(
                    p["content"].value<std::string>().c_str(),
                    p["type"].value<int>(),
                    p["remind_enabled"].value<int>(),
                    p["remind_month"].value<int>(),
                    p["remind_day"].value<int>(),
                    p["remind_hour"].value<int>(),
                    p["remind_minute"].value<int>());
            });
    }

  public:
    WavesharePhotoPaintV2()
        : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing Photopaint V2 hardware...");
        InitializeExtI2c();
        InitializeIoExpander();
        InitializeLeds();
        InitializeButtons();
        InitializeTouch();
        InitializeUiTask();
        InitializeTools();
        ESP_LOGI(TAG, "Photopaint V2 initialization complete");
    }

    virtual AudioCodec *GetAudioCodec() override {
        static BoxAudioCodec codec(
            (void*)i2c_bus_handle,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC,    // HT6872 AMP_EN is controlled by AW9523 P0.0
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &codec;
    }

    virtual Led* GetLed() override {
        return code_light_ ? code_light_ : Board::GetLed();
    }
};

DECLARE_BOARD(WavesharePhotoPaintV2);
WavesharePhotoPaintV2 *WavesharePhotoPaintV2::instance_ = nullptr;
