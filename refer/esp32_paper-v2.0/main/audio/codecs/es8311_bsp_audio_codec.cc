#include "es8311_bsp_audio_codec.h"

#include <esp_log.h>

#include "es8311_bsp.h"
#include "settings.h"

#define TAG "Es8311BspCodec"

Es8311BspAudioCodec& Es8311BspAudioCodec::GetInstance() {
    static Es8311BspAudioCodec instance;
    return instance;
}

Es8311BspAudioCodec::Es8311BspAudioCodec() {
    duplex_ = true;
    input_reference_ = false;
    input_channels_ = 1;
    output_channels_ = 1;
    input_sample_rate_ = EXAMPLE_SAMPLE_RATE;
    output_sample_rate_ = EXAMPLE_SAMPLE_RATE;
    output_volume_ = EXAMPLE_VOICE_VOLUME;
    input_gain_ = EXAMPLE_MIC_GAIN * 3.0f;

    EnsureInitialized();
}

void Es8311BspAudioCodec::EnsureInitialized() {
    if (initialized_) {
        return;
    }

    esp_err_t ret = i2s_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = es8311_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "es8311_codec_init failed: %s", esp_err_to_name(ret));
        return;
    }

    if (record_dev_handle) {
        esp_codec_dev_set_in_gain(record_dev_handle, input_gain_);
    }

    initialized_ = true;
}

void Es8311BspAudioCodec::Start() {
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);
    if (output_volume_ <= 0) {
        ESP_LOGW(TAG, "Output volume value (%d) is too small, setting to default (10)", output_volume_);
        output_volume_ = 10;
    }

    EnableInput(true);
    EnableOutput(true);
    ESP_LOGI(TAG, "ES8311 BSP audio codec started");
}

void Es8311BspAudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    if (play_dev_handle) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_vol(play_dev_handle, output_volume_));
        if (output_volume_ == 0) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_mute(play_dev_handle, true));
        } else if (output_enabled_) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_mute(play_dev_handle, false));
        }
    }
    AudioCodec::SetOutputVolume(volume);
}

void Es8311BspAudioCodec::SetInputGain(float gain) {
    input_gain_ = gain;
    if (record_dev_handle && input_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_in_gain(record_dev_handle, input_gain_));
    }
    AudioCodec::SetInputGain(gain);
}

void Es8311BspAudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    AudioCodec::EnableInput(enable);
    if (record_dev_handle) {
        float gain = enable ? input_gain_ : 0.0f;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_in_gain(record_dev_handle, gain));
    }
}

void Es8311BspAudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    AudioCodec::EnableOutput(enable);
    if (play_dev_handle) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_mute(play_dev_handle, !enable));
        if (enable) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_vol(play_dev_handle, output_volume_));
        }
    }
}

int Es8311BspAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_ || !record_dev_handle) {
        return 0;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_read(record_dev_handle, (void*)dest, samples * sizeof(int16_t)));
    return samples;
}

int Es8311BspAudioCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_ || !play_dev_handle) {
        return 0;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(play_dev_handle, (void*)data, samples * sizeof(int16_t)));
    return samples;
}
