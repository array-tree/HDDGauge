#pragma once

#include "DiskTypes.h"
#include "WinUtil.h"

#include <QString>

/// Fallback collector built on IOCTL_DISK_PERFORMANCE.
///
/// Independent of PDH, but it needs a read/write handle on the physical drive,
/// i.e. elevation. Used only when the performance counters are unavailable.
class IoPerfProbe
{
public:
    IoPerfProbe() = default;
    ~IoPerfProbe();

    bool open(int deviceNumber, QString* error);
    void close();
    bool isOpen() const { return m_handle.valid(); }
    int  deviceNumber() const { return m_device; }

    /// Diff against the previous snapshot. The first call after open() only
    /// establishes a baseline and returns valid == false.
    bool poll(DiskSample* out, QString* error);

private:
    wu::Handle       m_handle;
    int              m_device = -1;
    bool             m_havePrevious = false;
    DISK_PERFORMANCE m_previous;
    qint64           m_previousMs = 0;
};
