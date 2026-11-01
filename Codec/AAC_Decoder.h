#ifndef AAC_DECODER_H
#define AAC_DECODER_H
#include <QThread>
#include "AV_Common.h"

class AudioResampler;
class AAC_Decoder : public QThread, public DecodBase
{
    Q_OBJECT
public:
    AAC_Decoder(AVContext* ac, QObject* parent = nullptr);
    AAC_Decoder(AAC_Decoder const&) = delete;
    AAC_Decoder& operator=(AAC_Decoder const&) = delete;
    ~AAC_Decoder();
    int Open(AVCodecParameters const* codecParamer);
    inline void put_packet(AVPacketPtr const packet)
    {
        audio_queue_.push(packet);
    }
    inline bool isFull()
    {
        return audio_queue_.size() > 50;
    }

protected:
    void Close();
    virtual void run() override;

private:
    bool quit_ = false;
    AVQueue<AVPacketPtr> audio_queue_;
    AVContext* avContext_ = nullptr;
    std::unique_ptr<AudioResampler> audioResampler_;
};
#endif  // AAC_DECODER_H
