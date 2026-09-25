#include "MainWindow.h"

#include "../core/MonitorWorker.h"
#include "../core/WinUtil.h"
#include "DashboardPage.h"
#include "InfoBar.h"
#include "Sparkline.h"
#include "Theme.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QSettings>
#include <QScreen>
#include <QSignalBlocker>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// Sampling periods offered in the picker. 100 ms is the measured floor: see
/// tools/probes/05_fast_pdh_poll.cpp — the storage stack updates its counters on
/// roughly that cadence, so faster polling only repeats the same numbers.
const int kIntervalPresets[] = { 100, 200, 250, 500, 1000, 2000 };
const int kDefaultIntervalMs = 250;

/// Length of the collapse/expand slide. Long enough to read as motion, short
/// enough that a double toggle never feels laggy.
const int kDetailSlideMs = 180;

/// Strip everything a drive descriptor carries that points back at this
/// machine.
///
/// Applied to the list the instant it arrives from the worker, so no widget
/// downstream — the drive picker, the parameter grid, the tray tooltip, the
/// status line — can leak a real model name, serial or drive letter into a
/// documentation capture. Capacity, bus, media type and RPM are deliberately
/// left alone: they describe the class of drive, not the machine it is in.
void anonymizeDevices(QVector<DriveDescriptor>& devices)
{
    for (int i = 0; i < devices.size(); ++i) {
        DriveDescriptor& descriptor = devices[i];
        descriptor.model = QStringLiteral("示例磁盘 %1").arg(i + 1);
        descriptor.vendor.clear();
        descriptor.serial.clear();
        descriptor.revision.clear();
        descriptor.letters = QStringLiteral("%1:").arg(QChar('X' + i));
    }
}

QString intervalText(int ms)
{
    if (ms < 1000)
        return QStringLiteral("%1 ms").arg(ms);
    return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'f', (ms % 1000) ? 1 : 0);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RootPanel"));
    setWindowTitle(QStringLiteral("硬盘状态仪表盘"));
    setWindowIcon(QIcon(QStringLiteral(":/res/app.ico")));

    m_detailAnim = new QPropertyAnimation(this, "detailProgress", this);
    m_detailAnim->setDuration(kDetailSlideMs);
    m_detailAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_detailAnim, &QPropertyAnimation::finished,
            this, &MainWindow::onDetailTransitionFinished);

    buildUi();
    wireWorker();

    // QApplication::quit() (the tray menu, the screenshot path) never delivers a
    // closeEvent, and Qt 5.8 does emit aboutToQuit() for it — verified with
    // tools/probes/06_about_to_quit.cpp. Without this the settings were only
    // ever persisted when the user closed the window with the X.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::saveSettings);

    // Only fall back to the default size when nothing was restored — calling
    // resize() unconditionally after restoreGeometry() silently threw the
    // remembered size away (the position survived, the size did not).
    const bool restored = loadSettings();
    if (!restored)
        resize(720, 880);

    // ...and never start larger than the screen. The default height does not fit
    // a 1366x768 laptop, and a geometry remembered from a bigger monitor is
    // just as unusable; Qt would otherwise clamp the window on its own, at a
    // size nobody chose deliberately.
    QScreen* screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    const int maxWidth = qMax(460, available.width() - 24);
    const int maxHeight = qMax(460, available.height() - 24);
    if (width() > maxWidth || height() > maxHeight)
        resize(qMin(width(), maxWidth), qMin(height(), maxHeight));
}

MainWindow::~MainWindow()
{
    m_shuttingDown = true;
    if (m_tray) {
        m_tray->hide();
        delete m_tray;
        m_tray = nullptr;
    }
    if (m_thread && m_thread->isRunning()) {
        QMetaObject::invokeMethod(m_worker, "stopTicking", Qt::BlockingQueuedConnection);
        m_thread->quit();
        if (!m_thread->wait(3000))
            m_thread->terminate();
    }
}

void MainWindow::setRingModeForTesting(RingMode mode)
{
    m_modeCombo->setCurrentIndex(mode == RingMode::TransferRate ? 1 : 0);
}

void MainWindow::setPreferredDeviceForTesting(int deviceNumber)
{
    m_preferredDevice = deviceNumber;
    // The device list may already be in (the worker thread is fast); if so,
    // re-run the selection now instead of waiting for the next enumeration.
    const int index = indexOfDevice(deviceNumber);
    if (index >= 0)
        m_deviceCombo->setCurrentIndex(index);
}

void MainWindow::setDetailVisibleForTesting(bool sparkline, bool infoBar)
{
    // `--screenshot` grabs the window; an animated toggle could be captured
    // halfway, so the off-screen path always snaps.
    setDetailShown(sparkline, infoBar, true, false);
}

void MainWindow::setAnonymizeForTesting(bool on)
{
    m_anonymize = on;
}

void MainWindow::setIntervalForTesting(int intervalMs)
{
    int index = m_intervalCombo->findData(intervalMs);
    if (index < 0) {
        // Nearest preset, so an arbitrary value still renders something sane.
        int best = 0;
        for (int i = 0; i < m_intervalCombo->count(); ++i) {
            if (qAbs(m_intervalCombo->itemData(i).toInt() - intervalMs)
                < qAbs(m_intervalCombo->itemData(best).toInt() - intervalMs))
                best = i;
        }
        index = best;
    }
    m_intervalCombo->setCurrentIndex(index);
}

// QWidget::setWindowFlag() only exists from Qt 5.9; Qt 5.8 needs the flags
// round-trip, which also requires re-showing the window.
void MainWindow::applyAlwaysOnTop(bool on)
{
    const bool wasVisible = isVisible();
    Qt::WindowFlags flags = windowFlags();
    if (on)
        flags |= Qt::WindowStaysOnTopHint;
    else
        flags &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    if (wasVisible)
        show();
}

void MainWindow::buildUi()
{
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(16, 14, 16, 14);
    m_root->setSpacing(11);

    // ---- header row 1: title + drive picker ------------------------------
    QHBoxLayout* header = new QHBoxLayout;
    header->setSpacing(9);

    QLabel* title = new QLabel(QStringLiteral("硬盘状态仪表盘"), this);
    title->setObjectName(QStringLiteral("Title"));

    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
    m_deviceCombo->setMinimumContentsLength(22);
    m_deviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_deviceCombo->setToolTip(QStringLiteral("选择要监视的物理磁盘"));

    header->addWidget(title);
    header->addSpacing(6);
    header->addWidget(m_deviceCombo, 1);
    m_root->addLayout(header);

    // ---- header row 2: every control -------------------------------------
    QHBoxLayout* controls = new QHBoxLayout;
    controls->setSpacing(7);

    QLabel* modeCaption = new QLabel(QStringLiteral("外圈"), this);
    modeCaption->setObjectName(QStringLiteral("Caption"));

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(QStringLiteral("等效转速"));
    m_modeCombo->addItem(QStringLiteral("读写速率"));
    m_modeCombo->setToolTip(QStringLiteral("切换外圈仪表显示的内容"));

    QLabel* intervalCaption = new QLabel(QStringLiteral("采样"), this);
    intervalCaption->setObjectName(QStringLiteral("Caption"));

    m_intervalCombo = new QComboBox(this);
    for (size_t i = 0; i < sizeof(kIntervalPresets) / sizeof(kIntervalPresets[0]); ++i) {
        const int ms = kIntervalPresets[i];
        m_intervalCombo->addItem(intervalText(ms), ms);
    }
    m_intervalCombo->setToolTip(QStringLiteral(
        "数据刷新间隔\n"
        "100 ms 为实测下限：硬盘计数器的更新周期就在这个量级，再快只会采到重复值"));
    {
        const int index = m_intervalCombo->findData(kDefaultIntervalMs);
        m_intervalCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    m_detailButton = new QToolButton(this);
    m_detailButton->setCheckable(true);
    m_detailButton->setChecked(true);
    m_detailButton->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    m_detailButton->setObjectName(QStringLiteral("DetailToggle"));

    m_refreshButton = new QToolButton(this);
    m_refreshButton->setText(QStringLiteral("刷新"));
    m_refreshButton->setToolTip(QStringLiteral("重新枚举物理磁盘并读取硬件信息（型号 / 容量 / 转速）"));

    m_pinButton = new QToolButton(this);
    m_pinButton->setText(QStringLiteral("置顶"));
    m_pinButton->setCheckable(true);
    m_pinButton->setToolTip(QStringLiteral("窗口始终保持在其他窗口之上"));

    controls->addWidget(modeCaption);
    controls->addWidget(m_modeCombo);
    controls->addSpacing(6);
    controls->addWidget(intervalCaption);
    controls->addWidget(m_intervalCombo);
    controls->addStretch(1);
    controls->addWidget(m_refreshButton);
    controls->addWidget(m_pinButton);
    m_root->addLayout(controls);

    // ---- dial ------------------------------------------------------------
    m_dashboard = new DashboardPage(this);
    m_root->addWidget(m_dashboard, 1);

    // ---- collapsible detail panel ----------------------------------------
    m_detailPanel = new QWidget(this);
    m_detailPanel->setObjectName(QStringLiteral("DetailPanel"));
    QVBoxLayout* detailLayout = new QVBoxLayout(m_detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(9);

    m_sparkline = new Sparkline(m_detailPanel);
    m_sparkline->setWindowSeconds(60);
    detailLayout->addWidget(m_sparkline);

    m_infoBar = new InfoBar(m_detailPanel);
    detailLayout->addWidget(m_infoBar);

    m_root->addWidget(m_detailPanel);

    // ---- footer: status on the left, the detail toggle in the corner -----
    QHBoxLayout* footer = new QHBoxLayout;
    footer->setSpacing(9);

    m_statusLabel = new QLabel(QStringLiteral("正在初始化…"), this);
    m_statusLabel->setObjectName(QStringLiteral("Status"));
    m_statusLabel->setWordWrap(true);

    // The toggle lives here rather than in the toolbar: it is about the panel
    // that sits directly above it, and it stays in the same corner whether the
    // panel is open or closed, so the button never moves out from under the
    // pointer.
    footer->addWidget(m_statusLabel, 1);
    footer->addWidget(m_detailButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_root->addLayout(footer);

    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDeviceComboChanged);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeComboChanged);
    connect(m_intervalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onIntervalComboChanged);
    connect(m_detailButton, &QToolButton::toggled, this, &MainWindow::onDetailToggled);
    connect(m_refreshButton, &QToolButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(m_dashboard, &DashboardPage::contextMenuRequested, this, &MainWindow::showContextMenu);

    // Only enable the always-on-top toggle once we know the window flags work.
    connect(m_pinButton, &QToolButton::toggled, this, &MainWindow::applyAlwaysOnTop);

    syncDetailControls();

    // ---- tray ------------------------------------------------------------
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(windowIcon(), this);
        m_tray->setToolTip(QStringLiteral("硬盘状态仪表盘"));
        QMenu* trayMenu = new QMenu(this);
        trayMenu->addAction(QStringLiteral("显示 / 隐藏窗口"), this, [this]() {
            setVisible(!isVisible());
            if (isVisible()) {
                raise();
                activateWindow();
            }
        });
        trayMenu->addSeparator();
        trayMenu->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);
        m_tray->setContextMenu(trayMenu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            onTrayActivated(int(reason));
        });
        m_tray->show();
    }
}

void MainWindow::wireWorker()
{
    m_thread = new QThread(this);
    m_worker = new MonitorWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &MonitorWorker::initialize);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &MonitorWorker::devicesReady, this, &MainWindow::onDevicesReady);
    connect(m_worker, &MonitorWorker::sampleReady, this, &MainWindow::onSampleReady);
    connect(m_worker, &MonitorWorker::statusMessage, this, &MainWindow::onStatusMessage);

    connect(this, &MainWindow::requestActiveDevice, m_worker, &MonitorWorker::setActiveDevice);
    connect(this, &MainWindow::requestTicking, m_worker, &MonitorWorker::startTicking);
    connect(this, &MainWindow::requestStopTicking, m_worker, &MonitorWorker::stopTicking);
    connect(this, &MainWindow::requestRefresh, m_worker, &MonitorWorker::refreshDevices);

    m_thread->start();
}

int MainWindow::currentIntervalMs() const
{
    const int index = m_intervalCombo->currentIndex();
    if (index < 0)
        return kDefaultIntervalMs;
    const int ms = m_intervalCombo->itemData(index).toInt();
    return ms > 0 ? ms : kDefaultIntervalMs;
}

bool MainWindow::loadSettings()
{
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    const bool restored = !geometry.isEmpty();
    if (restored)
        restoreGeometry(geometry);

    m_preferredDevice = settings.value(QStringLiteral("device/number"), -1).toInt();

    const int mode = settings.value(QStringLiteral("ring/mode"), 0).toInt();
    m_modeCombo->setCurrentIndex(mode == 1 ? 1 : 0);
    m_dashboard->setMode(mode == 1 ? RingMode::TransferRate : RingMode::EquivalentRpm);

    // sampling interval
    {
        const int wanted = settings.value(QStringLiteral("sampling/intervalMs"), kDefaultIntervalMs).toInt();
        int index = m_intervalCombo->findData(wanted);
        if (index < 0)
            index = m_intervalCombo->findData(kDefaultIntervalMs);
        if (index < 0)
            index = 0;
        m_intervalCombo->blockSignals(true);
        m_intervalCombo->setCurrentIndex(index);
        m_intervalCombo->blockSignals(false);
        m_infoBar->setSamplingIntervalMs(currentIntervalMs());
    }

    // detail panel state — restored without touching the window geometry
    m_showSparkline = settings.value(QStringLiteral("ui/sparkline"), true).toBool();
    m_showInfoBar = settings.value(QStringLiteral("ui/infoBar"), true).toBool();
    setDetailShown(m_showSparkline, m_showInfoBar, false);

    const bool pinned = settings.value(QStringLiteral("window/onTop"), false).toBool();
    m_pinButton->setChecked(pinned);
    if (pinned)
        applyAlwaysOnTop(true);

    return restored;
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/onTop"), m_pinButton->isChecked());
    settings.setValue(QStringLiteral("ring/mode"), m_modeCombo->currentIndex());
    settings.setValue(QStringLiteral("sampling/intervalMs"), currentIntervalMs());
    settings.setValue(QStringLiteral("ui/sparkline"), m_showSparkline);
    settings.setValue(QStringLiteral("ui/infoBar"), m_showInfoBar);
    if (m_deviceCombo->currentIndex() >= 0)
        settings.setValue(QStringLiteral("device/number"), m_deviceCombo->currentData().toInt());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    event->accept();
}

// ---------------------------------------------------------------------------
// collapsible detail panel
// ---------------------------------------------------------------------------
void MainWindow::syncDetailControls()
{
    const bool shown = detailVisible();

    {
        const QSignalBlocker blocker(m_detailButton);
        m_detailButton->setChecked(shown);
    }
    m_detailButton->setText(shown ? QStringLiteral("详情 ▾") : QStringLiteral("详情 ▸"));
    m_detailButton->setToolTip(shown
                                   ? QStringLiteral("隐藏曲线图与详细参数栏 (Ctrl+D)")
                                   : QStringLiteral("显示曲线图与详细参数栏 (Ctrl+D)"));
}

/// Force a synchronous layout pass. Resizing the window and then reading a
/// child's height only works if the layout has actually run in between; a posted
/// LayoutRequest would not have been delivered yet.
void MainWindow::relayout()
{
    m_root->invalidate();
    m_root->activate();
}

/// Position the panel (and the window edge that follows it) at `progress`.
///
/// The panel is pinned to a fraction of its expanded height and the window
/// shrinks by the same fraction of the total delta, so the dial — the only
/// stretchy widget — keeps the size it had. Doing this per frame is what makes
/// the transition a slide instead of a jump: without it the panel would pop to
/// its full height first and squeeze the dial for one frame.
void MainWindow::setDetailProgress(qreal progress)
{
    m_detailProgress = progress;
    if (m_detailDelta <= 0)
        return;

    const int panelHeight = qRound(progress * m_detailPanelHeight);
    m_detailPanel->setFixedHeight(panelHeight);
    resize(width(), m_detailBase + qRound(progress * m_detailDelta));
    relayout();
}

/// Land the panel at one end of the slide: the window takes the height that
/// belongs to that end, and the dial is then checked against the size it had
/// before the toggle. The check is a safety net — if the layout spacing or the
/// parameter grid ever stops behaving the way the delta assumes, the dial is
/// still exactly the size the user was looking at before they touched anything.
void MainWindow::finishDetailAt(bool shown)
{
    m_detailPanel->setMinimumHeight(0);
    m_detailPanel->setMaximumHeight(QWIDGETSIZE_MAX);
    m_detailPanel->setVisible(shown);

    relayout();
    resize(width(), shown ? m_detailBase + m_detailDelta : m_detailBase);
    relayout();

    // Hand back the width the expanded layout forced on the window. This is the
    // only moment it can be done: while the panel is on screen its minimum width
    // is what holds the window open, so the window cannot be narrowed until the
    // panel is gone. Subtracting the measured overshoot (rather than restoring a
    // remembered absolute width) keeps any resize the user did *while* expanded.
    if (!shown && m_expandAddedWidth > 0) {
        resize(qMax(1, width() - m_expandAddedWidth), height());
        relayout();
        m_expandAddedWidth = 0;
    }

    if (m_dialTarget > 0) {
        const int drift = m_dashboard->height() - m_dialTarget;
        if (drift != 0)
            resize(width(), height() - drift);
    }

    // Measure what the expanded layout cost in width, after the layout has run —
    // this is what the next collapse gives back.
    if (shown)
        m_expandAddedWidth = qMax(0, width() - m_expandBaseWidth);
}

/// Land a transition that is still running, so the geometry measured by the next
/// toggle is a settled one. Toggling twice inside 180 ms is rare, but without
/// this the second toggle would measure a half-collapsed panel and drift.
void MainWindow::settleDetailTransition()
{
    if (m_detailAnim->state() == QAbstractAnimation::Stopped)
        return;

    m_detailAnim->stop();
    finishDetailAt(m_detailTargetShown);
}

void MainWindow::onDetailTransitionFinished()
{
    // Hiding the panel is the last step of the collapse: the freed layout
    // spacing goes back to the dial and the window stays where the slide left
    // it. Both happen in this one pass, so no frame is ever painted with the
    // panel gone but the dial still short.
    finishDetailAt(m_detailTargetShown);
}

void MainWindow::setDetailShown(bool sparkline, bool infoBar, bool adjustWindow, bool animate)
{
    settleDetailTransition();
    m_detailAnim->stop();
    m_detailPanel->setMinimumHeight(0);
    m_detailPanel->setMaximumHeight(QWIDGETSIZE_MAX);

    const int dialBefore = m_dashboard->height();
    const int panelBefore = m_detailPanel->isVisible() ? m_detailPanel->height() : 0;
    const bool wasShown = m_detailShown;
    const int widthBefore = width();

    m_showSparkline = sparkline;
    m_showInfoBar = infoBar;

    m_sparkline->setVisible(m_showSparkline);
    m_infoBar->setVisible(m_showInfoBar);

    const bool nowShown = m_showSparkline || m_showInfoBar;
    m_detailPanel->setVisible(nowShown);
    m_detailShown = nowShown;

    // A fresh hidden -> shown transition starts a new width measurement. Taken
    // before the panel is laid out, so it is the width the user was looking at.
    if (!wasShown && nowShown) {
        m_expandBaseWidth = widthBefore;
        m_expandAddedWidth = 0;
    }

    syncDetailControls();

    const bool canAdjust = adjustWindow && isVisible() && !isMaximized() && !isFullScreen();
    if (!canAdjust) {
        relayout();
        return;
    }

    // What the panel costs the window: its own height, plus the one layout
    // spacing that appears with it (a visible item adds a gap on each side but
    // replaces the gap that used to join its neighbours). Read from the live
    // panel rather than assumed, so a window narrow enough to wrap the parameter
    // grid onto more rows still gets the right number.
    const int panelHeight = nowShown ? m_detailPanel->sizeHint().height() : panelBefore;

    m_detailPanelHeight = qMax(0, panelHeight);
    m_detailDelta = qMax(0, panelHeight + m_root->spacing());
    m_detailBase = nowShown ? height() : height() - m_detailDelta;
    m_dialTarget = dialBefore;
    m_detailTargetShown = nowShown;

    if (!animate || m_detailDelta <= 0) {
        finishDetailAt(nowShown);
        return;
    }

    // The panel has to be on screen for the whole slide, and the first frame is
    // the state the window is already in. Nothing is painted until this
    // function returns, so the frame is never seen twice.
    m_detailPanel->setVisible(true);
    m_detailAnim->setStartValue(nowShown ? 0.0 : 1.0);
    m_detailAnim->setEndValue(nowShown ? 1.0 : 0.0);
    setDetailProgress(m_detailAnim->startValue().toReal());
    m_detailAnim->start();
}

void MainWindow::onDetailToggled(bool on)
{
    setDetailShown(on, on, true);
}

void MainWindow::onIntervalComboChanged(int index)
{
    if (index < 0)
        return;
    const int ms = m_intervalCombo->itemData(index).toInt();
    if (ms <= 0)
        return;

    m_infoBar->setSamplingIntervalMs(ms);
    if (m_haveSample)
        m_infoBar->setSample(m_lastSample);

    emit requestTicking(ms);
}

// ---------------------------------------------------------------------------
// devices / samples
// ---------------------------------------------------------------------------
int MainWindow::indexOfDevice(int deviceNumber) const
{
    for (int i = 0; i < m_deviceCombo->count(); ++i) {
        if (m_deviceCombo->itemData(i).toInt() == deviceNumber)
            return i;
    }
    return -1;
}

DriveDescriptor MainWindow::currentDescriptor() const
{
    const int index = m_deviceCombo->currentIndex();
    if (index < 0 || index >= m_devices.size())
        return DriveDescriptor();
    return m_devices.at(index);
}

void MainWindow::onDevicesReady(const QVector<DriveDescriptor>& devices)
{
    const int previous = m_deviceCombo->currentIndex() >= 0
                             ? m_deviceCombo->currentData().toInt()
                             : m_preferredDevice;

    m_devices = devices;
    if (m_anonymize)
        anonymizeDevices(m_devices);

    m_deviceCombo->blockSignals(true);
    m_deviceCombo->clear();
    // Feed the picker from `m_devices`, never from the raw `devices` argument:
    // under --anonymize the raw list still holds the real model names.
    for (const DriveDescriptor& descriptor : m_devices)
        m_deviceCombo->addItem(descriptor.displayName(), descriptor.deviceNumber);

    int index = indexOfDevice(previous);
    if (index < 0) {
        index = 0;
        // Prefer a rotating drive so the gauge shows something meaningful.
        for (int i = 0; i < devices.size(); ++i) {
            if (devices.at(i).isRotating()) {
                index = i;
                break;
            }
        }
    }
    m_deviceCombo->setCurrentIndex(index);
    m_deviceCombo->blockSignals(false);

    applyDescriptorToUi();

    if (index >= 0)
        emit requestActiveDevice(m_deviceCombo->itemData(index).toInt());

    emit requestTicking(currentIntervalMs());
}

void MainWindow::applyDescriptorToUi()
{
    const DriveDescriptor descriptor = currentDescriptor();
    m_dashboard->setDescriptor(descriptor);
    m_infoBar->setDescriptor(descriptor);
    m_infoBar->setSamplingIntervalMs(currentIntervalMs());
    m_sparkline->reset();
    m_dashboard->clearHistory();

    if (descriptor.deviceNumber < 0) {
        onStatusMessage(QStringLiteral("未选择磁盘"), true);
        return;
    }

    QString summary = QStringLiteral("%1 · %2 · %3")
                          .arg(descriptor.model.isEmpty() ? QStringLiteral("未知型号") : descriptor.model,
                               descriptor.sizeBytes > 0 ? wu::formatBytes(descriptor.sizeBytes)
                                                        : QStringLiteral("容量未知"),
                               descriptor.busName);
    if (!descriptor.accessNote.isEmpty())
        summary += QStringLiteral(" · %1").arg(descriptor.accessNote);
    onStatusMessage(summary, !descriptor.accessNote.isEmpty());
}

void MainWindow::onDeviceComboChanged(int index)
{
    if (index < 0)
        return;
    applyDescriptorToUi();
    emit requestActiveDevice(m_deviceCombo->itemData(index).toInt());
}

void MainWindow::onModeComboChanged(int index)
{
    m_dashboard->setMode(index == 1 ? RingMode::TransferRate : RingMode::EquivalentRpm);
    if (m_haveSample) {
        m_dashboard->setSample(m_lastSample);
        m_infoBar->setSample(m_lastSample);
    }
}

void MainWindow::onRefreshClicked()
{
    onStatusMessage(QStringLiteral("正在重新枚举物理磁盘…"), false);
    emit requestRefresh();
}

void MainWindow::onSampleReady(const DiskSample& sample)
{
    m_lastSample = sample;
    m_haveSample = sample.valid;

    m_dashboard->setSample(sample);
    m_infoBar->setSample(sample);
    if (sample.valid) {
        const qint64 stamp = sample.timestampMs > 0 ? sample.timestampMs
                                                    : QDateTime::currentMSecsSinceEpoch();
        m_sparkline->addSample(sample.busyPercent, stamp);
    }

    updateTray(sample);
}

void MainWindow::updateTray(const DiskSample& sample)
{
    if (!m_tray)
        return;
    const DriveDescriptor descriptor = currentDescriptor();
    m_tray->setToolTip(QStringLiteral("%1\n占用 %2%  读 %3  写 %4")
                           .arg(descriptor.model.isEmpty() ? QStringLiteral("物理磁盘") : descriptor.model)
                           .arg(sample.valid ? QString::number(sample.busyPercent, 'f', 0)
                                             : QStringLiteral("--"),
                                wu::formatRate(sample.readBps),
                                wu::formatRate(sample.writeBps)));
}

void MainWindow::onStatusMessage(const QString& text, bool warning)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setObjectName(warning ? QStringLiteral("StatusWarn") : QStringLiteral("Status"));
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void MainWindow::onTrayActivated(int reason)
{
    if (reason != int(QSystemTrayIcon::Trigger) && reason != int(QSystemTrayIcon::DoubleClick))
        return;
    setVisible(!isVisible());
    if (isVisible()) {
        raise();
        activateWindow();
    }
}

void MainWindow::showContextMenu(const QPoint& globalPosition)
{
    QMenu menu(this);
    menu.addAction(QStringLiteral("刷新磁盘列表"), this, &MainWindow::onRefreshClicked);

    QMenu* modeMenu = menu.addMenu(QStringLiteral("外圈显示"));
    modeMenu->addAction(QStringLiteral("等效负载转速"), this, [this]() { m_modeCombo->setCurrentIndex(0); });
    modeMenu->addAction(QStringLiteral("读写速率 (MB/s)"), this, [this]() { m_modeCombo->setCurrentIndex(1); });

    QMenu* rateMenu = menu.addMenu(QStringLiteral("采样间隔"));
    {
        QActionGroup* group = new QActionGroup(rateMenu);
        group->setExclusive(true);
        for (int i = 0; i < m_intervalCombo->count(); ++i) {
            QAction* action = rateMenu->addAction(m_intervalCombo->itemText(i));
            action->setCheckable(true);
            action->setChecked(i == m_intervalCombo->currentIndex());
            group->addAction(action);
            const int index = i;
            connect(action, &QAction::triggered, this, [this, index]() {
                m_intervalCombo->setCurrentIndex(index);
            });
        }
    }

    menu.addSeparator();
    QAction* detailsAction = menu.addAction(QStringLiteral("显示详细面板"));
    detailsAction->setCheckable(true);
    detailsAction->setChecked(detailVisible());
    connect(detailsAction, &QAction::toggled, m_detailButton, &QToolButton::setChecked);

    QAction* sparkAction = menu.addAction(QStringLiteral("　└ 曲线图"));
    sparkAction->setCheckable(true);
    sparkAction->setChecked(m_showSparkline);
    connect(sparkAction, &QAction::toggled, this, [this](bool on) {
        setDetailShown(on, m_showInfoBar, true);
    });

    QAction* infoAction = menu.addAction(QStringLiteral("　└ 参数栏"));
    infoAction->setCheckable(true);
    infoAction->setChecked(m_showInfoBar);
    connect(infoAction, &QAction::toggled, this, [this](bool on) {
        setDetailShown(m_showSparkline, on, true);
    });

    menu.addSeparator();
    QAction* pinAction = menu.addAction(QStringLiteral("窗口置顶"));
    pinAction->setCheckable(true);
    pinAction->setChecked(m_pinButton->isChecked());
    connect(pinAction, &QAction::toggled, m_pinButton, &QToolButton::setChecked);

    menu.addSeparator();
    menu.addAction(QStringLiteral("退出"), qApp, &QApplication::quit);

    menu.exec(globalPosition);
}
