#pragma once

#include "../core/SpringValue.h"

#include <QBasicTimer>
#include <QColor>
#include <QFont>
#include <QVector>
#include <QWidget>

#include <functional>

class QPainter;
class QTimerEvent;

/// Shared machinery for every dial: spring-animated values plus the arc / tick /
/// needle primitives. Subclasses only implement paintGauge().
///
/// Angles follow Qt's convention: 0 deg = 3 o'clock, positive = counter
/// clockwise. The default 225 deg -> -270 deg sweep therefore leaves the
/// classic 90 deg gap at the bottom of the dial.
class GaugeBase : public QWidget
{
    Q_OBJECT

public:
    struct Zone
    {
        Zone() = default;
        Zone(double start, double end, const QColor& colour)
            : from(start), to(end), color(colour) {}

        double from = 0.0;
        double to = 0.0;
        QColor color;
    };

    struct Geom
    {
        QPointF center;
        double  radius = 0.0;   // half of the shorter side
        double  side = 0.0;
        double  startDeg = 225.0;
        double  spanDeg = -270.0;
    };

    explicit GaugeBase(QWidget* parent = nullptr);

    void setRange(double minimum, double maximum);
    double rangeMin() const { return m_min; }
    double rangeMax() const { return m_max; }

    void setZones(const QVector<Zone>& zones);
    void setStartAngle(double degrees);
    void setSpanAngle(double degrees);
    void setSpringFrequency(double hz);

    double value() const { return m_spring.value(); }
    double value2() const { return m_spring2.value(); }
    double target() const { return m_spring.target(); }

    /// Seconds of needle travel; used to slow the ring down a touch.
    void setAnimationStep(double seconds) { m_step = seconds; }

public slots:
    void setValue(double value);
    void setValue2(double value);
    void setValueImmediate(double value);
    void setValueImmediate(double value, double value2);

signals:
    void animatedValueChanged(double value);

protected:
    Geom geometryFor(const QRectF& area, double radiusScale = 1.0) const;
    double fractionFor(double value) const;
    double angleForFraction(double fraction, const Geom& geometry) const;
    QPointF pointAt(const Geom& geometry, double radius, double degrees) const;

    // ---- painting primitives -------------------------------------------
    void paintArc(QPainter& painter, const Geom& geometry, double radius, double thickness,
                  double fromValue, double toValue, const QColor& color,
                  Qt::PenCapStyle cap = Qt::FlatCap) const;

    /// Radial tick marks with optional numeric labels.
    void paintTicks(QPainter& painter, const Geom& geometry, double outerRadius,
                    double majorLength, double minorLength, int divisions, int labelEvery,
                    const QColor& tickColor, const QColor& labelColor,
                    const std::function<QString(double)>& formatter,
                    double labelRadius, const QFont& labelFont) const;

    void paintNeedle(QPainter& painter, const Geom& geometry, double value, double length,
                     double width, const QColor& color, double tail = 0.0) const;

    /// Marker drawn on top of a thin ring band (used by the outer ring gauge).
    void paintRingMarker(QPainter& painter, const Geom& geometry, double radius,
                         double value, double width, double reach, const QColor& color) const;

    void paintHub(QPainter& painter, const Geom& geometry, double radius,
                  const QColor& ringColor, const QColor& fillColor) const;

    void paintEvent(QPaintEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

    virtual void paintGauge(QPainter& painter, const Geom& geometry) = 0;

    double m_min = 0.0;
    double m_max = 100.0;
    double m_startAngle = 225.0;
    double m_spanAngle = -270.0;

    QVector<Zone> m_zones;
    SpringValue   m_spring;
    SpringValue   m_spring2;

private:
    void ensureAnimating();

    QBasicTimer m_animationTimer;
    double      m_step = 0.016;
};
