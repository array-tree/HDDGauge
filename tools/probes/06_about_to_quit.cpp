// Probe 6: does QCoreApplication::aboutToQuit fire on QApplication::quit()?
//
// Why it matters: HddGauge persists its window geometry, sampling interval and
// detail-panel state through QSettings. QWidget::closeEvent() covers the X
// button, but the tray menu calls QApplication::quit(), which is documented as
// "about to leave the main event loop" — whether that really reaches a slot
// registered with connect() on this Qt build decides whether saving settings
// needs an explicit call or can rely on the signal.
//
// This is the only probe that needs Qt, so it is built differently from 01-05:
//
//   set "QT_DIR=C:\Qt\5.8\mingw53_32"
//   set "MINGW_DIR=C:\Qt\Tools\mingw530_32"
//   set "PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%"
//   g++ -std=gnu++11 -o p6.exe 06_about_to_quit.cpp ^
//       -I%QT_DIR%/include ^
//       -I%QT_DIR%/include/QtCore ^
//       -I%QT_DIR%/include/QtGui ^
//       -I%QT_DIR%/include/QtWidgets ^
//       -L%QT_DIR%/lib -lQt5Core -lQt5Gui -lQt5Widgets
//   p6.exe
//
// The registry key is cleaned up on exit.

#include <QApplication>
#include <QSettings>
#include <QTimer>

#include <cstdio>

static bool g_fired = false;
static bool g_connectedSlotRan = false;

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("HddGaugeProbe"));
    QApplication::setApplicationName(QStringLiteral("aboutToQuit"));

    QSettings clear;
    clear.clear();

    // Exactly the shape used by MainWindow.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        g_fired = true;
        g_connectedSlotRan = true;
        std::printf("aboutToQuit slot ran (Qt %s)\n", qVersion());
    });

    QTimer::singleShot(300, &app, &QApplication::quit);
    const int rc = app.exec();

    std::printf("exec() returned %d\n", rc);
    std::printf("aboutToQuit fired: %s\n", g_fired ? "YES" : "NO");
    std::printf("slot actually entered: %s\n", g_connectedSlotRan ? "YES" : "NO");

    // Prove the signal's usefulness: a slot that writes settings must actually
    // land somewhere. The registry backend is used by the application, but a
    // sandboxed shell can be denied HKCU writes (QSettings then fails *silently*
    // — worth knowing before blaming your own code), so the round trip is
    // checked against an INI file here.
    {
        QSettings probe(QStringLiteral("p6_settings.ini"), QSettings::IniFormat);
        probe.clear();
        probe.setValue(QStringLiteral("probe/writtenBySignal"), g_fired);
        probe.sync();
        const bool readBack = QSettings(QStringLiteral("p6_settings.ini"), QSettings::IniFormat)
                                  .value(QStringLiteral("probe/writtenBySignal"), false).toBool();
        std::printf("settings round trip (ini): written=%s read=%s -> %s\n",
                    g_fired ? "true" : "false", readBack ? "true" : "false",
                    (readBack == g_fired) ? "OK" : "MISMATCH");
    }

    // And report whether the registry backend the app uses is writable here.
    {
        QSettings reg;
        reg.setValue(QStringLiteral("probe/registryWritable"), 1);
        reg.sync();
        const bool ok = QSettings().value(QStringLiteral("probe/registryWritable"), 0).toInt() == 1;
        std::printf("settings round trip (registry): %s%s\n", ok ? "OK" : "DENIED",
                    ok ? "" : "  <- this environment blocks HKCU writes; the app's "
                              "settings would not persist here");
        reg.clear();
    }

    return g_fired ? 0 : 1;
}
