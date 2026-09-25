# API 可用性探测程序

这些小工具是开发 HddGauge 之前用来摸清「这台机器 + MinGW 5.3 + Qt 5.8」到底支持什么的。
`01`–`05` 只用 Win32 / PDH，不依赖 Qt；`06` 是唯一需要 Qt 的一个。

```bat
set PATH=D:\Software\Qt\QT5.8\5.8\mingw53_32\bin;D:\Software\Qt\QT5.8\Tools\mingw530_32\bin;%PATH%

g++ -std=c++11 -O2 -o p1.exe 01_api_availability.cpp -lpdh      && p1.exe
g++ -std=c++11 -O2 -o p2.exe 02_drives_and_smart.cpp            && p2.exe
g++ -std=c++11 -O2 -o p3.exe 03_unprivileged_access.cpp         && p3.exe
g++ -std=c++11 -O2 -o p4.exe 04_size_api_and_pdh_instances.cpp -lpdh && p4.exe
g++ -std=c++11 -O2 -o p5.exe 05_fast_pdh_poll.cpp -lpdh -lwinmm && p5.exe 0 3

rem 06 需要 Qt（下面几行实际写成一行）
set QT=D:/Software/Qt/QT5.8/5.8/mingw53_32
g++ -std=gnu++11 -o p6.exe 06_about_to_quit.cpp ^
    -I%QT%/include -I%QT%/include/QtCore -I%QT%/include/QtGui -I%QT%/include/QtWidgets ^
    -L%QT%/lib -lQt5Core -lQt5Gui -lQt5Widgets && p6.exe
```

> ⚠️ **PATH 不只是编译时需要，运行也需要。** 不把 MinGW 的 `bin` 放进 PATH 就跑 `p5.exe`，
> 会以 `0xC0000139 STATUS_ENTRYPOINT_NOT_FOUND` 静默退出——加载到的是系统里另一个
> `libstdc++-6.dll` / `libwinpthread-1.dll`。这个错误码当时查了好一会儿。

| 文件 | 回答的问题 | 实测结论（本机） |
|---|---|---|
| `01_api_availability.cpp` | IOCTL 宏、结构体、PDH 英文计数器是否可用 | 全部可用；`PdhAddEnglishCounterW` 需运行时 `GetProcAddress`（`libpdh.a` 未导出该符号） |
| `02_drives_and_smart.cpp` | 卷→物理盘映射、型号、容量、ATA IDENTIFY、SMART | 非管理员时 `\\.\PhysicalDriveN` 以读写权限打开返回 err=5；卷映射可用 |
| `03_unprivileged_access.cpp` | 不提权到底能读到什么 | **`dwDesiredAccess=0` 可以打开物理盘**，型号/序列号/容量都能读；ATA IDENTIFY 与 `GET_LENGTH_INFO` 不行 |
| `04_size_api_and_pdh_instances.cpp` | PDH 实例命名规则、哪种容量 IOCTL 不需要提权 | 实例形如 `0 D:` / `1 C:`；`IOCTL_DISK_GET_DRIVE_GEOMETRY_EX` 在 0 权限下可用，`IOCTL_STORAGE_READ_CAPACITY` 不可用 |
| `05_fast_pdh_poll.cpp` | **能不能比 1 Hz 更快地采样 PDH 物理盘计数器** | **能，100 ms 没问题**：五种间隔下 0 次采集失败、0 个无效状态，均值互差 ≤7%；单次采集 0.5–1 ms |
| `06_about_to_quit.cpp` | `QApplication::quit()` 会不会触发 `aboutToQuit`；`QSettings` 写得进去吗 | **会触发**（Qt 5.8 实测），设置可以挂在该信号上落盘；同时测出**沙箱环境会静默拒绝 HKCU 写入**——`QSettings` 不报错，但也不生效 |

> `04`、`05` 需要 `#define _WIN32_WINNT 0x0601`，否则 `PdhAddEnglishCounterW` 不会在头文件里声明。

### `p5` 的测量方法

`p5.exe [盘号=0] [每档秒数=3] [IO文件路径]`。它先起一个后台线程，用真实
`WriteFile` + `FlushFileBuffers` 反复写一个 48 MiB 的文件
（走 `FlushFileBuffers` 是为了绕开写缓存，否则物理盘计数器只看得到缓存流量），
把目标盘压到高占用，然后依次用 100 / 200 / 250 / 500 / 1000 ms 采样同一组计数器，
最后把每档的均值与 1 秒参考档对比。退出码 0 = 各档一致，1 = 有档位对不上。

本机输出（PhysicalDrive0 = TOSHIBA MQ04ABF100）：

```
interval | samples | collectFail | invalidVal | meanBusy% | meanWrite MB/s | collect ms(avg/max)
   100 ms|      30 |           0 |          0 |    96.24% |          27.90 |     0.53/  0.75
   250 ms|      12 |           0 |          0 |    96.08% |          30.38 |     0.61/  1.17
  1000 ms|       3 |           0 |          0 |    96.43% |          29.18 |     0.55/  0.60

   100 ms: write  -4.4%   busy  -0.2%   -> OK
   250 ms: write  +4.1%   busy  -0.4%   -> OK
VERDICT: sub-second polling looks safe
```

这些结论直接决定了正式代码里的几个选择，详见项目根目录 `README.md` 的「数据采集的几个关键决定」。
