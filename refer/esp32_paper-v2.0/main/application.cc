#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "page_fiction.h"
#include "assets.h"
#include "settings.h"
#include "page_network.h"
#include "chat_history.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"

extern "C" bool page_audio_is_playing(void);
extern "C" bool page_audio_is_recording(void);
extern "C" void page_audio_request_stop_playback(void);

static AudioCodec* GetSystemAudioCodec() {
    return Board::GetInstance().GetAudioCodec();
}

extern "C" int global_adjust_output_volume(int delta) {
    auto codec = GetSystemAudioCodec();
    if (codec == nullptr) {
        return -1;
    }

    int current_volume = codec->output_volume();
    int volume = current_volume + delta;
    if (volume > 100) {
        volume = 100;
    } else if (volume < 0) {
        volume = 0;
    }

    if (volume == current_volume) {
        return volume;
    }

    codec->SetOutputVolume(volume);

    auto display = Board::GetInstance().GetDisplay();
    if (display != nullptr) {
        if (volume >= 100) {
            display->ShowNotification(Lang::Strings::MAX_VOLUME);
        } else if (volume <= 0) {
            display->ShowNotification(Lang::Strings::MUTED);
        } else {
            display->ShowNotification(
                std::string(Lang::Strings::VOLUME) + std::to_string(volume)
            );
        }
    }

    return volume;
}

static std::string GetFilenameFromPath(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

static std::string ExtractReadingNoteThoughts(const std::string& text) {
    if (text.empty() || text.front() != '{') {
        return text;
    }

    cJSON* root = cJSON_Parse(text.c_str());
    if (!root) {
        return text;
    }

    std::string extracted = text;
    cJSON* content = cJSON_GetObjectItem(root, "content");
    if (cJSON_IsString(content) && content->valuestring) {
        extracted = content->valuestring;
    }
    cJSON_Delete(root);
    return extracted;
}

static std::string ReplaceUrlScheme(std::string url) {
    if (url.rfind("ws://", 0) == 0) {
        url.replace(0, 5, "http://");
    } else if (url.rfind("wss://", 0) == 0) {
        url.replace(0, 6, "https://");
    }
    return url;
}

static size_t GetAuthorityStart(const std::string& url) {
    size_t scheme_end = url.find("://");
    return (scheme_end == std::string::npos) ? 0 : (scheme_end + 3);
}

static std::string ReplaceAuthorityPort(const std::string& url, int port) {
    if (url.empty() || port <= 0) {
        return url;
    }

    size_t authority_start = GetAuthorityStart(url);
    size_t path_pos = url.find('/', authority_start);
    std::string authority = path_pos == std::string::npos ? url.substr(authority_start)
                                                          : url.substr(authority_start, path_pos - authority_start);
    if (authority.empty()) {
        return url;
    }

    std::string host_part = authority;
    if (authority.front() == '[') {
        size_t bracket_end = authority.find(']');
        if (bracket_end != std::string::npos) {
            host_part = authority.substr(0, bracket_end + 1);
        }
    } else {
        size_t colon_pos = authority.rfind(':');
        if (colon_pos != std::string::npos) {
            host_part = authority.substr(0, colon_pos);
        }
    }

    std::string new_url = url.substr(0, authority_start) + host_part + ":" + std::to_string(port);
    if (path_pos != std::string::npos) {
        new_url += url.substr(path_pos);
    }
    return new_url;
}

static int ExtractAuthorityPort(const std::string& url) {
    if (url.empty()) {
        return 0;
    }

    size_t authority_start = GetAuthorityStart(url);
    size_t path_pos = url.find('/', authority_start);
    std::string authority = path_pos == std::string::npos ? url.substr(authority_start)
                                                          : url.substr(authority_start, path_pos - authority_start);
    if (authority.empty()) {
        return 0;
    }

    size_t colon_pos = std::string::npos;
    if (!authority.empty() && authority.front() == '[') {
        size_t bracket_end = authority.find(']');
        if (bracket_end != std::string::npos && bracket_end + 1 < authority.size() && authority[bracket_end + 1] == ':') {
            colon_pos = bracket_end + 1;
        }
    } else {
        colon_pos = authority.rfind(':');
    }

    if (colon_pos == std::string::npos || colon_pos + 1 >= authority.size()) {
        return 0;
    }

    char* end_ptr = nullptr;
    long parsed = strtol(authority.c_str() + colon_pos + 1, &end_ptr, 10);
    if (end_ptr == nullptr || *end_ptr != '\0' || parsed <= 0 || parsed > 65535) {
        return 0;
    }
    return static_cast<int>(parsed);
}

static std::vector<std::string> GetDefaultUploadUrls() {
    std::vector<std::string> upload_urls;
    Settings settings("websocket", false);
    auto upload_url = settings.GetString("upload_url");
    if (!upload_url.empty()) {
        upload_urls.push_back(upload_url);
        return upload_urls;
    }

    auto websocket_url = ReplaceUrlScheme(settings.GetString("url"));
    if (websocket_url.empty()) {
        return upload_urls;
    }

    auto path_pos = websocket_url.find('/', GetAuthorityStart(websocket_url));
    if (path_pos != std::string::npos) {
        websocket_url = websocket_url.substr(0, path_pos);
    }

    int configured_http_port = settings.GetInt("http_port", 0);
    if (configured_http_port > 0) {
        websocket_url = ReplaceAuthorityPort(websocket_url, configured_http_port);
        upload_urls.push_back(websocket_url + "/xiaozhi/upload");
    } else {
        int websocket_port = ExtractAuthorityPort(websocket_url);
        if (websocket_port == 8000) {
            ESP_LOGI(TAG, "Infer upload port 8003 from websocket port 8000");
            upload_urls.push_back(ReplaceAuthorityPort(websocket_url, 8003) + "/xiaozhi/upload");
            upload_urls.push_back(websocket_url + "/xiaozhi/upload");
            return upload_urls;
        }
        upload_urls.push_back(websocket_url + "/xiaozhi/upload");
    }

    return upload_urls;
}

static std::vector<std::string> ResolveUploadUrls(const std::string& upload_url) {
    if (!upload_url.empty()) {
        return {upload_url};
    }
    return GetDefaultUploadUrls();
}

static std::string GetDefaultUploadToken() {
    Settings settings("websocket", false);
    return settings.GetString("token");
}

static std::string NormalizeAuthHeader(const std::string& token) {
    if (token.empty()) {
        return "";
    }
    if (token.find(' ') == std::string::npos) {
        return "Bearer " + token;
    }
    return token;
}

static std::string GetAttachmentHistoryMessage(const std::string& file_path) {
    auto filename = GetFilenameFromPath(file_path);
    if (filename.empty()) {
        return "我上传了一个附件";
    }
    return std::string("[已上传附件] ") + filename;
}

static std::string UploadAttachmentToXiaozhi(
    const std::string& upload_url,
    const std::string& upload_token,
    const std::string& file_path,
    const std::string& attachment_type,
    const std::string& session_id
) {
    if (upload_url.empty()) {
        throw std::runtime_error("Upload URL is empty");
    }

    FILE* file = fopen(file_path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("Failed to open attachment file");
    }

    auto close_file = [&file]() {
        if (file != nullptr) {
            fclose(file);
            file = nullptr;
        }
    };

    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    std::string boundary = "----ESP32_XIAOZHI_UPLOAD_BOUNDARY";

    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    auto auth_header = NormalizeAuthHeader(upload_token);
    if (!auth_header.empty()) {
        http->SetHeader("Authorization", auth_header);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");

    if (!http->Open("POST", upload_url)) {
        int last_error = http->GetLastError();
        close_file();
        char error_message[96];
        snprintf(error_message, sizeof(error_message), "Failed to open upload URL, code=0x%x", last_error);
        throw std::runtime_error(error_message);
    }

    std::string type_field;
    type_field += "--" + boundary + "\r\n";
    type_field += "Content-Disposition: form-data; name=\"type\"\r\n";
    type_field += "\r\n";
    type_field += attachment_type + "\r\n";
    http->Write(type_field.c_str(), type_field.size());

    if (!session_id.empty()) {
        std::string session_field;
        session_field += "--" + boundary + "\r\n";
        session_field += "Content-Disposition: form-data; name=\"session_id\"\r\n";
        session_field += "\r\n";
        session_field += session_id + "\r\n";
        http->Write(session_field.c_str(), session_field.size());
    }

    auto filename = GetFilenameFromPath(file_path);
    std::string file_header;
    file_header += "--" + boundary + "\r\n";
    file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"";
    file_header += filename + "\"\r\n";
    file_header += "Content-Type: application/octet-stream\r\n";
    file_header += "\r\n";
    http->Write(file_header.c_str(), file_header.size());

    char buffer[4096];
    while (true) {
        size_t read_size = fread(buffer, 1, sizeof(buffer), file);
        if (read_size > 0) {
            http->Write(buffer, read_size);
        }
        if (read_size < sizeof(buffer)) {
            if (feof(file)) {
                break;
            }
            close_file();
            throw std::runtime_error("Failed to read attachment file");
        }
    }
    close_file();

    std::string multipart_footer = "\r\n--" + boundary + "--\r\n";
    http->Write(multipart_footer.c_str(), multipart_footer.size());
    http->Write("", 0);

    int status_code = http->GetStatusCode();
    std::string result = http->ReadAll();
    http->Close();

    if (status_code != 200) {
        throw std::runtime_error(
            "Attachment upload failed, code: " + std::to_string(status_code)
        );
    }

    cJSON* root = cJSON_Parse(result.c_str());
    if (root == nullptr) {
        throw std::runtime_error("Invalid upload response");
    }

    auto cleanup = [&root]() {
        if (root != nullptr) {
            cJSON_Delete(root);
            root = nullptr;
        }
    };

    auto success = cJSON_GetObjectItem(root, "success");
    if (!cJSON_IsBool(success) || !cJSON_IsTrue(success)) {
        auto message = cJSON_GetObjectItem(root, "message");
        std::string error_message = cJSON_IsString(message) ? message->valuestring : "upload failed";
        cleanup();
        throw std::runtime_error(error_message);
    }

    auto attachment = cJSON_GetObjectItem(root, "attachment");
    if (!cJSON_IsObject(attachment)) {
        cleanup();
        throw std::runtime_error("Upload response missing attachment");
    }

    auto attachment_str = cJSON_PrintUnformatted(attachment);
    std::string attachment_json = attachment_str != nullptr ? attachment_str : "";
    if (attachment_str != nullptr) {
        cJSON_free(attachment_str);
    }
    cleanup();
    return attachment_json;
}

static std::string UploadAttachmentToXiaozhiWithFallback(
    const std::vector<std::string>& upload_urls,
    const std::string& upload_token,
    const std::string& file_path,
    const std::string& attachment_type,
    const std::string& session_id
) {
    if (upload_urls.empty()) {
        throw std::runtime_error("Upload URL is empty");
    }

    std::runtime_error last_error("Upload URL is empty");
    for (size_t i = 0; i < upload_urls.size(); ++i) {
        const std::string& upload_url = upload_urls[i];
        try {
            if (i > 0) {
                ESP_LOGW(TAG, "Retry upload with fallback URL: %s", upload_url.c_str());
            }
            return UploadAttachmentToXiaozhi(
                upload_url,
                upload_token,
                file_path,
                attachment_type,
                session_id
            );
        } catch (const std::runtime_error& e) {
            last_error = std::runtime_error(e.what());
            if (i + 1 >= upload_urls.size() || strstr(e.what(), "Failed to open upload URL") == nullptr) {
                throw;
            }
            ESP_LOGW(TAG, "Upload to %s failed before request start: %s", upload_url.c_str(), e.what());
        }
    }

    throw last_error;
}

void Application::ApplyUploadedAttachmentToDialogueContext(
    const std::string& file_path,
    const std::string& attachment_json
) {
    if (protocol_ == nullptr || attachment_json.empty()) {
        return;
    }

    std::string pending_attachments_json;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_dialogue_attachment_json_ = attachment_json;
        pending_attachments_json = BuildAttachmentArrayJson(*active_dialogue_attachment_json_);
    }

    if (protocol_->IsAudioChannelOpened()) {
        protocol_->SendAttachmentContext(pending_attachments_json);
    }

    auto history_message = GetAttachmentHistoryMessage(file_path);
    ChatHistory::AddMessage("user", history_message.c_str());
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("user", history_message.c_str());
}


Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = GetSystemAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Register network callbacks from page_network
    page_network_set_callbacks(
        [this]() { xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED); },
        [this]() { xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED); }
    );

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Start network asynchronously
    // board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::OnWakeWordDetected() {
    if (page_audio_is_recording()) {
        ESP_LOGW(TAG, "Ignore wake word: local recording is active");
        return;
    }
    if (!wake_word_enabled_) {
        ESP_LOGW(TAG, "Ignore wake word: wake word disabled");
        return;
    }
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        InterruptLocalAudioForConversation();
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);
    main_task_handle_ = xTaskGetCurrentTaskHandle();

    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    // Play the success sound to indicate the device is ready
    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveMode(true);
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version
    CheckNewVersion();

    // Initialize the protocol
    InitializeProtocol();

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = GetSystemAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
        Schedule([this]() {
            std::optional<std::string> pending_tts_text;
            std::optional<std::string> pending_tts_metadata_json;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_tts_text = pending_tts_text_;
                pending_tts_metadata_json = pending_tts_metadata_json_;
                pending_tts_text_.reset();
                pending_tts_metadata_json_.reset();
            }
            if (pending_tts_text.has_value() && !pending_tts_text->empty() && protocol_ && protocol_->IsAudioChannelOpened()) {
                if (GetDeviceState() == kDeviceStateConnecting) {
                    SetDeviceState(kDeviceStateIdle);
                }
                ESP_LOGI(TAG, "Resend pending reading TTS after audio channel opened");
                SendTtsText(*pending_tts_text, pending_tts_metadata_json.value_or(""));
            }
        });
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            ResetTransientListeningContext();
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
#if CONFIG_TRANSCRIPTION_ONLY_MODE
            ESP_LOGI(TAG, "Ignore TTS message in transcription-only mode");
            return;
#endif
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                bool reading_ai_mode = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    reading_ai_mode = reading_ai_mode_;
                }
                if (reading_ai_mode) {
                    reading_ai_playback_started_.store(true);
                    reading_ai_playback_finished_.store(false);
                }
                Schedule([this]() {
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                bool reading_ai_mode = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    reading_ai_mode = reading_ai_mode_;
                }
                if (reading_ai_mode) {
                    std::thread([this]() {
                        for (int i = 0; i < 200; ++i) {
                            if (audio_service_.IsIdle() && reading_ai_playback_started_.load()) {
                                reading_ai_playback_finished_.store(true);
                                reading_ai_playback_started_.store(false);
                                break;
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        }
                    }).detach();
                }
                Schedule([this]() {
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        bool reading_ai_mode = false;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            reading_ai_mode = reading_ai_mode_;
                        }
                        bool reading_ai_prefetch_pending = reading_ai_prefetch_pending_.load();
                        if (reading_ai_mode && reading_ai_prefetch_pending) {
                            return;
                        }
                        if (reading_ai_mode || listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    ChatHistory::AddMessage("assistant", text->valuestring);
                    Schedule([display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                bool reading_ai_mode = false;
                bool reading_note_mode = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    reading_ai_mode = reading_ai_mode_;
                    reading_note_mode = reading_note_mode_;
                }
                if (reading_ai_mode && !reading_note_mode) {
                    ESP_LOGI(TAG, "Ignore STT while reading AI mode is active: %s", text->valuestring);
                    return;
                }
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                if (!reading_note_mode) {
                    ChatHistory::AddMessage("user", text->valuestring);
                    Schedule([display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("user", message.c_str());
                    });
                }

                reading_note_mode = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    reading_note_mode = reading_note_mode_;
                    if (reading_note_mode) {
                        reading_note_mode_ = false;
                    }
                }

                if (reading_note_mode) {
                    Schedule([this, text_message = std::string(text->valuestring)]() {
                        std::string note_text = ExtractReadingNoteThoughts(text_message);
                        char result[192] = {0};
                        bool ok = fiction_append_reading_note(note_text.c_str(), result, sizeof(result));
                        auto display = Board::GetInstance().GetDisplay();
                        if (ok) {
                            ESP_LOGI(TAG, "Reading note mode saved from first STT: %s", result);
                            display->ShowNotification(result[0] != '\0' ? result : "已写入读书笔记");
                        } else {
                            ESP_LOGE(TAG, "Reading note mode save failed: %s", result[0] != '\0' ? result : "未知错误");
                            Alert("读书笔记", result[0] != '\0' ? result : "写入读书笔记失败", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
                        }

                        ResetTransientListeningContext();
                        if (protocol_ && protocol_->IsAudioChannelOpened()) {
                            protocol_->CloseAudioChannel();
                        } else {
                            SetDeviceState(kDeviceStateIdle);
                        }
                    });
                }
            }
        } else if (strcmp(type->valuestring, "reading") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state) && strcmp(state->valuestring, "request_next_chunk") == 0) {
                reading_ai_next_chunk_requests_.fetch_add(1);
                ESP_LOGI(TAG, "Reading server requested next chunk");
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    
    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::SetWakeWordEnabled(bool enabled) {
    wake_word_enabled_ = enabled;
}

void Application::SetUiMuted(bool muted) {
    bool previous = ui_muted_.exchange(muted);
    if (previous != muted) {
        auto display = Board::GetInstance().GetDisplay();
        display->UpdateStatusBar(true);
    }
}

void Application::HandleToggleChatEvent() {
    if (page_audio_is_recording()) {
        ESP_LOGW(TAG, "Ignore toggle chat: local recording is active");
        return;
    }
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        SetUiMuted(false);
        InterruptLocalAudioForConversation();
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(false);
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }
#if CONFIG_TRANSCRIPTION_ONLY_MODE
        SetListeningMode(kListeningModeManualStop);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
#if CONFIG_TRANSCRIPTION_ONLY_MODE
        protocol_->SendStopListening();
        ResetTransientListeningContext();
        SetDeviceState(kDeviceStateIdle);
#else
        protocol_->CloseAudioChannel();
#endif
    }
}

void Application::HandleStartListeningEvent() {
    if (page_audio_is_recording()) {
        ESP_LOGW(TAG, "Ignore start listening: local recording is active");
        return;
    }
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        SetUiMuted(false);
        InterruptLocalAudioForConversation();
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(false);
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }

        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        bool reading_note_mode = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            reading_note_mode = reading_note_mode_;
        }
        if (protocol_) {
            protocol_->SendStopListening();
        }
        if (!reading_note_mode) {
            ResetTransientListeningContext();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
#if CONFIG_TRANSCRIPTION_ONLY_MODE
    ESP_LOGI(TAG, "Ignore wake word event in transcription-only mode");
    return;
#endif
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (reading_note_mode_) {
            ESP_LOGI(TAG, "Ignore wake word event while reading note mode is active");
            return;
        }
    }
    if (page_audio_is_recording()) {
        ESP_LOGW(TAG, "Ignore wake word event: local recording is active");
        return;
    }
    if (!wake_word_enabled_) {
        ESP_LOGW(TAG, "Ignore wake word event: wake word disabled");
        return;
    }
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        SetUiMuted(false);
        InterruptLocalAudioForConversation();
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        // Set flag to play popup sound after state changes to listening
        // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
        play_popup_on_listening_ = true;
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;
    const bool recording_active = page_audio_is_recording();
    bool reading_ai_mode = false;
    bool reading_note_mode = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reading_ai_mode = reading_ai_mode_;
        reading_note_mode = reading_note_mode_;
    }

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
#if CONFIG_TRANSCRIPTION_ONLY_MODE
            audio_service_.EnableWakeWordDetection(false);
#else
            audio_service_.EnableWakeWordDetection(
                wake_word_enabled_ &&
                !recording_active &&
                !reading_ai_mode &&
                !reading_note_mode
            );
#endif
            break;
        case kDeviceStateConnecting:
            ui_muted_ = false;
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            ui_muted_ = false;
            if (recording_active) {
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(false);
                if (protocol_ && protocol_->IsAudioChannelOpened()) {
                    protocol_->CloseAudioChannel();
                }
                break;
            }
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (!audio_service_.IsAudioProcessorRunning()) {
                // Send the start listening command
                protocol_->SendStartListening(
                    listening_mode_,
                    reading_note_mode,
                    reading_note_mode
                );
                std::optional<std::string> pending_dialogue_text;
                std::string pending_attachments_json;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending_dialogue_text = pending_dialogue_text_;
                    pending_dialogue_text_.reset();
                    pending_attachments_json = active_dialogue_attachment_json_.has_value()
                        ? BuildAttachmentArrayJson(*active_dialogue_attachment_json_)
                        : "";
                }
                if (pending_dialogue_text.has_value() && !pending_dialogue_text->empty()) {
                    if (!pending_attachments_json.empty()) {
                        protocol_->SendDialogueTextWithAttachments(*pending_dialogue_text, pending_attachments_json);
                    } else {
                        protocol_->SendDialogueText(*pending_dialogue_text);
                    }
                } else if (!pending_attachments_json.empty()) {
                    protocol_->SendAttachmentContext(pending_attachments_json);
                }
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }

            // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateSpeaking:
            ui_muted_ = false;
            if (recording_active) {
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(false);
                if (protocol_ && protocol_->IsAudioChannelOpened()) {
                    protocol_->CloseAudioChannel();
                }
                break;
            }
            // display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Reading playback should not keep AFE wake word running; it adds load and can overflow AFE feed buffers.
                if (reading_ai_mode) {
                    audio_service_.EnableWakeWordDetection(false);
                } else {
                    // Only AFE wake word can be detected in speaking mode
                    audio_service_.EnableWakeWordDetection(wake_word_enabled_ && audio_service_.IsAfeWakeWord());
                }
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }

    display->UpdateStatusBar(true);
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::InterruptLocalAudioForConversation() {
    if (!page_audio_is_playing()) {
        return;
    }
    ESP_LOGI(TAG, "Interrupt local playback for conversation");
    page_audio_request_stop_playback();
    for (int i = 0; i < 50 && page_audio_is_playing(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void Application::InterruptConversationForLocalAudio() {
    // Stop dialogue-side input paths first to avoid concurrent RX on I2S during local recording.
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(false);

    auto state = GetDeviceState();
    if (state != kDeviceStateConnecting && state != kDeviceStateListening && state != kDeviceStateSpeaking) {
        return;
    }

    ESP_LOGI(TAG, "Interrupt conversation for local playback");
    auto close_conversation = [this]() {
        auto current_state = GetDeviceState();
        if (current_state == kDeviceStateSpeaking) {
            AbortSpeaking(kAbortReasonNone);
        }
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        } else if (current_state == kDeviceStateConnecting || current_state == kDeviceStateListening || current_state == kDeviceStateSpeaking) {
            SetDeviceState(kDeviceStateIdle);
        }
    };
    if (main_task_handle_ != nullptr && xTaskGetCurrentTaskHandle() == main_task_handle_) {
        close_conversation();
    } else {
        Schedule(std::move(close_conversation));
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    // display->SetChatMessage("system", message.c_str());
    board.SetPowerSaveMode(false);

    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            // display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveMode(true); // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        // display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (page_audio_is_recording()) {
        ESP_LOGW(TAG, "Ignore wake word invoke: local recording is active");
        return;
    }
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        InterruptLocalAudioForConversation();
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        // Set flag to play popup sound after state changes to listening
        // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
        play_popup_on_listening_ = true;
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

std::string Application::BuildAttachmentArrayJson(const std::string& attachment_json) const {
    if (attachment_json.empty()) {
        return "";
    }

    cJSON* root = cJSON_CreateArray();
    cJSON* item = cJSON_Parse(attachment_json.c_str());
    if (item != nullptr && cJSON_IsObject(item)) {
        cJSON_AddItemToArray(root, item);
    } else if (item != nullptr) {
        cJSON_Delete(item);
    }

    if (cJSON_GetArraySize(root) == 0) {
        cJSON_Delete(root);
        return "";
    }

    auto json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return result;
}

void Application::SendDialogueText(const std::string& text) {
    Schedule([this, text = std::move(text)]() {
        if (protocol_) {
            std::string pending_attachments_json;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_attachments_json = active_dialogue_attachment_json_.has_value()
                    ? BuildAttachmentArrayJson(*active_dialogue_attachment_json_)
                    : "";
            }
            if (!pending_attachments_json.empty()) {
                protocol_->SendDialogueTextWithAttachments(text, pending_attachments_json);
            } else {
                protocol_->SendDialogueText(text);
            }
        }
    });
}

void Application::SendDialogueTextWithAttachments(
    const std::string& text,
    const std::string& attachments_json
) {
    Schedule([this, text = std::move(text), attachments_json = std::move(attachments_json)]() {
        if (protocol_) {
            if (!attachments_json.empty()) {
                protocol_->SendDialogueTextWithAttachments(text, attachments_json);
                return;
            }

            std::string active_attachment_json;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                active_attachment_json = active_dialogue_attachment_json_.has_value()
                    ? BuildAttachmentArrayJson(*active_dialogue_attachment_json_)
                    : "";
            }
            protocol_->SendDialogueTextWithAttachments(text, active_attachment_json);
        }
    });
}

void Application::SendTtsText(
    const std::string& text,
    const std::string& metadata_json
) {
    Schedule([this, text = std::move(text), metadata_json = std::move(metadata_json)]() {
        if (!protocol_) {
            ESP_LOGE(TAG, "Protocol not initialized");
            return;
        }

        auto state = GetDeviceState();
        if (state == kDeviceStateIdle) {
            SetUiMuted(false);
            InterruptLocalAudioForConversation();
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            if (!protocol_->IsAudioChannelOpened()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending_tts_text_ = text;
                    pending_tts_metadata_json_ = metadata_json;
                }
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending_tts_text_.reset();
                    pending_tts_metadata_json_.reset();
                    return;
                }
                return;
            }
        } else if (state == kDeviceStateSpeaking) {
            bool reading_ai_mode = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                reading_ai_mode = reading_ai_mode_;
            }
            if (!reading_ai_mode) {
                AbortSpeaking(kAbortReasonNone);
            }
        }

        reading_ai_playback_started_.store(false);
        reading_ai_playback_finished_.store(false);
        protocol_->SendTtsText(text, metadata_json);
    });
}

void Application::UploadAttachmentAndSendDialogueText(
    const std::string& text,
    const std::string& file_path,
    const std::string& attachment_type,
    const std::string& upload_url,
    const std::string& upload_token
) {
    Schedule([this,
              text = std::move(text),
              file_path = std::move(file_path),
              attachment_type = std::move(attachment_type),
              upload_url = std::move(upload_url),
              upload_token = std::move(upload_token)]() {
        try {
            if (!protocol_) {
                throw std::runtime_error("Protocol not initialized");
            }

            const std::vector<std::string> resolved_upload_urls = ResolveUploadUrls(upload_url);
            const std::string resolved_upload_token = upload_token.empty() ? GetDefaultUploadToken() : upload_token;
            ESP_LOGI(TAG, "Uploading attachment to: %s", resolved_upload_urls.empty() ? "" : resolved_upload_urls.front().c_str());
            const std::string attachment_json = UploadAttachmentToXiaozhiWithFallback(
                resolved_upload_urls,
                resolved_upload_token,
                file_path,
                attachment_type,
                protocol_->session_id()
            );

            if (!attachment_json.empty()) {
                protocol_->SendDialogueTextWithAttachments(text, "[" + attachment_json + "]");
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "UploadAttachmentAndSendDialogueText failed: %s", e.what());
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", e.what());
        }
    });
}

void Application::UploadAttachmentForNextDialogue(
    const std::string& file_path,
    const std::string& attachment_type,
    const std::string& upload_url,
    const std::string& upload_token
) {
    Schedule([this,
              file_path = std::move(file_path),
              attachment_type = std::move(attachment_type),
              upload_url = std::move(upload_url),
              upload_token = std::move(upload_token)]() {
        try {
            if (!protocol_) {
                throw std::runtime_error("Protocol not initialized");
            }

            const std::vector<std::string> resolved_upload_urls = ResolveUploadUrls(upload_url);
            const std::string resolved_upload_token = upload_token.empty() ? GetDefaultUploadToken() : upload_token;
            ESP_LOGI(TAG, "Uploading attachment context to: %s", resolved_upload_urls.empty() ? "" : resolved_upload_urls.front().c_str());
            const std::string attachment_json = UploadAttachmentToXiaozhiWithFallback(
                resolved_upload_urls,
                resolved_upload_token,
                file_path,
                attachment_type,
                protocol_->session_id()
            );

            if (!attachment_json.empty()) {
                ApplyUploadedAttachmentToDialogueContext(file_path, attachment_json);
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "UploadAttachmentForNextDialogue failed: %s", e.what());
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", e.what());
        }
    });
}

bool Application::UploadAttachmentForNextDialogueSync(
    const std::string& file_path,
    const std::string& attachment_type,
    const std::string& upload_url,
    const std::string& upload_token,
    std::string* error_message
) {
    // Pause active AFE pipelines during the blocking upload so feed() cannot outrun fetch().
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(false);

    try {
        if (!protocol_) {
            throw std::runtime_error("Protocol not initialized");
        }

        const std::vector<std::string> resolved_upload_urls = ResolveUploadUrls(upload_url);
        const std::string resolved_upload_token = upload_token.empty() ? GetDefaultUploadToken() : upload_token;
        ESP_LOGI(TAG, "Synchronously uploading attachment context to: %s", resolved_upload_urls.empty() ? "" : resolved_upload_urls.front().c_str());
        const std::string attachment_json = UploadAttachmentToXiaozhiWithFallback(
            resolved_upload_urls,
            resolved_upload_token,
            file_path,
            attachment_type,
            protocol_->session_id()
        );

        if (!attachment_json.empty()) {
            ApplyUploadedAttachmentToDialogueContext(file_path, attachment_json);
        }
        HandleStateChangedEvent();
        return true;
    } catch (const std::exception& e) {
        HandleStateChangedEvent();
        ESP_LOGE(TAG, "UploadAttachmentForNextDialogueSync failed: %s", e.what());
        if (error_message != nullptr) {
            *error_message = e.what();
        }
        return false;
    }
}

void Application::QueueDialogueTextForNextListening(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_dialogue_text_ = text;
}

void Application::EnableReadingNoteMode() {
    std::lock_guard<std::mutex> lock(mutex_);
    reading_note_mode_ = true;
    pending_dialogue_text_.reset();
}

bool Application::IsReadingNoteMode() {
    std::lock_guard<std::mutex> lock(mutex_);
    return reading_note_mode_;
}

void Application::EnableReadingAiMode() {
    std::lock_guard<std::mutex> lock(mutex_);
    reading_ai_mode_ = true;
    reading_ai_prefetch_pending_.store(false);
    reading_ai_playback_started_.store(false);
    reading_ai_playback_finished_.store(false);
    reading_ai_next_chunk_requests_.store(0);
    pending_dialogue_text_.reset();
}

void Application::DisableReadingAiMode(bool close_audio_channel) {
    bool should_close_channel = close_audio_channel;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reading_ai_mode_ = false;
        reading_ai_prefetch_pending_.store(false);
        reading_ai_playback_started_.store(false);
        reading_ai_playback_finished_.store(false);
        reading_ai_next_chunk_requests_.store(0);
    }

    Schedule([this, should_close_channel]() {
        if (should_close_channel && protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        } else if (!should_close_channel && GetDeviceState() == kDeviceStateSpeaking) {
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::SetReadingAiPrefetchPending(bool pending) {
    reading_ai_prefetch_pending_.store(pending);
}

bool Application::ConsumeReadingAiPlaybackFinished() {
    return reading_ai_playback_finished_.exchange(false);
}

int Application::ConsumeReadingAiNextChunkRequests() {
    return reading_ai_next_chunk_requests_.exchange(0);
}

void Application::RestoreWakeWordDetectionIfIdle() {
    Schedule([this]() {
        if (GetDeviceState() != kDeviceStateIdle) {
            return;
        }
        audio_service_.EnableVoiceProcessing(false);
#if CONFIG_TRANSCRIPTION_ONLY_MODE
        audio_service_.EnableWakeWordDetection(false);
#else
        audio_service_.EnableWakeWordDetection(
            wake_word_enabled_ &&
            !page_audio_is_recording()
        );
#endif
    });
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload = std::move(payload)]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::ResetTransientListeningContext() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_dialogue_text_.reset();
    active_dialogue_attachment_json_.reset();
    pending_tts_text_.reset();
    pending_tts_metadata_json_.reset();
    reading_note_mode_ = false;
    reading_ai_mode_ = false;
    reading_ai_prefetch_pending_.store(false);
    reading_ai_playback_started_.store(false);
    reading_ai_playback_finished_.store(false);
    reading_ai_next_chunk_requests_.store(0);
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            // display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            // display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            // display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::ResetProtocol() {
    Schedule([this]() {
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}
