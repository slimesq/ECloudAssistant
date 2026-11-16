# ECloudAssistant 阶段四：Winsock 与客户端事件循环学习讲义

> 对应[项目学习规划的阶段四](项目学习规划.md#stage-4)。参考用时：8～12 小时，额外基础补学、环境准备与排障另计。
>
> 本阶段沿调度、收包、立即发送、可写续发和关闭路径阅读客户端自建网络组件。已有 Linux socket/epoll、TCP 字节流与缓冲知识直接复用，先补 Winsock 差异，再核对项目怎样实现这些职责。

## 1. 学完后应能回答什么

1. Winsock 初始化、socket 类型、错误获取、非阻塞和关闭，与熟悉的 Linux API 有哪些差别？
2. `EventLoop(2)` 创建几条线程，连接怎样选择调度器？
3. `select` 前为什么保留 fd 集合备份，返回的事件怎样进入连接回调？
4. 数据从 `recv` 到业务回调、从 `Send` 到系统 `send` 分别经过什么缓冲？
5. 立即发送与可写事件续发在哪个线程，哪些位置加锁，哪些条件还没有得到保证？
6. 关闭标志、移除 Channel、`closesocket` 和停止线程分别发生在哪里？

本讲的 `Close()` 主要指 `Net/TcpConnection::Close()`。界面、采集、编码和播放器对象的完整退出顺序，在阶段九统一检查。

### 1.1 先认清本讲处理哪部分网络

| 路径 | 主要接口 | 与本讲的关系 |
| --- | --- | --- |
| 负载查询、登录 | `LoginWgt` 中的 `QTcpSocket` | 沿用[阶段三讲义](阶段三Qt登录链与二进制协议学习讲义.md)，不由这里的 select 调度器接管 |
| 信令收发 | `SigConnection → TcpConnection` | 本讲主要业务入口 |
| RTMP 发布 | `RtmpConnection → TcpConnection` | 共用本讲收发组件，协议和媒体封装留阶段七 |
| FFmpeg 拉流与播放 | `AVDEMuxer` 等 | 内部读取与播放线程留阶段八，不概括为都使用本套调度器 |

当前主构建使用 `SelectTaskScheduler`；`Net/main.cpp`、`Acceptor`、`TcpServer`、`EpoolTaskScheduler` 被主目标排除。阅读范围沿用阶段零，不要求另跑这些演示入口。

### 1.2 阅读顺序

| 先后 | 文件 | 要回答的问题 |
| --- | --- | --- |
| 1 | [TcpSocket.h](../Net/TcpSocket.h)、[TcpSocket.cpp](../Net/TcpSocket.cpp) | 句柄、连接、非阻塞和资源释放 |
| 2 | [EventLoop.cpp](../Net/EventLoop.cpp)、[TaskScheduler.cpp](../Net/TaskScheduler.cpp) | 调度对象与线程怎样创建、循环和停止 |
| 3 | [SelectTaskScheduler.cpp](../Net/SelectTaskScheduler.cpp)、[Channel.h](../Net/Channel.h) | 关注事件如何变成回调 |
| 4 | [TcpConnection.cpp](../Net/TcpConnection.cpp)、[BufferReader.cpp](../Net/BufferReader.cpp)、[BufferWriter.cpp](../Net/BufferWriter.cpp) | 一次读写、缓冲消费和发送续传 |
| 5 | 调度器头文件、`Close()`、`Quit()`、[Timer.cpp](../Net/Timer.cpp) | 注销能否抵达实现，线程何时结束，定时器怎样接入 |

ENET 阶段三、四的就绪通知和缓冲理论直接复用。阅读时同时核对组件承担什么职责，以及现有代码实际完成了哪些动作。

## 2. 先补 Winsock 差异

### 2.1 初始化、类型、错误与关闭

先对照微软的 [Complete Winsock Client Code](https://learn.microsoft.com/en-us/windows/win32/winsock/complete-client-code)，认出以下顺序：

```text
WSAStartup → socket → connect → send/recv
  → shutdown（按需关闭方向）→ closesocket → WSACleanup
```

重点对照已有 Linux 经验：

| 内容 | Winsock 中怎样理解 |
| --- | --- |
| 使用网络 API 前 | 成功调用 `WSAStartup`，如请求 Winsock 2.2 |
| socket 句柄 | 用 `SOCKET`；不能假定是小的正整数或普通文件描述符 |
| `socket / accept` 失败 | 与 `INVALID_SOCKET` 比较 |
| `connect / send / recv` 等操作失败 | 返回 `SOCKET_ERROR` 后，立即在当前线程取 `WSAGetLastError()` |
| 暂时不能非阻塞读写 | 看 `WSAEWOULDBLOCK`，不照搬 `errno == EAGAIN` |
| 释放单个 socket | `closesocket()` |
| 停止一个方向的收发 | `shutdown()`，它本身不释放句柄 |
| 结束 Winsock 使用期 | 成功的 `WSAStartup` 要有相应的 `WSACleanup` |

`WSAStartup` 是一个例外：它自己返回错误码，0 表示成功，不能按普通 socket 操作的错误获取方式处理。初始化作用于进程中的 Winsock 使用期，不要求每条调度线程或每个连接分别初始化；由明确的使用期管理保证成功调用与清理配对。参见 [WSAStartup](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsastartup)、[Winsock 错误处理](https://learn.microsoft.com/en-us/windows/win32/winsock/handling-winsock-errors)。

当前 `TaskScheduler` 构造函数用 `call_once` 发起一次初始化，但失败分支调用了 `WSACleanup`，没有阻止对象继续构造，成功路径也未在这里配对清理。

当前 `TcpSocket / Channel / BufferReader / BufferWriter` 等接口仍用 `int` 传递句柄。Win64 的 `SOCKET` 不能无条件装进 `int`，有效值也可能为 0；这是需要识别的移植缺口，见 [Socket Data Type](https://learn.microsoft.com/en-us/windows/win32/winsock/socket-data-type-2)。

### 2.2 当前连接先阻塞，再尝试设置非阻塞

沿 `RemoteManager::Init()` 看调用顺序：

```text
GUI 入口
  → TcpSocket::Create
  → TcpSocket::Connect
  → 创建 SigConnection
      → TcpConnection 构造
          → 尝试设置非阻塞、发送缓冲和 keepalive
          → 注册 Channel
```

`TcpSocket::Connect(ip, port, timeout)` 内直接调用系统 `connect()`，没有使用 `timeout` 参数。在这条路径上，GUI 线程要等阻塞式 `connect()` 返回后才能继续；传入的 `timeout` 没有提供额外的超时控制。

`SocketUtil::SetNonBlock()` 内部将 `on` 设为 1，再调用 `ioctlsocket(sockfd, FIONBIO, &on)`。控制模式的是 `on` 的值：非零表示启用非阻塞，0 表示恢复阻塞，见 [Winsock FIONBIO](https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls#fionbio)。

当前非阻塞、发送缓冲和 keepalive 设置均未检查返回值。因此应区分“尝试设置”和“已经确认设置成功”。本阶段先认识实际顺序，不要求改造连接状态机或另建示例服务。

## 3. 调度对象、连接与线程

### 3.1 EventLoop 构造时已经启动线程

`EventLoop` 的构造函数立即调用 `Loop()`。它为每个编号创建一个 `SelectTaskScheduler`，再创建一条 `std::thread` 执行该调度器的 `Start()`：

```text
EventLoop(2)
  ├─ scheduler[0] → 工作线程 → TaskScheduler::Start()
  └─ scheduler[1] → 工作线程 → TaskScheduler::Start()
```

`EventLoop` 是持有和组织这些对象的管理类，不是在两条线程外又增加一条调度线程。

客户端主窗口构造时，`RemoteManager` 的基类 `RtmpPushManager` 已创建 `EventLoop(1)`，派生类自身又创建 `EventLoop(2)`。前者供 RTMP 发布使用，后者供信令连接使用；它们是两组不同的调度对象。

### 3.2 GetTaskSchduler 怎样分配连接

`GetTaskSchduler()` 沿用源码拼写：

- 只有一个调度器时，返回编号 0。
- 有两个时，`index_` 初值是 1，串行调用的选择顺序为 **1 → 0 → 1 → 0**。
- 返回某个调度器后，连接保存并使用该调度器；不是每收到一个事件再换线程。

[RemoteManager::Init](../UI/center/RemoteManager.cpp) 与 [AVPlayer::Connect](../Puller/UI/AVPlayer.cpp) 都从双调度器对象中取调度器。实际落在哪条线程，取决于调用先后，不能把某个角色永久等同于编号 0 或 1。

`EventLoop::UpdateChannel() / AddTimer()` 包装接口固定转发编号 0；但 `TcpConnection` 构造时是直接调用它拿到的 `TaskScheduler*` 的 `UpdateChannel()`，没有经过这个固定 0 号的包装。

`GetId()` 是调度器的编号，不是操作系统线程 ID；两组 `EventLoop` 中都可以存在编号 0。检查线程时，结合实例和调试器中的实际线程区分。

### 3.3 Start 每一轮实际做什么

`TaskScheduler::Start()` 的主体是：

```text
设置 is_shutdown_ = false
循环直到停止标志为 true：
  → TimerQueue::HandleTimerEvent()
  → SelectTaskScheduler::HandleEvent()
```

本循环分发的定时回调、就绪事件，以及由这些事件同步调用的业务回调，都在该调度线程中依次执行。`Send()` 主动触发的立即写仍由调用线程执行，见第 6.1 节。调度回调长时间不返回，会拖住这条线程的其他连接和后续停止检查。

客户端 `TimerQueue` 使用 `steady_clock` 的毫秒计时；`EventLoop::AddTimer()` 接入编号 0 的调度器，回调返回 true 表示重新安排，false 表示移除。它不是 Qt 的 `QTimer`，也不同于登录报文里的系统纪元秒。

当前主业务源码未见注册 `AddTimer()` 的调用。本阶段只定位接入点，定时器算法复用 ENET 阶段五，不另安排心跳或定时任务实验。其队列接口也没有自动投递到调度线程或完整的同步保护。

## 4. select 如何变成 Channel 回调

### 4.1 先分清关注集合、备份与就绪集合

`Channel` 保存一个 socket 的句柄、关注事件和回调，本身不创建线程，也不执行系统 `recv / send`。`EnableReading() / EnableWriting()` 只修改它的事件位；随后调用 `UpdateChannel()`，才会通知调度器更新关注集合。

对于尚未登记且有关注事件的 Channel，`SelectTaskScheduler::UpdateChannel()` 将其加入 `channels_`；对于已登记的 Channel，则更新集合变化标志，若已不关注任何事件就移除。

下一轮 `HandleEvent()` 按变化标志重建集合，或者从备份复制工作集合：

```text
channels_ + Channel 关注事件
  → 需要变化时重建，并保存 fd_set 备份
  → 本轮工作 fd_set
  → select 修改工作集合，只留下相应就绪 socket
  → 用 FD_ISSET 查询结果
```

备份保存的是持续关注的对象，本轮返回集合保存的是当前就绪结果。不能把上次的就绪子集直接当作下一轮全部关注对象。

代码中 `FD_ZERO` 用于清空集合，`FD_SET` 用于加入句柄，`FD_ISSET` 用于查询返回集合中是否包含该句柄。

Winsock 的 `fd_set` 是句柄集合，默认 `FD_SETSIZE` 为 64，限制的是单个集合中的 socket 数量，不是 socket 数值必须小于 64。具体编译配置仍需核对，见 [fd_set](https://learn.microsoft.com/en-us/windows/win32/api/winsock/ns-winsock-fd_set)。

### 4.2 本项目没有在 select 中休眠等待

当前代码传入的是：

```cpp
struct timeval tv = {0, 0};
int ret = select(maxfd_ + 1, &fd_read, &fd_write, &fd_exp, &tv);
```

Winsock 忽略 `nfds` 参数，因此 `maxfd_ + 1` 在这里不承担 Linux select 的扫描上界含义。零超时表示立即检查后返回，不是无限等待，见 [select](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select)。

| 返回情况 | 本项目接下来做什么 |
| --- | --- |
| 正数 | 收集就绪 Channel 并分发 |
| 0 | 本轮没有就绪结果，返回外层循环 |
| `SOCKET_ERROR` | `HandleEvent()` 返回 false，但 `Start()` 没有检查它，仍继续下一轮 |

Winsock 在三个集合都为空时可以返回 `WSAEINVAL`。结合当前零超时与外层循环，空调度器可能重复出错，有连接但无事件时也会持续轮询。源码没有根据最近定时器计算等待时间，也没有实现阻塞等待时的唤醒机制。

这里能从源码判断轮询方式；实际 CPU 占用仍需运行观察，不能把某个固定占用率当成已验证结果。

### 4.3 在映射锁外执行回调

`HandleEvent()` 在锁内根据就绪集合收集 `ChannelPtr + 事件位`，然后离开调度器锁的保护范围，再调用 `Channel::HandleEvent()`。这样业务回调执行时可以再次更新关注事件。

本项目映射如下：

| select 结果 | 项目事件 |
| --- | --- |
| 读集合命中 | `EVENT_IN` |
| 写集合命中 | `EVENT_OUT` |
| 异常集合命中 | `EVENT_HUP` |

最后一行是**当前代码的映射**。Winsock 的 `exceptfds` 涉及带外数据、非阻塞连接失败等异常条件，并非对端关闭的专用通知。普通 TCP 对端关闭还要经可读事件后的 `recv()` 结果判断，不能将 `exceptfds` 直接解释为“对端正常关闭”。

`Channel::HandleEvent()` 按读 → 写 → 关闭 → 错误的顺序执行回调；一个结果可以命中多个分支，关闭分支执行后直接返回。当前调度器没有生成 `EVENT_PRI / EVENT_ERR`。

## 5. 收包：从 recv 到业务消费

### 5.1 一条实际读事件路径

```text
调度工作线程
  → TaskScheduler::Start
  → SelectTaskScheduler::HandleEvent
  → Channel::HandleEvent(EVENT_IN)
  → TcpConnection::HandleRead
      → 持有连接 mutex，检查关闭标志
      → BufferReader::Read → recv
      → 退出这一段锁范围
      → readCb_(shared_from_this(), *read_buffer_)
```

`readCb_` 是普通 C++ 回调，在当前调度线程同步执行，不是投递到 Qt GUI 线程。业务回调前已经释放连接锁，因此回调可以再进入 `Send()`；但这不代表业务内部所有共享字段都已受到保护。

`BufferReader::Read()` 每次最多调用一次 `recv(..., 4096, 0)`，正数时才推进写索引。这里使用 select 的持续就绪检查，不需要把它当成 epoll ET 模式的“必须一次读到暂缓”循环。

### 5.2 API 语义与当前返回值处理

先按 [Winsock recv](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recv) 区分情况：

| 结果 | API 的含义 | 当前 HandleRead 行为 |
| --- | --- | --- |
| `recv > 0` | 收到这些字节，不保证完整消息 | 更新写索引后进入业务回调 |
| `recv == 0` | TCP 对端有序关闭了发送方向 | 没有专门关闭，仍进入业务回调 |
| `SOCKET_ERROR + WSAEWOULDBLOCK` | 非阻塞接收暂时不能完成 | 没有区分原因，因负数调用 `Close()` |
| 其他接收错误 | 依据错误码判断 | 同样按负数调用 `Close()` |

此外，`BufferReader::Read()` 在缓冲增长限制分支也可能直接返回 0，根本没有调用 `recv`。所以包装函数的 0 与系统 `recv` 的 EOF 不能不加区分地混为一谈。

对端发送方向进入 EOF 后，socket 仍会表现为可读。当前代码保留读关注，因而可能反复读到 0 并进入业务回调。就绪通知只表示可以尝试 I/O，最终仍要检查系统调用结果。

### 5.3 BufferReader 保留了什么

`BufferReader.h` 中的关系是：

```text
可读字节数 = writer_index_ - reader_index_
可读区间   = [reader_index_, writer_index_)
尾部可写量 = buffer_.size() - writer_index_
Peek()     = 可读区起点
```

- `recv` 追加成功后，写索引向后移动。
- `Peek()` 只借用当前数据，不消费。
- `Retrieve(n)` 消费前缀；全部消费时，两个索引归零。
- `RetrieveAll()` 重置索引，不释放 vector，也不擦除其内容。
- `Size()` 返回存储区的当前大小，不是有效可读字节数。

例如当前 `reader=10、writer=14`，保留 4 字节；再读入 6 字节后，`writer=20`，可读量成为 10。新增数据仍从 `writer_index_` 追加；当前实现不会把未读数据搬到开头来回收已读前缀空间。

当前存储区初始为 2048，尾部不足 4096 时再增加 4096，所以第一次读前会先扩到 6144。增长限制检查的是扩容前大小，使用 `> MAX_BUFFER_SIZE`，不是严格的接收总量上限或完整背压机制。扩容也可能使旧 `Peek()` 指针失效。

`BufferReader::ReadAll(std::string&)` 只是取出这个用户态缓冲中的可读数据并重置索引，与阶段三从 Qt socket 取数据的 `readAll()` 所在层次不同。

持久读缓冲具备保留半包的能力，但业务解析还需在数据不足时返回、等待后续数据。当前 `SigConnection::OnRead()` 在缓冲非空时循环，`HandleMessage()` 遇到不完整消息却不消费也不告知外层，可能在回调内反复循环；具体包头与状态处理在阶段五继续检查。

## 6. 发包：立即写、可写续发与队列

### 6.1 Send 先入队，再在调用者线程尝试发送

当前两种 `TcpConnection::Send()` 都按以下顺序执行：

```text
调用 Send 的线程
  → 检查 is_closed_
  → 加连接 mutex，Append 到写队列
  → 解锁
  → 立即调用 HandleWrite()
```

这里没有任务投递接口。GUI 线程调用，就先在 GUI 线程尝试发送；业务回调在调度线程调用，就先在该调度线程尝试发送。

`HandleWrite()` 使用 `try_lock()`；取得连接锁后，调用 `BufferWriter::Send()`。判断队列后：

- 队列非空且尚未关注写：启用 `EVENT_OUT`，更新调度器。
- 队列已空且仍关注写：关闭 `EVENT_OUT`，更新调度器。

已连接的 TCP socket 通常有发送空间。如果队列已空仍关注可写，就可能反复执行无数据可发的写回调；因此只在有待发数据时保留写关注。

之后 select 命中可写事件，会在所属调度线程再次调用同一个 `HandleWrite()`。因此，一条连接的立即写与后续续发可能来自不同线程。

`try_lock()` 失败时，当前函数直接返回。不能仅凭这次调用就保证已经安排了后续重试，还要看当时是否已有可写关注或其他发送入口。

### 6.2 两种 Append 的数据寿命不同

| Append 入口 | 当前行为 |
| --- | --- |
| `char const* + size` | 分配新内存并复制这些字节，队列保存自己的副本 |
| `shared_ptr<char> + size` | 共享传入内存，不复制内容 |

队列持有同一个 `shared_ptr` 的副本，调用方释放自己的副本不会使这份数据立即销毁。但其他线程仍可能修改同一块内存，共享持有也不等于零拷贝网络发送。

队列中的 `Packet` 包含 `data / size / writeIndex`。这里的 Packet 是一个待发送数据项，不是 TCP 报文段；分次 `send` 不改变应用消息的逻辑内容。

### 6.3 用 14 字节 JOIN 推演部分发送

以 `SigConnection::Join()` 提交的一条 14 字节消息为例，假定它作为一个 Packet 入队。下面是假定系统调用结果的索引推演，不要求实际制造网络拥塞：

| 时刻或结果 | writeIndex | 尚未提交的字节 | 后续动作 |
| --- | ---: | ---: | --- |
| 刚入队 | 0 | 14 | 首次立即尝试 |
| 系统 `send` 返回 3 | 3 | 11 | Packet 保留，关注可写 |
| 下次 `send` 暂时不能完成 | 3 | 11 | 位置不前进，保留待续发 |
| 后续系统 `send` 返回 11 | 14 | 0 | Packet 出队；队列空则关闭可写关注 |

每次都从 `data + writeIndex` 发送 `size - writeIndex` 个字节。成功写出部分后，下次不能从头重发，也不能提前丢弃余下字节。系统 send 语义见 [Winsock send](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-send)。

对应到 `BufferWriter::Send()` 的 `count`：每次尝试前先减 1，只有完整发完当前项才加回 1 并出队。因此部分发送或暂缓都会结束本次循环，交给后续可写事件续发；完整发完一项后，才继续检查下一项。

这里要区分两个返回值：

- 系统 `::send()` 的正数表示本次接受了多少字节，不表示远端已完成处理。
- `BufferWriter::Send()` 不是累计发送量。它在整项发完后可以继续处理下一项；队列全部发空后，下一轮遇到空队列可能直接返回 0。

所以，“包装函数返回 0”不能直接解释为本轮没有发出字节。`HandleWrite()` 还要根据 `IsEmpty()` 决定是否继续关注写事件。

当前 Writer 将 `WSAEWOULDBLOCK / WSAEINPROGRESS / 错误码 0` 都转成返回 0。前者是常见的非阻塞暂缓；后两者不能不加区分地解释成相同的标准语义。这是现有代码分支，不是完整错误分类。

### 6.4 队列上限还没有形成完整背压

`TcpConnection` 实际创建 `BufferWriter(500)`，限制的是 **500 个 Packet**，不是 500 字节，也不是类声明中的默认 10000 项。它没有按待发送总字节数限制内存。

满队列时 `Append()` 返回 false，但 `TcpConnection::Send()` 忽略了结果，且自身返回 `void`。因此新数据可能没有入队，上层却得不到失败反馈。

这不同于生产者根据压力减速、重试或按约定丢弃。设置内核 `SO_SNDBUF = 100 * 1024`，也不能代替应用队列的流量控制。背压概念沿用 ENET 阶段四，媒体生产策略留到对应阶段。

## 7. 把实际执行线程与锁范围对应起来

以现有调用方为例：

| 行为 | 当前执行线程 |
| --- | --- |
| GUI 入口创建 `SigConnection`，构造内 `Join → Send → HandleWrite` | GUI 调用线程 |
| `select → HandleRead` 或可写事件续发 | 该连接所属调度工作线程 |
| 信令回调中再次调用 `Send` | 执行该回调的调度工作线程 |
| 后续编码线程提交 RTMP 数据 | 该编码调用线程；内部媒体流程留阶段六、七 |

`TcpConnection` 的连接锁保护入队、部分读写操作，但业务读回调在锁外执行，`is_closed_` 也有锁外访问。调度器对映射加锁，不代表 Channel 状态、变化标志与定时器数据都已统一受同一把锁保护。

例如 `SelectTaskScheduler::HandleEvent()` 开头会在锁外读取 reset 标志，而其他线程可能通过 `UpdateChannel()` 写入它们。当前实现不是“所有操作都自动串行到网络线程”的模型，也不能因为声明里出现了 `mutex` 就认定整体线程安全。

本阶段要能查到实际调用线程与加锁范围。完整的对象生命周期、回调寿命和跨线程停止顺序，留阶段九集中检查。

## 8. 关闭：标记、注销、句柄与线程分别看

### 8.1 TcpConnection::Close 实际做了什么

`DisConnect()`、`HandleClose()`、`HandleError()`，以及部分读写错误分支，都会进入 `Close()`。其主体意图是：

```text
尚未标记关闭
  → is_closed_ = true
  → 请求调度器移除 Channel
  → 调用 closeCb_
  → 如果设置过 disconnectCb_，再调用它
```

当前 `Close()` 没有调用 `closesocket()`，也没有等待写队列排空。连接对象的析构函数才尝试关闭系统句柄，使用的条件是 `fd > 0`；这又不同于 Winsock 的有效句柄判断。

上述入口持有连接 `mutex` 时调用 `Close()`，两个关闭回调也在该锁内同步执行。它们的执行线程由触发关闭的入口决定；这与第 5.1 节中先解锁再执行的读回调不同。

`SigConnection` 设置的关闭回调主要是进入 `OnClose()`、修改业务退出标志。不能仅因关闭回调执行过，就认定调度器注销、系统 socket 释放和业务线程停止都已完成。

### 8.2 当前移除请求没有抵达派生实现

必须把以下三个位置一起看：

| 位置 | 实际名称与作用 |
| --- | --- |
| `TcpConnection::Close()` | 经 `TaskScheduler*` 调用 `RmoveChannel(channel_)` |
| [TaskScheduler.h](../Net/TaskScheduler.h) | `virtual RmoveChannel(ChannelPtr&)` 是空实现 |
| [SelectTaskScheduler.h](../Net/SelectTaskScheduler.h)、对应 cpp | 定义的是 `RemoveChannel(ChannelPtr&)` |

`RmoveChannel` 与 `RemoveChannel` 不是同名函数，后者没有覆盖前者。因此当前关闭路径调用到基类空实现，**没有执行 SelectTaskScheduler 中真正的删除逻辑**。

这不只是拼写风格差异，而是虚函数派发没有抵达预期函数。`is_closed_ = true` 不能证明 `channels_` 和后续 select 集合已经移除了连接。

### 8.3 谁保持对象存活，谁释放 socket

```text
业务对象的 shared_ptr ──持有──> TcpConnection / SigConnection
连接对象 ──持有──> Channel
调度器 channels_、本轮 event_list ──持有──> Channel
Channel 中的回调 ──捕获裸 this──> TcpConnection
```

后两行并没有形成对连接对象的共享持有。如果连接对象已被释放而 Channel 仍留在调度器中，旧回调就存在访问失效对象的风险；只保住 Channel 的寿命并不足够。

`TcpSocket` 的析构函数当前为空。局部封装对象结束，不会自动关闭它保存的句柄；成功路径把句柄交给连接对象使用，但没有明确的所有权转移接口，失败路径也要按实际代码确认清理。

`shutdown(SD_SEND)` 只关闭发送方向，后续仍需 `closesocket` 释放句柄。当前 `TcpSocket::ShutdownWrite()` 调用 shutdown 后就把成员设为 -1，不能把这种赋值当作资源已释放。该方法未见业务调用，本阶段只用于辨认关闭概念。见 [shutdown](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-shutdown)、[closesocket](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-closesocket)。

### 8.4 停止事件循环不是自动关闭所有连接

`EventLoop::Quit()` 先对调度器调用 `Stop()`，再对工作线程 `join()`，最后清理内部容器。`Stop()` 设置原子停止标志，工作线程回到循环条件时才能结束。

当前 select 为零超时，没有阻塞等待网络事件，但业务或定时回调仍可能迟迟不返回，影响 `join()`。`Start()` 还会重置停止标志，过早的停止请求可能被覆盖，具体时序已在[阶段一](阶段一已有基础复用与源码阅读方法学习讲义.md)说明。

停止循环、销毁调度器、关闭每个连接与销毁业务对象，需要相互匹配的顺序；连接还保存着调度器裸指针，不能假定它会反过来保持整个 EventLoop 存活。完整顺序留阶段九，本阶段先追清这些动作的边界。

## 9. 用一次路径观察或索引推演核对

先对照微软官方 client 认识初始化、错误判断和关闭即可。有现成 echo 服务时可复用官方样例观察；没有现成服务，不另搭一个服务作为读项目的前提。

已有可用的自建网络连接和调试器时，选择一条路径：

1. 若观察读取，在 `TcpConnection::HandleRead()` 调用 `read_buffer_->Read()` 后设断点；若观察发送，在 `Send()` 的 `Append()` 之后设断点。再用已有调试器运行 Debug 产物，沿既有信令流程观察一次调用。
2. 查看调用栈、当前线程和 `task_schduler_->GetId()`。发送从哪里调用，就先在哪个线程尝试；不要只凭函数属于网络类就判断为网络线程。
3. 若观察读取，核对本次读入后的索引和返回值，再跟到业务回调看它消费多少；若观察发送，先确认入队，再跟入 `BufferWriter::Send()` 查看队首 Packet 的 `writeIndex`，返回 `HandleWrite()` 后核对 `IsWriting()`。读写任选一条观察即可。

没有连接条件时，先按源码确认调度线程和三条 I/O 路径，再用第 6.3 节的 3 / 暂缓 / 11 字节结果推演索引。能解释部分发送即可，不强行制造队列满、网络错误或完整远控。

关闭部分直接对照两个头文件和 `Close()`，确认虚函数名称、真正移除实现与 socket 析构位置。识别当前缺口，不把修完所有网络和退出问题作为进入下一阶段的条件。

## 10. 自查与下一步

结合源码回答：

1. `WSAStartup` 与其他 Winsock 函数怎样报告错误？`SOCKET` 为什么不能直接当 Linux 的 `int fd` 使用？
2. 有两个调度器时，选择为什么从 1 开始？EventLoop 包装接口与连接直接使用的调度器有什么区别？
3. fd 集合为什么要复制？零超时、空集合和异常集合在当前循环中各意味着什么？
4. `recv == 0`、`WSAEWOULDBLOCK` 与缓冲限制返回 0，当前分别会走到哪里？
5. 立即发送与可写续发怎样衔接？包装函数返回 0 为什么不能当作本轮发送字节数？
6. 满 500 项、逻辑 Close、移除 Channel、释放 socket 和停止线程，哪些动作目前没有形成可靠闭环？

完成标准：

- [ ] 能解释 Windows API 差异及项目实际连接、非阻塞设置顺序。
- [ ] 能跟通收包、立即发送和可写事件续发三条路径，指出实际执行线程。
- [ ] 能解释缓冲索引和发送队列单位，完成一次部分发送推演。
- [ ] 能追到逻辑关闭、移除请求、句柄释放与线程退出，并指出当前确定存在的缺口。

随后进入[阶段五：信令状态与远程输入](项目学习规划.md#stage-5)，阅读消息怎样驱动角色、状态和输入执行。网络组件的职责与疑点保留在本阶段，完整对象生命周期在阶段九集中检查。
