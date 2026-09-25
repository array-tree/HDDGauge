#pragma once

#include <QString>
#include <QVector>

#include <windows.h>
#include <pdh.h>

/// One physical disk's counters, as reported by PDH in a single collection.
struct PdhReading
{
    int    deviceNumber = -1;
    double idlePercent = 100.0;
    double readBps = 0.0;
    double writeBps = 0.0;
    double queueLength = 0.0;
    double latencySec = 0.0;

    double busyPercent() const
    {
        double busy = 100.0 - idlePercent;
        if (busy < 0.0) busy = 0.0;
        if (busy > 100.0) busy = 100.0;
        return busy;
    }
};

/// Reads the Windows "PhysicalDisk" performance counter set.
///
/// Notes learned the hard way on this toolchain:
///  * PdhAddEnglishCounterW exists in pdh.dll but is *not* exported by MinGW
///    5.3's libpdh.a, so it is resolved with GetProcAddress.
///  * English counter names must be used; on a localized Windows the names
///    returned by PdhAddCounter are translated and would not match.
///  * Instance names look like "0 D:" / "1 C:" — the leading integer is the
///    PhysicalDrive number, which is exactly the key we want.
///  * Rate counters are differential, but the interval is *not* fixed at one
///    second: PDH derives the rate from the time stamps of the last two
///    collections. tools/probes/05_fast_pdh_poll.cpp measured the counter set
///    at 100/200/250/500/1000 ms and found no invalid status codes and means
///    agreeing to within a few percent, so polling down to 100 ms is safe.
class PdhProbe
{
public:
    PdhProbe();
    ~PdhProbe();

    bool start(QString* error);
    void stop();
    bool isRunning() const { return m_query != nullptr; }

    /// Collect and decode every instance. Safe between 100 ms and several
    /// seconds; see the class comment.
    bool poll(QVector<PdhReading>* out, QString* error);

    /// True when the previous poll() failed only because the counter set had
    /// no new data for that tick (PDH_INVALID_DATA / PDH_NO_DATA). Sampling
    /// faster than the counter update can hit this; it is not a fault and must
    /// not be surfaced to the user.
    bool lastPollWasTransient() const { return m_transient; }

private:
    PDH_HQUERY   m_query = nullptr;
    PDH_HCOUNTER m_idle = nullptr;
    PDH_HCOUNTER m_read = nullptr;
    PDH_HCOUNTER m_write = nullptr;
    PDH_HCOUNTER m_queue = nullptr;
    PDH_HCOUNTER m_latency = nullptr;
    QString      m_error;
    bool         m_transient = false;
};

/// Parse "0 D:" / "1 C:" / "_Total" into a physical drive number (-1 if none).
int deviceNumberFromPdhInstance(const QString& instanceName);
