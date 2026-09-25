#include "DashboardPage.h"
#include "CornerReadout.h"
#include "RingGauge.h"
#include "Theme.h"
#include "UsageGauge.h"

#include "../core/WinUtil.h"

#include <QContextMenuEvent>
#include <QResizeEvent>

#include <cmath>

namespace {

/// Distance from the top edge of the dashboard to the corner readouts.
const int kCornerTop = 4;
/// Horizontal inset of the corner readouts from the window edge.
const int kCornerMargin = 10;

} // namespace

DashboardPage::DashboardPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Dashboard"));
    m_ring = new RingGauge(this);
    m_usage = new UsageGauge(this);

    m_cornerLeft = new CornerReadout(this);
    m_cornerRight = new CornerReadout(this);
    m_cornerLeft->setObjectName(QStringLiteral("CornerLeft"));
    m_cornerRight->setObjectName(QStringLiteral("CornerRight"));

    m_usage->raise();

    setMinimumSize(320, 320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    refreshReadouts();
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

    refreshReadouts();
}

void DashboardPage::setUnavailable(const QString& reason)
{
    m_usage->setAvailable(false, reason);
    m_hasSample = false;
    refreshReadouts();
}

void DashboardPage::clearHistory()
{
    m_hasSample = false;
    m_usage->setValueImmediate(0.0);
    m_usage->setAvailable(false, QStringLiteral("切换中"));
    m_ring->setValueImmediate(0.0, 0.0);
    refreshReadouts();
}

/// Feed the corner readouts from the newest sample.
///
/// A missing number is shown as `--`, never as a stale or invented one: the two
/// corners are the numbers a glance lands on first, so they have to be as
/// honest as the dial itself.
void DashboardPage::refreshReadouts()
{
    const bool live = m_hasSample && m_sample.valid;

    const QString readText = live ? wu::formatRate(m_sample.readBps) : QStringLiteral("--");
    const QString writeText = live ? wu::formatRate(m_sample.writeBps) : QStringLiteral("--");
    // Same convention as the parameter grid: "--" means there is no sample at
    // all, "—" means this interval carried no measurable transfer.
    QString latencyText;
    if (!live)
        latencyText = QStringLiteral("--");
    else if (m_sample.latencySec > 0.0)
        latencyText = QStringLiteral("%1 ms").arg(m_sample.latencySec * 1000.0, 0, 'f', 2);
    else
        latencyText = QStringLiteral("—");

    QVector<CornerReadout::Row> left;
    CornerReadout::Row readRow;
    readRow.caption = QStringLiteral("读取");
    readRow.value = readText;
    readRow.color = Theme::accent();
    left.append(readRow);

    CornerReadout::Row writeRow;
    writeRow.caption = QStringLiteral("写入");
    writeRow.value = writeText;
    writeRow.color = Theme::amber();
    left.append(writeRow);

    m_cornerLeft->setRows(left);

    QVector<CornerReadout::Row> right;
    CornerReadout::Row latencyRow;
    latencyRow.caption = QStringLiteral("响应时间");
    latencyRow.value = latencyText;
    latencyRow.color = Theme::text();
    right.append(latencyRow);

    m_cornerRight->setRows(right);

    // A wider value ("0.0 KB/s" -> "123.4 MB/s") changes the size hint, so the
    // boxes have to be re-placed now rather than at the next resize.
    layoutReadouts();
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

    layoutReadouts();
}

/// Park the two readouts in the corners the inscribed dial leaves free.
///
/// The dial is a circle inside a square, so its corners are empty — but only
/// while the circle is small enough relative to the widget. `kKeepOut` sits a
/// little outside the ring's own outer edge (0.905 * radius plus its glow), and
/// a readout whose box comes closer than that is hidden rather than allowed to
/// collide with the dial. Squeezing the window right down therefore drops the
/// readouts instead of printing numbers on top of the gauge.
void DashboardPage::layoutReadouts()
{
    const int side = qMin(width(), height());
    const QRect square(QPoint((width() - side) / 2, (height() - side) / 2), QSize(side, side));
    const QPointF centre(square.center());
    const qreal keepOut = side * 0.48;

    CornerReadout* const readouts[] = { m_cornerLeft, m_cornerRight };
    for (int i = 0; i < 2; ++i) {
        CornerReadout* label = readouts[i];
        const QSize hint = label->sizeHint();
        const int x = i == 0 ? kCornerMargin : width() - kCornerMargin - hint.width();
        const QRect box(x, kCornerTop, hint.width(), hint.height());

        label->setGeometry(box);
        label->raise();

        const qreal dx = qMax(qMax(centre.x() - box.right(), qreal(box.left()) - centre.x()), 0.0);
        const qreal dy = qMax(qMax(centre.y() - box.bottom(), qreal(box.top()) - centre.y()), 0.0);
        label->setVisible(std::sqrt(dx * dx + dy * dy) >= keepOut);
    }
}

void DashboardPage::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuRequested(event->globalPos());
    event->accept();
}
