#ifndef AUDIO_RENDER_H
#define AUDIO_RENDER_H
#include <QAudioOutput>
#include <QAudioFormat>
#include <QIODevice>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioSink>
#endif
#include "AV_Common.h"

class AudioRender
{
public:
    AudioRender();
    ~AudioRender();
    inline bool IsInit()
    {
        return is_initail_;
    }
    //获取缓冲区pcm大小
    int AvailableBytes();
    bool InitAudio(int nChannels, int SampleRate, int nSampleSize);
    //播放音频
    void Write(AVFramePtr frame);

private:
    bool is_initail_ = false;
    int nSampleSize_ = -1;
    int volume_ = 50;
    //接入设备
    QAudioFormat audioFmt_;
    QIODevice* device_ = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioSink* audioOut_ = nullptr;
#else
    QAudioOutput* audioOut_ = nullptr;
#endif
};

#endif
