#include "protocol.h"

#include <esp_log.h>

#define TAG "Protocol"

void Protocol::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    on_incoming_json_ = callback;
}

void Protocol::OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback) {
    on_incoming_audio_ = callback;
}

void Protocol::OnAudioChannelOpened(std::function<void()> callback) {
    on_audio_channel_opened_ = callback;
}

void Protocol::OnAudioChannelClosed(std::function<void()> callback) {
    on_audio_channel_closed_ = callback;
}

void Protocol::OnNetworkError(std::function<void(const std::string& message)> callback) {
    on_network_error_ = callback;
}

void Protocol::OnConnected(std::function<void()> callback) {
    on_connected_ = callback;
}

void Protocol::OnDisconnected(std::function<void()> callback) {
    on_disconnected_ = callback;
}

void Protocol::SetError(const std::string& message) {
    error_occurred_ = true;
    if (on_network_error_ != nullptr) {
        on_network_error_(message);
    }
}

void Protocol::SendAbortSpeaking(AbortReason reason) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
    if (reason == kAbortReasonWakeWordDetected) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendWakeWordDetected(const std::string& wake_word) {
    std::string json = "{\"session_id\":\"" + session_id_ + 
                      "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + wake_word + "\"}";
    SendText(json);
}

void Protocol::SendDialogueText(const std::string& text) {
    SendDialogueTextWithAttachments(text, "");
}

void Protocol::SendDialogueTextWithAttachments(
    const std::string& text,
    const std::string& attachments_json
) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "text", text.c_str());
#if CONFIG_TEXT_DIALOGUE_TTS_ONLY
    cJSON_AddBoolToObject(root, "tts_only", true);
#endif
    if (!attachments_json.empty()) {
        cJSON* attachments = cJSON_Parse(attachments_json.c_str());
        if (attachments != nullptr && cJSON_IsArray(attachments)) {
            cJSON_AddItemToObject(root, "attachments", attachments);
        } else if (attachments != nullptr) {
            cJSON_Delete(attachments);
        }
    }
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    SendText(message);
}

void Protocol::SendAttachmentContext(const std::string& attachments_json) {
    if (attachments_json.empty()) {
        return;
    }

    cJSON* attachments = cJSON_Parse(attachments_json.c_str());
    if (attachments == nullptr || !cJSON_IsArray(attachments)) {
        if (attachments != nullptr) {
            cJSON_Delete(attachments);
        }
        ESP_LOGW(TAG, "Ignore invalid attachment context payload");
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddItemToObject(root, "attachments", attachments);

    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    SendText(message);
}

void Protocol::SendTtsText(
    const std::string& text,
    const std::string& metadata_json
) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "text", text.c_str());
    cJSON_AddBoolToObject(root, "tts_only", true);

    if (!metadata_json.empty()) {
        cJSON* metadata = cJSON_Parse(metadata_json.c_str());
        if (metadata != nullptr && cJSON_IsObject(metadata)) {
            cJSON_AddItemToObject(root, "reading_meta", metadata);
            cJSON_AddBoolToObject(root, "reading_mode", true);
        } else if (metadata != nullptr) {
            cJSON_Delete(metadata);
        }
    }

    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    SendText(message);
}

void Protocol::SendStartListening(
    ListeningMode mode,
    bool transcription_only,
    bool reading_note_mode
) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";
    if (mode == kListeningModeRealtime) {
        message += ",\"mode\":\"realtime\"";
    } else if (mode == kListeningModeAutoStop) {
        message += ",\"mode\":\"auto\"";
    } else {
        message += ",\"mode\":\"manual\"";
    }
    if (transcription_only) {
        message += ",\"transcription_only\":true";
    }
    if (reading_note_mode) {
        message += ",\"reading_note_mode\":true";
    }
#if CONFIG_TRANSCRIPTION_ONLY_MODE
    if (!transcription_only) {
    message += ",\"transcription_only\":true";
    }
#endif
    message += "}";
    SendText(message);
}

void Protocol::SendStopListening() {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"listen\",\"state\":\"stop\"}";
    SendText(message);
}

void Protocol::SendMcpMessage(const std::string& payload) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    SendText(message);
}

bool Protocol::IsTimeout() const {
    const int kTimeoutSeconds = 120;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_incoming_time_);
    bool timeout = duration.count() > kTimeoutSeconds;
    if (timeout) {
        ESP_LOGE(TAG, "Channel timeout %ld seconds", (long)duration.count());
    }
    return timeout;
}
