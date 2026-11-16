# ECloudAssistant 阶段三：Qt 登录链与二进制协议学习讲义

> 对应[项目学习规划的阶段三](项目学习规划.md#stage-3)。参考用时：6～8 小时，额外基础补学、环境准备与排障另计。
>
> 本阶段从客户端追踪负载查询、登录服务切换、报文收发和登录通知，读到 `RemoteManager::Init()` 的入口为止。已有 TCP 知识直接复用，重点补 Qt socket、接收缓冲和当前协议的实际用法。

## 1. 学完后应能回答什么

1. 两次请求分别发给谁，两种响应中的地址各指向哪个服务？
2. `QTcpSocket` 的连接、等待、写入、可读通知和 `readAll()` 分别说明什么？
3. 四种报文的 `len/cmd`、字段偏移、字节序、字符串和时间戳怎样解释？
4. 两类响应都使用 `Login` 命令，代码为什么能进入不同分支，又依赖哪些前提？
5. 登录按钮、账号密码输入和真正发送 `UserLogin` 分别发生在什么时候？
6. 半包、合并到达、连接失败和登录失败，在当前实现中怎样处理？

Qt 对象、事件循环和 signal/slot 基础沿用[阶段二讲义](阶段二应用入口与Qt业务编排学习讲义.md)。TCP 字节流与公共布局知识复用 ENET [阶段二](https://github.com/slimesq/ENET/blob/main/docs/阶段二TCP字节流与自定义协议学习讲义.md)，服务分工与数据库内部复用 ENET [阶段六](https://github.com/slimesq/ENET/blob/main/docs/阶段六负载分配登录与数据库学习讲义.md)。

先读第 2 节的通信顺序，再补第 3 节中不熟的 Qt API；随后按报文定义、正常路径、接收处理和失败分支阅读。最后选择一次实际响应观察或不联网的字节推演即可。

## 2. 先分清两次请求与两种地址

### 2.1 负载服务提供登录入口

本阶段的四种消息是：

```text
客户端                         负载服务
  ├─ Login_Info ────────────────>│ 查询登录入口
  │<─ LoginReply ────────────────┘ 返回登录服务 IP、端口
  │
  │ 尝试关闭旧连接，使用同一个 QTcpSocket 连接登录服务
  │
客户端                         登录服务
  ├─ UserLogin ────────────────>│ 提交识别码、账号、密码、时间戳
  │<─ LoginResult ──────────────┘ 返回结果码；成功时附信令服务地址
  │
  └─ sig_logined(信令 IP, 端口)
       → RemoteWgt::handleLogined
       → RemoteManager::Init
```

负载服务不替客户端转发后续登录请求。客户端收到登录地址后，会直接连接登录服务；成功通知中的信令地址，则供远控模块建立另一条连接。

同一个 `QTcpSocket` 对象先后服务于负载连接和登录连接，但这仍是两条不同的 TCP 连接、两段不同的字节流。后面的信令连接由 `RemoteManager` 使用项目网络库另行创建，不是把这个 Qt socket 交过去。

### 2.2 对照这些文件阅读

| 文件 | 本阶段关注什么 |
| --- | --- |
| [UI/tool/defin.h](../UI/tool/defin.h) | 五个类型：包头，以及四种请求、响应 |
| [LoginWgt.h](../UI/center/LoginWgt.h) | socket、两个布尔标志和槽函数声明 |
| [LoginWgt.cpp](../UI/center/LoginWgt.cpp) | 自动查询、按钮、地址切换、收发和结果分派 |
| [RemoteWgt.cpp](../UI/center/RemoteWgt.cpp) | 接收信令地址，读取本机识别码 |
| [RemoteManager.cpp](../UI/center/RemoteManager.cpp) | 被控端信令初始化入口 |

客户端头文件把登录、信令、输入等定义放在一起，本阶段只展开登录所需的四种报文。后续命令仍在相应阶段阅读。

## 3. 先补本端使用的 QTcpSocket API

### 3.1 连接与等待不是同一件事

`QTcpSocket` 继承 `QAbstractSocket`，提供 TCP 字节流通信。在本项目里，它以 `LoginWgt` 为 parent 创建，正常调用和通知处理都在 GUI 线程，见 [QTcpSocket](https://doc.qt.io/qt-6/qtcpsocket.html)。

| 当前代码 | 能确定什么 |
| --- | --- |
| `connectToHost(ip, port)` | 发起连接尝试，函数返回不代表连接已建立 |
| `waitForConnected(1000)` | 阻塞当前线程等待连接结果，参数单位为毫秒；true 表示连接已建立 |
| `state()` | 查询 Qt socket 当前连接状态 |
| `peerAddress() / peerPort()` | 已连接时核对实际对端 |
| `errorString()` | 查看最近错误的说明 |

异步写法用 `connected` 等信号接收后续结果；当前代码则在发起连接后立即等待。`waitForConnected()` 只等 TCP 连接，不等账号校验、登录响应或信令登记；在 GUI 线程等待会影响界面响应。两种方式可对照 [Fortune Client](https://doc.qt.io/qt-6/qtnetwork-fortuneclient-example.html) 理解。

### 3.2 写入与 flush 不是业务确认

项目发送消息的典型代码是：

```cpp
socket_->write((char const*)&info, info.len);
socket_->flush();
```

这里从对象地址开始提交 `info.len` 个字节。`write()` 返回接受的字节数，失败时返回 `-1`；`flush()` 非阻塞地尝试将内部待写数据交给底层 socket。两者都不能证明对端已经完整接收或处理请求。

当前代码没有检查这两个返回值。看到发送语句执行过，应先解释为“代码尝试提交了这些字节”，不能直接当成“服务器已收到请求”。API 说明见 [QAbstractSocket::flush](https://doc.qt.io/qt-6/qabstractsocket.html#flush)。

### 3.3 readyRead、Qt 缓冲与业务缓冲

`readyRead` 表示有新的数据可读，`readAll()` 取出当前可读数据；它们都不承诺恰好对应一条完整业务消息，见 [QIODevice](https://doc.qt.io/qt-6/qiodevice.html#readyRead)。

核对字节数时要区分读取前后：`socket_->bytesAvailable()` 是调用当时 socket 中尚可读取的字节数；`readAll()` 返回后，`buf.size()` 才是本次已经取出的字节数。此时再查 `bytesAvailable()` 可能为 0，表示当前 socket 缓冲已读空，不表示 `buf` 也是空的。

```text
TCP 字节到达
  → QTcpSocket 的内部接收缓冲
  → readyRead 通知
  → readAll() 将当前可读数据取到 QByteArray
  → 应用按协议识别完整消息，并保留尚未处理的字节
```

最后一步是应用的责任。Qt 保留尚未被读取的数据，但不会替项目把已经由 `readAll()` 取出的局部字节数组与下次读到的数据自动拼成完整包。

Fortune 示例使用 `QDataStream` 及其配套编码处理字符串，可以借它认识 Qt 的连接和接收方式。本项目直接发送结构体内存，没有使用这套编码，不能直接用示例中的 `QDataStream >> QString` 解码这里的包。

## 4. 按字段解释当前二进制报文

### 4.1 包头和长度的含义

[defin.h](../UI/tool/defin.h) 的包头只有两个字段：

```cpp
uint16_t len;
uint16_t cmd;
```

当前布局中包头为 4 字节。具体消息构造函数设置 `len = sizeof(消息类型)`，所以 `len` 是**包含包头的整包字节数**，不是包体长度。

在正常登录路径中，四种消息均使用 `Login = 2`。因此，只有 `cmd` 还不足以决定具体类型，必须结合当前连接的服务和所处步骤。负载的部分失败分支会改发 `ERROR`（客户端名为 `ERROR_`），详见第 7.1 节。

### 4.2 当前大小与字段偏移

以下数值已用当前 MSVC x64、C++17 的临时探针核对；偏移从完整消息的起始地址计算。它们与 ENET 对应定义及既有讲义一致。

| 类型、方向 | 整包大小 | 包头之后的字段：偏移、字节数 |
| --- | ---: | --- |
| `Login_Info`，客户端 → 负载 | 12 | `timestamp`：4，8 |
| `LoginReply`，负载 → 客户端 | 22 | `port`：4，2；`ip`：6，16 |
| `UserLogin`，客户端 → 登录 | 77 | `code`：4，20；`count`：24，12；`passwd`：36，33；`timestamp`：69，8 |
| `LoginResult`，登录 → 客户端 | 26 | `resultCode`：4，4；`port`：8，2；`ctrSvrIp`：10，16 |

包头的 `len` 在偏移 0，占 2 字节；`cmd` 在偏移 2，占 2 字节。本次 `Cmd` 为 2 字节，`ResultCode` 为 4 字节。

头文件使用 `#pragma pack(push, 1)` 限制成员对齐，并在末尾恢复。它不负责转换字节序，也不会固定没有显式底层类型的 `ResultCode` 的宽度。换工具链或编译选项时，仍要核对实际布局，见 [MSVC pack pragma](https://learn.microsoft.com/en-us/cpp/preprocessor/pack?view=msvc-170)。

当前双方直接发送对象内存，没有逐字段转换为网络大端。在当前小端布局中，长度 26 是 `1A 00`、命令 2 是 `02 00`。socket 地址中的 `htons(port)` 不会同时转换报文里的 `port` 字段。

这些派生消息的基类与自身都有数据成员，不能当作可无条件跨编译器复制的标准序列化格式。字节布局相符，也不意味着把任意接收字节强转为结构体指针，就已经满足长度、对齐和 C++ 对象访问的全部条件。公共说明复用 ENET 阶段二。

### 4.3 结合具体字段区分响应

| 字段或常量 | 本阶段的含义 |
| --- | --- |
| `LoginReply.ip / port` | 下一步要连接的登录服务地址 |
| `LoginResult.resultCode` | 登录处理结果；成功值为 `S_OK_ = 0` |
| `LoginResult.ctrSvrIp / port` | 成功结果中的信令服务地址，失败时不应读取为有效地址 |
| `timestamp` | 客户端 `GetTimeStamp()` 使用 `toSecsSinceEpoch()` 取得的纪元秒 |
| 客户端 `ERROR_` / 服务端 `ERROR` | 都是命令值 1，名称差异不构成字节值差异 |
| 客户端 `S_OK_` / 服务端 `S_OK` | 都是结果值 0 |

客户端连接等待的 1000 毫秒与服务端请求时间戳的校验是两回事。当前负载、登录服务用系统时钟的秒计数检查请求，源码阈值为 60 秒；时间单位、两端时钟偏差和实现中的差值类型都会影响结果，细节沿用 ENET 阶段六。

这里的 IP 是固定容量字符数组中的地址文本，端口则是二进制整数。`"6539"` 四个数字字符与整数端口 6539 不是同一种字段表示。

### 4.4 UserLogin 的字段怎样被填入

实际发送前的代码位于 `LoginWgt::HandleLoadLogin()`：

```cpp
UserLogin login;
QString acount = acountEdit_->text();
QString passwd = passwordEdit_->text();
login.SetCode("345");
login.SetCount(acount.toStdString());
login.SetPasswd(passwd.toStdString());
login.timestamp = GetTimeStamp();
socket_->write((char const*)&login, login.len);
```

`count` 是协议字段名，用来存账号；`acountEdit_` 沿用源码拼写。识别码固定为 `"345"`，与远程页本机码输入框没有自动绑定。

字符数组的容量按字节计算。当前 setter 使用 `std::string::copy()`，它不会追加 `'\0'`，参见 [C++ string::copy](https://eel.is/c++draft/string.copy)。

`UserLogin` 的构造函数只设置 `len/cmd`，没有清零数组；发送代码虽然给 `timestamp` 赋了值，却没有补齐字符串剩余区。因此：

- 短输入复制后，不能假定后面的字节为零；空输入也不能保证字段得到一个空字符串。
- 输入达到或超过容量时，会受复制上限限制，也没有自动保留字符串终止位置。
- `GetCode()` 等使用 `std::string(array.data())`，要求有效范围内存在终止符，否则不能可靠地按 C 字符串读取。

`UserLogin` 有自行实现的默认构造函数，只把局部声明改成 `UserLogin login{};` 也不会自动清零这些未初始化成员；要结合构造函数实际做了什么判断初始化结果，见 [C++ 初始化规则](https://eel.is/c++draft/dcl.init)。

`passwd` 的 33 字节容量旁虽然有 `//Md5` 注释，当前发送路径没有计算 MD5，而是复制输入框转换后的字节。

接收字段也要看实际生产方：当前服务端 `LoginResult::SetIp()` 已限制复制并补终止符，客户端同名 setter 仍使用 `string::copy`。不能仅看客户端 setter 就认定服务器成功响应的 IP 必然缺少终止符；也不能据此认为其他字符数组都已处理完整。

## 5. 按执行顺序追踪 LoginWgt

### 5.1 构造函数自动查询负载

`LoginWgt` 创建 `QTcpSocket(this)`，连接 `readyRead → ReadData` 后，立即尝试连接源码中的 `192.168.31.30:8523`。

`waitForConnected(1000)` 为 true 时，它将 `is_connect_` 置为 true，构造 `Login_Info`、填写时间戳并发送。这个查询只有包头与时间戳，没有账号、密码。

因此，第一步不是由用户点击登录才开始。服务响应足够快时，后续自动登录请求可能在用户输入账号、密码之前就已发送。

### 5.2 第一种 Login 响应如何进入地址切换

当前分派路径是：

```text
readyRead
  → ReadData：readAll()
  → HandleMessage：查看 cmd
  → cmd == Login → 先转成 LoginResult*，进入 HandleLogin
  → is_login_ == false → 再按 LoginReply* 交给 HandleLoadLogin
```

指针转换本身不会自动读取一整个 `LoginResult`，也不会验证长度。当前 false 分支没有读取 `resultCode`，而是继续按 `LoginReply` 的布局读取登录地址；不能因为中间出现过 `LoginResult*` 就把负载响应解释成 26 字节。

`HandleLoadLogin()` 将返回的登录地址保存到 `LoginWgt::ip_ / port_`，然后执行：

```cpp
socket_->disconnectFromHost();
is_connect_ = false;

socket_->connectToHost(ip_, port_);
if (socket_->waitForConnected(1000))
{
    is_connect_ = true;
    is_login_ = true;
    // 随后读取输入框、构造并发送 UserLogin
}
```

这是同一个 socket 对象上的连接切换尝试。`disconnectFromHost()` 是关闭请求；待写数据尚未处理完时可能处于 `ClosingState`，之后才进入 `UnconnectedState`。当前代码立即调用 `connectToHost()`，没有确认旧连接已经断开，不能断言每次一定切换成功或一定失败，见 [QAbstractSocket::disconnectFromHost](https://doc.qt.io/qt-6/qabstractsocket.html#disconnectFromHost)。

这条正常路径还依赖响应类型正确、数据足够完整、地址有效。实际切换进度应结合 `state()` 与对端信息观察，而不是仅看两个布尔值。

### 5.3 连接登录服务后自动发送 UserLogin

只有本次连接等待成功，才进入上面的 if 分支。在这里读取当时的账号和密码输入框，填写固定码及时间戳，然后发送 77 字节的 `UserLogin`。

输入框没有非空验证。`QLineEdit::Password` 只改变密码显示方式，不会补齐字符串、计算 MD5 或完成服务端认证。

当前登录按钮的 lambda 始终只发送 `Login_Info`：

```text
clicked
  → socket_ 非空且 is_connect_ 为 true
  → 构造 Login_Info、填写时间戳
  → 写到当前 socket
```

它不负责发起连接，也不直接构造 `UserLogin`。已经切到登录服务后再点击，入口查询包会被尝试发到当前登录连接，不能解释为重新提交完整登录请求。

### 5.4 第二种 Login 响应怎样发出通知

在登录服务连接等待成功后，`is_login_` 已为 true。此时预期的响应是 `LoginResult`：

```text
readyRead → ReadData → HandleMessage → HandleLogin
  → is_login_ == true
  → resultCode == S_OK_
  → 读取信令 IP、端口
  → emit sig_logined(IP, port)
  → RemoteWgt::handleLogined
  → 本机码非空时保存信令地址并调用 RemoteManager::Init
```

`LoginWgt::ip_ / port_` 保存的是登录服务地址；`RemoteWgt` 中同名成员保存的是通知交来的信令地址。它们属于不同对象，不能混为同一组地址。

后面的被控端信令初始化沿用阶段二。登录收到成功结果、信令 TCP 连接成功、被控端登记完成，是不同完成点。

### 5.5 两个标志实际上记录什么

下表描述正常单次路径中的赋值，并非完整连接状态机：

| 时刻 | `is_connect_` | `is_login_` |
| --- | --- | --- |
| 构造初值 | false | false |
| 负载连接等待成功 | true | false |
| 请求关闭负载连接后 | false | false |
| 登录服务连接等待成功 | true | true |
| 登录服务返回成功或失败结果 | true（结果分支不更新） | true（结果分支不更新） |

`is_login_` 用于选择响应解释分支，在通过登录校验之前就被置为 true。`is_connect_` 也只是代码保存的结果，当前没有用断开、错误等信号持续同步它，不是 `socket_->state()` 的替代。

同一个 `Login` 命令能被这样分派，依赖“当前阶段确实收到预期服务的对应消息”。多次点击、重复响应或状态没有复位时，不能只靠这个命令值保证解释正确。

## 6. readAll 之后还缺哪些消息边界处理

### 6.1 当前实现只处理一次局部字节数组

现有 `ReadData()` 的核心是：

```cpp
QByteArray buf = socket_->readAll();
if (!buf.isEmpty())
{
    HandleMessage((packet_head*)buf.data());
}
```

它没有先确认 4 字节包头，没有按 `len` 检查完整包，也没有向处理函数传入实际可用字节数。`HandleMessage()` 只分派一次，没有循环消费后面的消息。

`buf` 是本次调用的局部对象。已经取出的半包或第二条消息，不会留在 Qt socket 内等下次自动补齐；当前函数也没有把未处理部分保存在跨回调的缓冲里。

### 6.2 用一条响应推演分片与合并

下面手工构造一条 `LoginResult` 成功响应，地址字段选 `127.0.0.1:6539`，仅用于不联网的字节推演。它不是本次运行的抓包结果，也不是要求使用的服务配置。

按第 4 节的小端布局，其 26 字节为：

```text
len     cmd     resultCode       port
1A 00 | 02 00 | 00 00 00 00 | 8B 19 |
IP 文本与补零，共 16 字节
31 32 37 2E 30 2E 30 2E 31 00 00 00 00 00 00 00
```

假定当前处于登录服务阶段，即 `is_login_ == true`，对照以下到达方式：

| 同一 TCP 连接上的数据 | 组帧应怎样处理 | 当前实现的问题 |
| --- | --- | --- |
| 先到 2 字节 `1A 00`，之后再到其余 24 字节 | 包头未齐，先保留 2 字节，收齐后再解析 | 非空就尝试读 `cmd`，缺少包头长度检查 |
| 先到前 12 字节，之后再到其余 14 字节 | 已知整包长 26，等待补齐 | 可能提前进入成功分支读取尚未收齐的 IP |
| 两条完整响应一起到达，共 52 字节 | 循环处理两个完整消息 | 只分派第一条，后续字节随局部数组丢弃 |
| 一条完整响应再加下一条的前 2 字节，共 28 字节 | 处理第一条，保留 2 字节尾部 | 第一条之后的半包尾部没有保留 |

越过实际可读范围后，可能出现错误字段、状态变化、非法访问等结果，不能保证只表现为某一种固定报错。

整包收齐后，还要确认阶段与类型匹配。例如，若将上面的 26 字节成功响应交给错误的 `is_login_ == false` 分支，代码会按 `LoginReply` 从偏移 4 读到端口 0，从偏移 6 读到以零字节开头的 IP 字段，得到空字符串。字节完整并不能替代正确的类型判断。

`LoginReply` 与 `LoginResult` 正常来自切换前后的不同连接，不把它们拼成同一条 TCP 字节流推演。这里的多包样例只是解释接收职责，不要求服务器在正常登录时重复发送结果。

### 6.3 能说明正确的处理顺序即可

结合 ENET 阶段二，Qt 端同样需要完成这些职责：

```text
readAll() 取出的字节 → 追加到跨回调保存的接收缓冲
循环：
  不够 4 字节包头 → 保留并等待
  读取 len/cmd，校验长度范围
  不够 len 字节 → 保留并等待
  根据当前连接阶段、cmd 和该类型所需长度解释一条消息
  消费恰好 len 字节
  继续查看剩余字节
```

本阶段的两种响应固定为 22、26 字节，可据当前阶段核对具体类型长度；还要区分 `ERROR_` 等分支，不是任意 `cmd` 都按成功结果读取。

接收缓冲属于相应连接的字节流。复用 socket 对象切换服务时，还要核对旧数据、缓冲和阶段怎样结束或重置，不能把两条连接的残片直接拼接。

这段顺序用于解释当前实现缺少什么。本阶段能推演完整包、半包与剩余字节的处理即可，不要求重写接收器或另外建立测试服务。

## 7. 对照失败分支与当前业务边界

### 7.1 失败时客户端实际做了什么

| 场景 | 当前客户端的处理 |
| --- | --- |
| 初次连接负载失败 | 没有设置 `is_connect_`；按钮本身不会重连 |
| 首次切换登录服务失败 | `is_connect_` 为 false，`is_login_` 仍为 false，没有按钮重新建连的流程 |
| 负载回复 `cmd == ERROR_` | `HandleError()` 只打印 `"error"` |
| 登录结果不是 `S_OK_` | `HandleLogin()` 没有相应提示、复位或重试处理 |
| 对端断开或出现 socket 错误 | 没有相应信号处理来持续更新两个布尔标志 |
| 重复点击登录 | 满足 `socket_ && is_connect_` 时向当前 socket 尝试发送 `Login_Info`，没有防重与请求关联；条件不满足时不发送 |
| 收到 `Register` 响应 | 进入当前为空的 `HandleRegister()` |

负载服务在请求超时等分支改变的是 `cmd`；登录服务的超时则通过 `LoginResult.resultCode = REQUEST_TIMEOUT` 表达。它们不是同一个字段，不能统一从 `resultCode` 读取所有错误。

未知命令目前只走 `default` 返回，输入框也没有完整的校验和错误展示。这些是阅读当前路径时要识别的边界，不把完善重连、提示和注册流程列为本阶段必做任务。

### 7.2 S_OK 当前代表什么

按 ENET 登录服务的分支意图，设置 `S_OK` 的条件是：请求未超时，按识别码查到用户，在线标记为 0。当前没有比较本次请求中的账号与密码，因此不能将 `S_OK` 当作已经完成密码认证。

成功分支返回的信令地址当前在服务端硬编码为 `172.21.205.121:6539`，并不证明该地址对当前 Windows 客户端可达。它与负载回复中的登录地址也不是同一种地址。

服务端数据库行指针与在线状态写入仍有 ENET 阶段六已说明的实现问题，所以这里是在解释分支意图，不能据此保证每次可靠完成登录。已有测试用户与环境沿用服务端讲义，本端不重复安排数据库实验。

当前界面没有完整注册流程，`HandleRegister()` 也是空实现；不要仅凭结构体和函数名就认定相应业务已能使用。

## 8. 选择一次局部观察或字节推演

已有可用服务和调试器时，可先观察一条负载响应；如果要观察登录成功分支，再复用 ENET 阶段六的测试用户。沿用阶段零的 Debug 产物：

1. 先在 `LoginWgt::ReadData()` 的 `if (!buf.isEmpty())` 行设置断点，再用已有调试器启动本次 Debug 产物。自动流程收到响应并停到该断点时，`readAll()` 已完成，`buf` 保存本次读取的数据。
2. 先看 `buf.size()`、`is_login_`、`socket_->state()` 和当前对端，再按第 4 节核对这次字节适合哪种响应。字节不足时不要直接把强转后的字段当作有效数据。
3. 若观察负载响应，跟到 `HandleLoadLogin()` 看登录地址与连接切换；若观察登录结果，查看结果分支，成功时跟到 `sig_logined` 的通知入口即可。

初次查询由构造函数发起，不需要额外点登录按钮来“触发第一次查询”。观察前先确认服务端实验条件、地址与两端时间；它们不具备时，不要求本阶段完成成功登录。

已有日志或服务端观察能说明同一响应的部分，直接复用。

没有调试器或服务条件时，使用第 6.2 节的一条响应推演分片与尾部，结合 `ReadData()` 解释当前行为即可。Qt API 仍不熟时再看 Fortune 示例；它不是本阶段另一个必须完成的工程。

本阶段实际遇到的接口差异，在[阶段十](项目学习规划.md#stage-10)对照，并复用 ENET 阶段十二的共同联调记录。

## 9. 自查与下一步

结合源码回答：

1. `Login_Info / LoginReply / UserLogin / LoginResult` 各发给谁，长度为何是 12 / 22 / 77 / 26？
2. 同为 `Login = 2`，当前代码用什么区分两种响应？这个做法依赖哪些前提？
3. 账号密码何时读取，点击登录按钮实际发送什么，两个类中的 `ip_ / port_` 分别保存哪种地址？
4. `waitForConnected`、`write`、`flush` 和 `readyRead` 各能证明到哪一步？
5. Qt 已有接收缓冲，为什么应用仍需保留半包、处理剩余消息？
6. 字符串初始化、字节序、时间戳与成功结果，各有哪些当前不能直接假定的条件？

完成标准：

- [ ] 能解释两次请求、两次响应、地址切换与通知入口。
- [ ] 能指出实际发送字段、当前工具链下的布局及字符串处理前提。
- [ ] 能推演一条响应分片或合并到达时的当前行为，说明完整组帧需要完成的职责。
- [ ] 能区分连接状态、阶段标志、登录结果和信令登记，明确实际观察与源码推演。

随后进入[阶段四：Winsock 与客户端事件循环](项目学习规划.md#stage-4)，再看项目自建网络库怎样调度、收发和关闭。Qt 文档以 Qt 6 为入口，查具体 API 时按当前构建所用主版本核对。
