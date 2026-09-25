# HddGauge · 机械硬盘状态仪表盘

一个用 **C++ / Qt 5.8（MinGW 5.3 32-bit）** 写的 Windows 桌面小工具，把一个物理硬盘的实时工作状态压缩成
一个一眼就能看懂的仪表盘：**主指针 = 磁盘占用百分比**，**外圈 = 等效负载转速 或 读写速率（可切换）**。

![HDD · 等效转速](docs/screenshot-hdd-rpm.png)

| 折叠详情（简洁模式） | 外圈：读写速率 | SSD（非旋转介质） |
|---|---|---|
| ![折叠](docs/screenshot-compact.png) | ![HDD · 读写速率](docs/screenshot-hdd-rate.png) | ![SSD](docs/screenshot-ssd.png) |

---

## 1. 先说清楚「转速」这件事（重要）

这不是免责声明，而是这个软件的立身之本：

| 指标 | 软件能否真实测得 | 本项目怎么做 |
|---|---|---|
| 磁盘占用百分比 | ✅ 能 | PDH `\PhysicalDisk(*)\% Idle Time` 反算 |
| 读/写速率、队列深度、响应时间 | ✅ 能 | PDH 计数器（同一查询内取值） |
| **主轴实时转速 RPM** | ❌ **测不到** | 机械盘主轴是恒速闭环，不存在「当前 6832 rpm」这种读数 |
| 标称转速（5400/7200/10000/15000） | ✅ 能（需管理员） | ATA IDENTIFY DEVICE word 217 |
| 温度 | ✅ 能（需管理员） | S.M.A.R.T. 属性 194 |

因此外圈「转速」表提供两种**明确区分**的模式：

* **等效负载转速**（默认）—— 把标称转速当作 100% 负载的参考点，
  `等效转速 = 标称转速 × min(1, 占用率 × (1 + 0.25 × min(队列深度, 4)))`。
  指针会随负载实时摆动，视觉上最直观，但它是**推导值**，界面上用「等效负载转速」明确标注。
* **读写速率** —— 外圈变成双环：青色 = 读，琥珀色 = 写，量程按峰值自动分档（1/2/5×10ⁿ）。

SSD/NVMe 会被识别为非旋转介质，转速外圈自动退化成虚线圈并标注「非旋转介质」，不再显示假刻度。

---

## 2. 功能

* **主表盘**：270° 弧形，0–100%，绿(<60)/黄(60–85)/红(≥85) 三段色区，
  指针用**临界阻尼弹簧**以 60 fps 追踪采样值，不会跳变。
* **外圈双模式**：等效负载转速 / 读写速率，右键菜单或顶部下拉切换，选择被记住。
* **多物理盘**：下拉框切换 PhysicalDrive，自动优先选中机械盘；支持热刷新。
* **可折叠详情面板**：曲线图与参数栏一键收起，**按钮固定在窗口右下角**（也可 `Ctrl+D`），
  收起后窗口同步收缩约 204 px 且**表盘尺寸逐像素不变**——想要的就是「一眼看懂」。
  展开/收起是 180 ms 的滑动过渡（`QPropertyAnimation` 驱动），不是瞬间跳一下：
  面板高度按进度取 `p × 展开高度`，窗口高度按同一进度取 `p × 总高度差`，两者同步，
  表盘因此在整段动画里几乎不动（残余 ±11 px，即一个布局间距）。
  右键菜单里还可以把曲线图和参数栏**分开**开关。
* **采样频率可调**：100 / 200 / 250 / 500 ms、1 s、2 s，默认 250 ms。
  当前速率显示在参数栏的「数据源」格（如 `PDH · 250 ms`）。
* **信息面板**：型号、容量、接口、介质、标称转速、温度、读、写、队列深度、响应时间、盘符、数据源。
* **60 秒滚动曲线**：占用率折线 + 峰值；纵轴按峰值自适应（空闲盘 0.2% 也能看出形状），
  时间轴在缓冲未满时自动收紧，横轴跨度始终写在标题里。
* **系统托盘**：悬停显示占用率/读写速率，双击显隐。
* **窗口置顶**、几何尺寸与设置通过 `QSettings` 持久化；启动尺寸不会超过屏幕可用区域。
* **`--selftest`**：无界面跑通整条采集链并输出报告，便于在别的机器上验证环境。
* **`--screenshot`**：把窗口离屏渲染成 PNG，便于评审界面。

---

## 3. 构建

### 依赖

* Qt 5.8（`mingw53_32` kit）
* MinGW 5.3.0 32-bit（随 Qt 安装包附带）
* 无需额外第三方库；PDH 通过 `-lpdh` 链接

工具链路径**没有写死**。两个脚本都先读环境变量 `QT_DIR` / `MINGW_DIR`，
只在未设置时才回落到作者的本地路径，并在找不到 `qmake` 时直接报错退出。
换机器时这样即可：

```bat
set "QT_DIR=C:\Qt\5.8\mingw53_32"
set "MINGW_DIR=C:\Qt\Tools\mingw530_32"
build.bat
```

> 本项目开发时使用的实际路径（仅供参考）：
> `Qt = D:\Software\Qt\QT5.8\5.8\mingw53_32`、
> `MinGW = D:\Software\Qt\QT5.8\Tools\mingw530_32`

### 一键脚本

```bat
build.bat          :: 开发版（asInvoker，不弹 UAC），输出 build\release\HddGauge.exe
build_release.bat  :: 发布版（requireAdministrator）+ windeployqt 部署到 dist\
```

### 手动构建

```bat
set "PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%"
mkdir build && cd build
qmake ..\HddGauge.pro CONFIG+=no_admin_manifest
mingw32-make -j4
```

去掉 `CONFIG+=no_admin_manifest` 即得到内嵌 `requireAdministrator` 的正式版。

> ⚠️ **不要把工程放在含中文的路径下再用 `$$OUT_PWD`。**
> 本项目实测：一旦把绝对路径（含 CJK 字符）写进 Makefile，moc 会因为
> `--include` 参数解析失败而报 usage 错误。所以 `.pro` 里所有输出目录都保持**相对路径**。
>
> 顺带一提：在含中文的路径下，`qmake` 每次会额外打印两行
> `The system cannot find the path specified.`。**这两行无害**，构建照常成功；
> 把工程放在纯 ASCII 路径下就不会出现（已实测对比）。

---

## 4. 架构

```
                     ┌──────────────────────── UI 线程 ────────────────────────┐
                     │  MainWindow → DashboardPage → RingGauge + UsageGauge     │
                     │              → 可折叠详情面板: Sparkline / InfoBar       │
                     └───────────────▲──────────────────────┬──────────────────┘
                         sampleReady │                      │ requestActiveDevice
                                     │                      │ requestTicking(ms)
                     ┌───────────────┴──────────────────────▼──────────────────┐
                     │  MonitorWorker（QThread，可调 100 ms – 2 s）             │
                     │    PdhProbe ──► 失败则 IoPerfProbe（降级链）             │
                     │    DriveEnumerator + SmartProbe（启动时一次）            │
                     └─────────────────────────────────────────────────────────┘
```

```
src/
  main.cpp                    程序入口、元类型注册、--selftest / --screenshot
  SelfTest.cpp                无界面采集链自检
  core/
    DiskTypes.h               DriveDescriptor / DiskSample（含等效转速公式）
    BusTypes.h                STORAGE_BUS_TYPE 数值表（绕开 MinGW 头文件缺失）
    WinUtil.{h,cpp}           RAII HANDLE、错误文本、容量/速率格式化、提权检测
    SpringValue.h             临界阻尼弹簧（指针动画）
    PdhProbe.{h,cpp}          性能计数器，通配符计数器 + 实例名→盘号映射
    IoPerfProbe.{h,cpp}       IOCTL_DISK_PERFORMANCE 降级通道
    SmartProbe.{h,cpp}        ATA IDENTIFY / SMART READ DATA / 型号推断
    DriveEnumerator.{h,cpp}   枚举物理盘、型号、序列号、容量、总线
    MonitorWorker.{h,cpp}     采集线程与信号槽
  ui/
    Theme.{h,cpp}             调色板与全局 QSS
    GaugeBase.{h,cpp}         表盘基类：弧/刻度/指针/轴心 绘制原语 + 弹簧动画
    UsageGauge.{h,cpp}        主表盘（0–100%，分区着色，底部数字）
    RingGauge.{h,cpp}         外圈（等效转速 / 读写速率双模式）
    DashboardPage.{h,cpp}     同心叠放两个表盘
    Sparkline.{h,cpp}         占用率滚动曲线（时间窗 + 自适应纵轴）
    InfoBar.{h,cpp}           信息网格
    MainWindow.{h,cpp}        窗口整合、折叠详情面板、采样间隔、托盘、设置持久化
```

### 数据采集的几个关键决定

1. **主通道用 PDH，并且必须用英文计数器名。**
   `PdhAddEnglishCounterW` 在本机 `pdh.dll` 里存在，但 **MinGW 5.3 的 `libpdh.a` 没有导出这个符号**，
   直接链接会 undefined reference，所以代码里用 `GetProcAddress` 动态绑定。
   中文系统上如果用 `PdhAddCounter` 配英文字符串会匹配失败，这是最常见的坑。

2. **PDH 实例名直接给出物理盘号。** 实例是 `0 D:` / `1 C:` / `_Total`，
   前缀整数就是 `PhysicalDriveN`，不需要任何额外映射；盘符则另外用
   `IOCTL_STORAGE_GET_DEVICE_NUMBER` 从卷句柄反查。

3. **降级链：PDH → IOCTL_DISK_PERFORMANCE → 明确报「无数据」，绝不显示假数据。**

4. **刻意不使用 `IOCTL_DISK_PERFORMANCE_ON` / `_OFF`：**
   * MinGW 5.3 与 mingw-w64 上游头文件都**没有** `_ON` 的声明，猜控制码可能调到无关的 IOCTL；
   * `_OFF` 会**全局关闭该设备的性能计数器**，反过来把主用的 PDH 通道搞坏。
   所以只读不写，计数器未启用时提示管理员执行 `diskperf -y`。

5. **非管理员也能拿到不少东西。** 实测（见 `tools/probes/03_unprivileged_access.cpp`）：
   以 `dwDesiredAccess = 0` 打开 `\\.\PhysicalDriveN` 可以成功，并且
   `IOCTL_STORAGE_QUERY_PROPERTY`（型号/序列号/总线）与
   `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`（容量）都能用；
   而 `IOCTL_DISK_GET_LENGTH_INFO`、ATA IDENTIFY、SMART 会返回 `ERROR_ACCESS_DENIED(5)`。
   所以正式版内嵌需要管理员的 manifest，而所有特权字段都有优雅降级。

6. **占用率 = 100 − `% Idle Time`，并夹到 [0,100]。**
   不用 `% Disk Time`，它在排队深度高时会超过 100%。

7. **采样间隔不必是 1 秒——这是实测出来的，不是猜的。**
   最初的注释写着「PDH 速率计数器是差分的，所以只能 1 Hz」。这只对了一半：
   PDH 是用**最近两次 `PdhCollectQueryData` 的时间戳**去算速率的，间隔本来就可以任取。
   于是写了 `tools/probes/05_fast_pdh_poll.cpp`：后台线程用真实
   `WriteFile` + `FlushFileBuffers` 把 HDD 压到约 96% 占用，再分别以
   100 / 200 / 250 / 500 / 1000 ms 采样同一个计数器集。结果：

   | 间隔 | 采样数 | 采集失败 | 无效状态 | 平均占用 | 平均写入 | 单次采集耗时 |
   |---|---|---|---|---|---|---|
   | 100 ms | 30 | 0 | 0 | 96.24% | 27.90 MB/s | 0.53 ms |
   | 250 ms | 12 | 0 | 0 | 96.08% | 30.38 MB/s | 0.61 ms |
   | 1000 ms | 3 | 0 | 0 | 96.43% | 29.18 MB/s | 0.55 ms |

   没有任何 `PDH_INVALID_DATA` / `PDH_NO_DATA`，各间隔的均值互差 ≤7%（写速率）与 ≤0.6%（占用率），
   采集本身只花 0.5–1 ms。所以把间隔做成可调、默认 **250 ms**，下限取 **100 ms**：
   再快也只是重复读同一个计数周期，只会增加抖动。
   代码里另外把 `PDH_INVALID_DATA` / `PDH_NO_DATA` 归为**瞬时缺帧**
   （`PdhProbe::lastPollWasTransient()`）：这种 tick 不发信号、保持上一帧，
   而不是掉进 IOCTL 降级通道——那会让「数据源」莫名其妙地跳变。

8. **`QApplication::quit()` 不会走 `closeEvent`，但会走 `aboutToQuit`。**
   托盘菜单的「退出」和 `--screenshot` 都走 `quit()`，所以设置必须在
   `aboutToQuit` 里落盘，否则几何尺寸/采样间隔只在点窗口 X 时才保存。
   这条同样是验证过的（`tools/probes/06_about_to_quit.cpp`），顺手也测出
   **沙箱环境会静默拒绝 HKCU 写入**——`QSettings` 失败时一声不吭，
   遇到「设置不生效」先确认注册表到底能不能写。

9. **折叠/展开按「实测的高度差」改窗口，而不是猜一个常量。**
   面板显示后布局会多出**一个**间距（相邻两项之间原本就有一条，面板插进来变成两条），
   所以总高度差 = 面板高度 + 1 个 spacing。这个值不是写死的：
   代码读的是面板实时的 `sizeHint()`，并且动画落位时会拿「切换前的表盘高度」
   对一次账（`m_dialTarget`），对不上就把窗口补回去——万一以后布局里多一个控件，
   表盘也不会莫名其妙变大变小。
   动画每帧同时设置面板高度与窗口高度，缺一不可：只改窗口的话，面板会先弹到全高
   把表盘挤扁一帧，那正是「尺寸突变」的来源。

---

## 5. 命令行

```
HddGauge.exe                                    正常启动
HddGauge.exe --selftest [report.txt]            无界面运行采集链，输出报告
HddGauge.exe --screenshot out.png [--shot-delay 4000]
             [--ring-mode 0|1] [--device N]
             [--interval 100|200|250|500|1000|2000] [--compact]
                                                离屏渲染窗口截图
```

`--screenshot` 会切到独立的 `HddGauge-shot` 设置作用域，因此上一次真实使用留下的
窗口几何/折叠状态不会污染文档截图。

---

## 6. 目录产物

仓库里只放源码、资源与文档：

```
src\             全部源码（core\ 采集层，ui\ 绘制层）
res\             图标、manifest、.qrc / .rc
docs\            界面截图（README 引用）
tools\
  make_icon.cpp  零依赖的 .ICO 生成器（4×4 超采样，输出 16/24/32/48/64 多尺寸）
  probes\        开发期用来摸清这台机器 API 可用性的探测程序，见其 README
```

下面这些由脚本生成，**不进版本库**（见 `.gitignore`）：

```
build\           开发版中间产物            ← build.bat
build-admin\     发布版中间产物            ← build_release.bat
dist\            可直接运行的发布版，约 22 MB：HddGauge.exe + Qt5Core/Gui/Widgets +
                 qwindows 平台插件 + qico 图标插件 + MinGW 运行时
```

`dist\` 体积大又含第三方二进制，因此改以 **GitHub Release 附件**（zip）形式发布，
不放进仓库。

---

## 7. 已知限制

* 体积统计与温度依赖 S.M.A.R.T. 的 ATA 直通；USB 硬盘盒、RAID 控制器后面的盘通常拿不到，
  此时转速会退化为「型号推断」并在界面上标注来源。
* 型号推断表只覆盖常见消费级系列（东芝 MQ/DT、WD、希捷、HGST 等），未命中就显示「未知」。
* 采样频率下限为 100 ms。实测（`tools/probes/05_fast_pdh_poll.cpp`）在这个量级已经稳定，
  再快只会采到同一个计数器更新周期内的重复值。
* 只测物理盘；分区/卷级别的细分不在范围内。
* 折叠/展开的滑动过程中，表盘会在**一个布局间距（11 px）**的范围内轻微缩放，
  这是「面板可见时多占一条间距」的几何后果，两个稳态下表盘尺寸完全一致。
* 参数栏的值格最宽 230 px，超长型号（如 `WDC PC SN520 SDAPMUW-128G-1001`）
  会被裁掉尾部，完整值在悬停提示里。
* 设置存于 `HKCU\Software\HddGauge\HddGauge`。若运行环境禁止写注册表，
  `QSettings` 会**静默失败**，此时窗口几何与偏好不会被记住（功能本身不受影响）。
* 界面为固定深色主题，未做浅色方案与高 DPI 多屏适配（manifest 已置 `dpiAware=true`）。

---

## 8. 许可

本项目源码以 [MIT](LICENSE) 授权，© 2026 array-tree。

发布版捆绑的 Qt 5.8 运行库（`Qt5Core.dll` / `Qt5Gui.dll` / `Qt5Widgets.dll`、
`platforms\qwindows.dll`、`imageformats\qico.dll` 等）版权归 The Qt Company，
按 **LGPLv3** 授权，不在上述 MIT 范围之内。
