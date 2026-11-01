#include "Audio_Resampler.h"

extern "C"
{
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

AudioResampler::AudioResampler() : swr_context_(nullptr)
{
}

AudioResampler::~AudioResampler()
{
    Close();
}

void AudioResampler::Close()
{
    if (swr_context_)
    {
        if (swr_is_initialized(swr_context_))
        {
            swr_close(swr_context_);
            swr_context_ = nullptr;
        }
    }
}

int AudioResampler::Convert(AVFramePtr in_frame, AVFramePtr& out_frame)
{
    //重采样
    if (!swr_context_)
    {
        return -1;
    }

    //更新输出参数
    out_frame.reset(av_frame_alloc(), [](AVFrame* ptr) { av_frame_free(&ptr); });
    out_frame->sample_rate = out_samplerate_;
    out_frame->format = out_format_;
    av_channel_layout_default(&out_frame->ch_layout, out_channels_);
    int64_t delay = swr_get_delay(swr_context_, in_frame->sample_rate);
    out_frame->nb_samples = av_rescale_rnd(
        delay + in_frame->nb_samples, out_samplerate_, in_frame->sample_rate, AV_ROUND_UP);
    out_frame->pts = out_frame->pkt_dts = in_frame->pts;

    //获取内存
    if (av_frame_get_buffer(out_frame.get(), 0) != 0)
    {
        return -1;
    }

    //开始重采样
    int len = swr_convert(swr_context_,
                          (uint8_t**)&out_frame->data,
                          out_frame->nb_samples,
                          (uint8_t const**)in_frame->data,
                          in_frame->nb_samples);
    if (len < 0)
    {
        //清空内存
        out_frame.reset();
        out_frame = nullptr;
        return -1;
    }
    //更新实际样品数
    out_frame->nb_samples = len;
    return len;
}

bool AudioResampler::Open(int in_samplerate,
                          int in_channels,
                          AVSampleFormat in_format,
                          int out_samplerate,
                          int out_channels,
                          AVSampleFormat out_format)
{
    if (swr_context_)
    {
        return false;
    }

    //初始化转换器
    AVChannelLayout in_layout;
    AVChannelLayout out_layout;
    av_channel_layout_default(&in_layout, in_channels);
    av_channel_layout_default(&out_layout, out_channels);
    int alloc_result = swr_alloc_set_opts2(&swr_context_,
                                           &out_layout,
                                           out_format,
                                           out_samplerate,
                                           &in_layout,
                                           in_format,
                                           in_samplerate,
                                           0,
                                           nullptr);
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);
    if (alloc_result < 0)
    {
        return false;
    }

    int ret = swr_init(swr_context_);
    if (ret < 0)
    {
        return false;
    }

    in_samplerate_ = in_samplerate;
    in_channels_ = in_channels;
    in_bits_per_sample_ = av_get_bytes_per_sample(in_format);
    in_format_ = in_format;

    out_samplerate_ = out_samplerate;
    out_channels_ = out_channels;
    out_bits_per_sample_ = av_get_bytes_per_sample(out_format);
    out_format_ = out_format;
    return true;
}
