#include "Sparkline.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

Sparkline::Sparkline(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(72);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Sparkline::setWindowSeconds(int seconds)
{
    m_windowMs = qBound(5, seconds, 3600) * 1000;
    prune();
    update();
}

void Sparkline::prune()
{
    if (m_points.isEmpty())
        return;
    const qint64 newest = m_points.last().ms;
    const qint64 oldest = newest - m_windowMs;

    int drop = 0;
    while (drop < m_points.size() && m_points.at(drop).ms < oldest)
        ++drop;
    if (drop > 0)
        m_points.remove(0, drop);

    if (m_points.size() > kMaxPoints)
        m_points.remove(0, m_points.size() - kMaxPoints);
}

void Sparkline::addSample(double percent, qint64 timestampMs)
{
    const Point p(timestampMs, qBound(0.0, percent, 100.0));
    if (!m_points.isEmpty() && timestampMs <= m_points.last().ms)
        m_points.last() = p;      // same tick, or a clock that went backwards
    else
        m_points.append(p);

    prune();
    update();
}

void Sparkline::reset()
{
    m_points.clear();
    update();
}

void Sparkline::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.setBrush(Theme::panelAlt());
    painter.drawRoundedRect(box, 9.0, 9.0);

    const QRectF plot = box.adjusted(10.0, 20.0, -10.0, -8.0);
    if (plot.width() < 20.0 || plot.height() < 10.0)
        return;

    double peak = 0.0;
    for (const Point& p : m_points)
        peak = qMax(peak, p.value);

    // Auto-range. An idle disk sitting at 0.4 % would otherwise be a flat line
    // glued to the baseline and tell the user nothing; the axis maximum is
    // printed in the header so the scale is never a secret.
    double axisMax = qMax(10.0, std::ceil(peak / 5.0) * 5.0);
    axisMax = qMin(100.0, axisMax);

    // Time axis. The span grows with the buffer until it reaches the window,
    // so the chart uses the full width from the first seconds instead of
    // parking a 10 px sliver against the right edge for a whole minute. The
    // header always states the span actually drawn, so the compression is
    // never hidden from the reader.
    qint64 span = m_windowMs;
    if (m_points.size() >= 2) {
        const qint64 covered = m_points.last().ms - m_points.first().ms;
        span = qBound<qint64>(1000, covered, qint64(m_windowMs));
    }

    painter.setFont(Theme::uiFont(10));
    painter.setPen(Theme::textFaint());
    const QRectF header(box.left() + 10.0, box.top() + 4.0, box.width() - 20.0, 14.0);
    painter.drawText(header, Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("占用率 · 最近 %1 秒").arg(int((span + 999) / 1000)));
    painter.drawText(header, Qt::AlignRight | Qt::AlignVCenter,
                     m_points.isEmpty()
                         ? QStringLiteral("--")
                         : QStringLiteral("峰值 %1% · 量程 %2%")
                               .arg(peak, 0, 'f', peak < 10.0 ? 1 : 0)
                               .arg(axisMax, 0, 'f', 0));

    // grid at 25 / 50 / 75 % of the current axis maximum
    painter.setPen(QPen(QColor(0x25, 0x30, 0x3D), 1.0, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const double y = plot.bottom() - plot.height() * i / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    if (m_points.isEmpty())
        return;

    const qint64 newest = m_points.last().ms;
    const double scaleX = plot.width() / double(span);

    if (m_points.size() >= 2) {
        QPainterPath line;
        QPainterPath fill;
        bool started = false;

        for (const Point& p : m_points) {
            const double x = plot.right() - double(newest - p.ms) * scaleX;
            const double y = plot.bottom() - plot.height() * qBound(0.0, p.value / axisMax, 1.0);
            if (!started) {
                line.moveTo(x, y);
                fill.moveTo(x, plot.bottom());
                fill.lineTo(x, y);
                started = true;
            } else {
                line.lineTo(x, y);
                fill.lineTo(x, y);
            }
        }
        fill.lineTo(plot.right(), plot.bottom());
        fill.closeSubpath();

        QLinearGradient gradient(plot.topLeft(), plot.bottomLeft());
        gradient.setColorAt(0.0, QColor(0x22, 0xD3, 0xEE, 120));
        gradient.setColorAt(1.0, QColor(0x22, 0xD3, 0xEE, 8));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawPath(fill);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Theme::accent(), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(line);
    }

    // current sample marker
    const double lastValue = m_points.last().value;
    const QPointF last(plot.right(),
                       plot.bottom() - plot.height() * qBound(0.0, lastValue / axisMax, 1.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::usageColor(lastValue));
    painter.drawEllipse(last, 3.0, 3.0);
}
