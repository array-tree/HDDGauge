#include "MonitorWorker.h"
#include "DriveEnumerator.h"

#include <QDateTime>
#include <QTimer>

MonitorWorker::MonitorWorker(QObject* parent)
    : QObject(parent)
{
}

MonitorWorker::~MonitorWorker()
{
    m_fallback.close();
    m_pdh.stop();
}

void MonitorWorker::ensureTimer()
{
    if (m_timer)
        return;
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &MonitorWorker::onTick);
}

void MonitorWorker::initialize()
{
    ensureTimer();

    m_pdhRunning = m_pdh.start(&m_pdhError);
    if (!m_pdhRunning)
        emit statusMessage(QStringLiteral("性能计数器不可用：%1").arg(m_pdhError), true);

    refreshDevices();
}

void MonitorWorker::refreshDevices()
{
    QVector<int> candidates;

    // The PDH instance list is the authoritative set of disks we can actually
    // measure; add a plain 0..7 sweep so a drive that PDH does not expose still
    // appears in the picker.
    if (m_pdhRunning) {
        QVector<PdhReading> readings;
        QString error;
        if (m_pdh.poll(&readings, &error)) {
            for (const PdhReading& reading : readings)
                candidates.append(reading.deviceNumber);
        }
    }
    for (int i = 0; i < 8; ++i)
        candidates.append(i);

    QString note;
    const QVector<DriveDescriptor> found = DriveEnumerator::enumerate(candidates, &note);

    if (!found.isEmpty()) {
        m_devices = found;
        emit devicesReady(m_devices);
    } else {
        emit statusMessage(QStringLiteral("未发现任何物理磁盘"), true);
    }
    if (!note.isEmpty())
        emit statusMessage(note, true);

    if (m_active < 0 && !m_devices.isEmpty()) {
        int chosen = m_devices.first().deviceNumber;
        for (const DriveDescriptor& descriptor : m_devices) {
            if (descriptor.isRotating() && !descriptor.solidState) {
                chosen = descriptor.deviceNumber;
                break;
            }
        }
        m_active = chosen;
    }
}

void MonitorWorker::setActiveDevice(int deviceNumber)
{
    if (m_active == deviceNumber)
        return;
    m_active = deviceNumber;
    m_fallback.close();
    m_fallbackTried = false;
    m_fallbackFailed = false;
    m_firstTickDone = false;

    QVector<PdhReading> readings;
    QString error;
    if (m_pdhRunning)
        m_pdh.poll(&readings, &error);   // re-baseline after a switch
}

void MonitorWorker::startTicking(int intervalMs)
{
    ensureTimer();
    m_firstTickDone = false;
    m_timer->setTimerType(intervalMs < 500 ? Qt::PreciseTimer : Qt::CoarseTimer);
    m_timer->start(qBound(kMinIntervalMs, intervalMs, kMaxIntervalMs));
}

void MonitorWorker::stopTicking()
{
    if (m_timer)
        m_timer->stop();
}

void MonitorWorker::onTick()
{
    DiskSample sample;
    sample.deviceNumber = m_active;
    sample.timestampMs = QDateTime::currentMSecsSinceEpoch();
    sample.source = QStringLiteral("-");

    if (m_active < 0) {
        emit sampleReady(sample);
        return;
    }

    // ---- primary path: performance counters -------------------------------
    bool pdhTransient = false;
    if (m_pdhRunning) {
        QVector<PdhReading> readings;
        QString error;
        if (m_pdh.poll(&readings, &error)) {
            for (const PdhReading& reading : readings) {
                if (reading.deviceNumber != m_active)
                    continue;
                sample.valid = true;
                sample.source = QStringLiteral("PDH");
                sample.busyPercent = reading.busyPercent();
                sample.readBps = reading.readBps;
                sample.writeBps = reading.writeBps;
                sample.queueLength = reading.queueLength;
                sample.latencySec = reading.latencySec;
                break;
            }
        } else if (m_pdh.lastPollWasTransient()) {
            pdhTransient = true;
        } else if (!m_firstTickDone) {
            emit statusMessage(error, true);
        }
    }

    // A tick where the counter set carried no new data is not a failure: hold
    // the previous reading (by emitting nothing) instead of dropping into the
    // IOCTL path, which would silently change the reported data source.
    if (!sample.valid && pdhTransient) {
        m_firstTickDone = true;
        return;
    }

    // ---- fallback path: IOCTL_DISK_PERFORMANCE ----------------------------
    if (!sample.valid) {
        if (!m_fallback.isOpen() || m_fallback.deviceNumber() != m_active) {
            if (!m_fallbackTried) {
                m_fallbackTried = true;
                QString error;
                if (!m_fallback.open(m_active, &error)) {
                    m_fallbackFailed = true;
                    emit statusMessage(error, true);
                }
            }
        }
        if (m_fallback.isOpen()) {
            QString error;
            DiskSample fallbackSample;
            if (m_fallback.poll(&fallbackSample, &error) && fallbackSample.valid) {
                sample = fallbackSample;
                sample.deviceNumber = m_active;
            } else if (!error.isEmpty() && !m_fallbackFailed) {
                m_fallbackFailed = true;
                emit statusMessage(error, true);
            }
        }
    }

    m_firstTickDone = true;
    emit sampleReady(sample);
}
