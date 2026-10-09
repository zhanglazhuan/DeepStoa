#ifndef _ES8311_BSP_AUDIO_CODEC_H
#define _ES8311_BSP_AUDIO_CODEC_H

#include "audio_codec.h"

class Es8311BspAudioCodec : public AudioCodec {
public:
    static Es8311BspAudioCodec& GetInstance();

    void SetOutputVolume(int volume) override;
    void SetInputGain(float gain) override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;
    void Start() override;

private:
    Es8311BspAudioCodec();
    ~Es8311BspAudioCodec() override = default;

    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;
    void EnsureInitialized();

    bool initialized_ = false;
};

#endif // _ES8311_BSP_AUDIO_CODEC_H
