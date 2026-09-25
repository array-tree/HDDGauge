QT += core gui widgets

TARGET   = HddGauge
TEMPLATE = app

CONFIG += c++11
CONFIG -= console

DEFINES += UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0601 WINVER=0x0601

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/SelfTest.cpp \
    src/core/WinUtil.cpp \
    src/core/PdhProbe.cpp \
    src/core/IoPerfProbe.cpp \
    src/core/SmartProbe.cpp \
    src/core/DriveEnumerator.cpp \
    src/core/MonitorWorker.cpp \
    src/ui/Theme.cpp \
    src/ui/GaugeBase.cpp \
    src/ui/UsageGauge.cpp \
    src/ui/RingGauge.cpp \
    src/ui/DashboardPage.cpp \
    src/ui/Sparkline.cpp \
    src/ui/InfoBar.cpp \
    src/ui/MainWindow.cpp

HEADERS += \
    src/SelfTest.h \
    src/core/DiskTypes.h \
    src/core/WinUtil.h \
    src/core/SpringValue.h \
    src/core/PdhProbe.h \
    src/core/IoPerfProbe.h \
    src/core/SmartProbe.h \
    src/core/DriveEnumerator.h \
    src/core/MonitorWorker.h \
    src/ui/Theme.h \
    src/ui/GaugeBase.h \
    src/ui/UsageGauge.h \
    src/ui/RingGauge.h \
    src/ui/DashboardPage.h \
    src/ui/Sparkline.h \
    src/ui/InfoBar.h \
    src/ui/MainWindow.h

RESOURCES += res/app.qrc

# pdh.lib is needed for the performance counter API. Note that MinGW 5.3's
# import library does NOT export PdhAddEnglishCounterW, which the probe
# resolves through GetProcAddress at runtime instead.
LIBS += -lpdh

# --- admin manifest ----------------------------------------------------------
# The shipped build embeds requireAdministrator so ATA IDENTIFY (nominal RPM)
# and S.M.A.R.T. (temperature) can be read from the physical drive.
# For unprivileged testing:  qmake CONFIG+=no_admin_manifest
no_admin_manifest {
    RC_FILE = res/app.rc
} else {
    RC_FILE = res/app_admin.rc
    DEFINES += HDDGAUGE_ADMIN_MANIFEST
}

# --- build layout ------------------------------------------------------------
# Paths must stay RELATIVE. Because this project lives under a non-ASCII path,
# embedding $$OUT_PWD (an absolute path containing CJK characters) into the
# generated Makefile makes moc choke on its --include argument.
CONFIG(debug, debug|release) {
    DESTDIR     = debug
    OBJECTS_DIR = obj/debug
    MOC_DIR     = moc/debug
    RCC_DIR     = rcc/debug
} else {
    DESTDIR     = release
    OBJECTS_DIR = obj/release
    MOC_DIR     = moc/release
    RCC_DIR     = rcc/release
}
