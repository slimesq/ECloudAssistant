# ECloudAssistant 阶段七：RTMP 发布状态与媒体发送学习讲义

> 对应[项目学习规划的阶段七](项目学习规划.md#stage-7)。参考用时：8～12 小时，共同 RTMP 基础补学、环境准备与排障另计。
>
> 沿“打开发布连接 → 握手与命令应答 → 配置和媒体负载 → Message → Chunk → TCP 队列”阅读。重点分清协议应有的顺序、当前源码实际完成的动作，以及它们是否足以证明发布和播放成功。

## 1. 学完后应能回答什么

1. `OpenUrl()`、`IsConnected()`、`NetStream.Publish.Start` 分别能确认哪一步？
2. `_result / onStatus` 怎样推进状态，事务 ID 与流 ID 怎样区分？
3. 首个关键包怎样影响 AVC/AAC 配置及普通音频，是否等待了发布应答？
4. H.264、AAC 的编码字节怎样组成媒体负载，当前实现有什么格式假设？
5. 媒体负载怎样成为 Message、Chunk 和 TCP 待发数据，哪里发生复制？
6. 关闭时 `DeleteStream()` 是否真的发出，重新发布会重置哪些状态？

### 1.1 先复用共同基础，再读客户端差异

你此前没有 RTMP 基础，应先完成 ENET [阶段八共同基础](https://github.com/slimesq/ENET/blob/main/docs/项目学习规划.md#rtmp-foundation)中的简单握手、Message/Chunk、三个 ID、AMF0 和离线样例。已完成的部分直接复用，不重复编写握手器、AMF 编码器或另搭服务。

AVC 即 H.264。本讲再补客户端实际使用的发布应答、AVC/AAC 负载与发送条件。原始图像、PCM、NAL、编码配置和 Packet 寿命复用[客户端阶段六](阶段06_屏幕音频采集与编码学习讲义.md)；TCP 立即写、可写续发和队列复用[阶段四](阶段04_Winsock与客户端事件循环学习讲义.md)。

### 1.2 源码入口

| 顺序 | 文件 | 本阶段看什么 |
| --- | --- | --- |
| 1 | [rtmp.h](../Pusher/rtmp/rtmp.h)、[RtmpPublisher.cpp](../Pusher/rtmp/RtmpPublisher.cpp) | URL、配置、OpenUrl、媒体入口与首关键包 |
| 2 | [RtmpConnection.h](../Pusher/rtmp/RtmpConnection.h)、[RtmpConnection.cpp](../Pusher/rtmp/RtmpConnection.cpp) | 接收分派、AMF 命令、发布状态与发送消息 |
| 3 | [RtmpHandshake.cpp](../Pusher/rtmp/RtmpHandshake.cpp)、[amf.cpp](../Pusher/rtmp/amf.cpp) | 复用基础，核对客户端的接入与参数 |
| 4 | [RtmpMessage.h](../Pusher/rtmp/RtmpMessage.h)、[RtmpChunk.cpp](../Pusher/rtmp/RtmpChunk.cpp) | 字段、输入/输出块大小、分块及重组 |
| 5 | [RtmpPushManager.cpp](../Pusher/RtmpPushManager.cpp)、[TcpConnection.cpp](../Net/TcpConnection.cpp) | 编码提交、复制、执行线程和关闭交界 |

`CretaeStream / lenght / playload` 等名称沿用源码拼写。

## 2. 先分清地址、编号与连接成功的层次

### 2.1 URL 的字段进入哪里

以 `rtmp://<媒体服务IPv4>:1935/live/demo` 为例：

| 内容 | 当前用途 |
| --- | --- |
| IP、port | 建立媒体 TCP 连接，不是信令服务地址 |
| `live` | app，进入 `connect` 的 AMF 对象 |
| `demo` | stream name，进入 `publish` 命令 |
| `/live/demo` | 当前对象保存的 stream path |

`ParseRtmpUrl()` 拆出这些字段，`RtmpConnection` 构造时从 Publisher 读取。当前 TCP 连接使用 IPv4 字面量解析，没有域名解析步骤；阅读与局部配置沿用阶段四，不因 URL 中能写字符串就认定已支持域名。

### 2.2 不同编号不能互相替代

| 编号 | 所在层次 | 本项目的例子 |
| --- | --- | --- |
| CSID | Chunk Basic Header，选择分块上下文 | 控制 2、命令 3、音频 4、视频 5 |
| Message Type ID | 消息类型 | Set Chunk Size=1、音频=8、视频=9、AMF0 命令=20 |
| Message Stream ID | 消息所属的逻辑流 | 初始 0；createStream 应答后保存到 `stream_id_` |
| transaction ID | AMF 命令参数，关联请求与应答 | `number_` 递增产生 |
| CodecID / SoundFormat | 媒体负载内部 | H.264 为 7，AAC 为 10 |

CSID=4 不代表 Message Type ID=4。流 ID 也不是固定等于 CSID，不能用 `RtmpChunk::GetStreamId()` 的静态计数代替发布会话取得的流 ID。

后文的字节序列均按十六进制书写；事务号、长度等数值按各处说明理解。

### 2.3 OpenUrl 返回 0 只到发起握手

当前调用路径：

```text
RtmpPublisher::OpenUrl
  → ParseRtmpUrl
  → TcpSocket::Connect
  → 创建 RtmpConnection
  → Handshake() 发送 C0+C1
  → return 0
```

参数 `msec` 没有用于等待握手或发布结果。`IsConnected()` 也只是检查 `rtmp_conn_` 存在且没有被逻辑关闭。

因此还要依次区分：TCP 已连接、简单握手已完成、连接命令成功、取得流 ID、发布获准，以及媒体实际可播放。这些不是同一个返回值能证明的结果。

## 3. 从握手完成追到发布应答

### 3.1 握手和 Chunk 怎样衔接

客户端 `Handshake()` 构造并提交 1537 字节的 C0+C1。客户端收到服务器应答时，`RtmpHandshake::Parse()` 当前等待完整的 3073 字节 S0+S1+S2：

- 不足时返回 0，不消费输入，等待下一次接收。
- 版本不是 3 时返回负数。
- 到齐后消费该段，复制 S1 生成 1536 字节 C2，本地握手状态变为完成；当前未进一步验证 S2 回显。

本地握手刚完成时，`RtmpConnection::OnRead()` 提交 C2。若缓冲区还有剩余字节，就调用 `HandleChunk()` 解析；随后调用 `SetChunkSize()` 和 `Connect()`。即使 `HandleChunk()` 返回 false，当前代码仍执行这两个调用，之后才把失败结果返回给外层。后续收包直接进入 Chunk 解析。

`HandleChunk()` 还要区分两个条件：`Parse()` 返回正数表示本次消费推进，`IsCompleted()` 才表示得到完整 Message。返回 0 时退出本轮，保留已有数据等待后续输入，不是阶段五信令半包处理中原地反复解析的写法。

### 3.2 当前发出了哪些 AMF 参数

正常首次调用时，`number_` 从 0 开始递增：

| 方法 | 当前源码编码的主要参数 | 发送时的流 ID |
| --- | --- | --- |
| `Connect()` | `"connect", 1, {app, type}` | 0 |
| `CretaeStream()` | `"createStream", 2, null` | 0 |
| `Publish()` | `"publish", 3, null, stream_name` | createStream 返回的值 |
| `DeleteStream()` | `"DeleteStream", 下一个事务号, null, stream_id` | 当前 `stream_id_` |

这里的 null 来自 `AmfEncoder::encodeObjects()`：传入空对象集合时，它实际编码 AMF0 Null，而不是空 Object。非空 connect 对象的属性顺序由容器遍历决定，不要求按固定字节顺序背诵。

与 [Adobe RTMP 原始规范的 Commands 文档](https://ossrs.net/lts/en-us/assets/files/rtmp_specification_1.0-25a467618b92a3115bc97d4b0038b0ff.pdf)对照，当前 publish 使用递增事务号，且未编码 `live / record / append` 发布类型；标准的 publish 事务号为 0。它是当前实现的差异，不能把表中的所有参数当成通用发布模板。

### 3.3 状态由哪条应答推进

`HandleInvoke()` 先解第一个 AMF 值保存方法名，再解剩余参数，按 `_result / onStatus` 分派：

| 当前状态与收到的内容 | 当前动作 |
| --- | --- |
| 调用 Connect() | 设为 `START_CONNECT` |
| `START_CONNECT` 收到 _result，code 为 `NetConnection.Connect.Success` | 调用 CretaeStream，设为 `START_CREATE_STREAM` |
| `START_CREATE_STREAM` 收到 _result，取得的 Number 大于 0 | 保存 `stream_id_`，调用 Publish，设为 `START_PUBLISH` |
| `START_PUBLISH` 收到 onStatus，code 为 `NetStream.Publish.Start` | 设 `is_publishing_=true`；枚举仍为 `START_PUBLISH` |

实际的 AMF decoder 保存有限的当前值和对象字段，不返回完整的类型化参数列表。createStream 应答中最后解析到的 Number 被拿来当流 ID；客户端没有核对 `_result` 的事务号，主要根据当前 `state_` 判断应答用途。

用一个固定例子区分编号：

```text
connect：事务号 1
createStream：事务号 2
createStream 的 _result 负载返回流 ID 7
publish：本实现事务号 3，消息头中的流 ID 为 7，CSID 为 3
```

这里两个 3 恰好相同，含义不同；Message Stream ID 7 又与它们无关。

### 3.4 失败不一定会回退或关闭

| 收到的内容 | 当前处理 |
| --- | --- |
| connect 的 _result 缺少或不是成功 code | 保持原态，仍返回 true |
| `_error` 方法 | 没有对应分支 |
| 发布 onStatus 有 code，但不是 `NetStream.Publish.Start` | 返回 false，最终经读回调失败进入 Close |
| onStatus 没有 code | 返回 true，保留原态 |

所以日志没有成功应答时，应先看方法名、code 和状态分支，不能只等某个 bool 自动变为成功或失败。

## 4. 发布状态与首关键包是否真的限制了发送

### 4.1 is_publishing_ 当前没有成为发送门槛

`is_publishing_` 被发布成功应答置 true，但当前媒体发送路径没有读取它。结合阶段六的启动顺序：

```text
OpenUrl 刚发起握手就返回
  → 管理器启动音视频编码线程
  → IsConnected 可能已为 true
  → 编码输出进入 PushVideoFrame / PushAudioFrame
```

这些媒体入口和 `SendVideoData / SendAudioData` 没有确认握手完成、流 ID 有效或发布已获准。因此应答较慢时，媒体可能在握手、createStream 或发布成功之前就被提交，甚至使用初始流 ID 0。

这不是说每次运行都必然失败，而是当前代码没有保证“先收到发布成功，再发媒体”的顺序。

### 4.2 has_key_frame_ 只表示本地曾识别到关键包

`RtmpPublisher::PushVideoFrame()` 首次判定为关键包时，实际顺序是：

```text
Publisher.has_key_frame_ = true
  → SendVideoData(0, AVC 配置)
  → SendVideoData(0, AAC 配置)    // 当前源码也走视频入口
  → 封装并提交本次普通视频数据
```

标志在配置发送调用前就已置 true，既没等待发布应答，也没检查配置是否成功发送。普通 AAC 发送受 `has_key_frame_` 限制，所以视频关键包识别也会影响声音何时开始提交。

首个关键包之前，非关键视频直接返回 0，普通 AAC 也跳过提交并返回 0。这些数据没有缓存等待补发；这里的 0 不能解释为已经提交媒体。

这里说的是 **Publisher 的成员**；`RtmpConnection` 中另有同名字段，未负责这条门槛。

### 4.3 关键包检测要结合前一层的输入

阶段六的管理器 `PushVideo()` 固定去掉输入前 4 字节，再交给 Publisher。Publisher 的 `IsKeyFrame()` 在选定偏移后，用 `data[startcode] & 0x1F` 取低五位作为 NAL 类型，并将类型 5 或 7 视为关键包：

| 传给 Publisher 的开头 | 当前检测的含义 |
| --- | --- |
| 直接以 `65` 或 `67` 开头 | 可按类型 5/7 判 true |
| `00 00 01 ...` | 没正确跳过三字节起始码 |
| `00 00 00 01 ...` | 当前错误使用偏移 3，读到 `01`；四字节分支还被前一条件覆盖 |

因此不能把这段代码解释成完整的起始码解析器，也不能反过来断言所有视频都检测失败：管理器先删四字节可能掩盖一部分问题。

类型 5 是 IDR 图像切片，IDR 图像可作为刷新参考图像关系的解码起点；类型 7 是 SPS 参数，并不证明输入含完整的 IDR 图像。当前只看开头，编码封装又可能拼接多个 NAL，要结合实际输入字节判断 `has_key_frame_` 为什么被设置。类型定义可对照 [FFmpeg 的 H.264 定义](https://github.com/FFmpeg/FFmpeg/blob/n8.0/libavcodec/h264.h)。

## 5. AVC/AAC 配置和普通数据怎样封装

以下按当前代码对照传统 FLV 的媒体负载格式；RTMP 发送的是这类**媒体负载**，不需要把 FLV 文件头、完整文件 Tag 头和 PreviousTagSize 一起放进去。格式参考 [Adobe FLV 原始规范 Annex E.4.2、E.4.3](https://veovera.org/docs/legacy/video-file-format-v10-1-spec.pdf)。

### 5.1 AVC 配置：让接收方知道怎样解释 H.264

`SetMediaInfo()` 按一个 SPS、一个 PPS 构造：

```text
17 | 00 | 00 00 00 | AVCDecoderConfigurationRecord
```

前五字节由当前代码写入：

- `17`：高 4 位标记关键帧，低 4 位 CodecID=7，表示 AVC。
- `00`：AVCPacketType，表示配置。
- 三个 `00`：配置中的 CompositionTime 为 0。

后面的配置记录写入版本 1、SPS 中的 profile/兼容性/level、`FF`、`E1`，再写 SPS/PPS 长度和字节。`FF` 的低两位是 `lengthSizeMinusOne=3`，所以后续每个 NAL 的长度字段占 `3+1=4` 字节；`E1` 的低五位表示这里有一个 SPS。SPS/PPS 的长度字段各用两个大端字节。可对照 [FFmpeg 的 AVC 配置构造](https://github.com/FFmpeg/FFmpeg/blob/n8.0/libavformat/avc.c)。

这些参数来自阶段六复制的编码配置。`SetMediaInfo()` 主要准备配置负载，当前主调用没有因此自动发送一条完整的 `onMetaData` 命令。

### 5.2 普通 AVC：每个 NAL 都需要自己的长度

Publisher 当前写入的是：

```text
17 或 27 | 01 | 00 00 00 | 四字节整个输入 size | 整个输入
```

`17 / 27` 分别是当前检测结果下的关键/非关键标记；`01` 表示 AVC NALU 数据。CompositionTime 是 `PTS - DTS`，需先统一时间基，按毫秒写入有符号 24 位字段；当前固定写 0，没有从编码 Packet 计算。可对照 [FFmpeg 的 FLV 封装实现](https://github.com/FFmpeg/FFmpeg/blob/n8.0/libavformat/flvenc.c)。

若输入确实是一个**没有起始码的 NAL**，可以用以下纯结构例子核对：

```text
假定 NAL 共 6 字节：65 11 22 33 44 55

媒体负载：
17 01 00 00 00 | 00 00 00 06 | 65 11 22 33 44 55
前缀 5 字节    | NAL 长度 4 字节 | NAL 6 字节
总长 = 15 字节
```

NAL 内容仅用于字段计数，不是可解码测试向量。输入若含多个 NAL，应为“长度1 + NAL1 + 长度2 + NAL2……”；把多个 NAL 连同内部起始码一起包成一个总长度，不能完成这种转换。当前管理器固定去掉前四字节，也不会自动替其余 NAL 分别生成长度。

### 5.3 AAC 配置的字节与消息类型要同时看

`SetMediaInfo()` 构造的 AAC 配置为：

```text
AF | 00 | AudioSpecificConfig
```

普通 AAC 为：

```text
AF | 01 | raw AAC
```

这里 `AF` 的高四位表示 AAC，第二字节区分配置与普通数据；实际采样率和声道配置要看 AudioSpecificConfig，不能单凭 `AF` 推断为固定采样率或双声道。当前路径没有给普通 AAC 另加 ADTS 头。

确定的实现差异在**外层消息类型**：

| 提交内容 | 当前调用 | 最终 Message Type ID |
| --- | --- | --- |
| AVC 配置 | SendVideoData | 9，视频 |
| AAC 配置 | SendVideoData | **9，视频；应走音频类型 8** |
| 普通 AVC | SendVideoData | 9，视频 |
| 普通 AAC | SendAudioData | 8，音频 |

因此 AAC 配置的内部字节以 `AF 00` 开头，也不能抵消外层 type_id 错误。接收方按消息类型分派，不会仅凭变量名把它送进正常音频配置处理路径。

### 5.4 时间戳和配置零时刻

两个配置发送调用都传时间戳 0。普通音视频从 `Timestamp::Elapsed()` 取得毫秒数，计时起点是 Publisher 构造，不是发布成功或首帧时刻；当前 Publisher 没有调用 Reset。

按传统 AVC 封装约定，视频消息时间戳对应解码时刻 DTS，呈现时刻等于该时间戳加 CompositionTime。当前代码直接使用运行时长并把偏移固定为 0，没有从编码 Packet 的 PTS/DTS 建立这套映射。阶段六的视频 PTS 问题与这里的发布时钟仍需分别追踪；封装实现可对照第 5.2 节的 FFmpeg 源码。

## 6. 从 Message 到 Chunk：复用样例，核对客户端

### 6.1 两个方向的块大小独立

`RtmpChunk` 的 in/out 初值都是 128。管理器先调用 `Publisher::SetChunkSize(60000)`，连接构造时保存这个值；握手完成后 `RtmpConnection::SetChunkSize()` 才更新本端 out，并发送类型 1 通知。

通知负载是四字节大端整数 `00 00 EA 60`，不是 AMF Number。本端 in 仍保持原值，直到收到对端的 Set Chunk Size 才更新，不能写成握手后两个方向都自动变成 60000。

`HandleMessage()` 当前主要处理 AMF0 命令和 Set Chunk Size，其他控制消息没有对应完整处理；本讲不把它描述为支持所有 RTMP 控制交互。

### 6.2 CreateChunk 使用哪些字段

`SendVideoData / SendAudioData` 设置 `type_id、_timestamp、stream_id、playload、lenght`；`SendInvokeMsg` 则设置命令类型，并使用默认的零时间戳。

`CreateChunk()` 当前为每条消息建立 fmt0 首块，后续块用 fmt3。24 位时间、消息长度和扩展时间戳使用大端，fmt0 中 Message Stream ID 使用小端；这是必须保留的字节序区别。

一条消息的全部 Chunk 在本函数中连续生成，再整体传给 TCP 队列。有 Chunk 结构，不代表当前发送器已实现按 Chunk 自动交错调度多个消息。

### 6.3 对照共同基础的 300 字节样例

继续使用已学过的固定条件：CSID=3、Message Stream ID=1、type=20、timestamp=0、payload=300、out Chunk Size=128。

```text
首块头：
03 | 00 00 00 | 00 01 2C | 14 | 01 00 00 00
CSID | 时间      | 长度300    | 类型20 | 流ID1，小端

首块：12 字节头 + 128 字节负载
续块：C3 + 128 字节负载
末块：C3 +  44 字节负载
总长：12 + 128 + 1 + 128 + 1 + 44 = 314 字节
```

这仍是分块结构样例，300 字节填充内容不等于合法 AMF 命令。客户端主路径把 out 设为 60000 后，同样 300 字节只需一个首块，共 312 字节；不用再建立一套样例工程。

### 6.4 调用副作用和扩展时间戳

`CreateChunk(RtmpMessage&)` 会边复制边减 `lenght`，结束时变为 0。断点应在调用前看消息原长度，调用后看返回的 Chunk 总字节数；不能将调用后的 0 解释为消息原本没有内容。

内部容量预估没有计入完整首头：上述样例估算 310，实际需要 314。外层 `SendRtmpChunks` 另加 1024 提供余量，但这不使内部预检查变成完整容量保证。

扩展时间戳还有当前字段差异：

```text
媒体发送设置 _timestamp = 0x01000000
  → 24 位时间写 FF FF FF
  → CreateChunk 实际写 extend_timestamp
  → 该字段未同步填写时，仍可能写出 00 00 00 00
```

这个算例足以定位问题，不需要等运行时长自然跨过边界。扩展 CSID 与头部继承等通用解析边界复用 ENET 阶段八，避免在本讲重复展开协议解析器。

## 7. 缓冲、复制和线程在哪里交接

### 7.1 共享持有不等于全程没有复制

当前主要发送链：

```text
编码输出
  → Manager::Push* 复制到临时数组
  → Publisher 复制编码字节，组成媒体 payload
  → RtmpMessage 共享持有 payload
  → CreateChunk 复制到独立 Chunk 数组
  → Send(buffer.get(), size)
  → BufferWriter 的 char const* 入口再次复制入队
  → 立即尝试发送，未完成则等可写续发
```

因此局部 Chunk 数组结束后，队列仍保存自己的字节副本。AMF 的 `data()` 虽共享内部缓冲、`reset()` 只重置索引，但当前命令会先同步复制到 Chunk 和 TCP 队列，下一次 reset 不会改写已经入队的副本。

另一处需要区分的释放缺口是 AAC 配置：当前 `shared_ptr<char>::reset(new char[...])` 没有使用数组删除器，不能把“有 shared_ptr”直接解释为数组释放方式正确。完整销毁关系留阶段九。

### 7.2 命令、媒体与续发可能来自不同线程

| 行为 | 当前执行线程 |
| --- | --- |
| 主信令流程调用 OpenUrl、发 C0+C1 | 信令调度线程，复用阶段五、六的入口 |
| RTMP OnRead、命令应答及后续命令 | RTMP 连接所属调度线程 |
| 视频/音频编码完成后调用 Push* | 各自编码工作线程 |
| TCP 可写事件续发 | RTMP 连接所属调度线程 |

`SendRtmpChunks()` 没有把调用自动投递到网络线程。后续写队列行为复用阶段四；生成了 Chunk、方法返回 true 或日志打印成功，都不等于数据已入队、远端已受理或画面已显示。

## 8. 关闭和重新发布只追到本阶段边界

### 8.1 DeleteStream 被调用，不代表命令已经发出

当前关闭路径：

```text
Publisher::Close
  → DisConnect
  → TcpConnection::Close 先设 is_closed_=true
  → 关闭回调 OnClose
  → DeleteStream 组包并设 START_DELETE_STREAM
  → SendInvokeMsg 因 IsClosed() 返回 false
```

所以这条路径没有完成“先通知远端取消发布，再关闭连接”的过程。`DeleteStream()` 自身仍返回 true，也不能拿它证明发送成功。

当前命令名为大写 `DeleteStream`，ENET 也识别这个拼写；原始规范 Commands 第 4.2.3 节使用小写 `deleteStream`，事务号为 0，并说明不发送应答。客户端 `HandleOnStatus()` 中对 `NetStream.Unpublish.Success` 的期待，不是标准 deleteStream 必然返回的确认。标准依据同第 3.2 节的 RTMP 规范。

### 8.2 分清重用 Publisher 与主流程新建对象

`Publisher::Close()` 在连接存在分支重置其 `has_key_frame_`。直接重用同一个 Publisher 调 `OpenUrl()` 时，替换连接分支没有统一重置这个标志或计时器，不能将它理解为全新发布会话。

主 `RtmpPushManager::Init()` 则每次创建新 Publisher，属于另一条对象路径。完整的停止编码、断开网络、释放对象与重开顺序，在阶段九统一检查。

## 9. 学习步骤：状态、媒体与分块推演

1. 先复用 ENET 阶段八的共同基础与离线结果，再沿 `OnRead → HandleInvoke → HandleResult / HandleOnStatus` 追一遍状态，使用“事务号 1、2、3，流 ID 7”算例区分编号。
2. 沿首关键包分支核对 AVC/AAC 配置的字节、Message Type ID 和时间戳；再用第 5.2 节的 6 字节 NAL 算出 15 字节媒体负载。
3. 对照共同分块样例与客户端 `CreateChunk`，确认 128 和 60000 两种 out 配置下的分块数量，以及调用前后的长度含义。

已有可用连接与调试条件时，任选一个观察点：发布应答进入 `HandleOnStatus` 时查看 code 与状态；或在 `SendVideoData / SendAudioData` 看消息类型，再跟到 `CreateChunk`。没有运行条件时完成上述静态推演即可，不要求为本阶段另搭服务、重写协议组件或制造所有失败。

完整一推一拉、配置与媒体能否被消费，以及正常停止的验收，继续复用 ENET [阶段十二](https://github.com/slimesq/ENET/blob/main/docs/项目学习规划.md#stage-12)。

## 10. 完成标准与后续衔接

- [ ] 能跟通握手、connect、createStream、publish 和发布应答的入口。
- [ ] 能区分各层编号、连接状态、发布状态与实际媒体就绪。
- [ ] 能说明配置和普通媒体的当前发送条件及封装差异。
- [ ] 能把负载追到 Message、Chunk 与 TCP 队列，解释复制和实际线程。
- [ ] 能指出关闭、重开及协议处理的当前边界，不把函数返回当作完整会话成功。

随后进入[阶段八：拉流、解码与画面声音输出](项目学习规划.md#stage-8)，继续阅读控制端怎样消费收到的媒体。
