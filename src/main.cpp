#include <QApplication>
#include <QIcon>
#include <QTimer>

#include <cstdio>

#include <windows.h>

#include "SelfTest.h"
#include "core/DiskTypes.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

namespace {

/// A GUI-subsystem binary has no console of its own; borrow the launching
/// terminal's so `--selftest` can print where the user can see it.
void attachParentConsole()
{
    if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
}

int intArgument(const QStringList& args, const QString& name, int fallback)
{
    const int index = args.indexOf(name);
    if (index >= 0 && index + 1 < args.size()) {
        bool ok = false;
        const int value = args.at(index + 1).toInt(&ok);
        if (ok)
            return value;
    }
    return fallback;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const QStringList args = QApplication::arguments();

    // The screenshot path must be reproducible: run it under its own settings
    // scope so a stale window geometry (or a collapsed detail panel) saved by a
    // real user session can never change what the documentation captures.
    const bool shotMode = args.contains(QStringLiteral("--screenshot"));

    QApplication::setOrganizationName(QStringLiteral("HddGauge"));
    QApplication::setApplicationName(shotMode ? QStringLiteral("HddGauge-shot")
                                             : QStringLiteral("HddGauge"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/res/app.ico")));
    app.setStyleSheet(Theme::styleSheet());
    app.setFont(Theme::uiFont(12));

    qRegisterMetaType<DriveDescriptor>("DriveDescriptor");
    qRegisterMetaType<DiskSample>("DiskSample");
    qRegisterMetaType<QVector<DriveDescriptor> >("QVector<DriveDescriptor>");

    // ---- head-less verification path -------------------------------------
    const int selfTestIndex = args.indexOf(QStringLiteral("--selftest"));
    if (selfTestIndex >= 0) {
        attachParentConsole();
        QString report = QStringLiteral("selftest.txt");
        if (selfTestIndex + 1 < args.size() && !args.at(selfTestIndex + 1).startsWith(QLatin1String("--")))
            report = args.at(selfTestIndex + 1);
        return SelfTest::run(report);
    }

    MainWindow window;
    window.show();

    // ---- off-screen render path (used to review the layout) --------------
    const int shotIndex = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex >= 0 && shotIndex + 1 < args.size()) {
        const QString path = args.at(shotIndex + 1);
        const int delay = intArgument(args, QStringLiteral("--shot-delay"), 3500);
        const int mode = intArgument(args, QStringLiteral("--ring-mode"), -1);
        if (mode >= 0)
            window.setRingModeForTesting(mode == 1 ? RingMode::TransferRate : RingMode::EquivalentRpm);
        const int device = intArgument(args, QStringLiteral("--device"), -1);
        if (device >= 0)
            window.setPreferredDeviceForTesting(device);
        const int interval = intArgument(args, QStringLiteral("--interval"), -1);
        if (interval > 0)
            window.setIntervalForTesting(interval);
        if (args.contains(QStringLiteral("--compact")))
            window.setDetailVisibleForTesting(false, false);

        QTimer::singleShot(delay, [&window, path]() {
            const QPixmap pixmap = window.grab();
            if (!pixmap.save(path))
                std::fprintf(stderr, "failed to write %s\n", qPrintable(path));
            QApplication::quit();
        });
    }

    return app.exec();
}
