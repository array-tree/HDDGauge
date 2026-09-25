#pragma once

#include <QMetaType>
#include <QString>
#include <climits>

// ---------------------------------------------------------------------------
// Static description of one physical drive.
// ---------------------------------------------------------------------------
struct DriveDescriptor
{
    int      deviceNumber = -1;     // PhysicalDriveN
    QString  devicePath;            // \\.\PhysicalDriveN
    QString  vendor;
    QString  model;
    QString  serial;
    QString  revision;
    quint64  sizeBytes = 0;
    int      busType = -1;
    QString  busName;
    bool     removable = false;

    // media / rotation. Both values come from the model string, never from the
    // hardware: the app runs unelevated, so ATA IDENTIFY is not an option.
    bool     solidState = false;    // true => rotating-media gauges are meaningless
    bool     mediaKnown = false;    // false => we could not decide HDD vs SSD
    int      nominalRpm = 0;        // 0 => unknown
    QString  rpmSource;             // where nominalRpm came from

    QString  letters;               // "C:, D:"
    bool     queried = false;       // device property query succeeded
    QString  accessNote;            // why a device could not be opened

    bool isRotating() const { return !solidState; }

    QString displayName() const
    {
        QString m = model.trimmed();
        if (m.isEmpty())
            m = QStringLiteral("物理磁盘 %1").arg(deviceNumber);
        QString tail = letters.trimmed();
        if (tail.isEmpty())
            tail = devicePath.section(QLatin1Char('\\'), -1);
        return QStringLiteral("#%1  %2   [%3]").arg(deviceNumber).arg(m, tail);
    }

    QString rpmText() const
    {
        if (solidState)
            return QStringLiteral("非旋转介质");
        if (nominalRpm > 0)
            return QStringLiteral("%1 RPM").arg(nominalRpm);
        return QStringLiteral("未知");
    }

    QString mediaText() const
    {
        if (solidState)  return solidState && mediaKnown ? QStringLiteral("固态 (SSD)")
                                                         : QStringLiteral("固态");
        if (!mediaKnown) return QStringLiteral("未知");
        return QStringLiteral("机械 (HDD)");
    }
};

// ---------------------------------------------------------------------------
// One instantaneous measurement of a physical drive.
// ---------------------------------------------------------------------------
struct DiskSample
{
    int     deviceNumber = -1;
    double  busyPercent = 0.0;      // 0..100
    double  readBps = 0.0;
    double  writeBps = 0.0;
    double  queueLength = 0.0;
    double  latencySec = 0.0;       // avg. disk sec / transfer
    bool    valid = false;
    QString source;                 // "PDH" / "IOCTL" / "-"
    qint64  timestampMs = 0;

    double transferBps() const { return readBps + writeBps; }

    // Equivalent spindle speed: how fast the platters would have to be spinning
    // to sustain this workload, using the nominal speed as the 100%-queue-free
    // reference. Honest labelling matters: this is a *derived* figure.
    double equivalentRpm(int nominalRpm) const
    {
        if (nominalRpm <= 0)
            return 0.0;
        double load = busyPercent / 100.0;
        load *= 1.0 + 0.25 * qBound(0.0, queueLength, 4.0);
        return nominalRpm * qBound(0.0, load, 1.0);
    }
};

// Mode of the outer ring.
enum class RingMode {
    EquivalentRpm = 0,   // 外圈 = 等效负载转速
    TransferRate  = 1    // 外圈 = 读写速率 MB/s
};

Q_DECLARE_METATYPE(DriveDescriptor)
Q_DECLARE_METATYPE(DiskSample)
