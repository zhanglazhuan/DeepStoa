#include "application.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "es8311_bsp.h"
#include "i2c_bsp.h"
#include "config.h"
#include "wifi_board.h"

#include "power_save_timer.h"
#include <esp_log.h>
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

#include "freertos/event_groups.h"
#include "freertos/task.h"

extern void esp_home(int selection, int Refresh_mode);
extern "C" void button_mcp_force_key_code(int key_code);
extern "C" TaskHandle_t get_user_task_handle(void);
extern int home_selection;
extern bool wifi_enable;

#define TAG "esp-s3-PhotoPainter"

class waveshare_PhotoPainter : public WifiBoard {
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
    std::atomic<bool> ui_action_in_progress_{false};
    std::mutex ui_param_mutex_;
    int pending_timezone_index_ = 20;
    PendingAlarmConfig pending_alarm_config_;
    std::string pending_audio_filename_;
    uint32_t pending_record_seconds_ = 10;
    static constexpr UBaseType_t kUiTaskPriority = 2;

    static void UiTaskEntry(void *arg) {
        auto *self = static_cast<waveshare_PhotoPainter *>(arg);
        self->UiTaskLoop();
        vTaskDelete(NULL);
    }

    static bool SuspendUserTask(TaskHandle_t *out_task) {
        TaskHandle_t user_task = get_user_task_handle();
        if (out_task != nullptr) {
            *out_task = nullptr;
        }
        if (!user_task || user_task == xTaskGetCurrentTaskHandle()) {
            return false;
        }
        vTaskSuspend(user_task);
        if (out_task != nullptr) {
            *out_task = user_task;
        }
        return true;
    }

    static void ResumeUserTask(TaskHandle_t task) {
        if (task) {
            vTaskResume(task);
        }
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
            case 0:
                file_browser_task();
                break;
            case 1:
                page_clock_show();
                break;
            case 2:
                page_calendar_show();
                break;
            case 3:
                page_alarm_menu();
                break;
            case 4:
                page_weather_city_select();
                break;
            case 5:
                page_handle_network_key_event();
                break;
            case 6:
                page_audio_main();
                break;
            case 7:
                page_fiction_file();
                break;
            case 8:
                page_todolist_show();
                break;
            case 9:
                page_pomodoro_timer_show();
                break;
            case 10:
                page_mistakebook_show();
                break;
            default:
                break;
        }
        EventBits_t pending_nav_bits = ui_event_group_ ? (xEventGroupGetBits(ui_event_group_) & kUiEventNavigationMask) : 0;
        if (pending_nav_bits == 0) {
            if (restore_chat_page && suspended_task != nullptr) {
                home_selection = 1;
                page_chat_request_redraw();
                ResumeUserTask(suspended_task);
                suspended_task = nullptr;
            } else {
                ShowHomePage(false);
            }
        } else {
            ESP_LOGI(TAG, "Skip home redraw, pending navigation bits: 0x%08lx", (unsigned long)pending_nav_bits);
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
        {
            std::lock_guard<std::mutex> lock(ui_param_mutex_);
            filename = pending_audio_filename_;
        }
        if (filename.empty()) {
            return;
        }

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
        uint32_t duration_seconds = 0;
        {
            std::lock_guard<std::mutex> lock(ui_param_mutex_);
            duration_seconds = pending_record_seconds_;
        }
        if (duration_seconds == 0) {
            return;
        }

        ui_action_in_progress_.store(true, std::memory_order_release);
        TaskHandle_t suspended_task = nullptr;
        SuspendUserTask(&suspended_task);
        home_selection = 6;
        EPD_Init();
        page_audio_record_for_duration(duration_seconds);
        ShowHomePage(false);
        ResumeUserTask(suspended_task);
        ui_action_in_progress_.store(false, std::memory_order_release);
    }

    void UiTaskLoop() {
        ESP_LOGI(TAG, "MCP UI task loop started");
        while (ui_event_group_) {
            EventBits_t bits = xEventGroupWaitBits(ui_event_group_, kUiEventMask, pdTRUE, pdFALSE, portMAX_DELAY);
            ESP_LOGI(TAG, "MCP UI event bits received: 0x%08lx", (unsigned long)bits);

            if (bits & kUiEventOpenHome) {
                ESP_LOGI(TAG, "Handle UI event: open_home");
                TaskHandle_t suspended_task = nullptr;
                SuspendUserTask(&suspended_task);
                home_selection = 0;
                ShowHomePage(true);
                ResumeUserTask(suspended_task);
            }
            if (bits & kUiEventOpenFile) {
                ESP_LOGI(TAG, "Handle UI event: open_file_browser");
                OpenPageBySelection(0);
            }
            if (bits & kUiEventOpenClock) {
                ESP_LOGI(TAG, "Handle UI event: open_clock");
                OpenPageBySelection(1);
            }
            if (bits & kUiEventOpenCalendar) {
                ESP_LOGI(TAG, "Handle UI event: open_calendar");
                OpenPageBySelection(2);
            }
            if (bits & kUiEventOpenAlarm) {
                ESP_LOGI(TAG, "Handle UI event: open_alarm");
                OpenPageBySelection(3);
            }
            if (bits & kUiEventOpenWeather) {
                ESP_LOGI(TAG, "Handle UI event: open_weather");
                OpenPageBySelection(4);
            }
            if (bits & kUiEventOpenNetwork) {
                ESP_LOGI(TAG, "Handle UI event: open_network");
                OpenPageBySelection(5);
            }
            if (bits & kUiEventOpenAudio) {
                ESP_LOGI(TAG, "Handle UI event: open_audio");
                OpenPageBySelection(6);
            }
            if (bits & kUiEventOpenFiction) {
                ESP_LOGI(TAG, "Handle UI event: open_fiction");
                OpenPageBySelection(7);
            }
            if (bits & kUiEventOpenTodolist) {
                ESP_LOGI(TAG, "Handle UI event: open_todolist");
                OpenPageBySelection(8);
            }
            if (bits & kUiEventOpenPomodoro) {
                ESP_LOGI(TAG, "Handle UI event: open_pomodoro");
                OpenPageBySelection(9);
            }
            if (bits & kUiEventOpenMistakebook) {
                ESP_LOGI(TAG, "Handle UI event: open_mistakebook");
                OpenPageBySelection(10);
            }
            if (bits & kUiEventSetTimezone) {
                ESP_LOGI(TAG, "Handle UI event: set_timezone");
                int timezone_idx = 20;
                {
                    std::lock_guard<std::mutex> lock(ui_param_mutex_);
                    timezone_idx = pending_timezone_index_;
                }
                if (!page_clock_set_timezone_index(timezone_idx)) {
                    ESP_LOGW(TAG, "Failed to set timezone index: %d", timezone_idx);
                }
            }
            if (bits & kUiEventSetAlarm) {
                ESP_LOGI(TAG, "Handle UI event: set_alarm");
                PendingAlarmConfig cfg;
                {
                    std::lock_guard<std::mutex> lock(ui_param_mutex_);
                    cfg = pending_alarm_config_;
                }
                if (!page_alarm_set_item(cfg.index, cfg.hour, cfg.minute, cfg.enabled, cfg.save_now)) {
                    ESP_LOGW(TAG, "Failed to set alarm index=%d", cfg.index);
                }
            }
            if (bits & kUiEventEnableWifi) {
                ESP_LOGI(TAG, "Handle UI event: enable_wifi");
                SetWifiEnabled(true);
            }
            if (bits & kUiEventDisableWifi) {
                ESP_LOGI(TAG, "Handle UI event: disable_wifi");
                SetWifiEnabled(false);
            }
            if (bits & kUiEventPlayTfAudio) {
                ESP_LOGI(TAG, "Handle UI event: play_tf_audio");
                PlayTfAudio();
            }
            if (bits & kUiEventRecordAudio) {
                ESP_LOGI(TAG, "Handle UI event: record_audio");
                RecordAudio();
            }
        }
        ESP_LOGW(TAG, "MCP UI task loop exited");
    }

    void PostUiEvent(EventBits_t bits) {
        if (ui_action_in_progress_.load(std::memory_order_acquire)) {
            ESP_LOGI(TAG, "UI action in progress, request current page to exit for new MCP event");
            button_mcp_force_key_code(8); // Function double-click semantics: exit/cancel in page loops
        }
        if (ui_event_group_) {
            xEventGroupSetBits(ui_event_group_, bits);
        }
    }

    void InitializeUiTask() {
        ui_event_group_ = xEventGroupCreate();
        if (!ui_event_group_) {
            ESP_LOGE(TAG, "Failed to create MCP UI event group");
            return;
        }
        if (xTaskCreate(UiTaskEntry, "mcp_ui_task", 12 * 1024, this, kUiTaskPriority, &ui_task_handle_) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create MCP UI task");
        }
    }

    void InitializeButtons() {
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
    }

    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.ui.open_file_browser", "打开文件浏览页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_file_browser");
                PostUiEvent(kUiEventOpenFile);
                return std::string("已打开文件浏览页面");
            });

        mcp_server.AddTool("self.ui.open_clock", "打开时钟页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_clock");
                PostUiEvent(kUiEventOpenClock);
                return std::string("已打开时钟页面");
            });

        mcp_server.AddTool("self.ui.open_calendar", "打开日历页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_calendar");
                PostUiEvent(kUiEventOpenCalendar);
                return std::string("已打开日历页面");
            });

        mcp_server.AddTool("self.ui.open_alarm", "打开闹钟页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_alarm");
                PostUiEvent(kUiEventOpenAlarm);
                return std::string("已打开闹钟页面");
            });

        mcp_server.AddTool("self.ui.open_weather_city_select", "打开天气城市选择页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_weather_city_select");
                PostUiEvent(kUiEventOpenWeather);
                return std::string("已打开天气城市选择页面");
            });

        mcp_server.AddTool("self.ui.open_network", "打开网络页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_network");
                PostUiEvent(kUiEventOpenNetwork);
                return std::string("已打开网络页面");
            });

        mcp_server.AddTool("self.ui.open_audio", "打开音频页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_audio");
                PostUiEvent(kUiEventOpenAudio);
                return std::string("已打开音频页面");
            });

        mcp_server.AddTool("self.ui.open_fiction", "打开小说页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_fiction");
                PostUiEvent(kUiEventOpenFiction);
                return std::string("已打开小说页面");
            });

        mcp_server.AddTool("self.ui.open_todolist", "打开待办页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_todolist");
                PostUiEvent(kUiEventOpenTodolist);
                return std::string("已打开待办页面");
            });

        mcp_server.AddTool("self.ui.open_pomodoro", "打开番茄钟页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_pomodoro");
                PostUiEvent(kUiEventOpenPomodoro);
                return std::string("已打开番茄钟页面");
            });

        mcp_server.AddTool("self.ui.open_mistakebook", "打开错题本页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_mistakebook");
                PostUiEvent(kUiEventOpenMistakebook);
                return std::string("已打开错题本页面");
            });

        mcp_server.AddTool("self.mistakebook.add_card",
            "添加一条错题卡片到本地错题本缓存，参数包括 subject/title/question_text/student_answer/correct_answer/knowledge_point/error_type/explanation/review_pending",
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
            [this](const PropertyList& properties) -> ReturnValue {
                std::string subject = properties["subject"].value<std::string>();
                std::string title = properties["title"].value<std::string>();
                std::string question_text = properties["question_text"].value<std::string>();
                std::string student_answer = properties["student_answer"].value<std::string>();
                std::string correct_answer = properties["correct_answer"].value<std::string>();
                std::string knowledge_point = properties["knowledge_point"].value<std::string>();
                std::string error_type = properties["error_type"].value<std::string>();
                std::string explanation = properties["explanation"].value<std::string>();
                int review_pending = properties["review_pending"].value<int>();
                ESP_LOGI(TAG, "MCP tool called: self.mistakebook.add_card subject=%s title=%s",
                         subject.c_str(), title.c_str());
                bool ok = mistakebook_add_item_from_mcp(subject.c_str(),
                                                        title.c_str(),
                                                        question_text.c_str(),
                                                        student_answer.c_str(),
                                                        correct_answer.c_str(),
                                                        knowledge_point.c_str(),
                                                        error_type.c_str(),
                                                        explanation.c_str(),
                                                        review_pending);
                return ok ? std::string("已写入错题卡片") : std::string("写入错题卡片失败");
            });

        mcp_server.AddTool("self.clock.set_timezone",
            "设置时区，参数 timezone_index(0~24，对应 UTC-12 到 UTC+12)",
            PropertyList({Property("timezone_index", kPropertyTypeInteger, 20, 0, 24)}),
            [this](const PropertyList &properties) -> ReturnValue {
                int timezone_index = properties["timezone_index"].value<int>();
                ESP_LOGI(TAG, "MCP tool called: self.clock.set_timezone timezone_index=%d", timezone_index);
                {
                    std::lock_guard<std::mutex> lock(ui_param_mutex_);
                    pending_timezone_index_ = timezone_index;
                }
                PostUiEvent(kUiEventSetTimezone);
                return std::string("已提交时区设置");
            });

        mcp_server.AddTool("self.alarm.set",
            "设置指定闹钟，参数 index(0~5)、hour(0~23)、minute(0~59)、enabled(0/1)、save_now(0/1)",
            PropertyList({
                Property("index", kPropertyTypeInteger, 0, 0, 5),
                Property("hour", kPropertyTypeInteger, 8, 0, 23),
                Property("minute", kPropertyTypeInteger, 0, 0, 59),
                Property("enabled", kPropertyTypeInteger, 1, 0, 1),
                Property("save_now", kPropertyTypeInteger, 1, 0, 1),
            }),
            [this](const PropertyList &properties) -> ReturnValue {
                PendingAlarmConfig cfg;
                cfg.index = properties["index"].value<int>();
                cfg.hour = properties["hour"].value<int>();
                cfg.minute = properties["minute"].value<int>();
                cfg.enabled = properties["enabled"].value<int>() != 0;
                cfg.save_now = properties["save_now"].value<int>() != 0;
                ESP_LOGI(TAG, "MCP tool called: self.alarm.set index=%d hour=%d minute=%d enabled=%d save_now=%d",
                         cfg.index, cfg.hour, cfg.minute, cfg.enabled ? 1 : 0, cfg.save_now ? 1 : 0);
                {
                    std::lock_guard<std::mutex> lock(ui_param_mutex_);
                    pending_alarm_config_ = cfg;
                }
                PostUiEvent(kUiEventSetAlarm);
                return std::string("已提交闹钟设置");
            });

        mcp_server.AddTool("self.network.enable_wifi", "开启 WiFi", PropertyList(),
            [this](const PropertyList &properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.network.enable_wifi");
                PostUiEvent(kUiEventEnableWifi);
                return std::string("已提交开启WiFi");
            });

        mcp_server.AddTool("self.network.disable_wifi", "关闭 WiFi", PropertyList(),
            [this](const PropertyList &properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.network.disable_wifi");
                PostUiEvent(kUiEventDisableWifi);
                return std::string("已提交关闭WiFi");
            });

        mcp_server.AddTool("self.audio.play_tf_file",
            "播放 TF 卡音频，参数 filename（/sdcard/music 目录下文件名）",
            PropertyList({Property("filename", kPropertyTypeString)}),
            [this](const PropertyList &properties) -> ReturnValue {
                auto filename = properties["filename"].value<std::string>();
                if (filename.empty()) {
                    ESP_LOGW(TAG, "MCP tool called: self.audio.play_tf_file filename is empty");
                    return std::string("filename 不能为空");
                }
                ESP_LOGI(TAG, "MCP tool called: self.audio.play_tf_file filename=%s", filename.c_str());
                {
                    std::lock_guard<std::mutex> lock(ui_param_mutex_);
                    pending_audio_filename_ = filename;
                }
                PostUiEvent(kUiEventPlayTfAudio);
                return std::string("已提交播放TF卡音频");
            });

        mcp_server.AddTool("self.audio.record",
            "录制音频，参数 duration_seconds(1~3600)",
            PropertyList({Property("duration_seconds", kPropertyTypeInteger, 10, 1, 3600)}),
            [this](const PropertyList &properties) -> ReturnValue {
                int duration_seconds = properties["duration_seconds"].value<int>();
                ESP_LOGI(TAG, "MCP tool called: self.audio.record duration_seconds=%d", duration_seconds);
                {
                    std::lock_guard<std::mutex> lock(ui_param_mutex_);
                    pending_record_seconds_ = duration_seconds;
                }
                PostUiEvent(kUiEventRecordAudio);
                return std::string("已提交录音任务");
            });

        mcp_server.AddTool("self.fiction.append_note",
            "把当前阅读页内容和用户感想追加写入读书笔记。仅在阅读页面调用。参数 thoughts 为用户语音转写后的感想内容。"
            "工具会自动读取当前书名和当前页文本，并追加写入 /sdcard/fiction/<书名>-读书笔记.txt。",
            PropertyList({Property("thoughts", kPropertyTypeString)}),
            [](const PropertyList &properties) -> ReturnValue {
                auto thoughts = properties["thoughts"].value<std::string>();
                ESP_LOGI(TAG, "MCP tool called: self.fiction.append_note thoughts_len=%u thoughts=%s",
                         static_cast<unsigned>(thoughts.size()), thoughts.c_str());
                char result[192] = {0};
                if (!fiction_append_reading_note(thoughts.c_str(), result, sizeof(result))) {
                    throw std::runtime_error(result[0] != '\0' ? result : "追加读书笔记失败");
                }
                return std::string(result);
            });

        mcp_server.AddTool("self.todo.add",
            "添加待办事项。参数：content(事件内容,需要控制在9个字以内)、type(0-3)、remind_enabled(0=不提醒,1=提醒；默认为0)、remind_month(1-12, 可选)、remind_day(1-31, 可选)、remind_hour、remind_minute。"
            "如果用户说“明天/后天/大后天/星期几/周几/礼拜几/几分钟后/几小时后”，必须先基于当前本地时间换算成准确的绝对月、日、时、分后再传参，绝不能把相对时间直接当成参数。",
            PropertyList({
                Property("content", kPropertyTypeString),
                Property("type", kPropertyTypeInteger, 0, 0, 3),
                Property("remind_enabled", kPropertyTypeInteger, 0, 0, 1),
                Property("remind_month", kPropertyTypeInteger, 0, 0, 12),
                Property("remind_day", kPropertyTypeInteger, 0, 0, 31),
                Property("remind_hour", kPropertyTypeInteger, 0, 0, 23),
                Property("remind_minute", kPropertyTypeInteger, 0, 0, 59)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto content = properties["content"].value<std::string>();
                int type = properties["type"].value<int>();
                int remind_enabled = properties["remind_enabled"].value<int>();
                int remind_month = properties["remind_month"].value<int>();
                int remind_day = properties["remind_day"].value<int>();
                int remind_hour = properties["remind_hour"].value<int>();
                int remind_minute = properties["remind_minute"].value<int>();
                ESP_LOGI(TAG, "MCP tool called: self.todo.add content=%s type=%d remind_enabled=%d month=%d day=%d hour=%d minute=%d",
                         content.c_str(), type, remind_enabled, remind_month, remind_day, remind_hour, remind_minute);
                return todolist_add_item_from_mcp(content.c_str(),
                                                  type,
                                                  remind_enabled,
                                                  remind_month,
                                                  remind_day,
                                                  remind_hour,
                                                  remind_minute);
            });

        mcp_server.AddTool("self.ui.open_home", "返回主页面", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(TAG, "MCP tool called: self.ui.open_home");
                PostUiEvent(kUiEventOpenHome);
                return std::string("已返回主页面");
            });
    }

  public:
    waveshare_PhotoPainter()
        : boot_button_(BOOT_BUTTON_GPIO) {
        // InitializePowerSaveTimer();
        InitializeUiTask();
        InitializeButtons();
        InitializeTools();
    }

    virtual AudioCodec *GetAudioCodec() override {
        static Es8311AudioCodec codec(
            (void*)i2c_bus_handle,
            I2C_NUM,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            I2S_MCLK_PIN,
            I2S_BCK_PIN,
            I2S_WS_PIN,
            I2S_DATA_POUT,
            I2S_DATA_PIN,
            I2S_PA_PIN,
            ES8311_I2C_ADDR,
            true,
            false);
        return &codec;
    }

    //virtual void SetPowerSaveMode(bool enabled) override {
    //    if (!enabled) {
    //        power_save_timer_->WakeUp();
    //    }
    //    WifiBoard::SetPowerSaveMode(enabled);
    //}
};

DECLARE_BOARD(waveshare_PhotoPainter);

/*
afe_config->agc_init = false;
*/
