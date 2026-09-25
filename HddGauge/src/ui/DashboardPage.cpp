#include "DashboardPage.h"
#include "RingGauge.h"
#include "UsageGauge.h"

#include <QContextMenuEvent>
#include <QResizeEvent>

DashboardPage::DashboardPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Dashboard"));
    m_ring = new RingGauge(this);
    m_usage = new UsageGauge(this);
    m_usage->raise();

    setMinimumSize(320, 320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DashboardPage::setMode(RingMode mode)
{
    m_ring->setMode(mode);
    if (m_hasSample)
        setSample(m_sample);
    update();
}

RingMode DashboardPage::mode() const
{
    return m_ring->mode();
}

void DashboardPage::setDescriptor(const DriveDescriptor& descriptor)
{
    m_descriptor = descriptor;
    m_ring->setMedia(descriptor.nominalRpm, descriptor.isRotating(), descriptor.rpmSource);
}

void DashboardPage::setSample(const DiskSample& sample)
{
    m_sample = sample;
    m_hasSample = true;

    m_usage->setBusy(sample.busyPercent);
    m_usage->setAvailable(sample.valid,
                          sample.valid ? QString() : QStringLiteral("等待采样"));

    if (!m_descriptor.isRotating()) {
        m_ring->setEquivalentRpm(0.0);
    } else {
        m_ring->setEquivalentRpm(sample.equivalentRpm(m_descriptor.nominalRpm));
    }
    m_ring->setTransfer(sample.readBps, sample.writeBps);
}

void DashboardPage::setUnavailable(const QString& reason)
{
    m_usage->setAvailable(false, reason);
    m_hasSample = false;
}

void DashboardPage::clearHistory()
{
    m_hasSample = false;
    m_usage->setValueImmediate(0.0);
    m_usage->setAvailable(false, QStringLiteral("切换中"));
    m_ring->setValueImmediate(0.0, 0.0);
}

void DashboardPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    const int side = qMin(width(), height());
    const QRect square(QPoint((width() - side) / 2, (height() - side) / 2), QSize(side, side));

    m_ring->setGeometry(square);

    const int inner = int(side * 0.74);
    m_usage->setGeometry(QRect(square.center() - QPoint(inner / 2, inner / 2), QSize(inner, inner)));
    m_usage->raise();
}

void DashboardPage::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuRequested(event->globalPos());
    event->accept();
}
