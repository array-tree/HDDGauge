#pragma once

#include "../core/DiskTypes.h"

#include <QWidget>

class RingGauge;
class UsageGauge;

/// Composes the concentric dial: an outer ring gauge plus the main usage dial.
///
/// The two widgets are positioned manually in resizeEvent so that they stay
/// perfectly concentric regardless of the window aspect ratio (a layout would
/// not guarantee that).
class DashboardPage : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardPage(QWidget* parent = nullptr);

    void setMode(RingMode mode);
    RingMode mode() const;

    void setDescriptor(const DriveDescriptor& descriptor);
    void setSample(const DiskSample& sample);
    void setUnavailable(const QString& reason);
    void clearHistory();

    RingGauge* ring() const { return m_ring; }
    UsageGauge* usage() const { return m_usage; }

signals:
    void contextMenuRequested(const QPoint& globalPos);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    RingGauge*      m_ring = nullptr;
    UsageGauge*     m_usage = nullptr;
    DriveDescriptor m_descriptor;
    DiskSample      m_sample;
    bool            m_hasSample = false;
};
