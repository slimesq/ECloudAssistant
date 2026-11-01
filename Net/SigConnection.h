#ifndef SIGCONNECTION_H
#define SIGCONNECTION_H
#include <functional>
#include "BufferReader.h"
#include "TcpConnection.h"
#include <QtGlobal>
#include <QScreen>
#include <QCursor>
#include "AV_Queue.h"

struct packet_head;
class SigConnection : public TcpConnection
{
public:
    enum UserType
    {
        CONTROLLED,  //被控端
        CONTROLLING  //控制端
    };
    enum State
    {
        NONE,
        IDLE,
        PULLER,
        PUSHER
    };
    SigConnection(TaskScheduler* scheduler,
                  int sockfd,
                  QString const& code,
                  UserType const& type = CONTROLLED);
    virtual ~SigConnection();
    inline bool isIdle()
    {
        return state_ == IDLE;
    }
    inline bool isPusher()
    {
        return state_ == PUSHER;
    }
    inline bool isPuller()
    {
        return state_ == PULLER;
    }
    inline bool isNone()
    {
        return state_ == NONE;
    }
    using StopStreamCallBack = std::function<void()>;
    using StartStreamCallBack = std::function<bool(QString const& streamAddr)>;
    inline void SetStartStreamCallBack(StartStreamCallBack const& cb)
    {
        startStreamCb_ = cb;
    }
    inline void SetStopStreamCallBack(StopStreamCallBack const& cb)
    {
        stopStreamCb_ = cb;
    }

protected:
    bool OnRead(BufferReader& buffer);
    void OnClose();
    void HandleMessage(BufferReader& buffer);

private:
    qint32 Join();
    qint32 obtainStream();
    void doJoin(packet_head const* data);
    void doPlayStream(packet_head const* data);
    void doCtreatStream(packet_head const* data);
    void doDeleteStream(packet_head const* data);

private:
    void doMouseEvent(packet_head const* data);
    void doMouseMoveEvent(packet_head const* data);
    void doKeyEvent(packet_head const* data);
    void doWheelEvent(packet_head const* data);

private:
    bool quit_ = false;
    State state_;
    QString code_ = "";
    UserType const type_;
    QScreen* screen_ = nullptr;
    StopStreamCallBack stopStreamCb_ = []() {};
    StartStreamCallBack startStreamCb_ = [](QString const&) -> bool { return true; };
    std::unique_ptr<std::thread> eventthread_ = nullptr;
};
#endif  // SIGCONNECTION_H
