#include "IoPerfProbe.h"

#include <QDateTime>

IoPerfProbe::~IoPerfProbe()
{
    close();
}

bool IoPerfProbe::open(int deviceNumber, QString* error)
{
    close();

    const QString path = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(deviceNumber);
    wu::Handle handle(::CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()),
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr));
    if (!handle.valid()) {
        const DWORD code = ::GetLastError();
        if (error) {
            *error = (code == ERROR_ACCESS_DENIED)
                         ? QStringLiteral("打开 %1 失败：需要管理员权限").arg(path)
                         : QStringLiteral("打开 %1 失败：%2").arg(path, wu::winError(code));
        }
        return false;
    }

    // Deliberately no IOCTL_DISK_PERFORMANCE_ON / _OFF here:
    //   * MinGW does not declare _ON at all, and guessing its control code
    //     could invoke an unrelated IOCTL;
    //   * _OFF disables the counters for the *whole* device, which would break
    //     the PDH path we actually prefer.
    // So the counters are simply read as-is, and a stalled QueryTime is
    // reported as "counters not enabled".
    m_handle = std::move(handle);
    m_device = deviceNumber;
    m_havePrevious = false;
    return true;
}

void IoPerfProbe::close()
{
    m_handle.close();
    m_device = -1;
    m_havePrevious = false;
}

bool IoPerfProbe::poll(DiskSample* out, QString* error)
{
    if (!m_handle.valid()) {
        if (error)
            *error = QStringLiteral("磁盘性能句柄未打开");
        return false;
    }

    DISK_PERFORMANCE current;
    ZeroMemory(&current, sizeof(current));
    DWORD returned = 0;
    if (!::DeviceIoControl(m_handle.get(), IOCTL_DISK_PERFORMANCE,
                           nullptr, 0, &current, sizeof(current), &returned, nullptr)) {
        if (error)
            *error = QStringLiteral("读取磁盘性能数据失败：%1").arg(wu::lastError());
        return false;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    if (!m_havePrevious) {
        m_previous = current;
        m_previousMs = nowMs;
        m_havePrevious = true;
        if (out) {
            out->deviceNumber = m_device;
            out->valid = false;
            out->source = QStringLiteral("IOCTL");
            out->timestampMs = nowMs;
        }
        return true;
    }

    const LONGLONG dRead   = current.ReadTime.QuadPart  - m_previous.ReadTime.QuadPart;
    const LONGLONG dWrite  = current.WriteTime.QuadPart - m_previous.WriteTime.QuadPart;
    const LONGLONG dQuery  = current.QueryTime.QuadPart - m_previous.QueryTime.QuadPart;
    const LONGLONG dBytesR = current.BytesRead.QuadPart - m_previous.BytesRead.QuadPart;
    const LONGLONG dBytesW = current.BytesWritten.QuadPart - m_previous.BytesWritten.QuadPart;

    m_previous = current;
    m_previousMs = nowMs;

    if (!out)
        return true;

    out->deviceNumber = m_device;
    out->source = QStringLiteral("IOCTL");
    out->timestampMs = nowMs;
    out->queueLength = double(current.QueueDepth);
    out->valid = true;

    // QueryTime is in 100 ns ticks of elapsed counter time.
    if (dQuery > 0) {
        const double elapsedSec = double(dQuery) / 1.0e7;
        double busy = double(dRead + dWrite) / double(dQuery) * 100.0;
        out->busyPercent = qBound(0.0, busy, 100.0);
        out->readBps = double(dBytesR) / elapsedSec;
        out->writeBps = double(dBytesW) / elapsedSec;
        if (out->readBps < 0.0) out->readBps = 0.0;
        if (out->writeBps < 0.0) out->writeBps = 0.0;
        // DISK_PERFORMANCE exposes no per-transfer service time, so latency is
        // only available through the PDH path; leave it at zero rather than
        // reporting a made-up number.
        out->latencySec = 0.0;
    } else {
        // QueryTime did not advance: the disk performance counters are off
        // (they can be force-enabled with an elevated `diskperf -y`).
        out->busyPercent = 0.0;
        out->readBps = 0.0;
        out->writeBps = 0.0;
        out->valid = false;
        if (error)
            *error = QStringLiteral("磁盘性能计数器未启用，请以管理员身份执行 diskperf -y 后重试");
        return false;
    }

    return true;
}
