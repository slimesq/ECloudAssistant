# ECloudAssistant 阶段二：应用入口与 Qt 业务编排学习讲义

> 对应[项目学习规划的阶段二](项目学习规划.md#stage-2)。参考用时：4～6 小时，额外基础补学、环境准备与排障另计。
>
> 本阶段沿窗口创建、侧栏切页、登录通知和开始远程四条路径，理解 Qt 怎样把界面与业务入口连接起来。已有 C++ 基础直接使用，Qt 不熟的部分先读概念，再对照项目；网络与媒体内部留到对应阶段。

## 1. 学完后应能回答什么

1. 主窗口、三个界面区域、四个页面和业务对象分别何时创建，由谁管理？
2. `QApplication`、`show()`、`exec()`、事件处理函数和 signal/slot 各做什么？
3. 侧栏行号为什么要加 1，谁真正切换页面，图标高亮由谁更新？
4. 登录通知传递哪些数据，如何进入被控端初始化？
5. 本机识别码与目标识别码分别进入哪个 API，开始远程时又创建了什么？
6. 哪些调用在 GUI 线程，哪些行为要继续到网络或媒体阶段才能确认？

构建和运行依赖沿用[阶段零讲义](阶段零工程与构建基线学习讲义.md)。对象、回调或并发的通用阅读方法按需查[阶段一讲义](阶段一已有基础复用与源码阅读方法学习讲义.md)。

阅读顺序是：先补第 2 节中不熟的 Qt 概念，再跟第 3～6 节的项目路径，最后看辅助界面与资源。能运行时，只需观察一次页面切换；登录或远控尚未具备条件时，继续沿源码确认入口。

## 2. 先补够本阶段用到的 Qt 概念

### 2.1 QObject、QWidget、parent 与布局

`QObject` 提供对象树、signal/slot 等基础能力；`QWidget` 在其上提供可见控件和窗口能力。业务对象继承 `QObject`，并不意味着它本身就是窗口。

在本项目中，重点区分三个关系：

| 关系 | 解决什么问题 | 项目中的例子 |
| --- | --- | --- |
| Qt parent | 对象归属；父对象析构时删除仍归它所有的子对象 | `new TitleWgt(this)`、`new QPushButton(this)` |
| 布局 | 控件放在哪里，空间怎样分配 | 主窗口的 `QGridLayout`，表单的 `QVBoxLayout` |
| 当前显示页 | 多个已有页面中，哪一个被显示 | `QStackedWidget::setCurrentWidget()` |

布局负责管理控件的位置，并可能将控件的 parent 调整为安装该布局的 QWidget。控件归属于这个 QWidget，而不是 QLayout。参见 [Qt 对象树与所有权](https://doc.qt.io/qt-6/objecttrees.html)、[Qt 布局管理](https://doc.qt.io/qt-6/layout.html)。

`QStackedWidget` 本身是控件，内部管理一组页面，一次显示其中一页。页面可以早已创建，暂时没有显示；切页通常只改变显示对象，不会重新执行页面构造函数或自动停止页面中的网络业务。

### 2.2 应用对象、显示与事件循环

主入口的四行是：

```cpp
QApplication a(argc, argv);
ECloudAssistant w;
w.show();
return a.exec();
```

- 先构造 `QApplication`，建立 Widgets 应用所需环境，再构造界面对象。
- 构造 `w` 时，项目已经创建子页面、业务对象和信号连接，也可能执行联网等操作。
- `show()` 使窗口进入显示状态，后续界面事件仍需被处理。
- `exec()` 在主线程运行 Qt 主事件循环，分发鼠标、键盘、绘制及其他事件，直到循环退出。

`exec()` 在当前主线程运行事件循环，不会另起一个 GUI 线程。阻塞调用或耗时函数占住 GUI 线程时，会延迟界面事件的处理，使窗口看起来卡住。应用环境和主循环的说明见 [QApplication](https://doc.qt.io/qt-6/qapplication.html)。

项目的 `Net/EventLoop` 是另一套网络调度对象，会自行创建工作线程；它不等同于 Qt 主事件循环，内部实现放[阶段四](项目学习规划.md#stage-4)。

### 2.3 事件处理与 signal/slot 怎样衔接

鼠标事件先由 Qt 分发到控件的事件处理函数，例如本项目的 `ListWidget::mousePressEvent()`。该函数识别被点击的条目后，再发出业务信号。事件处理函数和信号是两个环节，见 [Qt 事件系统](https://doc.qt.io/qt-6/eventsandfilters.html)。

以主窗口中的连接为例：

```cpp
connect(listWgt_, &ListInfoWgt::sig_Select, mainWgt_, &MainWgt::slot_ItemCliked);
```

四个参数分别是：

| 参数 | 本项目的含义 |
| --- | --- |
| `listWgt_` | 发出信号的侧栏对象 |
| `&ListInfoWgt::sig_Select` | 页面选择信号，携带一个 `int` |
| `mainWgt_` | 接收通知的中心区域对象 |
| `&MainWgt::slot_ItemCliked` | 接收页号并切页的成员函数 |

`connect()` 建立连接，注册时不执行槽函数。`emit sig_Select(index)` 发出信号时，Qt 才按已有连接调用接收函数。槽仍是成员函数，也可以被普通 C++ 调用；写在 `slots:` 下不会自行创建线程。

`Q_OBJECT` 使类使用 Qt 元对象能力；`signals:` 中声明的信号由 moc 生成相应支持代码。当前目标已开启 `AUTOMOC`，构建原理沿用阶段零。参见 [Signals & Slots](https://doc.qt.io/qt-6/signalsandslots.html)、[moc](https://doc.qt.io/qt-6/moc.html)。

项目还保留了 `SIGNAL(readyRead()) / SLOT(ReadData())` 这种字符串写法。它和上面的函数指针写法都用于建立 Qt 连接，但字符串签名主要在运行时核对，函数指针写法能在编译期检查更多类型关系。本阶段识别两种写法即可。

### 2.4 直接调用、排队调用与线程归属

`QObject` 的线程归属，是 Qt 用来分发该对象事件、处理排队调用的线程。默认归属创建该对象的线程，可通过 `QObject::thread()` 查询。

默认连接类型是 `Qt::AutoConnection`。判断时比较的是**信号实际发出线程**与**接收对象所属线程**：

| 情况或连接类型 | 槽函数怎样执行 |
| --- | --- |
| Auto，发信号线程与接收对象所属线程相同 | 直接调用，槽返回后才继续信号发出点后面的代码 |
| Auto，两者不同 | 排队交给接收对象所属线程的 Qt 事件循环处理 |
| `Qt::DirectConnection` | 在发信号的线程直接执行 |
| `Qt::QueuedConnection` | 排队处理，不在发信号处同步等待槽执行完 |

连接类型的定义见 [Qt::ConnectionType](https://doc.qt.io/qt-6/qt.html#ConnectionType-enum)。

因此，`emit` 不等于异步，`slots:` 也不等于工作线程。当前侧栏、按钮和登录通知的正常界面路径都在 GUI 线程，同线程的默认连接会直接调用。`QWidget` 及其子类的界面操作应在 GUI 主线程中进行；确定某次调用在哪个线程执行后，还要判断该线程是否适合操作相应对象。

连接到 lambda 时，项目常写 `connect(sender, signal, this, [this]() { ... })`。第三个参数 `this` 是 Qt context，用来关联连接生命周期和排队调用的目标线程；`[this]` 则是 C++ 捕获对象指针。发送者或 context 销毁会断开该 Qt 连接，捕获本身不延长对象寿命。

排队调用由接收对象所属线程的 Qt 事件循环处理；参数类型也需能被 Qt 元类型系统识别和传递。遇到自定义类型时，再核对声明与注册；`AVPlayer` 的媒体参数和渲染交接在阶段八具体学习。规则见 [Threads and QObjects](https://doc.qt.io/qt-6/threads-qobject.html)、[QObject::connect](https://doc.qt.io/qt-6/qobject.html)。

普通成员函数或 `std::function` 回调不会因为目标对象继承 `QObject` 就自动切线程。后续信令回调与 GUI 调用的边界，可按阶段一的方法核对。

## 3. 从 main 跟到窗口与页面

### 3.1 按构造顺序读入口

先看 [main.cpp](../main.cpp)、[ECloudAssistant.cpp](../ECloudAssistant.cpp)、[MainWgt.cpp](../UI/center/MainWgt.cpp)。下面表示构造过程中调用的顺序：

```text
main
  → QApplication
  → ECloudAssistant
      → MainWgt
          → QStackedWidget
          → LoginWgt
          → RemoteWgt → RemoteManager
          → deviceWgt_、settingWgt_
          → 添加页面、连接登录通知、指定默认页
      → TitleWgt
      → ListInfoWgt
      → 连接侧栏与中心区域、设置整体布局
  → w.show()
  → a.exec()
```

`ECloudAssistant` 用 `QGridLayout` 将左侧 `ListInfoWgt`、右上 `TitleWgt`、右下 `MainWgt` 组合起来。`MainWgt` 负责中心页面容器，登录和远程页面再分别调用自己的业务对象。

三个创建时机需要分开：

- `LoginWgt` 在构造中已经尝试连接负载服务，不等点击登录或切到登录页。
- `RemoteWgt` 在构造中创建 `RemoteManager`；其基类和自身分别创建 `EventLoop(1)` 与 `EventLoop(2)`。
- 窗口构造尚未调用推流 `Open()`，也没有因为默认显示远程页就自动创建控制端播放器窗口。

调度线程的创建位置已在阶段零定位。本阶段只知道这些对象已存在，网络调度内部留阶段四，采集和编码的启动留阶段六。

### 3.2 页面添加后的对象归属

`MainWgt` 最初以 `this` 创建四个页面，随后将它们加入 `stackWgt_`。`addWidget()` 会把页面所有权交给 `QStackedWidget`；加入后的主要 QWidget 父子关系如下：

```text
ECloudAssistant
  ├─ TitleWgt
  ├─ ListInfoWgt
  └─ MainWgt
      └─ QStackedWidget
          ├─ LoginWgt
          ├─ RemoteWgt
          ├─ deviceWgt_
          └─ settingWgt_
```

`MainWgt` 中的页面裸指针用于访问这些页面，不代表它们需要被另行逐个删除。`RemoteWgt::manager_` 则是 `unique_ptr<RemoteManager>`，使用 C++ 的持有方式；这里没有给管理对象设置 QObject parent。

切到其他页面会隐藏原页面，但页面对象及其业务状态仍然存在。`close()`、隐藏、对象析构和网络关闭也需要分别看，完整关闭关系放阶段九。页面所有权与切换规则见 [QStackedWidget](https://doc.qt.io/qt-6/qstackedwidget.html#addWidget)。

### 3.3 默认远程页与自动联网

`MainWgt` 按以下顺序添加页面：

```cpp
stackWgt_->addWidget(login_);       // 0
stackWgt_->addWidget(remoteWgt_);   // 1
stackWgt_->addWidget(deviceWgt_);   // 2
stackWgt_->addWidget(settingWgt_);  // 3
```

添加第一个页面时，登录页先成为容器的当前页；随后 `stackWgt_->setCurrentWidget(remoteWgt_)` 将当前页设为远程页。这些操作都在主窗口显示前完成，构造结束后的默认页是远程页。

此时 `LoginWgt` 已经构造，它会连接源码中的 `192.168.31.30:8523`，在 GUI 线程调用 `waitForConnected(1000)`；成功后立即发送负载入口查询。因此窗口显示前可能先等待连接，没点击“登录”也不代表没有联网。

设备列表与高级设置目前只是设置了背景色的 `QWidget` 占位页。页面存在、能切换，与其业务功能是否实现是两件事。

## 4. 跟一次侧栏选择到页面切换

### 4.1 从鼠标事件到页号

`ECloudAssistant` 同时持有侧栏和中心区域，在这里建立连接。跨区域通知只传页号：侧栏负责选择入口，中心区域负责按页号取出并显示页面。

按 [ListWidget.cpp](../UI/tool/ListWidget.cpp) → [ListInfoWgt.cpp](../UI/list/ListInfoWgt.cpp) → `ECloudAssistant` 中的连接 → `MainWgt::slot_ItemCliked()` 阅读这条调用路径：

```text
左键按下列表条目
  → ListWidget::mousePressEvent
  → itemAt(event->pos()) 命中条目，row(item) 得到行号
  → emit itemCliked(index)
  → ListInfoWgt::HandleItemSelect(index)
  → emit sig_Select(index + 1)
  → MainWgt::slot_ItemCliked(pageIndex)
  → stackWgt_->widget(pageIndex)
  → 非空时 setCurrentWidget(widget)
```

`itemCliked`、`slot_ItemCliked` 沿用项目拼写；前者是项目定义的信号，不是 Qt 自带的 `QListWidget::itemClicked`。`CustomWgt` 负责条目外观，选择入口在 `ListWidget` 的鼠标事件处理中。

页号转换关系是：

| 用户入口 | 列表行号 | `sig_Select` 参数 | 对应页面 |
| --- | --- | --- | --- |
| 用户头像按钮 | 不经列表行号 | 0 | 登录 |
| 远程控制条目 | 0 | 1 | 远程 |
| 设备列表条目 | 1 | 2 | 设备占位页 |
| 高级设置条目 | 2 | 3 | 设置占位页 |

`+1` 用来跳过编号为 0 的登录页。用户头像按钮单独发送 `sig_Select(0)`。

`MainWgt::slot_ItemCliked()` 先按页号取页面指针，非空才显示该页；它不创建新页面，也不直接处理登录、连接或媒体。

### 4.2 图标高亮、列表选择与当前页

`ListInfoWgt::HandleItemSelect()` 发送页面选择信号后，再遍历 `customWgts_`，调用 `CustomWgt::setHightLight()` 更换普通或高亮图片。用户头像按钮则发送页号 0，并取消图标高亮、清除列表选择。

这里有三种状态：

| 状态 | 谁负责 |
| --- | --- |
| 当前堆叠页面 | `MainWgt` 调用 `setCurrentWidget()` |
| 条目高亮图片 | `ListInfoWgt` 调用 `CustomWgt::setHightLight()` |
| QListWidget 的选择状态 | 列表控件及其基类事件处理 |

默认远程图标的高亮来自 `setImageAndText(..., true)`；默认页面来自 `MainWgt` 的显式设置。`ListWidget` 构造中的 `setCurrentRow(0)` 发生在条目添加前，也不会发出项目的 `itemCliked` 信号，不能用它解释切页。

当前代码没有把 `currentRowChanged` 接到页面切换槽。因此，不能把“任何列表当前行变化”都当成本项目的页面切换入口。

`ListWidget::AddWidget()` 用 `QListWidgetItem` 承载条目，再通过 `setItemWidget()` 放入 `CustomWgt`。列表接管条目控件，`customWgts_` 保存的是访问指针；具体 API 见 [QListWidget::setItemWidget](https://doc.qt.io/qt-6/qlistwidget.html#setItemWidget)。

## 5. 从登录通知到被控端初始化

### 5.1 通知在哪里发出，在哪里接收

[MainWgt.cpp](../UI/center/MainWgt.cpp) 将登录页与远程页连接起来：

```cpp
connect(login_, &LoginWgt::sig_logined, remoteWgt_, &RemoteWgt::handleLogined);
```

本阶段按 UI 编排读以下路径：

```text
LoginWgt 收到登录服务响应
  → HandleLogin：is_login_ 为 true，且 resultCode == S_OK_
  → emit sig_logined(信令 IP, 信令端口)
  → RemoteWgt::handleLogined
  → 读取本机识别码，空则返回
  → 保存 ip_、port_
  → manager_->Init(信令 IP, 信令端口, 本机识别码)
```

`sig_logined` 携带的是信令服务的 IP 与端口，本机识别码由槽函数从远程页的 `selfCodeEdit_` 读取。这条通知不负责分配识别码，也没有在这里切换页面。

`is_login_` 在连接登录服务器成功时就被置为 true；发出成功通知还要经过响应结果判断。不能单凭字段名把它等同于“已经通过登录验证”。负载查询、地址切换、收包与字段解释在[阶段三](项目学习规划.md#stage-3)继续学习。

### 5.2 Init 建立的是哪种连接

[RemoteManager::Init()](../UI/center/RemoteManager.cpp) 尝试连接信令服务，成功后创建默认 `CONTROLLED` 角色的 `SigConnection`，并注册启动、停止流的回调。这是本机作为**被控端**时使用的连接。

`RemoteManager` 早在窗口构造时就已创建；登录通知是在调用已有管理对象的初始化接口，不是在这时才创建管理对象。

该接口返回 `void`，连接失败时只在当前实现中打印日志并返回。`RemoteWgt` 保存了地址或执行过 `Init()`，不能单独证明被控端已成功登记；后续应答和状态在阶段五检查。

## 6. 从“开始远程”到控制端入口

### 6.1 先区分两种识别码

[RemoteWgt.cpp](../UI/center/RemoteWgt.cpp) 中有两个输入框，变量名看似接近，用途不同：

| 数据 | 当前来源 | 进入的入口 |
| --- | --- | --- |
| 本机识别码 | `selfCodeEdit_`，默认文字为 `"345"`；登录通知到达时读取 | `RemoteManager::Init()`，用于本机被控端初始化 |
| 目标识别码 | `rmoteCodeEdit_`，点击开始远程时读取 | `RemoteManager::StartRemote()`，随后传给控制端连接 |
| 信令地址 | `sig_logined` 的参数，经 `handleLogined()` 保存 | 上述两个入口 |

[LoginWgt.cpp](../UI/center/LoginWgt.cpp) 的登录报文还独立写了 `login.SetCode("345")`。它与本机输入框恰好有相同默认值，但没有自动绑定；修改输入框不会自动修改这处报文赋值。字段一致性在阶段三、五核对。

### 6.2 按钮触发后实际创建什么

开始远程按钮的 lambda 读取目标识别码，非空时调用 `manager_->StartRemote(ip_, port_, code)`。当前只检查目标码非空，没有检查是否已收到登录通知或初始化是否成功；地址尚未取得时，仍可能使用默认值。

继续读到以下边界即可：

```text
开始远程按钮 clicked
  → 读取目标识别码，空则返回
  → RemoteManager::StartRemote
      → 创建 PullerWgt(event_loop_.get(), nullptr)
          → 创建 AVPlayer，并执行其初始化
      → 显示 PullerWgt
      → PullerWgt::Connect(信令地址, 目标码)
          → AVPlayer::Connect
          → 建立另一条 CONTROLLING 角色的信令连接
```

`PullerWgt` 的 parent 为 nullptr，是另外的顶层窗口，由 `RemoteManager::pullerWgt_` 持有。窗口显示发生在 `Connect()` 之前；连接失败时，也可能已经看到了播放器窗口。

`AVPlayer` 构造时会创建媒体上下文、解封装器并初始化音频输出，但还没有按远端流地址开始读取媒体。收到后续信令结果后，才经 `HandleStartStream(streamAddr)` 进入拉流和播放，内部在阶段八展开。

两条信令连接需要区分：

| 连接入口 | 角色 | 在本阶段理解的用途 |
| --- | --- | --- |
| `RemoteManager::Init` | `CONTROLLED` | 本机等待被控制 |
| `AVPlayer::Connect` | `CONTROLLING` | 本机主动请求控制目标 |

控制端没有复用 `RemoteManager::sig_conn_`。角色状态、目标查找和消息字段在阶段五学习，推流和播放分别在阶段六至八学习。

这里 `AVPlayer::Connect()` 返回 `true`，表示同步 TCP 连接成功，已经创建信令连接对象、发起 JOIN，并设置后续回调。它没有等待 JOIN 应答、目标查找和完整信令协商结束，所以“远程连接成功”日志还不能证明目标存在或已收到音视频。

### 6.3 这些入口在哪个线程执行

正常界面路径中的按钮回调、`LoginWgt` 的 `QTcpSocket::readyRead` 处理和 `handleLogined()` 都在 GUI 线程。默认 Qt 连接在这些路径上直接调用，`Init()`、`StartRemote()` 也随调用者执行。

因此，调用管理对象不等于把工作投递到它持有的网络线程。本项目的 `TcpSocket::Connect()` 在这些入口中同步执行，可能阻塞界面；内部套接字与超时实现到阶段四再查。

后续 `SigConnection` 通过普通 C++ 回调启动发布或播放，执行线程由网络事件分发路径决定，不会自动回到 GUI 线程。Qt 线程归属、实际调用线程与共享状态的检查方法可回看阶段一。

## 7. 辅助界面与资源只跟到使用位置

### 7.1 标题栏与无边框窗口

[TitleWgt.cpp](../UI/title/TitleWgt.cpp) 当前创建两个按钮：

- 最小化按钮调用父窗口 `showMinimized()`。
- 关闭按钮调用父窗口 `close()`。

主窗口设置了无边框标志，拖动入口在 `ECloudAssistant::mousePressEvent / mouseMoveEvent / mouseReleaseEvent`。本阶段定位到这些 Qt 事件函数即可，坐标细节不影响三条业务通知链的理解。

`close()` 先发送关闭事件；事件被接受后，窗口会隐藏。当前主窗口没有设置关闭时自动删除的属性，因此关闭动作本身不直接销毁它。规则见 [QWidget::close](https://doc.qt.io/qt-6/qwidget.html#close)。

在本项目的默认设置下，关闭最后一个可见顶层窗口会请求退出主事件循环。如果独立的 `PullerWgt` 仍显示，关闭主窗口不一定立即使 `exec()` 返回。默认退出行为见 [quitOnLastWindowClosed](https://doc.qt.io/qt-6/qguiapplication.html#quitOnLastWindowClosed-prop)。

当前 `main` 中的主窗口 `w` 是局部对象，主循环退出后离开作用域才执行其析构。窗口关闭、主循环退出和资源释放的完整关系在阶段九检查。

### 7.2 从 objectName 到样式与图片

对照 [StyleLoader.cpp](../UI/tool/StyleLoader.cpp)、[main.css](../UI/brown/main.css)、[res.qrc](../res.qrc)，读清一条路径：

```text
RemoteWgt 为开始远程按钮设置 objectName("remoteBtn")
  → StyleLoader 用 QFile 读取 :/UI/brown/main.css
  → 对传入的 RemoteWgt 调用 setStyleSheet(qss)
  → QPushButton#remoteBtn 等选择器匹配开始远程按钮并应用样式
```

文件虽然叫 `main.css`，这里按 Qt 样式表解析。样式表设置在远程页上，也能匹配它的子控件；`#remoteBtn` 匹配按钮的 `objectName`，不是 C++ 成员变量名。`:hover`、`:pressed` 等表示控件状态。说明见 [Qt 样式表语法](https://doc.qt.io/qt-6/stylesheet-syntax.html)。

例如，标题栏按钮通过样式中的 `image:url(:/...)` 引用图片；侧栏则在 `CustomWgt::setPicture()` 中调用 `QPixmap::load()`，再交给 `QLabel` 显示。

`res.qrc` 的资源前缀是 `/`，所以 `UI/brown/main.css` 在程序中可通过 `:/UI/brown/main.css` 访问。当前构建将这些资源编入程序，修改资源后要重新构建才能进入新产物，不依赖把仓库放在某个固定目录。见 [Qt 资源系统](https://doc.qt.io/qt-6/resources.html)。

`StyleLoader` 只负责读文本并设置样式；它不创建页面，也不建立业务连接。资源表中的最大化、更多菜单等图片，不代表主标题栏已有对应操作。着色器在这里确认位置，绘制原理留阶段八。

## 8. 用一次页面切换核对主线

沿用阶段零的 Debug 产物。有可用调试器时，用一个断点观察一次切页：

1. 在 `MainWgt::slot_ItemCliked()` 内设置断点，再用已有调试器启动本次 Debug 产物，确认默认是远程页。若启动前有等待，对照 `LoginWgt` 构造中的连接操作。
2. 左键点击侧栏“设备列表”。停到断点时，当前函数的 `index` 应为 2，这是页面编号。
3. 在调用栈中选中 `ListInfoWgt::HandleItemSelect()`，查看它的 `index` 应为 1，这是列表行号；对照源码中的 `emit sig_Select(index + 1)`。
4. 继续执行，观察中心页面与图标变化。用户头像对应页号 0，可直接对照第 4 节的连接与映射表确认。

当前路径使用同线程的默认连接，所以切页槽的调用栈中能看到信号发出方。查看调用栈即可核对参数传递，不必逐步进入 Qt 的生成调用；只有线程归属不清楚时，再查看断点所在的线程。

能启动程序但暂时没有可用调试器时，直接点击同一条目观察页面和图标，再结合源码核对 1 到 2 的转换即可。

页面切换本身不依赖服务端成功响应。登录或信令服务不可达时，第 5～6 节先沿源码核对参数来源和入口；不要求为本阶段准备完整远控环境。

当前不能运行时，按第 4 节的页号表推演同一次选择，保留“尚未实测”的说明。退出遇到阶段零已说明的空连接问题时，按实际调用栈定位，完整关闭留阶段九。

## 9. 自查与下一步

结合源码回答：

1. 为什么还没进入 `exec()`，登录页和网络调度对象就已经创建？
2. 页面为什么会从 `MainWgt` 初始 parent 转交给 `QStackedWidget`？布局与页面指针各做什么？
3. 点击“设备列表”时，哪些函数先后执行，哪里将 1 转成 2，哪里真正切页？
4. 默认远程页、图标高亮、列表当前行为什么要分别确认？
5. `sig_logined` 传了什么，本机码和目标码又分别从哪里取得？
6. `Init()` 和 `StartRemote()` 的两条信令连接有什么区别，窗口显示或连接日志能证明到哪一步？

完成标准：

- [ ] 能指出窗口与页面创建顺序、主要 parent 关系和业务对象持有者。
- [ ] 能解释事件、信号、槽与直接/排队调用在当前链路中的作用。
- [ ] 能跟通侧栏选择、登录通知、开始远程三条路径，并区分实际观察与源码推演。
- [ ] 能指出当前占位功能、识别码来源及本阶段尚未展开的网络和媒体边界。

随后进入[阶段三：Qt 登录链与二进制协议](项目学习规划.md#stage-3)，继续阅读负载入口查询、登录服务切换及报文处理。Qt 文档链接以 Qt 6 为入口，查具体 API 时按当前构建使用的 Qt 主版本核对。
