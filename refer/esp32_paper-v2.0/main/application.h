#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <deque>
#include <memory>
#include <atomic>
#include <optional>

#include "protocol.h"
#include "ota.h"
#include "audio_service.h"
#include "device_state.h"
#include "device_state_machine.h"

// Main event bits
#define MAIN_EVENT_SCHEDULE             (1 << 0)
#define MAIN_EVENT_SEND_AUDIO           (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED   (1 << 2)
#define MAIN_EVENT_VAD_CHANGE           (1 << 3)
#define MAIN_EVENT_ERROR                (1 << 4)
#define MAIN_EVENT_ACTIVATION_DONE      (1 << 5)
#define MAIN_EVENT_CLOCK_TICK           (1 << 6)
#define MAIN_EVENT_NETWORK_CONNECTED    (1 << 7)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 8)
#define MAIN_EVENT_TOGGLE_CHAT          (1 << 9)
#define MAIN_EVENT_START_LISTENING      (1 << 10)
#define MAIN_EVENT_STOP_LISTENING       (1 << 11)
#define MAIN_EVENT_STATE_CHANGED        (1 << 12)


enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // Delete copy constructor and assignment operator
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /**
     * Initialize the application
     * This sets up display, audio, network callbacks, etc.
     * Network connection starts asynchronously.
     */
    void Initialize();

    /**
     * Run the main event loop
     * This function runs in the main task and never returns.
     * It handles all events including network, state changes, and user interactions.
     */
    void Run();

    void MainEventLoop();

    DeviceState GetDeviceState() const { return state_machine_.GetState(); }
    bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); }
    
    /**
     * Request state transition
     * Returns true if transition was successful
     */
    bool SetDeviceState(DeviceState state);

    /**
     * Schedule a callback to be executed in the main task
     */
    void Schedule(std::function<void()>&& callback);

    /**
     * Alert with status, message, emotion and optional sound
     */
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();

    void AbortSpeaking(AbortReason reason);

    /**
     * Toggle chat state (event-based, thread-safe)
     * Sends MAIN_EVENT_TOGGLE_CHAT to be handled in Run()
     */volatile DeviceState device_state_ = kDeviceStateUnknown;
    void ToggleChatState();

    /**
     * Start listening (event-based, thread-safe)
     * Sends MAIN_EVENT_START_LISTENING to be handled in Run()
     */
    void StartListening();

    /**
     * Stop listening (event-based, thread-safe)
     * Sends MAIN_EVENT_STOP_LISTENING to be handled in Run()
     */
    void StopListening();
    void SetWakeWordEnabled(bool enabled);
    bool IsWakeWordEnabled() const { return wake_word_enabled_; }
    bool IsUiMuted() const { return ui_muted_.load(); }
    void SetUiMuted(bool muted);

    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    void SendDialogueText(const std::string& text);
    void SendDialogueTextWithAttachments(
        const std::string& text,
        const std::string& attachments_json
    );
    void UploadAttachmentAndSendDialogueText(
        const std::string& text,
        const std::string& file_path,
        const std::string& attachment_type = "file",
        const std::string& upload_url = "",
        const std::string& upload_token = ""
    );
    void UploadAttachmentForNextDialogue(
        const std::string& file_path,
        const std::string& attachment_type = "file",
        const std::string& upload_url = "",
        const std::string& upload_token = ""
    );
    bool UploadAttachmentForNextDialogueSync(
        const std::string& file_path,
        const std::string& attachment_type = "file",
        const std::string& upload_url = "",
        const std::string& upload_token = "",
        std::string* error_message = nullptr
    );
    void SendTtsText(
        const std::string& text,
        const std::string& metadata_json = ""
    );
    void QueueDialogueTextForNextListening(const std::string& text);
    void EnableReadingNoteMode();
    bool IsReadingNoteMode();
    void EnableReadingAiMode();
    void DisableReadingAiMode(bool close_audio_channel = false);
    void SetReadingAiPrefetchPending(bool pending);
    bool ConsumeReadingAiPlaybackFinished();
    int ConsumeReadingAiNextChunkRequests();
    void RestoreWakeWordDetectionIfIdle();
    bool UpgradeFirmware(const std::string& url, const std::string& version = "");
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string& payload);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_; }
    void PlaySound(const std::string_view& sound);
    AudioService& GetAudioService() { return audio_service_; }
    void InterruptConversationForLocalAudio();
    
    /**
     * Reset protocol resources (thread-safe)
     * Can be called from any task to release resources allocated after network connected
     * This includes closing audio channel, resetting protocol and ota objects
     */
    void ResetProtocol();

private:
    Application();
    ~Application();

    std::mutex mutex_;
    std::deque<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    DeviceStateMachine state_machine_;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;
    std::string last_error_message_;
    AudioService audio_service_;
    std::unique_ptr<Ota> ota_;

    bool has_server_time_ = false;
    bool aborted_ = false;
    bool assets_version_checked_ = false;
    bool play_popup_on_listening_ = false;  // Flag to play popup sound after state changes to listening
    int clock_ticks_ = 0;
    TaskHandle_t activation_task_handle_ = nullptr;
    TaskHandle_t main_task_handle_ = nullptr;
    bool wake_word_enabled_ = true;
    std::atomic<bool> ui_muted_{false};
    std::optional<std::string> pending_dialogue_text_;
    std::optional<std::string> active_dialogue_attachment_json_;
    std::optional<std::string> pending_tts_text_;
    std::optional<std::string> pending_tts_metadata_json_;
    bool reading_note_mode_ = false;
    bool reading_ai_mode_ = false;
    std::atomic<bool> reading_ai_prefetch_pending_{false};
    std::atomic<bool> reading_ai_playback_started_{false};
    std::atomic<bool> reading_ai_playback_finished_{false};
    std::atomic<int> reading_ai_next_chunk_requests_{0};


    // Event handlers
    void HandleStateChangedEvent();
    void HandleToggleChatEvent();
    void HandleStartListeningEvent();
    void HandleStopListeningEvent();
    void HandleNetworkConnectedEvent();
    void HandleNetworkDisconnectedEvent();
    void HandleActivationDoneEvent();
    void HandleWakeWordDetectedEvent();

    // Activation task (runs in background)
    void ActivationTask();

    // Helper methods
    void OnWakeWordDetected();
    void CheckAssetsVersion();
    void CheckNewVersion();
    void InitializeProtocol();
    void ShowActivationCode(const std::string& code, const std::string& message);
    void SetListeningMode(ListeningMode mode);
    void InterruptLocalAudioForConversation();
    void ResetTransientListeningContext();
    void ApplyUploadedAttachmentToDialogueContext(
        const std::string& file_path,
        const std::string& attachment_json
    );
    std::string BuildAttachmentArrayJson(const std::string& attachment_json) const;
    
    // State change handler called by state machine
    void OnStateChanged(DeviceState old_state, DeviceState new_state);
};


class TaskPriorityReset {
public:
    TaskPriorityReset(BaseType_t priority) {
        original_priority_ = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, priority);
    }
    ~TaskPriorityReset() {
        vTaskPrioritySet(NULL, original_priority_);
    }

private:
    BaseType_t original_priority_;
};

#endif // _APPLICATION_H_
