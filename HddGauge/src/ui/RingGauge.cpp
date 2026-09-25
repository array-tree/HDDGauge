#include "RingGauge.h"
#include "Theme.h"

#include <QPainter>
#include <QPen>
#include <QRadialGradient>

#include <cmath>

namespace {

/// Round up to the next 1/2/5 x 10^n so the auto-ranged transfer scale only
/// changes in visible steps instead of jittering on every sample.
double niceCeiling(double value)
{
    if (value <= 1.0)
        return 1.0;
    const double exponent = std::floor(std::log10(value));
    const double base = std::pow(10.0, exponent);
    const double mantissa = value / base;

    double step;
    if (mantissa <= 1.0)      step = 1.0;
    else if (mantissa <= 2.0) step = 2.0;
    else if (mantissa <= 5.0) step = 5.0;
    else                      step = 10.0;

    return step * base;
}

/// 5400 -> 6000, 7200 -> 8000, 15000 -> 15000 so the labels land on integers.
double rpmScaleTop(int nominalRpm)
{
    const double nominal = nominalRpm > 0 ? double(nominalRpm) : 7200.0;
    double top = std::ceil(nominal / 1000.0) * 1000.0;
    if (top < 2000.0)
        top = 2000.0;
    return top;
}

/// Largest step from a short list that yields 4..6 evenly spaced labels.
double rpmScaleStep(double top)
{
    static const double kSteps[] = { 250.0, 500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0, 5000.0 };
    for (double step : kSteps) {
        const double divisions = top / step;
        if (divisions >= 4.0 && divisions <= 6.0 &&
            std::fabs(divisions - std::floor(divisions + 0.5)) < 1e-9) {
            return step;
        }
    }
    return 1000.0;
}

QString formatRpmTick(double value)
{
    if (value < 0.5)
        return QStringLiteral("0");
    if (value >= 1000.0) {
        const double thousands = value / 1000.0;
        return (thousands == std::floor(thousands))
                   ? QStringLiteral("%1k").arg(int(thousands))
                   : QStringLiteral("%1k").arg(thousands, 0, 'g', 3);
    }
    return QString::number(int(value + 0.5));
}

} // namespace

RingGauge::RingGauge(QWidget* parent)
    : GaugeBase(parent)
{
    setSpringFrequency(2.2);
    rebuildScale();
}

void RingGauge::setMode(RingMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    rebuildScale();
    setValueImmediate(0.0, 0.0);
    update();
}

void RingGauge::setMedia(int nominalRpm, bool rotating, const QString& rpmSource)
{
    m_nominalRpm = nominalRpm;
    m_rotating = rotating;
    m_rpmSource = rpmSource;
    rebuildScale();
    update();
}

void RingGauge::rebuildScale()
{
    if (m_mode == RingMode::EquivalentRpm)
        setRange(0.0, rpmScaleTop(m_nominalRpm));
    else
        setRange(0.0, m_transferCeiling);
}

QString RingGauge::scaleUnit() const
{
    return m_mode == RingMode::EquivalentRpm ? QStringLiteral("RPM") : QStringLiteral("MB/s");
}

void RingGauge::setEquivalentRpm(double rpm)
{
    if (m_mode != RingMode::EquivalentRpm)
        return;
    setValue(qMax(0.0, rpm));
}

void RingGauge::setTransfer(double readBps, double writeBps)
{
    m_readBps = readBps;
    m_writeBps = writeBps;
    if (m_mode != RingMode::TransferRate)
        return;

    const double readMb = readBps / 1048576.0;
    const double writeMb = writeBps / 1048576.0;
    const double peak = qMax(readMb, writeMb);

    if (peak > m_transferCeiling)
        m_transferCeiling = peak * 1.08;
    else
        m_transferCeiling = qMax(20.0, m_transferCeiling * 0.994);

    const double nice = niceCeiling(m_transferCeiling);
    if (std::fabs(nice - m_max) > 1e-6) {
        m_transferCeiling = nice;
        setRange(0.0, nice);
    }

    setValue(readMb);
    setValue2(writeMb);
}

void RingGauge::paintGauge(QPainter& painter, const Geom& geometry)
{
    const double radius = geometry.radius;
    if (radius < 40.0)
        return;

    const double ringRadius = radius * 0.905;
    const QColor trackColor = Theme::trackStrong();

    // A spindle dial is meaningless on non-rotating media, so in that case the
    // ring is drawn as a broken, muted circle instead of a live scale.
    const bool rpmIdle = (m_mode == RingMode::EquivalentRpm) && !m_rotating;

    if (m_mode == RingMode::EquivalentRpm) {
        const double thickness = radius * 0.030;

        if (rpmIdle) {
            QPen pen(trackColor, thickness, Qt::CustomDashLine);
            QVector<qreal> pattern;
            pattern << 1.2 << 2.4;
            pen.setDashPattern(pattern);
            pen.setCapStyle(Qt::FlatCap);
            painter.save();
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            const QRectF box(geometry.center.x() - ringRadius, geometry.center.y() - ringRadius,
                             ringRadius * 2.0, ringRadius * 2.0);
            painter.drawArc(box, int(qRound(geometry.startDeg * 16.0)),
                            int(qRound(geometry.spanDeg * 16.0)));
            painter.restore();

            painter.save();
            painter.setPen(Theme::textFaint());
            painter.setFont(Theme::uiFont(qMax(8, int(radius * 0.070))));
            painter.drawText(QRectF(geometry.center.x() - radius * 0.35,
                                    geometry.center.y() + radius * 0.690,
                                    radius * 0.70, radius * 0.16),
                             Qt::AlignCenter, QStringLiteral("非旋转介质"));
            painter.restore();
        } else {
            paintArc(painter, geometry, ringRadius, thickness, m_min, m_max, Theme::track());

            const double current = value();
            QColor glow = Theme::accent();
            glow.setAlpha(65);
            paintArc(painter, geometry, ringRadius, thickness * 2.4, m_min, current, glow, Qt::RoundCap);
            paintArc(painter, geometry, ringRadius, thickness, m_min, current, Theme::accent().darker(165));
            paintArc(painter, geometry, ringRadius, thickness * 0.48, m_min, current, Theme::accent());
            paintRingMarker(painter, geometry, ringRadius, current, radius * 0.011,
                            thickness * 2.8, Theme::accent());
        }
    } else {
        const double bandThickness = radius * 0.022;
        const double readRadius = ringRadius + radius * 0.0180;
        const double writeRadius = ringRadius - radius * 0.0180;

        paintArc(painter, geometry, readRadius, bandThickness, m_min, m_max, Theme::track());
        paintArc(painter, geometry, writeRadius, bandThickness, m_min, m_max, Theme::track());

        paintArc(painter, geometry, readRadius, bandThickness, m_min, value(), Theme::accent());
        paintArc(painter, geometry, writeRadius, bandThickness, m_min, value2(), Theme::amber());

        paintRingMarker(painter, geometry, readRadius, value(), radius * 0.0095,
                        bandThickness * 2.6, Theme::accent());
        paintRingMarker(painter, geometry, writeRadius, value2(), radius * 0.0095,
                        bandThickness * 2.6, Theme::amber());
    }

    // ---- ticks + labels ---------------------------------------------------
    if (!rpmIdle) {
        const double tickOuter = ringRadius - radius * 0.038;
        const QFont tickFont = Theme::numberFont(qMax(7, int(radius * 0.066)));

        if (m_mode == RingMode::EquivalentRpm) {
            const double step = rpmScaleStep(m_max);
            const int divisions = qMax(2, int(m_max / step + 0.5));
            paintTicks(painter, geometry, tickOuter, radius * 0.030, radius * 0.015,
                       divisions, 1, trackColor.lighter(155), Theme::textFaint(),
                       formatRpmTick, radius * 0.792, tickFont);
        } else {
            paintTicks(painter, geometry, tickOuter, radius * 0.030, radius * 0.015,
                       8, 2, trackColor.lighter(155), Theme::textFaint(),
                       [](double v) {
                           return v < 0.5 ? QStringLiteral("0")
                                          : QString::number(v, 'f', v < 10.0 ? 1 : 0);
                       },
                       radius * 0.792, tickFont);
        }
    }

    // ---- bottom-gap legend -------------------------------------------------
    painter.save();

    auto legend = [&](double xOffset, const QColor& colour, const QString& text) {
        const QPointF dot(geometry.center.x() + xOffset - radius * 0.088,
                          geometry.center.y() + radius * 0.720);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        painter.drawEllipse(dot, radius * 0.018, radius * 0.018);
        painter.setPen(Theme::textMuted());
        painter.setFont(Theme::uiFont(qMax(8, int(radius * 0.064))));
        painter.drawText(QRectF(dot.x() + radius * 0.032, dot.y() - radius * 0.05,
                                radius * 0.24, radius * 0.10),
                         Qt::AlignLeft | Qt::AlignVCenter, text);
    };

    if (m_mode == RingMode::TransferRate) {
        legend(-radius * 0.132, Theme::accent(), QStringLiteral("读"));
        legend(radius * 0.168, Theme::amber(), QStringLiteral("写"));
    } else if (!rpmIdle) {
        // The provenance of the nominal speed lives in the info bar; the dial
        // itself only needs to say what the needle means.
        painter.setPen(Theme::textFaint());
        painter.setFont(Theme::uiFont(qMax(8, int(radius * 0.062))));
        painter.drawText(QRectF(geometry.center.x() - radius * 0.40,
                                geometry.center.y() + radius * 0.700,
                                radius * 0.80, radius * 0.12),
                         Qt::AlignCenter, QStringLiteral("等效负载转速"));
    }

    painter.restore();
}
