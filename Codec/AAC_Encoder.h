#ifndef AAC_ENCODER_H
#define AAC_ENCODER_H
#include "AV_Common.h"
class AudioEncoder;
class AACEncoder
{
public:
    AACEncoder();
    AACEncoder(AACEncoder const&) = delete;
    AACEncoder& operator=(AACEncoder const&) = delete;
    ~AACEncoder();

public:
    bool Open(int samplerate, int channels, int format, int bitrate_kbps);
    void Close();
    int GetFrames();
    int GetSpecificConfig(uint8_t* buf, int max_buf_size);
    AVPacketPtr Encode(uint8_t const* pcm, int samples);
    inline int GetChannel()
    {
        return channel_;
    }
    inline int GetSamplerate()
    {
        return samplerate_;
    }

private:
    int channel_ = 0;
    int bitrate_ = 0;
    int samplerate_ = 0;
    AVSampleFormat format_ = AV_SAMPLE_FMT_NONE;
    std::unique_ptr<AudioEncoder> AAC_encoder_;
};
#endif  // AAC_ENCODER_H
