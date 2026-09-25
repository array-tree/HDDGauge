#pragma once

#include "../core/DiskTypes.h"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QPropertyAnimation;
class QThread;
class QToolButton;
class QVBoxLayout;
class QSystemTrayIcon;

class DashboardPage;
class InfoBar;
class MonitorWorker;
class Sparkline;

class MainWindow : public QWidget
{
    Q_OBJECT

    /// Position of the collapsible detail panel, 0 = fully collapsed and
    /// 1 = fully expanded. Driven by a QPropertyAnimation so that toggling the
    /// panel slides the window edge instead of jumping it; the setter also
    /// resizes the window, which is what keeps the dial the same size for every
    /// value in between.
    Q_PROPERTY(qreal detailProgress READ detailProgress WRITE setDetailProgress)

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    qreal detailProgress() const { return m_detailProgress; }
    void  setDetailProgress(qreal progress);

    /// Used by `--screenshot --ring-mode 1` to capture the transfer layout.
    void setRingModeForTesting(RingMode mode);

    /// Used by `--screenshot --device N` to capture a specific drive.
    void setPreferredDeviceForTesting(int deviceNumber);

    /// `--screenshot --compact`: render with the detail panel collapsed.
    void setDetailVisibleForTesting(bool sparkline, bool infoBar);

    /// `--screenshot --interval <ms>`: render at a given sampling rate.
    void setIntervalForTesting(int intervalMs);

signals:
    void requestActiveDevice(int deviceNumber);
    void requestTicking(int intervalMs);
    void requestStopTicking();
    void requestRefresh();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onDevicesReady(const QVector<DriveDescriptor>& devices);
    void onSampleReady(const DiskSample& sample);
    void onStatusMessage(const QString& text, bool warning);
    void onDeviceComboChanged(int index);
    void onModeComboChanged(int index);
    void onIntervalComboChanged(int index);
    void onDetailToggled(bool on);
    void onRefreshClicked();
    void onTrayActivated(int reason);
    void showContextMenu(const QPoint& globalPosition);
    void onDetailTransitionFinished();

private:
    void buildUi();
    void wireWorker();
    void applyAlwaysOnTop(bool on);
    bool loadSettings();
    void saveSettings();
    void applyDescriptorToUi();
    void updateTray(const DiskSample& sample);
    DriveDescriptor currentDescriptor() const;
    int indexOfDevice(int deviceNumber) const;

    /// Show/hide the two detail widgets. `adjustWindow` makes the window grow
    /// or shrink by exactly the height the panel gains or loses, so collapsing
    /// the details gives a compact window with an unchanged dial. `animate`
    /// slides that height change over ~180 ms; the head-less / screenshot path
    /// turns it off so a capture can never land mid-transition.
    void setDetailShown(bool sparkline, bool infoBar, bool adjustWindow, bool animate = true);
    void settleDetailTransition();
    void finishDetailAt(bool shown);
    void relayout();
    void syncDetailControls();
    bool detailVisible() const { return m_showSparkline || m_showInfoBar; }
    int  currentIntervalMs() const;

    QThread*         m_thread = nullptr;
    MonitorWorker*   m_worker = nullptr;

    QVBoxLayout*     m_root = nullptr;
    DashboardPage*   m_dashboard = nullptr;
    QWidget*         m_detailPanel = nullptr;
    Sparkline*       m_sparkline = nullptr;
    InfoBar*         m_infoBar = nullptr;

    QComboBox*       m_deviceCombo = nullptr;
    QComboBox*       m_modeCombo = nullptr;
    QComboBox*       m_intervalCombo = nullptr;
    QToolButton*     m_detailButton = nullptr;
    QToolButton*     m_pinButton = nullptr;
    QToolButton*     m_refreshButton = nullptr;
    QLabel*          m_statusLabel = nullptr;
    QSystemTrayIcon* m_tray = nullptr;

    QVector<DriveDescriptor> m_devices;
    DiskSample               m_lastSample;
    bool                     m_haveSample = false;
    bool                     m_shuttingDown = false;
    bool                     m_showSparkline = true;
    bool                     m_showInfoBar = true;
    bool                     m_detailShown = true;
    int                      m_preferredDevice = -1;
    bool                     m_trayNoticeShown = false;

    // Detail panel transition. `m_detailBase` is the window height with the
    // panel fully collapsed, `m_detailDelta` the height it adds when expanded
    // (both measured from the live layout, never assumed), and
    // `m_detailPanelHeight` the panel's own height at full expansion.
    QPropertyAnimation*      m_detailAnim = nullptr;
    qreal                    m_detailProgress = 1.0;
    int                      m_detailBase = 0;
    int                      m_detailDelta = 0;
    int                      m_detailPanelHeight = 0;
    bool                     m_detailTargetShown = true;

    /// Dial height to restore when a transition lands; -1 before the first
    /// measured toggle.
    int                      m_dialTarget = -1;
};
