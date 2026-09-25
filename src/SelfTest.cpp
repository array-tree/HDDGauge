#include "SelfTest.h"

#include "core/DiskTypes.h"
#include "core/DriveEnumerator.h"
#include "core/IoPerfProbe.h"
#include "core/PdhProbe.h"
#include "core/SmartProbe.h"
#include "core/WinUtil.h"

#include <QDateTime>
#include <QFile>
#include <QTextStream>

#include <cstdio>

#include <windows.h>

namespace {

QStringList g_lines;

void emitLine(const QString& line)
{
    g_lines.append(line);
    std::printf("%s\n", line.toLocal8Bit().constData());
    std::fflush(stdout);
}

void emitField(const QString& key, const QString& value)
{
    emitLine(QStringLiteral("    %1%2").arg(key.leftJustified(14, QLatin1Char(' ')), value));
}

} // namespace

int SelfTest::run(const QString& reportPath)
{
    emitLine(QStringLiteral("=== HddGauge self-test ==="));
    emitLine(QStringLiteral("time            : %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    emitLine(QStringLiteral("elevated        : %1").arg(wu::isElevated() ? QStringLiteral("yes") : QStringLiteral("no")));
    emitLine(QString());

    // ------------------------------------------------------------------
    emitLine(QStringLiteral("[1] PDH performance counters"));
    PdhProbe pdh;
    QString error;
    QVector<int> candidates;

    if (!pdh.start(&error)) {
        emitLine(QStringLiteral("    FAILED: %1").arg(error));
    } else {
        ::Sleep(1100);   // rate counters need two collections
        QVector<PdhReading> readings;
        if (pdh.poll(&readings, &error)) {
            emitLine(QStringLiteral("    OK, %1 physical disk instance(s)").arg(readings.size()));
            for (const PdhReading& reading : readings) {
                candidates.append(reading.deviceNumber);
                emitLine(QStringLiteral("    drive %1 : busy=%2%  read=%3  write=%4  queue=%5  latency=%6 ms")
                         .arg(reading.deviceNumber, 2)
                         .arg(reading.busyPercent(), 6, 'f', 2)
                         .arg(wu::formatRate(reading.readBps), 12)
                         .arg(wu::formatRate(reading.writeBps), 12)
                         .arg(reading.queueLength, 5, 'f', 2)
                         .arg(reading.latencySec * 1000.0, 6, 'f', 3));
            }
        } else {
            emitLine(QStringLiteral("    FAILED: %1").arg(error));
        }
    }
    emitLine(QString());

    // ------------------------------------------------------------------
    emitLine(QStringLiteral("[2] drive enumeration"));
    for (int i = 0; i < 8; ++i)
        candidates.append(i);

    QString note;
    const QVector<DriveDescriptor> devices = DriveEnumerator::enumerate(candidates, &note);
    emitLine(QStringLiteral("    %1 drive(s) found").arg(devices.size()));
    if (!note.isEmpty())
        emitLine(QStringLiteral("    note: %1").arg(note));

    for (const DriveDescriptor& descriptor : devices) {
        emitLine(QStringLiteral("  -- PhysicalDrive%1").arg(descriptor.deviceNumber));
        emitField(QStringLiteral("model"), descriptor.model.isEmpty() ? QStringLiteral("(unknown)") : descriptor.model);
        emitField(QStringLiteral("vendor"), descriptor.vendor);
        emitField(QStringLiteral("serial"), descriptor.serial);
        emitField(QStringLiteral("bus"), descriptor.busName);
        emitField(QStringLiteral("capacity"), descriptor.sizeBytes ? wu::formatBytes(descriptor.sizeBytes)
                                                                   : QStringLiteral("(unknown)"));
        emitField(QStringLiteral("media"), descriptor.mediaText()
                                               + (descriptor.mediaKnown ? QString() : QStringLiteral(" (guessed)")));
        emitField(QStringLiteral("nominal rpm"), descriptor.nominalRpm > 0
                                                     ? QString::number(descriptor.nominalRpm)
                                                     : QStringLiteral("(unknown)"));
        emitField(QStringLiteral("rpm source"), descriptor.rpmSource);
        emitField(QStringLiteral("letters"), descriptor.letters);
        if (!descriptor.accessNote.isEmpty())
            emitField(QStringLiteral("access"), descriptor.accessNote);

        // derived equivalent speed at a couple of representative loads
        DiskSample demo;
        demo.busyPercent = 100.0;
        demo.queueLength = 1.0;
        emitField(QStringLiteral("equiv@100%"),
                  descriptor.nominalRpm > 0
                      ? QStringLiteral("%1 rpm").arg(demo.equivalentRpm(descriptor.nominalRpm), 0, 'f', 0)
                      : QStringLiteral("n/a"));
    }
    emitLine(QString());

    // ------------------------------------------------------------------
    emitLine(QStringLiteral("[3] IOCTL_DISK_PERFORMANCE fallback"));
    for (int i = 0; i < 3; ++i) {
        IoPerfProbe probe;
        QString ioError;
        if (!probe.open(i, &ioError)) {
            emitLine(QStringLiteral("  PhysicalDrive%1: %2").arg(i).arg(ioError));
            continue;
        }
        ::Sleep(600);
        DiskSample sample;
        probe.poll(&sample, &ioError);
        ::Sleep(600);
        probe.poll(&sample, &ioError);
        emitLine(QStringLiteral("  PhysicalDrive%1: valid=%2 busy=%3% read=%4 write=%5")
                 .arg(i)
                 .arg(sample.valid ? QStringLiteral("yes") : QStringLiteral("no"))
                 .arg(sample.busyPercent, 0, 'f', 2)
                 .arg(wu::formatRate(sample.readBps))
                 .arg(wu::formatRate(sample.writeBps)));
    }
    emitLine(QString());
    emitLine(QStringLiteral("=== end ==="));

    if (!reportPath.isEmpty()) {
        QFile file(reportPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out.setCodec("UTF-8");
            for (const QString& line : g_lines)
                out << line << "\n";
            file.close();
        }
    }
    return 0;
}
