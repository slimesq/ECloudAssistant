#ifndef REMOTEMANAGER_H
#define REMOTEMANAGER_H
#include "RtmpPushManager.h"
#include "PullerWgt.h"
#include "SigConnection.h"

class RemoteManager : public RtmpPushManager
{
public:
    ~RemoteManager();
    RemoteManager();
    RemoteManager(RemoteManager&&) = delete;
    RemoteManager(RemoteManager const&) = delete;
    RemoteManager& operator=(RemoteManager&&) = delete;
    RemoteManager& operator=(RemoteManager const&) = delete;

public:
    void Init(QString const& sigIp, uint16_t port, QString const& code);
    void StartRemote(QString const& sigIp, uint16_t port, QString const& code);

protected:
    void HandleStopStream();
    bool HandleStartStream(QString const& streamAddr);

private:
    void Close();

private:
    std::unique_ptr<PullerWgt> pullerWgt_;
    std::unique_ptr<EventLoop> event_loop_;
    std::shared_ptr<SigConnection> sig_conn_;
};
#endif  // REMOTEMANAGER_H
