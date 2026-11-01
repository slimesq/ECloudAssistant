#ifndef AVDEMUXER_H
#define AVDEMUXER_H
#include "AV_Common.h"
#include <functional>
#include <thread>

class AAC_Decoder;
class H264_Decoder;
class AVDEMuxer
{
public:
    AVDEMuxer(AVContext* ac);
    ~AVDEMuxer();
    bool Open(std::string const& path);
    using StreamCallBack = std::function<void(bool)>;
    inline void SetStreamCallBack(StreamCallBack const& cb)
    {
        streamCb_ = cb;
    }

protected:
    void Close();
    void FetchStream(std::string const& path);
    bool FetchStreamInfo(std::string const& path);
    double audioDuration();
    double videoDuration();
    static int InterruptFouction(void* arg);

private:
    int videoIndex = -1;
    int audioIndex = -1;
    AVContext* avContext_;
    AVDictionary* avDict_;
    std::atomic_bool quit_ = false;
    StreamCallBack streamCb_ = [](bool) {};
    AVFormatContext* pFormateCtx_ = nullptr;
    std::unique_ptr<std::thread> readthread_;
    //解码器
    std::unique_ptr<AAC_Decoder> aacDecoder_;
    std::unique_ptr<H264_Decoder> h264Decoder_;
};

#endif  // AVDEMUXER_H
