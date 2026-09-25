#pragma once

#include "DiskTypes.h"
#include "IoPerfProbe.h"
#include "PdhProbe.h"

#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

/// Lives on a worker QThread and owns every blocking Win32 call.
///
/// The collection interval is configurable (100 ms .. 60 s, default 250 ms).
/// PDH derives a rate counter from the time stamps of the last two collections,
/// so the interval is free; tools/probes/05_fast_pdh_poll.cpp verified that
/// 100 ms sampling yields the same means as 1 s sampling with no invalid status
/// codes. Anything faster than ~100 ms is pointless: the counter update period
/// of the storage stack is the limit, and the extra ticks only add jitter.
class MonitorWorker : public QObject
{
    Q_OBJECT

public:
    /// Hard floor for the sampling interval — see the class comment.
    static const int kMinIntervalMs = 100;
    static const int kMaxIntervalMs = 60000;

    explicit MonitorWorker(QObject* parent = nullptr);
    ~MonitorWorker() override;

    QVector<DriveDescriptor> devices() const { return m_devices; }

public slots:
    void initialize();
    void refreshDevices();
    void setActiveDevice(int deviceNumber);
    void startTicking(int intervalMs);
    void stopTicking();

signals:
    void devicesReady(const QVector<DriveDescriptor>& devices);
    void sampleReady(const DiskSample& sample);
    void statusMessage(const QString& text, bool warning);

private slots:
    void onTick();

private:
    void ensureTimer();

    QVector<DriveDescriptor> m_devices;
    PdhProbe                 m_pdh;
    IoPerfProbe              m_fallback;
    QTimer*                  m_timer = nullptr;

    int     m_active = -1;
    bool    m_pdhRunning = false;
    QString m_pdhError;
    bool    m_fallbackTried = false;
    bool    m_fallbackFailed = false;
    bool    m_firstTickDone = false;
};
