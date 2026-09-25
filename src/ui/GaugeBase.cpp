#include "GaugeBase.h"
#include "Theme.h"

#include <QConicalGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <QTimerEvent>

#include <cmath>

namespace {
const double kPi = 3.14159265358979323846;
const double kDeg2Rad = kPi / 180.0;
}

GaugeBase::GaugeBase(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);
    m_spring.setFrequency(1.7);
    m_spring2.setFrequency(1.7);
}

void GaugeBase::setRange(double minimum, double maximum)
{
    m_min = minimum;
    m_max = maximum;
    if (m_max <= m_min)
        m_max = m_min + 1.0;
    update();
}

void GaugeBase::setZones(const QVector<Zone>& zones)
{
    m_zones = zones;
    update();
}

void GaugeBase::setStartAngle(double degrees)
{
    m_startAngle = degrees;
    update();
}

void GaugeBase::setSpanAngle(double degrees)
{
    m_spanAngle = degrees;
    update();
}

void GaugeBase::setSpringFrequency(double hz)
{
    m_spring.setFrequency(hz);
    m_spring2.setFrequency(hz);
}

void GaugeBase::ensureAnimating()
{
    if (!m_animationTimer.isActive())
        m_animationTimer.start(16, this);
}

void GaugeBase::setValue(double value)
{
    m_spring.setTarget(qBound(m_min, value, m_max));
    ensureAnimating();
}

void GaugeBase::setValue2(double value)
{
    m_spring2.setTarget(qBound(m_min, value, m_max));
    ensureAnimating();
}

void GaugeBase::setValueImmediate(double value)
{
    m_spring.snapTo(qBound(m_min, value, m_max));
    update();
}

void GaugeBase::setValueImmediate(double value, double second)
{
    m_spring.snapTo(qBound(m_min, value, m_max));
    m_spring2.snapTo(qBound(m_min, second, m_max));
    update();
}

void GaugeBase::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == m_animationTimer.timerId()) {
        const bool moving1 = m_spring.step(m_step);
        const bool moving2 = m_spring2.step(m_step);
        update();
        emit animatedValueChanged(m_spring.value());
        if (!moving1 && !moving2)
            m_animationTimer.stop();
        return;
    }
    QWidget::timerEvent(event);
}

GaugeBase::Geom GaugeBase::geometryFor(const QRectF& area, double radiusScale) const
{
    Geom geometry;
    geometry.side = qMin(area.width(), area.height());
    geometry.center = area.center();
    geometry.radius = geometry.side * 0.5 * radiusScale;
    geometry.startDeg = m_startAngle;
    geometry.spanDeg = m_spanAngle;
    return geometry;
}

double GaugeBase::fractionFor(double value) const
{
    if (m_max <= m_min)
        return 0.0;
    return qBound(0.0, (value - m_min) / (m_max - m_min), 1.0);
}

double GaugeBase::angleForFraction(double fraction, const Geom& geometry) const
{
    return geometry.startDeg + geometry.spanDeg * fraction;
}

QPointF GaugeBase::pointAt(const Geom& geometry, double radius, double degrees) const
{
    const double radians = degrees * kDeg2Rad;
    return QPointF(geometry.center.x() + radius * std::cos(radians),
                   geometry.center.y() - radius * std::sin(radians));
}

void GaugeBase::paintArc(QPainter& painter, const Geom& geometry, double radius, double thickness,
                         double fromValue, double toValue, const QColor& color, Qt::PenCapStyle cap) const
{
    if (thickness <= 0.0 || radius <= 0.0)
        return;

    const double f0 = fractionFor(fromValue);
    const double f1 = fractionFor(toValue);
    if (std::fabs(f1 - f0) < 1e-6)
        return;

    const double a0 = angleForFraction(f0, geometry);
    const double a1 = angleForFraction(f1, geometry);

    QPen pen(color, thickness, Qt::SolidLine, cap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QRectF box(geometry.center.x() - radius, geometry.center.y() - radius,
                     radius * 2.0, radius * 2.0);
    painter.drawArc(box, int(qRound(a0 * 16.0)), int(qRound((a1 - a0) * 16.0)));
}

void GaugeBase::paintTicks(QPainter& painter, const Geom& geometry, double outerRadius,
                           double majorLength, double minorLength, int divisions, int labelEvery,
                           const QColor& tickColor, const QColor& labelColor,
                           const std::function<QString(double)>& formatter,
                           double labelRadius, const QFont& labelFont) const
{
    if (divisions <= 0)
        return;

    painter.save();
    painter.setFont(labelFont);

    for (int i = 0; i <= divisions; ++i) {
        const double fraction = double(i) / double(divisions);
        const double value = m_min + (m_max - m_min) * fraction;
        const double angle = angleForFraction(fraction, geometry);
        const bool major = (labelEvery > 0) && (i % labelEvery == 0);
        const double length = major ? majorLength : minorLength;

        QPen pen(major ? tickColor.lighter(140) : tickColor, major ? 1.8 : 1.0,
                 Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(pointAt(geometry, outerRadius, angle),
                         pointAt(geometry, outerRadius - length, angle));

        if (major && formatter) {
            const QString text = formatter(value);
            if (!text.isEmpty()) {
                painter.setPen(labelColor);
                const QPointF anchor = pointAt(geometry, labelRadius, angle);
                QRectF box(anchor.x() - 60.0, anchor.y() - 12.0, 120.0, 24.0);
                painter.drawText(box, Qt::AlignCenter, text);
            }
        }
    }

    painter.restore();
}

void GaugeBase::paintNeedle(QPainter& painter, const Geom& geometry, double value, double length,
                            double width, const QColor& color, double tail) const
{
    const double angle = angleForFraction(fractionFor(value), geometry);
    const double radians = angle * kDeg2Rad;
    const QPointF direction(std::cos(radians), -std::sin(radians));
    const QPointF perpendicular(-direction.y(), direction.x());

    const QPointF tip = geometry.center + direction * length;
    const QPointF base = geometry.center - direction * tail;

    QPolygonF needle;
    needle << tip
           << (geometry.center + direction * (length * 0.42) + perpendicular * (width * 0.5))
           << (base + perpendicular * (width * 0.22))
           << (base - perpendicular * (width * 0.22))
           << (geometry.center + direction * (length * 0.42) - perpendicular * (width * 0.5));

    QLinearGradient gradient(base, tip);
    gradient.setColorAt(0.0, color.darker(160));
    gradient.setColorAt(0.55, color);
    gradient.setColorAt(1.0, color.lighter(135));

    painter.save();
    painter.setPen(QPen(color.lighter(150), 1.0));
    painter.setBrush(gradient);
    painter.drawPolygon(needle);

    // counterweight
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawEllipse(base, width * 0.42, width * 0.42);
    painter.restore();
}

void GaugeBase::paintRingMarker(QPainter& painter, const Geom& geometry, double radius,
                                double value, double width, double reach, const QColor& color) const
{
    const double angle = angleForFraction(fractionFor(value), geometry);
    const double radians = angle * kDeg2Rad;
    const QPointF direction(std::cos(radians), -std::sin(radians));
    const QPointF perpendicular(-direction.y(), direction.x());

    const QPointF inner = geometry.center + direction * (radius - reach * 0.5);
    const QPointF outer = geometry.center + direction * (radius + reach * 0.5);

    painter.save();
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(inner, outer);

    painter.setPen(Qt::NoPen);
    QRadialGradient glow(outer, reach * 1.6);
    glow.setColorAt(0.0, QColor(color.red(), color.green(), color.blue(), 170));
    glow.setColorAt(1.0, QColor(color.red(), color.green(), color.blue(), 0));
    painter.setBrush(glow);
    painter.drawEllipse(outer, reach * 1.6, reach * 1.6);

    painter.setBrush(color.lighter(150));
    painter.drawEllipse(outer, reach * 0.32, reach * 0.32);
    painter.restore();
}

void GaugeBase::paintHub(QPainter& painter, const Geom& geometry, double radius,
                         const QColor& ringColor, const QColor& fillColor) const
{
    painter.save();
    painter.setPen(Qt::NoPen);

    QRadialGradient gradient(geometry.center, radius);
    gradient.setColorAt(0.0, fillColor.lighter(140));
    gradient.setColorAt(1.0, fillColor);
    painter.setBrush(gradient);
    painter.drawEllipse(geometry.center, radius, radius);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(ringColor, qMax(1.0, radius * 0.22)));
    painter.drawEllipse(geometry.center, radius * 0.82, radius * 0.82);
    painter.restore();
}

void GaugeBase::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const Geom geometry = geometryFor(QRectF(rect()));
    paintGauge(painter, geometry);
}
