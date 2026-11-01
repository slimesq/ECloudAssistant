#include "AudioRender.h"

AudioRender::AudioRender()
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    audioFmt_.setCodec("audio/pcm");
    audioFmt_.setByteOrder(QAudioFormat::LittleEndian);
    audioFmt_.setSampleType(QAudioFormat::SignedInt);
#endif
}

AudioRender::~AudioRender()
{
}

int AudioRender::AvailableBytes()
{
    //获取pcm大小
    if (!audioOut_)
    {
        return -1;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return audioOut_->bytesFree();
#else
    return audioOut_->bytesFree() - audioOut_->periodSize();
#endif
}

bool AudioRender::InitAudio(int nChannels, int SampleRate, int nSampleSize)
{
    //初始化音频输出
    if (audioOut_ || is_initail_ || device_)
    {
        return true;
    }

    nSampleSize_ = nSampleSize;

    //设置格式
    audioFmt_.setChannelCount(nChannels);
    audioFmt_.setSampleRate(SampleRate);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    audioFmt_.setSampleFormat(nSampleSize == 16 ? QAudioFormat::Int16 : QAudioFormat::UInt8);
#else
    audioFmt_.setSampleSize(nSampleSize);
#endif

//创建输出
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    audioOut_ = new QAudioSink(audioFmt_);
#else
    audioOut_ = new QAudioOutput(audioFmt_);
#endif
    //设置缓冲区大小
    audioOut_->setBufferSize(409600);
    //设置音量
    audioOut_->setVolume(volume_ / 100.0);
    //创建接入设备
    device_ = audioOut_->start();
    is_initail_ = true;
    return true;
}

void AudioRender::Write(AVFramePtr frame)
{
    //播放音频
    if (device_ && audioOut_ && nSampleSize_ != -1)
    {
        //获取frame数据
        QByteArray audioData(
            reinterpret_cast<char*>(frame->data[0]),
            (frame->nb_samples * frame->ch_layout.nb_channels) * (nSampleSize_ / 8));
        //开始播放
        device_->write(audioData.data(), audioData.size());
        //释放这个frame
        av_frame_unref(frame.get());
    }
}
