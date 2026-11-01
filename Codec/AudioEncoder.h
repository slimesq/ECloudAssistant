#ifndef AUDIO_ENCODER_H
#define AUDIO_ENCODER_H
#include "AV_Common.h"
class AudioResampler;
class AudioEncoder : public EncodBase
{
public:
    AudioEncoder();
    AudioEncoder(AudioEncoder const&) = delete;
    AudioEncoder& operator=(AudioEncoder const&) = delete;
    ~AudioEncoder();

public:
    virtual bool Open(AVConfig& config) override;
    virtual void Close() override;
    uint32_t GetFrameSamples();
    AVPacketPtr Encode(uint8_t const* pcm, int samples);

private:
    int64_t pts_ = 0;
    std::unique_ptr<AudioResampler> audio_resampler_;
};

#endif  // AUDIO_ENCODER_H
