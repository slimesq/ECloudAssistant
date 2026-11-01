#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H
#include <memory>
#include "AV_Common.h"
#include "VideoConvert.h"
class VideoEncoder : public EncodBase
{
public:
    VideoEncoder();
    VideoEncoder(VideoEncoder const&) = delete;
    VideoEncoder& operator=(VideoEncoder const&) = delete;
    ~VideoEncoder();

public:
    virtual bool Open(AVConfig& video_config) override;
    virtual void Close() override;
    virtual AVPacketPtr Encode(
        quint8 const* data, quint32 width, quint32 height, quint32 data_size, quint64 pts = 0);

private:
    qint64 pts_;
    quint32 width_;
    quint32 height_;
    bool force_idr_;
    AVFramePtr rgba_frame_;
    AVPacketPtr h264_packet_;
    std::unique_ptr<VideoConverter> converter_;
};
#endif  // VIDEO_ENCODER_H
