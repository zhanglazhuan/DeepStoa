#include "page_audio.h"
#include "es8311_bsp.h"
#include "board.h"
#include "audio_codec.h"
#include "application.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_system.h"
#include "esp_check.h"
#include "button_bsp.h"
#include "audio_player.h" 
#include "music.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <string>

#include <errno.h>
#include <string.h>
#include <atomic>

#include "epaper_port.h"
#include "GUI_BMPfile.h"
#include "GUI_Paint.h"
#include "pcf85063_bsp.h"
#include "axp_prot.h"
#include "sdcard_bsp.h"
#include "status_bar.h"

static const char *TAG = "page_audio";

// Recording status and DMA buffer
static volatile bool recording = false;
static QueueHandle_t audio_queue = NULL;
static volatile record_state_t record_state = RECORD_STATE_IDLE;
static float duration = 0;

extern bool wifi_enable;                
extern SemaphoreHandle_t rtc_mutex;  
// Define the data cache area of the e-paper
extern uint8_t *Image_Mono;
uint8_t *Image_Mono_audio;
// E-ink screen sleep time (S)
#define EPD_Sleep_Time   5
// Equipment shutdown time (minutes)
#define Unattended_Time  10

// icon size
#define GUI_WIDTH       32
#define GUI_HEIGHT      32

#define HEADER_HEIGHT   65    // The top path shows the height of the area
#define ITEM_HEIGHT     45    // The height of each file/directory entry
#define MARGIN_LEFT     10    // leftmargin
#define MARGIN_TOP      10    // top margin

#define RECORD_IO_BUFFER_SIZE   4096
#define RECORD_CHUNK_BYTES      2048
#define RECORD_TASK_CORE        1
#define RECORD_TASK_PRIORITY    4
#define PLAYBACK_PROGRESS_UPDATE_MS 2000


// Pre-declared function
static void display_message(const char* message);
static void display_music_file_list(file_entry_t* entries, int num_entries, int current_selection, int page_index, int total_pages);
static void update_music_selection(int new_selected);
static void truncate_filename_for_display(const char* filename, char* display_name, int max_len, int max_width);
static const char* get_file_type(const char* filename);
static int calculate_display_width(const char* str);
static void truncate_string_by_width(const char* source, char* dest, int dest_size, int max_width, cFONT *Font);
static void display_audio_option_file_management(int count);

static void display_audio_init(Time_data rtc_time);
static void display_audio_time_last(Time_data rtc_time);
static void display_audio_time(Time_data rtc_time);
static void display_audio_option(int count);

static void Forced_refresh_audio(uint8_t *EDP_buffer);
static void Refresh_page_audio(uint8_t *EDP_buffer);
static int Sleep_wake_audio(void);
static std::vector<music_file_t> build_music_playlist(void);
static int find_music_file_index(const std::vector<music_file_t>& playlist, const char* filename);
static uint32_t get_audio_file_duration_seconds(const char* file_path);
static void format_playback_time(uint32_t total_seconds, char* buffer, size_t buffer_size);
static void draw_playback_progress(uint32_t elapsed_seconds, uint32_t duration_seconds);
static void render_music_playback_screen(const char* file_path_name, int track_index, int track_count,
                                         bool paused, uint32_t elapsed_seconds, uint32_t duration_seconds);
static void page_audio_play_playlist(const std::vector<music_file_t>& playlist, int start_index);

int Volume = 90;  // Volume (reset after power failure)
static std::atomic<bool> g_local_audio_playing(false);
static std::atomic<bool> g_local_audio_stop_requested(false);
static AudioCodec* page_audio_get_codec();

class LocalPlaybackGuard {
public:
    LocalPlaybackGuard()
        : codec_(page_audio_get_codec()) {
        restore_input_enabled_ = codec_ ? codec_->input_enabled() : false;
        if (codec_ && restore_input_enabled_) {
            codec_->EnableInput(false);
        }
        g_local_audio_stop_requested.store(false, std::memory_order_release);
        g_local_audio_playing.store(true, std::memory_order_release);
    }
    ~LocalPlaybackGuard() {
        if (codec_) {
            if (restore_input_enabled_) {
                codec_->EnableInput(true);
            }
        }
        g_local_audio_playing.store(false, std::memory_order_release);
        g_local_audio_stop_requested.store(false, std::memory_order_release);
    }
private:
    AudioCodec* codec_;
    bool restore_input_enabled_;
};

static bool page_audio_should_stop_playback() {
    return g_local_audio_stop_requested.load(std::memory_order_acquire);
}

static bool page_audio_prepare_for_local_playback() {
    auto& app = Application::GetInstance();
    app.InterruptConversationForLocalAudio();
    for (int i = 0; i < 50; ++i) {
        auto state = app.GetDeviceState();
        if (state != kDeviceStateConnecting && state != kDeviceStateListening && state != kDeviceStateSpeaking) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGW(TAG, "Conversation is still active, cancel local playback");
    return false;
}

static bool page_audio_prepare_for_recording() {
    if (!page_audio_prepare_for_local_playback()) {
        return false;
    }

    // Recording must own RX exclusively. Turn off all dialogue-side input paths.
    auto& audio_service = Application::GetInstance().GetAudioService();
    audio_service.EnableVoiceProcessing(false);
    audio_service.EnableWakeWordDetection(false);
    return true;
}

static void page_audio_restore_after_recording() {
    auto& app = Application::GetInstance();
    auto& audio_service = app.GetAudioService();
    audio_service.EnableVoiceProcessing(false);

    if (app.GetDeviceState() == kDeviceStateIdle) {
        audio_service.EnableWakeWordDetection(true);
        return;
    }

    // Dialogue channel close/state transition may complete slightly later than record stop.
    // Retry briefly to avoid missing wake-word re-enable after local recording.
    xTaskCreate([](void*) {
        auto& app_ref = Application::GetInstance();
        auto& audio_service_ref = app_ref.GetAudioService();
        for (int i = 0; i < 30; ++i) {
            if (record_state != RECORD_STATE_IDLE) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            auto state = app_ref.GetDeviceState();
            if (state == kDeviceStateIdle) {
                audio_service_ref.EnableVoiceProcessing(false);
                audio_service_ref.EnableWakeWordDetection(true);
                ESP_LOGI(TAG, "Wake word detection restored after recording");
                break;
            }
            if (state == kDeviceStateWifiConfiguring ||
                state == kDeviceStateUpgrading ||
                state == kDeviceStateActivating ||
                state == kDeviceStateAudioTesting ||
                state == kDeviceStateFatalError) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelete(NULL);
    }, "rec_restore", 2048, NULL, 3, NULL);
}

static AudioCodec* page_audio_get_codec() {
    return Board::GetInstance().GetAudioCodec();
}

static void page_audio_set_output_volume(int volume) {
    auto* codec = page_audio_get_codec();
    if (codec) {
        codec->EnableOutput(true);
        codec->SetOutputVolume(volume);
    }
}

static void page_audio_set_mute(bool mute) {
    auto* codec = page_audio_get_codec();
    if (!codec) {
        return;
    }
    if (mute) {
        codec->EnableOutput(false);
        return;
    }
    codec->EnableOutput(true);
    codec->SetOutputVolume(Volume);
}

bool page_audio_is_playing(void) {
    return g_local_audio_playing.load(std::memory_order_acquire);
}

bool page_audio_is_recording(void) {
    return record_state != RECORD_STATE_IDLE;
}

void page_audio_request_stop_playback(void) {
    g_local_audio_stop_requested.store(true, std::memory_order_release);
    audio_player_state_t state = audio_player_get_state();
    if (state != AUDIO_PLAYER_STATE_SHUTDOWN && state != AUDIO_PLAYER_STATE_IDLE) {
        audio_player_stop();
    }
    page_audio_set_mute(true);
}

// Track current audio player output channel mode (1=mono, 2=stereo)
static int g_output_channels = 1;

// I2S write function - Adapted to existing I2S handles
static esp_err_t bsp_i2s_write(const void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    (void)timeout_ms;
    auto* codec = page_audio_get_codec();
    if (codec == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    Application::GetInstance().GetAudioService().NotifyExternalOutput();
    const size_t samples = len / sizeof(int16_t);
    codec->WriteRaw(reinterpret_cast<const int16_t*>(audio_buffer), samples);
    if (bytes_written) {
        *bytes_written = samples * sizeof(int16_t);
    }
    return ESP_OK;
}

// I2S clock reconfiguration function - Supports dual-channel file playback on TF cards
static esp_err_t bsp_i2s_reconfig_clk(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    (void)rate;
    (void)bits_cfg;
    (void)ch;
    return ESP_OK;
}
// Silent control function
static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting) {
    ESP_LOGI(TAG, "mute setting %d", setting);
    if (setting == AUDIO_PLAYER_MUTE) {
        page_audio_set_mute(true);
    } else {
        page_audio_set_mute(false);
    }
    return ESP_OK;
}

void page_audio_int(void)
{
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    vTaskDelay(pdMS_TO_TICKS(50));

    // Ensure codec device is opened before audio_player_new triggers mute callback
    auto* codec = page_audio_get_codec();
    if (codec) {
        codec->EnableOutput(true);
    }

    audio_player_config_t config = {};
    config.mute_fn = audio_mute_function;
    config.write_fn = bsp_i2s_write;
    config.clk_set_fn = bsp_i2s_reconfig_clk;
    config.priority = 5;
    config.coreID = 1;
    config.force_stereo = false;
    
    esp_err_t ret = audio_player_new(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio player init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "audio player init success");
    }

    page_audio_set_mute(true);
    
    ESP_LOGI(TAG, "Audio subsystem ready (muted)");
}

// 播放TF卡音频文件
void page_audio_play_file(const char* file_path_name)
{
    auto playlist = build_music_playlist();
    int start_index = find_music_file_index(playlist, file_path_name);
    if (start_index < 0) {
        music_file_t single_track = {};
        strncpy(single_track.filename, file_path_name, sizeof(single_track.filename) - 1);
        snprintf(single_track.full_path, sizeof(single_track.full_path), "/sdcard/music/%s", file_path_name);
        playlist.push_back(single_track);
        start_index = 0;
    }
    if (playlist.empty()) {
        ESP_LOGW(TAG, "No playable music file found");
        return;
    }
    page_audio_play_playlist(playlist, start_index);
}

// Play the built-in audio file
// For an alarm clock
void page_audio_play_memory(void)
{
    if (!page_audio_prepare_for_local_playback()) {
        return;
    }
    page_audio_int();
    audio_player_state_t state = audio_player_get_state();
    if (state == AUDIO_PLAYER_STATE_SHUTDOWN) {
        ESP_LOGE(TAG, "Audio player not initialized");
        return;
    }
    LocalPlaybackGuard playback_guard;

    page_audio_set_mute(false);
    page_audio_set_output_volume(Volume);

    ESP_LOGI(TAG, "Start playing the in-memory audio data");

    bsp_i2s_reconfig_clk(16000, 16, I2S_SLOT_MODE_MONO);

    // Play audio data, pass it in multiple batches, and allow for key detection
    size_t bytes_written = 0;
    const size_t chunk_size = 1024; 
    size_t offset = 0;

    while (offset < AUDIO_SAMPLES * sizeof(uint16_t)) {
        if (page_audio_should_stop_playback()) {
            ESP_LOGI(TAG, "Built-in playback interrupted by conversation");
            break;
        }
        size_t write_size = ((AUDIO_SAMPLES * sizeof(uint16_t) - offset) > chunk_size) ? chunk_size : (AUDIO_SAMPLES * sizeof(uint16_t) - offset);
        bsp_i2s_write((void*)((uint8_t*)audio_data + offset), write_size, &bytes_written, 1000);
        offset += write_size;

        int button = wait_key_event_and_return_code(1); 
        if (button != -1) { 
            ESP_LOGI(TAG, "The user exits the playback.");
            break;
        }
    }
    page_audio_set_mute(true);
}

// Built-in audio playback with responsive keys
void page_audio_play_memory_1(void)
{ 
    if (!page_audio_prepare_for_local_playback()) {
        return;
    }
    page_audio_int();
    int Image_Mono_audio_flag = 0;
    if (Image_Mono_audio == NULL) {
        Image_Mono_audio = (uint8_t*)heap_caps_malloc(EPD_SIZE_MONO, MALLOC_CAP_SPIRAM);
        if (Image_Mono_audio == NULL) {
            ESP_LOGE(TAG, "Image Mono audio cannot be allocated. Cancel playback");
            return;
        }
        Paint_NewImage(Image_Mono_audio, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
        Paint_SetScale(2);
        Image_Mono_audio_flag = 1;
    }

    Paint_SelectImage(Image_Mono_audio);
    Paint_Clear(WHITE);

    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    int last_minutes = rtc_time.minutes;
    display_audio_time(rtc_time);

    uint16_t x_or;
    x_or = reassignCoordinates_CH(240, " 正在播放 ", &Font24_UTF8);
    Paint_DrawString_CN(x_or, 256, " 正在播放 ", &Font24_UTF8, BLACK, WHITE);

    x_or = reassignCoordinates_CH(240, "内置音频文件", &Font24_UTF8);
    Paint_DrawString_CN(x_or, 316, "内置音频文件", &Font24_UTF8, WHITE, BLACK);

    Refresh_page_audio(Image_Mono_audio);

    audio_player_state_t state = audio_player_get_state();
    if (state == AUDIO_PLAYER_STATE_SHUTDOWN) {
        ESP_LOGE(TAG, "Audio player not initialized");
        return;
    }
    LocalPlaybackGuard playback_guard;

    page_audio_set_mute(false);
    page_audio_set_output_volume(Volume);

    ESP_LOGI(TAG, "Start playing the in-memory audio data");

        bsp_i2s_reconfig_clk(16000, 16, I2S_SLOT_MODE_MONO);

    size_t bytes_written = 0;
    const size_t chunk_size = 1024;
    size_t offset = 0;

    // Due to the refresh rate of the e-paper, it is not displayed here
    while (offset < AUDIO_SAMPLES * sizeof(uint16_t)) {
        if (page_audio_should_stop_playback()) {
            ESP_LOGI(TAG, "Built-in playback interrupted by conversation");
            break;
        }
        size_t write_size = ((AUDIO_SAMPLES * sizeof(uint16_t) - offset) > chunk_size) ? chunk_size : (AUDIO_SAMPLES * sizeof(uint16_t) - offset);
        bsp_i2s_write((void*)((uint8_t*)audio_data + offset), write_size, &bytes_written, 1000);
        offset += write_size;

        int button = wait_key_event_and_return_code(1);
        if (button == 0) { // Volume +
            Volume = Volume + Fixed_volume_increment;
            if (Volume > 100) Volume = 100;
            page_audio_set_output_volume(Volume);
            // ESP_LOGI(TAG, "The current volume is %d", Volume);
        }
        else if (button == 14) { // Volume -
            Volume = Volume - Fixed_volume_increment;
            if (Volume < 0) Volume = 0;
            page_audio_set_output_volume(Volume);
            // ESP_LOGI(TAG, "The current volume is %d", Volume);
        }
        else if (button == 8) {
            ESP_LOGI(TAG, "The user exits the playback.");
            break;
        }
    }
    page_audio_set_mute(true);
}

// Get the list of audio files in the "music" directory
static int get_music_files(music_file_t *file_list, int max_files)
{
    DIR *dir = opendir("/sdcard/music");
    if (!dir) {
        ESP_LOGE(TAG, "The music directory cannot be opened");
        return 0;
    }
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        // Check if it is an audio file
        if (strstr(entry->d_name, ".wav") || strstr(entry->d_name, ".WAV") ||
            strstr(entry->d_name, ".mp3") || strstr(entry->d_name, ".MP3")) {
            
            strncpy(file_list[count].filename, entry->d_name, sizeof(file_list[count].filename) - 1);
            snprintf(file_list[count].full_path, sizeof(file_list[count].full_path), "/sdcard/music/%s", entry->d_name);
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

static std::vector<music_file_t> build_music_playlist(void)
{
    std::vector<music_file_t> playlist;
    DIR *dir = opendir("/sdcard/music");
    if (!dir) {
        ESP_LOGE(TAG, "The music directory cannot be opened");
        return playlist;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* ext = strrchr(entry->d_name, '.');
        if (!ext) {
            continue;
        }
        if (strcasecmp(ext, ".wav") != 0 && strcasecmp(ext, ".mp3") != 0) {
            continue;
        }

        music_file_t item = {};
        strncpy(item.filename, entry->d_name, sizeof(item.filename) - 1);
        snprintf(item.full_path, sizeof(item.full_path), "/sdcard/music/%s", entry->d_name);
        playlist.push_back(item);
    }
    closedir(dir);

    std::sort(playlist.begin(), playlist.end(), [](const music_file_t& a, const music_file_t& b) {
        return strcasecmp(a.filename, b.filename) < 0;
    });
    return playlist;
}

static int find_music_file_index(const std::vector<music_file_t>& playlist, const char* filename)
{
    for (size_t i = 0; i < playlist.size(); ++i) {
        if (strcmp(playlist[i].filename, filename) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static uint32_t get_audio_file_duration_seconds(const char* file_path)
{
    const char* ext = strrchr(file_path, '.');
    if (!ext) {
        return 0;
    }

    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        return 0;
    }

    uint32_t duration_seconds = 0;
    if (strcasecmp(ext, ".wav") == 0) {
        wav_header_t header;
        if (fread(&header, sizeof(header), 1, fp) == 1 &&
            strncmp(header.riff, "RIFF", 4) == 0 &&
            strncmp(header.wave, "WAVE", 4) == 0 &&
            header.byte_rate > 0) {
            duration_seconds = header.data_size / header.byte_rate;
        }
    } else if (strcasecmp(ext, ".mp3") == 0) {
        id3v2_header_t id3v2;
        uint32_t id3v2_size = 0;
        if (fread(&id3v2, sizeof(id3v2), 1, fp) == 1) {
            if (strncmp(id3v2.identifier, "ID3", 3) == 0) {
                id3v2_size = parse_synchsafe_int(id3v2.size) + 10;
            }
        }
        fseek(fp, id3v2_size, SEEK_SET);
        long frame_pos = find_mp3_frame(fp);
        if (frame_pos >= 0) {
            fseek(fp, frame_pos, SEEK_SET);
            uint32_t frame_header;
            if (fread(&frame_header, 4, 1, fp) == 1) {
                frame_header = __builtin_bswap32(frame_header);
                int bitrate = 0;
                int samplerate = 0;
                int channels = 0;
                if (parse_mp3_frame_header(frame_header, &bitrate, &samplerate, &channels)) {
                    struct stat file_stat;
                    if (stat(file_path, &file_stat) == 0 && bitrate > 0) {
                        long audio_size = file_stat.st_size - id3v2_size;
                        if (fseek(fp, -128, SEEK_END) == 0) {
                            char check_tag[3];
                            if (fread(check_tag, 3, 1, fp) == 1 && strncmp(check_tag, "TAG", 3) == 0) {
                                audio_size -= 128;
                            }
                        }
                        if (audio_size > 0) {
                            duration_seconds = (audio_size * 8) / (bitrate * 1000);
                        }
                    }
                }
            }
        }
    }

    fclose(fp);
    return duration_seconds;
}

static void format_playback_time(uint32_t total_seconds, char* buffer, size_t buffer_size)
{
    uint32_t minutes = total_seconds / 60;
    uint32_t seconds = total_seconds % 60;
    snprintf(buffer, buffer_size, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
}

static void draw_playback_progress(uint32_t elapsed_seconds, uint32_t duration_seconds)
{
    const int bar_x = 30;
    const int bar_y = 420;
    const int bar_width = 420;
    const int bar_height = 22;
    Paint_DrawRectangle(bar_x, bar_y, bar_x + bar_width, bar_y + bar_height, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    int fill_width = 0;
    if (duration_seconds > 0) {
        fill_width = static_cast<int>((static_cast<uint64_t>(elapsed_seconds) * (bar_width - 4)) / duration_seconds);
        if (fill_width > bar_width - 4) {
            fill_width = bar_width - 4;
        }
    }
    if (fill_width > 0) {
        Paint_DrawRectangle(bar_x + 2, bar_y + 2, bar_x + 2 + fill_width, bar_y + bar_height - 2,
                            BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    }

    char elapsed_text[16];
    char duration_text[16];
    format_playback_time(elapsed_seconds, elapsed_text, sizeof(elapsed_text));
    if (duration_seconds > 0) {
        format_playback_time(duration_seconds, duration_text, sizeof(duration_text));
    } else {
        snprintf(duration_text, sizeof(duration_text), "--:--");
    }
    Paint_DrawString_EN(30, 452, elapsed_text, &Font16, WHITE, BLACK);
    Paint_DrawString_EN(400, 452, duration_text, &Font16, WHITE, BLACK);
}

static void render_music_playback_screen(const char* file_path_name, int track_index, int track_count,
                                         bool paused, uint32_t elapsed_seconds, uint32_t duration_seconds)
{
    if (Image_Mono_audio == NULL) {
        return;
    }

    Paint_SelectImage(Image_Mono_audio);
    Paint_Clear(WHITE);

    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    display_audio_time(rtc_time);

    uint16_t x_or = reassignCoordinates_CH(240, paused ? " 暂停播放 " : " 正在播放 ", &Font24_UTF8);
    Paint_DrawString_CN(x_or, 86, paused ? " 暂停播放 " : " 正在播放 ", &Font24_UTF8, BLACK, WHITE);

    char track_info[32];
    snprintf(track_info, sizeof(track_info), "%d/%d", track_index + 1, track_count);
    x_or = reassignCoordinates_CH(240, track_info, &Font18_UTF8);
    Paint_DrawString_CN(x_or, 146, track_info, &Font18_UTF8, WHITE, BLACK);

    char display_name[100];
    truncate_string_by_width(file_path_name, display_name, sizeof(display_name), 430, &Font24_UTF8);
    x_or = reassignCoordinates_CH(240, display_name, &Font24_UTF8);
    Paint_DrawString_CN(x_or, 220, display_name, &Font24_UTF8, WHITE, BLACK);

    char volume_text[16];
    snprintf(volume_text, sizeof(volume_text), "音量:%d%%", Volume);
    x_or = reassignCoordinates_CH(240, volume_text, &Font18_UTF8);
    Paint_DrawString_CN(x_or, 280, volume_text, &Font18_UTF8, WHITE, BLACK);

    draw_playback_progress(elapsed_seconds, duration_seconds);

    Paint_DrawString_CN(32, 540, "上:上一曲", &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(172, 540, "中:暂停/播放", &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(342, 540, "下:下一曲", &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(32, 585, "长按上:音量+", &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(262, 585, "长按下:音量-", &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(32, 630, "双击中/Boot:退出", &Font18_UTF8, WHITE, BLACK);

    Refresh_page_audio(Image_Mono_audio);
}

static void page_audio_play_playlist(const std::vector<music_file_t>& playlist, int start_index)
{
    if (playlist.empty() || start_index < 0 || start_index >= static_cast<int>(playlist.size())) {
        return;
    }
    if (!page_audio_prepare_for_local_playback()) {
        return;
    }

    page_audio_int();

    bool image_allocated = false;
    if (Image_Mono_audio == NULL) {
        Image_Mono_audio = (uint8_t*)heap_caps_malloc(EPD_SIZE_MONO, MALLOC_CAP_SPIRAM);
        if (Image_Mono_audio == NULL) {
            ESP_LOGE(TAG, "Image Mono audio cannot be allocated. Cancel playback");
            return;
        }
        Paint_NewImage(Image_Mono_audio, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
        Paint_SetScale(2);
        image_allocated = true;
    }

    audio_player_state_t state = audio_player_get_state();
    if (state == AUDIO_PLAYER_STATE_SHUTDOWN) {
        ESP_LOGE(TAG, "Audio player not initialized");
        if (image_allocated) {
            heap_caps_free(Image_Mono_audio);
            Image_Mono_audio = NULL;
        }
        return;
    }

    LocalPlaybackGuard playback_guard;
    page_audio_set_mute(false);
    page_audio_set_output_volume(Volume);

    int current_index = start_index;
    bool paused = false;
    bool exit_playback = false;
    int pending_index = -1;

    while (!exit_playback) {
        if (current_index < 0 || current_index >= static_cast<int>(playlist.size())) {
            break;
        }

        uint32_t duration_seconds = get_audio_file_duration_seconds(playlist[current_index].full_path);
        FILE *fp = fopen(playlist[current_index].full_path, "rb");
        if (fp == NULL) {
            ESP_LOGE(TAG, "Failed to open file: %s", playlist[current_index].full_path);
            break;
        }

        esp_err_t err = audio_player_play(fp);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to play file: %s, error: %s",
                     playlist[current_index].full_path, esp_err_to_name(err));
            fclose(fp);
            break;
        }

        paused = false;
        pending_index = -1;
        int64_t start_us = esp_timer_get_time();
        int64_t pause_started_us = 0;
        int64_t paused_total_us = 0;
        int64_t last_refresh_us = 0;
        render_music_playback_screen(playlist[current_index].filename, current_index, playlist.size(), false, 0, duration_seconds);

        while (true) {
            state = audio_player_get_state();
            if (page_audio_should_stop_playback()) {
                audio_player_stop();
                exit_playback = true;
                break;
            }

            int button = wait_key_event_and_return_code(pdMS_TO_TICKS(200));
            if (button == 7) {
                if (!paused) {
                    audio_player_pause();
                    paused = true;
                    pause_started_us = esp_timer_get_time();
                } else {
                    audio_player_resume();
                    paused = false;
                    paused_total_us += esp_timer_get_time() - pause_started_us;
                    pause_started_us = 0;
                }
                last_refresh_us = 0;
            } else if (button == 0) {
                pending_index = (current_index - 1 + static_cast<int>(playlist.size())) % static_cast<int>(playlist.size());
                audio_player_stop();
            } else if (button == 14) {
                pending_index = (current_index + 1) % static_cast<int>(playlist.size());
                audio_player_stop();
            } else if (button == 5 || button == 6) {
                if (Volume < 100) {
                    Volume += Fixed_volume_increment;
                    if (Volume > 100) {
                        Volume = 100;
                    }
                    page_audio_set_output_volume(Volume);
                    last_refresh_us = 0;
                }
            } else if (button == 19 || button == 20) {
                if (Volume > 0) {
                    Volume -= Fixed_volume_increment;
                    if (Volume < 0) {
                        Volume = 0;
                    }
                    page_audio_set_output_volume(Volume);
                    last_refresh_us = 0;
                }
            } else if (button == 8 || button == 21 || button == 22) {
                audio_player_stop();
                exit_playback = true;
            }

            int64_t now_us = esp_timer_get_time();
            if ((last_refresh_us == 0) || (now_us - last_refresh_us >= PLAYBACK_PROGRESS_UPDATE_MS * 1000LL)) {
                uint32_t elapsed_seconds = 0;
                if (!paused) {
                    elapsed_seconds = static_cast<uint32_t>((now_us - start_us - paused_total_us) / 1000000LL);
                } else {
                    elapsed_seconds = static_cast<uint32_t>((pause_started_us - start_us - paused_total_us) / 1000000LL);
                }
                if (duration_seconds > 0 && elapsed_seconds > duration_seconds) {
                    elapsed_seconds = duration_seconds;
                }
                render_music_playback_screen(playlist[current_index].filename, current_index, playlist.size(),
                                             paused, elapsed_seconds, duration_seconds);
                last_refresh_us = now_us;
            }

            if (state == AUDIO_PLAYER_STATE_IDLE || state == AUDIO_PLAYER_STATE_SHUTDOWN) {
                break;
            }
        }

        if (exit_playback) {
            break;
        }

        if (pending_index >= 0) {
            current_index = pending_index;
        } else {
            current_index = (current_index + 1) % static_cast<int>(playlist.size());
        }
    }

    page_audio_set_mute(true);
    if (image_allocated) {
        heap_caps_free(Image_Mono_audio);
        Image_Mono_audio = NULL;
    }
}

// Calculate the number of audio file lists displayed per page
int get_audio_page_size(void)
{
    // Total screen height - top title area - bottom status bar
    int header_height = HEADER_HEIGHT; 
    int status_height = Font16.Height * 2;   
    int available_height = EPD_WIDTH - header_height - status_height-10;
    int item_height = Font18_UTF8.Height + 10;
    
    int page_size = available_height / item_height;
    
    return page_size;
}


// Add a file selection menu for pagination display
static int select_music_file(int menu_idx)
{
    // Calculate pagination parameters
    int page_size = get_audio_page_size();
    if (page_size < 1) page_size = 5;

    file_entry_t* entries = (file_entry_t*)heap_caps_malloc(page_size * sizeof(file_entry_t), MALLOC_CAP_SPIRAM);
    if (!entries) {
        ESP_LOGE(TAG, "文件列表内存分配失败");
        return -1;
    }
    int total_num = 0;
    int num = 0; 
    int num_selection = 0;
    int page_index = 0; 
    bool first_display = true;

    Paint_DrawRectangle(5, 58, 475, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    uint16_t x_or = reassignCoordinates_CH(240, " 正在读取文件目录... ", &Font24_UTF8);
    Paint_DrawString_CN(x_or, 410, " 正在读取文件目录... ", &Font24_UTF8, BLACK, WHITE);
    Refresh_page_audio(Image_Mono);
    Paint_DrawRectangle(5, 58, 475, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    // Get the total number of files in the current directory
    total_num = get_dir_file_count("/sdcard/music");
    // Calculate the total number of pages
    int total_pages = (total_num + page_size - 1) / page_size;
    if (total_pages == 0) total_pages = 1;

    // Read the current directory by pagination
    num = list_dir_page("/sdcard/music", entries, page_index * page_size, page_size);
    
    int button;
    int time_count = 0;
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    int last_minutes = -1;
    last_minutes = rtc_time.minutes;
    display_audio_time(rtc_time);

    // Initial display
    display_music_file_list(entries, num, num_selection, page_index, total_pages);

    while (1) {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000)); 
        if(button == -1) time_count++;

        if((time_count > EPD_Sleep_Time)) {
            button = Sleep_wake_audio();
        }

        if (button == 14) { // next
            time_count = 0;
            num_selection++;          
            if (num_selection >= page_size || num_selection >= total_num) {
                // Turn to the next page
                // ESP_LOGI("num", "Turn to the next page");
                page_index++;
                if(page_index * page_size >= total_num)  page_index = 0;
                num = list_dir_page("/sdcard/music", entries, page_index * page_size, page_size);
                num_selection = 0;
                display_music_file_list(entries, num, num_selection, page_index, total_pages);    
            } else {
                update_music_selection(num_selection);
            }
            
        } else if (button == 0) { // last
            time_count = 0;
            num_selection--;
            if (num_selection < 0) {
                // Turn to the previous page
                // ESP_LOGI("num", "Turn to the previous page");
                page_index--;
                if(page_index < 0)  page_index = total_pages -1;
                num = list_dir_page("/sdcard/music", entries, page_index * page_size, page_size);
                num_selection = num-1;
                display_music_file_list(entries, num, num_selection, page_index, total_pages);
            } else {
                update_music_selection(num_selection);
            }
            
        } else if (button == 7) { // 
            // ESP_LOGI("num", "The selected file is %s", entries[num_selection].name);
            if(menu_idx == 1) {
                auto playlist = build_music_playlist();
                int selected_index = page_index * page_size + num_selection;
                if (!playlist.empty() && selected_index >= 0 && selected_index < static_cast<int>(playlist.size())) {
                    page_audio_play_playlist(playlist, selected_index);
                }
            } else if (menu_idx == 3) {
                // file management
                if(page_audio_file_management(entries[num_selection].name))
                {
                    Paint_SelectImage(Image_Mono);
                    Paint_DrawRectangle(5, 58, 480, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                    x_or = reassignCoordinates_CH(240, " 正在重新读取文件目录... ", &Font24_UTF8);
                    Paint_DrawString_CN(x_or, 410, " 正在重新读取文件目录... ", &Font24_UTF8, BLACK, WHITE);
                    Refresh_page_audio(Image_Mono);

                    Paint_DrawRectangle(5, 58, 475, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

                    total_num = get_dir_file_count("/sdcard/music");
                    total_pages = (total_num + page_size - 1) / page_size;
                    if (total_pages == 0) total_pages = 1;

                    num = list_dir_page("/sdcard/music", entries, page_index * page_size, page_size);

                    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
                    rtc_time = PCF85063_GetTime();
                    xSemaphoreGive(rtc_mutex);
                    last_minutes = rtc_time.minutes;
                    display_audio_time(rtc_time);

                    display_music_file_list(entries, num, num_selection, page_index, total_pages);
                    time_count = 0;

                    wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
                    continue;
                }
            }
            // ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            Refresh_page_audio(Image_Mono_audio);
            Paint_SelectImage(Image_Mono);
            Refresh_page_audio(Image_Mono);
            time_count = 0;
        } else if (button == 8 || button == 22) { // 返回
            heap_caps_free(entries);
            return -1;
        } else if (button == 12) {
            // ESP_LOGI("home", "Long press to clear all");
            // ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            Forced_refresh_audio(Image_Mono);
            time_count = 0;
        }

        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            display_audio_time(rtc_time);
            Refresh_page_audio(Image_Mono);
        }
    }
}

void page_audio_main(void)
{
    int menu_idx = 0;

    page_audio_int();

    // Create a data cache area for the e-ink screen
    if((Image_Mono_audio = (UBYTE *)heap_caps_malloc(EPD_SIZE_MONO,MALLOC_CAP_SPIRAM)) == NULL) 
    {
        ESP_LOGE(TAG,"Failed to apply for black memory...");
        // return ESP_FAIL;
    }
    Paint_NewImage(Image_Mono_audio, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    int last_minutes = rtc_time.minutes;
    display_audio_init(rtc_time);
    display_audio_option(menu_idx);
    Refresh_page_audio(Image_Mono);

    int button;
    int time_count = 0;
    while (1) {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if(button == -1) time_count++;

        if((time_count > EPD_Sleep_Time)) {
            button = Sleep_wake_audio();
        }

        if (button == 14) { // next
            time_count = 0;
            menu_idx = (menu_idx + 1) % 5;
            display_audio_option(menu_idx);
            Refresh_page_audio(Image_Mono);
        } else if (button == 0) { // last
            time_count = 0;
            menu_idx = (menu_idx - 1 + 5) % 5;
            display_audio_option(menu_idx);
            Refresh_page_audio(Image_Mono);
        } else if (button == 7) {
            time_count = 0;
            if (menu_idx == 0) {
                ESP_LOGI(TAG, "------Play in-memory audio-----");
                // Play in-memory audio
                page_audio_play_memory_1(); // Use the version with key response
            } else if (menu_idx == 1) {
                // Play file - Select file to play
                select_music_file(menu_idx);
            } else if (menu_idx == 2) {
                // Recording control
                page_audio_record_control();
            } else if (menu_idx == 3) {
                // file management
                select_music_file(menu_idx);
            } else if (menu_idx == 4) {
                break;
            }
            display_audio_init(rtc_time);
            display_audio_option(menu_idx);
            Refresh_page_audio(Image_Mono);
        } else if (button == 12 ){
                // ESP_LOGI("home", "Long press to clear all");
                Forced_refresh_audio(Image_Mono);
                time_count = 0;
        } else if (button == 8 || button == 22) {
            // If you are recording, stop recording first
            if (record_state == RECORD_STATE_RECORDING) {
                record_state = RECORD_STATE_STOPPING;
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            break; // quit
        }

        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            display_audio_time(rtc_time);
            Refresh_page_audio(Image_Mono);
        }
    }
}

void page_audio_deinit(void)
{
    audio_player_state_t state = audio_player_get_state();
    if (state != AUDIO_PLAYER_STATE_SHUTDOWN) {
        audio_player_delete();
    }
}

// Recording processing
void write_wav_header(FILE *fp, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t data_size)
{
    wav_header_t header = {
        .riff = {'R', 'I', 'F', 'F'},
        .file_size = data_size + sizeof(wav_header_t) - 8,
        .wave = {'W', 'A', 'V', 'E'},
        .fmt = {'f', 'm', 't', ' '},
        .fmt_size = 16,
        .audio_format = 1, // PCM
        .channels = channels,
        .sample_rate = sample_rate,
        .byte_rate = sample_rate * channels * bits_per_sample / 8,
        .block_align = (uint16_t)(channels * bits_per_sample / 8), // explicit translation
        .bits_per_sample = bits_per_sample,
        .data = {'d', 'a', 't', 'a'},
        .data_size = data_size
    };
    
    fwrite(&header, sizeof(header), 1, fp);
}

// Generate a file name based on the current time - using RTC time
static void generate_record_filename(char *filename, size_t max_len, const char *prefix)
{
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    
    snprintf(filename, max_len, "/sdcard/music/%s_%04d%02d%02d_%02d%02d%02d.wav",
             prefix,
             rtc_time.years,  
             rtc_time.months,
             rtc_time.days,
             rtc_time.hours,
             rtc_time.minutes,
             rtc_time.seconds);
}

// Make sure the "music" directory exists
static esp_err_t ensure_music_directory(void)
{
    struct stat st;
    if (stat("/sdcard/music", &st) != 0) {
        // The directory does not exist. Create it
        if (mkdir("/sdcard/music", 0755) != 0) {
            ESP_LOGE(TAG, "The music directory cannot be created");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "The music directory has been created");
    }
    return ESP_OK;
}


// I2S RX configuration function
static esp_err_t bsp_i2s_reconfig_clk_rx(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    (void)bits_cfg;
    (void)ch;
    if (auto* codec = page_audio_get_codec()) {
        codec->SetInputGain((float)EXAMPLE_MIC_GAIN * 3.0f);
        if (rate != static_cast<uint32_t>(codec->input_sample_rate())) {
            ESP_LOGW(TAG, "Recording rate %lu Hz does not match codec input %d Hz", (unsigned long)rate, codec->input_sample_rate());
        }
    }
    return ESP_OK;
}

// Recording configuration function
static esp_err_t configure_recording_mode(uint32_t sample_rate)
{
    ESP_LOGI(TAG, "Configure the recording mode: %lu Hz", (unsigned long)sample_rate);
    if (auto* codec = page_audio_get_codec()) {
        codec->SetInputGain((float)EXAMPLE_MIC_GAIN * 3.0f);
        if (sample_rate != static_cast<uint32_t>(codec->input_sample_rate())) {
            ESP_LOGW(TAG, "Recording rate %lu Hz does not match codec input %d Hz", (unsigned long)sample_rate, codec->input_sample_rate());
        }
    }
    ESP_LOGI(TAG, "Recording mode configuration completed");
    return ESP_OK;
}

// audio record task
void page_audio_record_task(void *param)
{
    char file_path[128];
    uint32_t sample_rate = 16000;
    uint16_t channels = 1;
    auto* codec = page_audio_get_codec();
    if (codec) {
        sample_rate = static_cast<uint32_t>(codec->input_sample_rate());
        channels = static_cast<uint16_t>(codec->input_channels());
    }
    const uint16_t bits_per_sample = 16;
    ESP_LOGI(TAG, "Record format: %lu Hz, %u ch, %u bits",
             (unsigned long)sample_rate, channels, bits_per_sample);
    
    if (ensure_music_directory() != ESP_OK) {
        record_state = RECORD_STATE_IDLE;
        page_audio_restore_after_recording();
        vTaskDelete(NULL);
        return;
    }
    
    generate_record_filename(file_path, sizeof(file_path), "record");
    ESP_LOGI(TAG, "Path of the audio file: %s", file_path);
    
    esp_err_t ret = configure_recording_mode(sample_rate);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "The recording mode configuration failed");
        record_state = RECORD_STATE_IDLE;
        page_audio_restore_after_recording();
        vTaskDelete(NULL);
        return;
    }
    
    // Wait for the configuration to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));
    
    FILE *fp = fopen(file_path, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "The recording file cannot be created: %s", file_path);
        record_state = RECORD_STATE_IDLE;
        page_audio_restore_after_recording();
        vTaskDelete(NULL);
        return;
    }

    // Use a larger stdio buffer to reduce SDMMC transaction frequency.
    // This lowers the chance of long continuous bus occupation and WDT trips.
    static uint8_t file_io_buffer[RECORD_IO_BUFFER_SIZE];
    setvbuf(fp, (char*)file_io_buffer, _IOFBF, sizeof(file_io_buffer));
    
    write_wav_header(fp, sample_rate, channels, bits_per_sample, 0);
    
    // Recording buffer target: keep bytes per chunk stable across different channel count
    const uint16_t valid_channels = (channels == 0) ? 1 : channels;
    int samples_per_read = (RECORD_CHUNK_BYTES / sizeof(int16_t)) / valid_channels;
    if (samples_per_read <= 0) {
        samples_per_read = 1;
    }
    auto& audio_service = Application::GetInstance().GetAudioService();
    
    ESP_LOGI(TAG, "Start recording... Press the Function key again to stop");
    record_state = RECORD_STATE_RECORDING;

    uint32_t total_bytes = 0;
    const uint32_t byte_rate = sample_rate * valid_channels * bits_per_sample / 8;
    uint32_t last_log_time = 0;

    duration = 0;
    while (record_state != RECORD_STATE_STOPPING) {
        if (record_state == RECORD_STATE_RECORDING) {
            std::vector<int16_t> data;
            if (audio_service.ReadAudioData(data, static_cast<int>(sample_rate), samples_per_read)) {
                if (!data.empty()) {
                    const size_t bytes_to_write = data.size() * sizeof(int16_t);
                    const size_t bytes_written = fwrite(data.data(), 1, bytes_to_write, fp);
                    total_bytes += static_cast<uint32_t>(bytes_written);
                    if (bytes_written != bytes_to_write && ferror(fp)) {
                        ESP_LOGE(TAG, "File write error during recording");
                        record_state = RECORD_STATE_STOPPING;
                    }
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (current_time - last_log_time > 500) {
                duration = (byte_rate > 0) ? (float)total_bytes / byte_rate : 0.0f;
                // ESP_LOGI(TAG, "In the recording... %.1f seconds have been recorded", duration);
                last_log_time = current_time;
            }
            
        } else if (record_state == RECORD_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(100));
            
            static uint32_t last_pause_log = 0;
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (current_time - last_pause_log > 1000) {
                ESP_LOGI(TAG, "The recording has been paused... Press the Function key to continue or the Boot key to stop");
                last_pause_log = current_time;
            }
        }
    }

    // Update the actual data size in the WAV header
    fseek(fp, 0, SEEK_SET);
    write_wav_header(fp, sample_rate, channels, bits_per_sample, total_bytes);
    
    fclose(fp);
    
    page_audio_set_mute(true);
    
    ESP_LOGI(TAG, "The recording is complete. Save it to: %s. Duration: %.1f seconds",
             file_path, (byte_rate > 0) ? (float)total_bytes / byte_rate : 0.0f);
    
    record_state = RECORD_STATE_IDLE;
    page_audio_restore_after_recording();
    vTaskDelete(NULL);
}

void page_audio_record_control(void)
{   
    if (!page_audio_prepare_for_recording()) {
        return;
    }

    int button = 0;
    bool audio_flag = true;
    
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    int last_minutes = rtc_time.minutes;

    char duration_str[]="00:00";
    int duration_int = 0;

    ESP_LOGI(TAG, "Start the recording task");
    // Mark recording active immediately so conversation entry can be blocked
    record_state = RECORD_STATE_RECORDING;
    BaseType_t ret = xTaskCreatePinnedToCore(page_audio_record_task, "audio_record", 8192, NULL, RECORD_TASK_PRIORITY, NULL, RECORD_TASK_CORE);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recording task");
        record_state = RECORD_STATE_IDLE;
        page_audio_restore_after_recording();
        return;
    }

    Paint_DrawRectangle(5, 56, 480, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_CN(159, 336, " 正在录音 ", &Font24_UTF8, BLACK, WHITE);
    Paint_DrawString_CN(184, 700, " 暂停 ", &Font24_UTF8, BLACK, WHITE);

    while (1)
    {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if(button == 7) {
            if(audio_flag) {
                ESP_LOGI(TAG, "suspend record");
                record_state = RECORD_STATE_PAUSED;
                audio_flag = false;

                Paint_DrawRectangle(5, 56, 480, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                Paint_DrawString_CN(159, 336, " 录音暂停 ", &Font24_UTF8, BLACK, WHITE);
                Paint_DrawString_CN(184, 700, " 继续 ", &Font24_UTF8, BLACK, WHITE);
                duration_int = (int)duration;
                duration_str[4] = (duration_int%60)%10 + '0';
                duration_str[3] = (duration_int%60)/10 + '0';
                duration_str[1] = (duration_int/60)%10 + '0';
                duration_str[0] = (duration_int/60)/10 + '0';
                Paint_DrawString_CN(187, 390, duration_str, &Font24_UTF8, WHITE, BLACK);
                Refresh_page_audio(Image_Mono);

            } else {
                ESP_LOGI(TAG, "Continue recording");
                record_state = RECORD_STATE_RECORDING;
                audio_flag = true;

                Paint_DrawRectangle(5, 56, 480, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                Paint_DrawString_CN(159, 336, " 正在录音 ", &Font24_UTF8, BLACK, WHITE);
                Paint_DrawString_CN(184, 700, " 暂停 ", &Font24_UTF8, BLACK, WHITE);
            }
        } else if(button == 8 || button == 22) {
            // Stop recording
            ESP_LOGI(TAG, "Stop recording");
            record_state = RECORD_STATE_STOPPING;
            break;
        }

        if(record_state == RECORD_STATE_RECORDING) {
            duration_int = (int)duration;
            duration_str[4] = (duration_int%60)%10 + '0';
            duration_str[3] = (duration_int%60)/10 + '0';
            duration_str[1] = (duration_int/60)%10 + '0';
            duration_str[0] = (duration_int/60)/10 + '0';
            Paint_DrawString_CN(187, 390, duration_str, &Font24_UTF8, WHITE, BLACK);
            Refresh_page_audio(Image_Mono);
        }

        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            display_audio_time(rtc_time);
            Refresh_page_audio(Image_Mono);
        }
    }

}

bool page_audio_record_for_duration(uint32_t duration_seconds)
{
    if (duration_seconds == 0 || page_audio_is_recording()) {
        return false;
    }
    if (!page_audio_prepare_for_recording()) {
        return false;
    }

    ESP_LOGI(TAG, "Start MCP recording for %lu seconds", (unsigned long)duration_seconds);
    duration = 0;
    record_state = RECORD_STATE_RECORDING;
    BaseType_t ret = xTaskCreatePinnedToCore(page_audio_record_task, "audio_record", 8192, NULL, RECORD_TASK_PRIORITY, NULL, RECORD_TASK_CORE);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recording task");
        record_state = RECORD_STATE_IDLE;
        page_audio_restore_after_recording();
        return false;
    }

    const uint64_t duration_ms = (uint64_t)duration_seconds * 1000ULL;
    const TickType_t timeout_ticks = pdMS_TO_TICKS((duration_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)duration_ms);
    const TickType_t deadline = xTaskGetTickCount() + timeout_ticks;

    while (record_state != RECORD_STATE_IDLE) {
        if ((record_state == RECORD_STATE_RECORDING || record_state == RECORD_STATE_PAUSED) &&
            (xTaskGetTickCount() - deadline) < (TickType_t)0x80000000U) {
            record_state = RECORD_STATE_STOPPING;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return true;
}

// Add a function to list all the recording files in the music directory
void list_music_files(void)
{
    ESP_LOGI(TAG, "=== List of Music directory files ===");
    
    system("ls -la /sdcard/music/*.wav");
    
    ESP_LOGI(TAG, "The file list display is complete");
}





// File Management Menu
int page_audio_file_management(const char* file_path_name)
{
    Paint_SelectImage(Image_Mono_audio);
    Paint_Clear(WHITE);

    const char* menu[] = {
        "播放",
        "文件信息",
        "删除文件",
        "返回"
    };


    int button;
    int time_count = 0;
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    int last_minutes = -1;
    last_minutes = rtc_time.minutes;
    display_audio_time(rtc_time);

    const int menu_count = sizeof(menu)/sizeof(menu[0]);
    int menu_idx = 0;
    display_audio_option_file_management(menu_idx);
    Refresh_page_audio(Image_Mono_audio);
    
    while (1) {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if(button == -1) time_count++;
        if((time_count > EPD_Sleep_Time)) {
            button = Sleep_wake_audio();
        }

        if (button == 14) {
            time_count = 0;
            menu_idx = (menu_idx + 1) % menu_count;
            display_audio_option_file_management(menu_idx);
            Refresh_page_audio(Image_Mono_audio);
        } else if (button == 0) { 
            time_count = 0;
            menu_idx = (menu_idx - 1 + menu_count) % menu_count;
            display_audio_option_file_management(menu_idx);
            Refresh_page_audio(Image_Mono_audio);
        } else if (button == 7) {
            time_count = 0;
            if (menu_idx == 0) {
                page_audio_play_file(file_path_name);
                break;
            } else if (menu_idx == 1) {
                show_file_info(file_path_name);
                break;
            } else if (menu_idx == 2) {
                delete_music_file(file_path_name);
                return 1;
            } else if (menu_idx == 3) {
                break;
            }
        } else if (button == 8 || button == 22) {
            break;
        }

        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            display_audio_time(rtc_time);
            Refresh_page_audio(Image_Mono);
        }        
    }
    return 0;
}

// File deletion function
void delete_music_file(const char* file_path_name)
{
    char file_path[100];
    snprintf(file_path, sizeof(file_path), "/sdcard/music/%s", file_path_name);
    // Get the file name (excluding the path)
    const char *filename = strrchr(file_path, '/');
    if (filename) filename++;
    else filename = file_path;

    // CLEAR
    Paint_DrawRectangle(5, 58, 475, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    // First, check if the file exists
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) {
        // ESP_LOGI(TAG, "Error: The file does not exist or is inaccessible\n");
        Paint_DrawString_CN(10, 60, "错误: 文件不存在或无法访问", &Font24_UTF8, WHITE, BLACK);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return;
    }

    Paint_DrawString_CN(10, 60, "Confirm whether to delete the file", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, 110, "Function key confirmation", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, 160, "The Boot key is cancelled.", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, 210, "5 The deletion operation will be automatically performed in seconds", &Font24_UTF8, WHITE, BLACK);
    Refresh_page_audio(Image_Mono_audio);
    
    // ESP_LOGI(TAG, "确认删除文件: %s ?\n", filename);
    // ESP_LOGI(TAG, "完整路径: %s\n", file_path);
    // ESP_LOGI(TAG, "Function键确认, Boot键取消\n");
    // ESP_LOGI(TAG, "文件大小: %ld 字节\n", file_stat.st_size);
    
    int button;
    int time_count = 5;
    
    while (1)
    {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if(button == -1)  time_count--;
        if (button == 7 || time_count <= 0) {
            Paint_DrawRectangle(5, 58, 475, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawString_CN(10, 60, "开始删除文件...", &Font24_UTF8, WHITE, BLACK);
            Refresh_page_audio(Image_Mono_audio);
            ESP_LOGI(TAG, "Start deleting files...\n");
            
            // Try to delete the file
            int result = unlink(file_path);
            if (result == 0) {
                ESP_LOGI(TAG, "The file was deleted successfully.: %s\n", filename);
                Paint_DrawString_CN(10, 110, "文件删除成功", &Font24_UTF8, WHITE, BLACK);
                Refresh_page_audio(Image_Mono_audio);
                // Verify that the file was indeed deleted
                if (stat(file_path, &file_stat) != 0) {
                    ESP_LOGI(TAG, "Verification: The file has been successfully deleted\n");
                    Paint_DrawString_CN(10, 160, "验证: 文件已成功删除", &Font24_UTF8, WHITE, BLACK);
                    Refresh_page_audio(Image_Mono_audio);
                    break;
                } else {
                    ESP_LOGI(TAG, "Warning: The file may not have been completely deleted\n");
                    Paint_DrawString_CN(10, 160, "警告: 文件可能未完全删除", &Font24_UTF8, WHITE, BLACK);
                    Refresh_page_audio(Image_Mono_audio);
                    break;
                }
            } else {
                ESP_LOGI(TAG, "File deletion failed: %s\n", filename);
                ESP_LOGI(TAG, "error code: %d\n", result);
                ESP_LOGI(TAG, "errno: %d (%s)\n", errno, strerror(errno));

                Paint_DrawRectangle(5, 58, 475, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
                Paint_DrawString_CN(10, 60, "文件删除失败", &Font24_UTF8, WHITE, BLACK);
                
                if (errno == EACCES) {
                    ESP_LOGI(TAG, "insufficient privileges\n");
                    Paint_DrawString_CN(10, 110, "权限不足", &Font24_UTF8, WHITE, BLACK);
                } else if (errno == ENOENT) {
                    ESP_LOGI(TAG, "file does not exist\n");
                    Paint_DrawString_CN(10, 110, "文件不存在", &Font24_UTF8, WHITE, BLACK);
                } else if (errno == EBUSY) {
                    ESP_LOGI(TAG, "The file is in use\n");
                    Paint_DrawString_CN(10, 110, "文件正在使用中", &Font24_UTF8, WHITE, BLACK);
                }
                Refresh_page_audio(Image_Mono_audio);
                break;
            }
        } else if (button == 8 || button == 22) {
            ESP_LOGI(TAG, "undelete\n");
            break;
        }

        char file_path[100];
        snprintf(file_path, sizeof(file_path), "%d The deletion operation will be automatically performed in seconds", time_count);
        Paint_DrawString_CN(10, 210, file_path, &Font24_UTF8, WHITE, BLACK);
        Refresh_page_audio(Image_Mono_audio);
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
}

// Display file information
void show_file_info(const char* file_path_name)
{
    Paint_DrawRectangle(5, 58, 475, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    char file_path[100];
    snprintf(file_path, sizeof(file_path), "/sdcard/music/%s", file_path_name);
    struct stat file_stat;
    if (stat(file_path, &file_stat) == 0) {
        const char *filename = strrchr(file_path, '/');
        if (filename) filename++; // 跳过'/'
        else filename = file_path;

        ESP_LOGI(TAG, "\n=== file information ===\n");
        ESP_LOGI(TAG, "filename: %s\n", filename);
        ESP_LOGI(TAG, "size: %.2f KB (%.2f MB)\n", (float)file_stat.st_size / 1024.0, (float)file_stat.st_size / (1024.0 * 1024.0));
        
        char time_str[64];
        struct tm *timeinfo = localtime(&file_stat.st_mtime);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
        ESP_LOGI(TAG, "modification time: %s\n", time_str);
        

        uint16_t x_or = reassignCoordinates_CH(240, " === 文件信息 === ", &Font24_UTF8);
        Paint_DrawString_CN(x_or, 60, " === 文件信息 === ", &Font24_UTF8, BLACK, WHITE);
        char display_name[100];
        truncate_string_by_width(file_path_name, display_name, sizeof(display_name), 380, &Font18_UTF8);
        char file_str[120];
        snprintf(file_str, sizeof(file_str), "文件名:%s", display_name);
        Paint_DrawString_CN(10, 110, file_str, &Font18_UTF8, WHITE, BLACK);
        snprintf(file_str, sizeof(file_str), "大小:%.2f KB (%.2f MB)", (float)file_stat.st_size / 1024.0, (float)file_stat.st_size / (1024.0 * 1024.0));
        Paint_DrawString_CN(10, 145, file_str, &Font18_UTF8, WHITE, BLACK);
        snprintf(file_str, sizeof(file_str), "修改时间:%s", time_str);
        Paint_DrawString_CN(10, 180, file_str, &Font18_UTF8, WHITE, BLACK);
        
        if (strstr(filename, ".wav") || strstr(filename, ".WAV")) {
            show_wav_file_info(file_path);
        } else if (strstr(filename, ".mp3") || strstr(filename, ".MP3")) {
            show_mp3_file_info(file_path);
        }

        Paint_DrawLine(2, EPD_WIDTH - (Font12_UTF8.Height + 10), EPD_HEIGHT-2, EPD_WIDTH - (Font12_UTF8.Height + 10), BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
        Paint_DrawString_CN(5, EPD_WIDTH - (Font12_UTF8.Height + 5), "双击确认/Boot:返回上级", &Font12_UTF8, WHITE, BLACK);

        Refresh_page_audio(Image_Mono_audio);
        
        ESP_LOGI(TAG, "================\n");
    } else {
        ESP_LOGI(TAG, "The file information cannot be obtained\n");
        Paint_DrawString_CN(10, 60, " 无法获取文件信息 ", &Font24_UTF8, BLACK, WHITE);
    }

    int button;
    int time_count = 0;
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    int last_minutes = -1;
    last_minutes = rtc_time.minutes;
    while(1)
    {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if(button == -1) time_count++;
        if((time_count > EPD_Sleep_Time)) {
            button = Sleep_wake_audio();
        }
        if(button == 8 || button == 22) {
            return;
        } else {
            time_count = 0;
        }

        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            display_audio_time(rtc_time);
            Refresh_page_audio(Image_Mono);
        }
    }
}

// Display the audio information of the WAV file
void show_wav_file_info(const char *file_path)
{
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        ESP_LOGI(TAG, "The WAV file cannot be opened\n");
        return;
    }
    
    wav_header_t header;
    if (fread(&header, sizeof(header), 1, fp) == 1) {
        // Verify whether it is a WAV file
        if (strncmp(header.riff, "RIFF", 4) == 0 && strncmp(header.wave, "WAVE", 4) == 0) {
            ESP_LOGI(TAG, "audio format: %s\n", (header.audio_format == 1) ? "PCM" : "else");
            ESP_LOGI(TAG, "Number of channels: %d\n", header.channels);
            ESP_LOGI(TAG, "sampling frequency: %lu Hz\n", (unsigned long)header.sample_rate);
            ESP_LOGI(TAG, "bit depth: %d bit\n", header.bits_per_sample);
            
            float wav_duration =0.0f;
            if (header.byte_rate > 0) {
                wav_duration = (float)header.data_size / header.byte_rate;
                ESP_LOGI(TAG, "duration: %.1f s\n", wav_duration);
            }

            if(header.audio_format == 1)
                Paint_DrawString_CN(10, 215, "音频格式:PCM", &Font18_UTF8, WHITE, BLACK);
            else
                Paint_DrawString_CN(10, 215, "音频格式:其他", &Font18_UTF8, WHITE, BLACK);

            char file_str[100];
            snprintf(file_str, sizeof(file_str), "声道数:%d", header.channels);
            Paint_DrawString_CN(10, 250, file_str, &Font18_UTF8, WHITE, BLACK);
            snprintf(file_str, sizeof(file_str), "采样率:%ld", (unsigned long)header.sample_rate);
            Paint_DrawString_CN(10, 285, file_str, &Font18_UTF8, WHITE, BLACK);
            snprintf(file_str, sizeof(file_str), "位深度:%d", header.bits_per_sample);
            Paint_DrawString_CN(10, 320, file_str, &Font18_UTF8, WHITE, BLACK);
            snprintf(file_str, sizeof(file_str), "时长:%.1fs", wav_duration);
            Paint_DrawString_CN(10, 355, file_str, &Font18_UTF8, WHITE, BLACK);
        } else {
            ESP_LOGI(TAG, "File format: Non-standard WAV format\n");
            Paint_DrawString_CN(10, 215, " 文件格式: 非标准WAV格式 ", &Font18_UTF8, BLACK, WHITE);
        }
    }
    fclose(fp);
}

// MP3 Bit Rate Table (kbps)
static const int mp3_bitrate_table[16][5] = {
    // MPEG1 Layer1, Layer2, Layer3, MPEG2 Layer1, MPEG2 Layer2&3
    {0, 0, 0, 0, 0},
    {32, 32, 32, 32, 8},
    {64, 48, 40, 48, 16},
    {96, 56, 48, 56, 24},
    {128, 64, 56, 64, 32},
    {160, 80, 64, 80, 40},
    {192, 96, 80, 96, 48},
    {224, 112, 96, 112, 56},
    {256, 128, 112, 128, 64},
    {288, 160, 128, 144, 80},
    {320, 192, 160, 160, 96},
    {352, 224, 192, 176, 112},
    {384, 256, 224, 192, 128},
    {416, 320, 256, 224, 144},
    {448, 384, 320, 256, 160},
    {0, 0, 0, 0, 0} // 禁用
};

// MP3 Sampling Rate Table (Hz)
static const int mp3_samplerate_table[4][3] = {
    // MPEG1, MPEG2, MPEG2.5
    {44100, 22050, 11025},
    {48000, 24000, 12000},
    {32000, 16000, 8000},
    {0, 0, 0} // 保留
};

// Parse the tag size of ID3v2 (synchsafe integer）
static uint32_t parse_synchsafe_int(uint8_t *data)
{
    return (data[0] << 21) | (data[1] << 14) | (data[2] << 7) | data[3];
}

// Search for the MP3 frame header
static long find_mp3_frame(FILE *fp)
{
    uint8_t buffer[4];
    long pos = 0;
    
    // Start looking for the synchronization word 0xFF Fx from the current position
    while (fread(buffer, 1, 2, fp) == 2) {
        if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0) {
            // Find the possible frame header and read the complete 4 bytes
            if (fread(&buffer[2], 1, 2, fp) == 2) {
                // Verify whether it is a valid MP3 frame header
                uint32_t header = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
                
                // Check the validity of fields such as version, layer, and bit rate
                uint8_t version = (header >> 19) & 0x03;
                uint8_t layer = (header >> 17) & 0x03;
                uint8_t bitrate = (header >> 12) & 0x0F;
                uint8_t samplerate = (header >> 10) & 0x03;
                
                if (version != 1 && layer != 0 && bitrate != 0 && bitrate != 15 && samplerate != 3) {
                    // Find a valid frame header and roll back 4 bytes
                    fseek(fp, pos, SEEK_SET);
                    return pos;
                }
            }
        }
        pos++;
        fseek(fp, pos, SEEK_SET);
    }
    return -1;
}

// Parse the MP3 frame header information
static bool parse_mp3_frame_header(uint32_t header, int *bitrate, int *samplerate, int *channels)
{
    uint8_t version = (header >> 19) & 0x03;
    uint8_t layer = (header >> 17) & 0x03;
    uint8_t bitrate_idx = (header >> 12) & 0x0F;
    uint8_t samplerate_idx = (header >> 10) & 0x03;
    uint8_t channel_mode = (header >> 6) & 0x03;
    
    // verify the validity
    if (version == 1 || layer == 0 || bitrate_idx == 0 || bitrate_idx == 15 || samplerate_idx == 3) {
        return false;
    }
    
    // Obtain bit rate
    int version_idx = (version == 3) ? 0 : 1; // MPEG1 : MPEG2/2.5
    int layer_idx = (layer == 1) ? 0 : (layer == 2) ? 1 : 2; // Layer1/2/3
    if (version_idx == 1 && layer_idx > 0) layer_idx = 1; // MPEG2 Layer2&3共用
    
    *bitrate = mp3_bitrate_table[bitrate_idx][version_idx * 3 + layer_idx];
    
    // Obtain the sampling rate
    int version_col = (version == 3) ? 0 : (version == 2) ? 1 : 2; // MPEG1/2/2.5
    *samplerate = mp3_samplerate_table[samplerate_idx][version_col];
    
    // Get the number of channels
    *channels = (channel_mode == 3) ? 1 : 2; // 单声道 : 立体声
    
    return (*bitrate > 0 && *samplerate > 0);
}

// Read the ID3v1 tag
static bool read_id3v1_tag(FILE *fp, char *title, char *artist, size_t max_len)
{
    id3v1_tag_t tag;
    
    // Move to 128 bytes before the end of the file
    if (fseek(fp, -128, SEEK_END) != 0) {
        return false;
    }
    
    if (fread(&tag, sizeof(tag), 1, fp) != 1) {
        return false;
    }
    
    // Check the TAG identification
    if (strncmp(tag.tag, "TAG", 3) != 0) {
        return false;
    }
    
    // Copy the information (make sure the string ends)
    if (title) {
        strncpy(title, tag.title, max_len - 1);
        title[max_len - 1] = '\0';
        for (int i = strlen(title) - 1; i >= 0 && title[i] == ' '; i--) {
            title[i] = '\0';
        }
    }
    
    if (artist) {
        strncpy(artist, tag.artist, max_len - 1);
        artist[max_len - 1] = '\0';
        for (int i = strlen(artist) - 1; i >= 0 && artist[i] == ' '; i--) {
            artist[i] = '\0';
        }
    }
    return true;
}

// Display the information of MP3 files
void show_mp3_file_info(const char *file_path)
{
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        ESP_LOGI(TAG, "The MP3 file cannot be opened\n");
        return;
    }
    
    id3v2_header_t id3v2;
    uint32_t id3v2_size = 0;
    
    if (fread(&id3v2, sizeof(id3v2), 1, fp) == 1) {
        if (strncmp(id3v2.identifier, "ID3", 3) == 0) {
            id3v2_size = parse_synchsafe_int(id3v2.size) + 10; 
            ESP_LOGI(TAG, "Check the ID3v2 tag for the ID3v2 version: 2.%d.%d\n", id3v2.version[0], id3v2.version[1]);
            char file_str[100];
            snprintf(file_str, sizeof(file_str), "ID3v2版本:2.%d.%d", id3v2.version[0], id3v2.version[1]);
            Paint_DrawString_CN(10, 390, file_str, &Font18_UTF8, WHITE, BLACK);
        }
    }
    
    // Skip the ID3v2 tag and look for the MP3 frame header
    fseek(fp, id3v2_size, SEEK_SET);
    long frame_pos = find_mp3_frame(fp);
    
    if (frame_pos >= 0) {
        fseek(fp, frame_pos, SEEK_SET);
        uint32_t frame_header;
        
        if (fread(&frame_header, 4, 1, fp) == 1) {
            frame_header = __builtin_bswap32(frame_header);
            
            int bitrate, samplerate, channels;
            if (parse_mp3_frame_header(frame_header, &bitrate, &samplerate, &channels)) {
                ESP_LOGI(TAG, "audio format: MP3\n");
                ESP_LOGI(TAG, "bit rate: %d kbps\n", bitrate);
                ESP_LOGI(TAG, "sampling frequency: %d Hz\n", samplerate);
                ESP_LOGI(TAG, "Number of channels: %d\n", channels);
            
                float duration = 0.0f;
                struct stat file_stat;
                if (stat(file_path, &file_stat) == 0) {
                    long audio_size = file_stat.st_size - id3v2_size;
                    
                    fseek(fp, -128, SEEK_END);
                    char check_tag[3];
                    if (fread(check_tag, 3, 1, fp) == 1 && strncmp(check_tag, "TAG", 3) == 0) {
                        audio_size -= 128; //
                    }
                    
                    if (bitrate > 0) {
                        duration = (float)(audio_size * 8) / (bitrate * 1000);
                        ESP_LOGI(TAG, "Estimated duration: %.1f s\n", duration);
                    }
                }

                Paint_DrawString_CN(10, 215, "音频格式:MP3", &Font18_UTF8, WHITE, BLACK);
                char file_str[100];
                snprintf(file_str, sizeof(file_str), "声道数:%d", channels);
                Paint_DrawString_CN(10, 250, file_str, &Font18_UTF8, WHITE, BLACK);
                snprintf(file_str, sizeof(file_str), "采样率:%d", samplerate);
                Paint_DrawString_CN(10, 285, file_str, &Font18_UTF8, WHITE, BLACK);
                snprintf(file_str, sizeof(file_str), "比特率:%d", bitrate);
                Paint_DrawString_CN(10, 320, file_str, &Font18_UTF8, WHITE, BLACK);
                snprintf(file_str, sizeof(file_str), "时长:%.1fs", duration);
                Paint_DrawString_CN(10, 355, file_str, &Font18_UTF8, WHITE, BLACK);

            } else {
                ESP_LOGI(TAG, "The MP3 format parsing failed\n");
                Paint_DrawString_CN(10, 215, " MP3格式解析失败 ", &Font18_UTF8, BLACK, WHITE);
            }
        }
    } else {
        ESP_LOGI(TAG, "No valid MP3 frame header was found\n");
        Paint_DrawString_CN(10, 215, " 未找到有效的MP3帧头 ", &Font18_UTF8, BLACK, WHITE);
    }
    
    char title[64] = {0};
    char artist[64] = {0};
    if (read_id3v1_tag(fp, title, artist, sizeof(title))) {
        if (strlen(title) > 0) {
            ESP_LOGI(TAG, "headline: %s\n", title);
            char file_str[100];
            snprintf(file_str, sizeof(file_str), "标题: %s", title);
            Paint_DrawString_CN(10, 425, file_str, &Font18_UTF8, WHITE, BLACK);
        }
        if (strlen(artist) > 0) {
            ESP_LOGI(TAG, "artist: %s\n", artist);
            char file_str[100];
            snprintf(file_str, sizeof(file_str), "艺术家: %s", artist);
            Paint_DrawString_CN(10, 460, file_str, &Font18_UTF8, WHITE, BLACK);
        }
    }
    fclose(fp);
}


static void display_audio_init(Time_data rtc_time)
{
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    char Time_str[16]={0};
    int BAT_Power;
    char BAT_Power_str[16]={0};

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    DrawWifiStatusIcon(326, 8);
    Paint_ReadBmp(gImage_BAT, 370, 17, 32, 16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    DrawWifiStatusIcon(326, 8);
    GUI_ReadBmp(BMP_BAT_PATH, 370, 17);
#endif
    BAT_Power = get_battery_power();
    // ESP_LOGI("BAT_Power", "BAT_Power = %d%%",BAT_Power);
    snprintf(BAT_Power_str, sizeof(BAT_Power_str), "%d%%", BAT_Power);
    if(BAT_Power == -1) BAT_Power = 20;
    else BAT_Power = BAT_Power * 20 / 100;
    Paint_DrawString_EN(411, 11, BAT_Power_str, &Font16, WHITE, BLACK);
    Paint_DrawRectangle(375, 22, 395, 30, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(375, 22, 375+BAT_Power, 30, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    Paint_DrawLine(2, 54, EPD_HEIGHT-2, 54, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

    // Paint_DrawRectangle(5, 58, 475, 103, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_CN(10, 60, "播放内置音频", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, 110, "播放TF卡音频", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, 160, "录制音频", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, 210, "文件管理", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, 260, "返回主菜单", &Font24_UTF8, WHITE, BLACK);

    Paint_DrawLine(5, EPD_WIDTH - (Font12_UTF8.Height + 10), EPD_HEIGHT-5, EPD_WIDTH - (Font12_UTF8.Height + 10), BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_CN(5, EPD_WIDTH - (Font12_UTF8.Height + 5), "↑↓:选择,单击确认:打开,双击确认:返回上级", &Font12_UTF8, WHITE, BLACK);
}

static void display_audio_time_last(Time_data rtc_time)
{
    char Time_str[20]={0};
    int hours = rtc_time.hours;
    int minutes = rtc_time.minutes-1;

    if(minutes < 0){
        minutes = 59;
        hours = rtc_time.hours - 1;
        if(hours < 0 )
            hours = 23;
    }

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", hours, minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);
}

static void display_audio_time(Time_data rtc_time)
{
    char Time_str[16]={0};
    int BAT_Power;
    char BAT_Power_str[16]={0};

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    DrawWifiStatusIcon(326, 8);
    Paint_ReadBmp(gImage_BAT, 370, 17, 32, 16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    DrawWifiStatusIcon(326, 8);
    GUI_ReadBmp(BMP_BAT_PATH, 370, 17);
#endif
    BAT_Power = get_battery_power();
    // ESP_LOGI("BAT_Power", "BAT_Power = %d%%",BAT_Power);
    snprintf(BAT_Power_str, sizeof(BAT_Power_str), "%d%%", BAT_Power);
    if(BAT_Power == -1) BAT_Power = 20;
    else BAT_Power = BAT_Power * 20 / 100;
    Paint_DrawString_EN(411, 11, BAT_Power_str, &Font16, WHITE, BLACK);
    Paint_DrawRectangle(375, 22, 395, 30, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(375, 22, 375+BAT_Power, 30, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    Paint_DrawLine(2, 54, EPD_HEIGHT-2, 54, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

    // EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
}


static void display_audio_option(int count)
{
    if(count == 0){
        Paint_DrawRectangle(5, 108, 475, 153, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 110, "播放TF卡音频", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawRectangle(5, 258, 475, 303, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 260, "返回主菜单", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawRectangle(5, 58, 475, 103, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 60, "播放内置音频", &Font24_UTF8, BLACK, WHITE);
    } else if(count == 1) {
        Paint_DrawRectangle(5, 58, 475, 103, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 60, "播放内置音频", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawRectangle(5, 108, 475, 153, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 110, "播放TF卡音频", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawRectangle(5, 158, 475, 203, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 160, "录制音频", &Font24_UTF8, WHITE, BLACK);
    } else if(count == 2) {
        Paint_DrawRectangle(5, 108, 475, 153, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 110, "播放TF卡音频", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawRectangle(5, 158, 475, 203, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 160, "录制音频", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawRectangle(5, 208, 475, 253, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 210, "文件管理", &Font24_UTF8, WHITE, BLACK);
    } else if(count == 3) {
        Paint_DrawRectangle(5, 158, 475, 203, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 160, "录制音频", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawRectangle(5, 208, 475, 253, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 210, "文件管理", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawRectangle(5, 258, 475, 303, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 260, "返回主菜单", &Font24_UTF8, WHITE, BLACK);
    } else if(count == 4) {
        Paint_DrawRectangle(5, 58, 475, 103, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 60, "播放内置音频", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawRectangle(5, 208, 475, 253, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 210, "文件管理", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawRectangle(5, 258, 475, 303, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 260, "返回主菜单", &Font24_UTF8, BLACK, WHITE);
    }

    Paint_DrawLine(5, EPD_WIDTH - (Font12_UTF8.Height + 10), EPD_HEIGHT-5, EPD_WIDTH - (Font12_UTF8.Height + 10), BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_CN(5, EPD_WIDTH - (Font12_UTF8.Height + 5), "↑↓:选择,单击确认:打开,双击确认:返回上级", &Font12_UTF8, WHITE, BLACK);
}

static void display_audio_option_file_management(int count)
{
    Paint_DrawRectangle(5, 58, 475, 799, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    if(count == 0){
        Paint_DrawRectangle(5, 58, 475, 103, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 60, "播放", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawString_CN(10, 110, "文件信息", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 160, "删除文件", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 210, "返回", &Font24_UTF8, WHITE, BLACK);
    } else if(count == 1) {
        Paint_DrawRectangle(5, 108, 475, 153, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 60, "播放", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 110, "文件信息", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawString_CN(10, 160, "删除文件", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 210, "返回", &Font24_UTF8, WHITE, BLACK);
    } else if(count == 2) {
        Paint_DrawRectangle(5, 158, 475, 203, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 60, "播放", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 110, "文件信息", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 160, "删除文件", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawString_CN(10, 210, "返回", &Font24_UTF8, WHITE, BLACK);
    } else if(count == 3) {
        Paint_DrawRectangle(5, 208, 475, 253, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(10, 60, "播放", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 110, "文件信息", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 160, "删除文件", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(10, 210, "返回", &Font24_UTF8, BLACK, WHITE);
    }

    Paint_DrawLine(5, EPD_WIDTH - (Font12_UTF8.Height + 10), EPD_HEIGHT-5, EPD_WIDTH - (Font12_UTF8.Height + 10), BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_CN(5, EPD_WIDTH - (Font12_UTF8.Height + 5), "↑↓:选择,单击确认:打开,双击确认:返回上级", &Font12_UTF8, WHITE, BLACK);
}

static void Forced_refresh_audio(uint8_t *EDP_buffer)
{
    EPD_Display_Base(EDP_buffer);
}
static void Refresh_page_audio(uint8_t *EDP_buffer)
{
    EPD_Display_Partial(EDP_buffer,0,0,EPD_WIDTH,EPD_HEIGHT);
}
static int Sleep_wake_audio(void)
{
    int button = 0;
    int sleep_js = 0;
    Time_data rtc_time = {0};
    int last_minutes = -1;
    ESP_LOGI("home", "EPD_Sleep");
    EPD_Sleep();
    while(1)
    {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == 12){
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            break;
        } else if (button == 8 || button == 22 || button == 14 || button == 0 || button == 7){
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            Refresh_page_audio(Image_Mono);
            break;
        } 
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            Refresh_page_audio(Image_Mono);
            display_audio_time(rtc_time);
            Refresh_page_audio(Image_Mono);
            ESP_LOGI("home", "EPD_Sleep");
            EPD_Sleep();
            // sleep_js++;
            // if(sleep_js > Unattended_Time){
            //     ESP_LOGI("home", "pwr_off");
            //     axp_pwr_off();
            // }
        }
    }
    return button;
}



static const char* get_file_type(const char* filename)
{
    const char* ext = strrchr(filename, '.');
    if (!ext) return "unknown";
    
    if (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".mp3") == 0) {
        return "audio";
    }
    return "unknown";
}

static int calculate_display_width(const char* str)
{
    int width = 0;
    const char* p = str;
    
    while (*p) {
        if ((*p & 0x80) == 0) {
            width += 9;
            p++;
        } else {
            width += 18;
            if ((*p & 0xE0) == 0xC0) p += 2;
            else if ((*p & 0xF0) == 0xE0) p += 3;
            else if ((*p & 0xF8) == 0xF0) p += 4;
            else p++;
        }
    }
    return width;
}

static void truncate_string_by_width(const char* source, char* dest, int dest_size, int max_width, cFONT *Font) {
    int current_width = 0;
    int i = 0;
    int dest_pos = 0;
    
    int ellipsis_width = Font->Width_EN * 3; 
    int available_width = max_width - ellipsis_width; 
    
    while (source[i] != '\0' && dest_pos < dest_size - 4) { 
        unsigned char ch = (unsigned char)source[i];
        int char_width = 0;
        int char_bytes = 1;
        
        if (ch < 0x80) {
            char_width = Font->Width_EN;
            char_bytes = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            char_width = Font->Width_CH;
            char_bytes = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            char_width = Font->Width_CH;
            char_bytes = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            char_width = Font->Width_CH;
            char_bytes = 4;
        } else {
            char_width = Font->Width_EN;
            char_bytes = 1;
        }
        
        if (current_width + char_width > available_width) {
            break;
        }
        for (int j = 0; j < char_bytes && source[i + j] != '\0' && dest_pos < dest_size - 4; j++) {
            dest[dest_pos++] = source[i + j];
        }
        
        current_width += char_width;
        i += char_bytes;
    }

    if (source[i] != '\0') {
        dest[dest_pos++] = '.';
        dest[dest_pos++] = '.';
        dest[dest_pos++] = '.';
    }
    
    dest[dest_pos] = '\0';
}

static bool is_chinese_filename(const char* filename)
{
    for (int i = 0; filename[i]; i++) {
        if ((unsigned char)filename[i] > 127) {
            return true; 
        }
    }
    return false;
}

static void display_music_file_list(file_entry_t* entries, int num_entries, int current_selection, int page_index, int total_pages)
{
    Paint_DrawRectangle(5, 58, 475, 800, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    
    int start_y = HEADER_HEIGHT;
    int max_display = get_audio_page_size();

    int icon_width = GUI_WIDTH + 5;
    int available_width = 480 - 10 - icon_width - 20;

    for (int i = 0; i < num_entries && i < max_display; i++) {
        int y_pos = start_y + i * (Font18_UTF8.Height + 10 );
        
        const char* file_type = get_file_type(entries[i].name);
        if (strcmp(file_type, "audio") == 0) {
            #if defined(CONFIG_IMG_SOURCE_EMBEDDED)
                Paint_ReadBmp(gImage_audio,10,y_pos-1,32,32);
            #elif defined(CONFIG_IMG_SOURCE_TFCARD)
                GUI_ReadBmp(BMP_AUDIO_PATH,10,y_pos-1);
            #endif
        } else {
            #if defined(CONFIG_IMG_SOURCE_EMBEDDED)
                Paint_ReadBmp(gImage_rests,10,y_pos-1,32,32);
            #elif defined(CONFIG_IMG_SOURCE_TFCARD)
                GUI_ReadBmp(BMP_RESTS_PATH,10,y_pos-1);
            #endif
        }
        
        char display_name[100]; 
        truncate_string_by_width(entries[i].name, display_name, sizeof(display_name), available_width, &Font18_UTF8);
        
        if (is_chinese_filename(entries[i].name)) {
            Paint_DrawString_CN(10 + icon_width, y_pos, display_name, &Font18_UTF8, WHITE, BLACK);
        } else {
            Paint_DrawString_EN(10 + icon_width, y_pos, display_name, &Font16, WHITE, BLACK);
        }

        if (i == current_selection) {
            Paint_DrawRectangle(5, y_pos - 5, 475, y_pos + Font18_UTF8.Height + 5, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
        }
    }
    
    int status_y = EPD_WIDTH - (Font12_UTF8.Height*2+15);
    Paint_DrawLine(10, status_y - 5, EPD_HEIGHT - 10, status_y - 5, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    
    char page_info[50];
    snprintf(page_info, sizeof(page_info), "第%d/%d页 共%d个文件", page_index + 1, total_pages, num_entries);
    Paint_DrawString_CN(10, EPD_WIDTH-(Font12_UTF8.Height*2 + 10), page_info, &Font12_UTF8, WHITE, BLACK);
    
    Paint_DrawString_CN(10, EPD_WIDTH - (Font12_UTF8.Height + 5), "↑↓:选择,单击确认:播放,双击确认:返回上级", &Font12_UTF8, WHITE, BLACK);

    EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
}

// Update the selected items 
static void update_music_selection(int new_selected)
{
    static int last_selected = 0;
    
    if (last_selected == new_selected) return;
    
    int y_start = HEADER_HEIGHT;
    int item_height = Font18_UTF8.Height + 10;
    
    // Clear the old checkbox
    if (last_selected >= 0) {
        int old_y = y_start + last_selected * item_height;
        Paint_DrawRectangle(5, old_y - 5, 475, old_y + Font18_UTF8.Height + 5, WHITE, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    }
    
    // Draw a new selected box
    int new_y = y_start + new_selected * item_height;
    Paint_DrawRectangle(5, new_y - 5, 475, new_y + Font18_UTF8.Height + 5, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    
    last_selected = new_selected;
    
    Refresh_page_audio(Image_Mono);
}

static void truncate_filename_for_display(const char* filename, char* display_name, int max_len, int max_width)
{
    int filename_width = calculate_display_width(filename);
    
    if (filename_width <= max_width) {
        strncpy(display_name, filename, max_len - 1);
        display_name[max_len - 1] = '\0';
    } else {
        const char* ext = strrchr(filename, '.');
        int ext_len = ext ? strlen(ext) : 0;
        int available_len = max_len - ext_len - 4;
        
        strncpy(display_name, filename, available_len);
        display_name[available_len] = '\0';
        strcat(display_name, "...");
        if (ext) strcat(display_name, ext);
    }
}

static void display_message(const char* message)
{
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);
    uint16_t msg_x = reassignCoordinates_CH(EPD_WIDTH/2, message, &Font24_UTF8);
    int msg_y = EPD_HEIGHT/2 - Font24_UTF8.Height;
    
    Paint_DrawString_CN(msg_x, msg_y, message, &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(msg_x, msg_y + Font24_UTF8.Height + 10, "按任意键继续...", &Font16_UTF8, WHITE, BLACK);
    
    EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    
    wait_key_event_and_return_code(portMAX_DELAY);
}


