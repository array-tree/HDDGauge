#include "UsageGauge.h"
#include "Theme.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRadialGradient>

namespace {

/// The needle pivots at the centre and, at 0 % / 100 %, reaches this far down.
/// The digital readout is deliberately parked below that reach so the two can
/// never collide.
const double kNeedleLength = 0.585;
const double kNeedleTail   = 0.115;

} // namespace

UsageGauge::UsageGauge(QWidget* parent)
    : GaugeBase(parent)
{
    setRange(0.0, 100.0);

    QVector<Zone> zones;
    zones.append(Zone(0.0, 60.0, Theme::green()));
    zones.append(Zone(60.0, 85.0, Theme::amber()));
    zones.append(Zone(85.0, 100.0, Theme::red()));
    setZones(zones);

    setSpringFrequency(1.8);
    m_caption = QStringLiteral("磁盘占用率");
}

void UsageGauge::setBusy(double percent)
{
    setValue(percent);
}

void UsageGauge::setAvailable(bool available, const QString& reason)
{
    m_available = available;
    m_reason = reason;
    update();
}

void UsageGauge::setCaption(const QString& caption)
{
    m_caption = caption;
    update();
}

void UsageGauge::paintGauge(QPainter& painter, const Geom& geometry)
{
    const double radius = geometry.radius;
    if (radius < 24.0)
        return;

    const double arcRadius    = radius * 0.855;
    const double arcThickness = radius * 0.135;
    const double current      = m_available ? value() : 0.0;

    // --- soft dial face ----------------------------------------------------
    {
        QRadialGradient face(geometry.center, radius * 0.74);
        face.setColorAt(0.0, QColor(0x18, 0x21, 0x2B, 200));
        face.setColorAt(0.78, QColor(0x11, 0x17, 0x1F, 110));
        face.setColorAt(1.0, QColor(0x0B, 0x0F, 0x14, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(face);
        painter.drawEllipse(geometry.center, radius * 0.74, radius * 0.74);
    }

    // --- track, with the zone scale as a thin rail down its centre ---------
    // The rail must stay thin: at full thickness the coloured scale reads as a
    // completely full gauge even when the disk is idle.
    paintArc(painter, geometry, arcRadius, arcThickness, m_min, m_max,
             m_available ? Theme::track() : Theme::track().darker(115));
    if (m_available) {
        for (const Zone& zone : m_zones) {
            QColor hint = zone.color;
            hint.setAlpha(165);
            paintArc(painter, geometry, arcRadius, arcThickness * 0.30, zone.from, zone.to, hint);
        }
    }

    // --- progress, split so the fill changes colour across the zones -------
    if (m_available && current > m_min + 0.1) {
        QColor glow = Theme::usageColor(current);
        glow.setAlpha(65);
        paintArc(painter, geometry, arcRadius, arcThickness * 1.14, m_min, current, glow, Qt::RoundCap);

        for (const Zone& zone : m_zones) {
            const double from = qMax(zone.from, m_min);
            const double to = qMin(zone.to, current);
            if (to <= from)
                continue;
            paintArc(painter, geometry, arcRadius, arcThickness, from, to, zone.color.darker(160));
            paintArc(painter, geometry, arcRadius, arcThickness * 0.56, from, to, zone.color);
        }

        // Dark separators where the colour changes.
        painter.save();
        painter.setPen(QPen(QColor(0x0B, 0x0F, 0x14, 210), qMax(1.5, radius * 0.012)));
        for (const Zone& zone : m_zones) {
            if (zone.from <= m_min || zone.from > current)
                continue;
            const double angle = angleForFraction(fractionFor(zone.from), geometry);
            painter.drawLine(pointAt(geometry, arcRadius - arcThickness * 0.5, angle),
                             pointAt(geometry, arcRadius + arcThickness * 0.5, angle));
        }
        painter.restore();
    }

    // --- ticks -------------------------------------------------------------
    const QFont tickFont = Theme::numberFont(qMax(7, int(radius * 0.070)));
    paintTicks(painter, geometry, arcRadius - arcThickness * 0.52 - radius * 0.020,
               radius * 0.068, radius * 0.036, 50, 5,
               m_available ? Theme::trackStrong().lighter(160) : Theme::trackStrong(),
               m_available ? Theme::textFaint() : Theme::textFaint().darker(130),
               [](double v) { return QString::number(int(v + 0.5)); },
               radius * 0.700, tickFont);

    // --- needle ------------------------------------------------------------
    if (m_available) {
        paintNeedle(painter, geometry, current,
                    radius * kNeedleLength, radius * 0.050,
                    Theme::usageColor(current), radius * kNeedleTail);
    }

    paintHub(painter, geometry, radius * 0.085,
             Theme::usageColor(qMax(current, m_min)).darker(140), QColor(0x10, 0x16, 0x1D));

    // --- digital readout, parked under the needle's lowest reach -----------
    painter.save();

    const double textTop = geometry.center.y() + radius * 0.360;
    const double textHeight = radius * 0.290;

    if (m_available) {
        const QFont numberFont = Theme::numberFont(qMax(16, int(radius * 0.270)), true);
        const QString numberText = QString::number(current, 'f', current < 10.0 ? 1 : 0);

        const QFont unitFont = Theme::uiFont(qMax(8, int(radius * 0.095)));
        const QFontMetricsF numberMetrics(numberFont, this);
        const QFontMetricsF unitMetrics(unitFont, this);

        const double numberWidth = numberMetrics.width(numberText);
        const double unitWidth = unitMetrics.width(QStringLiteral("%"));
        const double gap = radius * 0.020;
        const double totalWidth = numberWidth + gap + unitWidth;
        const double left = geometry.center.x() - totalWidth * 0.5;

        painter.setFont(numberFont);
        painter.setPen(Theme::usageColor(current));
        painter.drawText(QRectF(left, textTop, numberWidth + 2.0, textHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, numberText);

        painter.setFont(unitFont);
        painter.setPen(Theme::textMuted());
        painter.drawText(QRectF(left + numberWidth + gap, textTop + radius * 0.045,
                                unitWidth + 2.0, textHeight * 0.7),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("%"));
    } else {
        painter.setFont(Theme::numberFont(qMax(14, int(radius * 0.22)), true));
        painter.setPen(Theme::textFaint());
        painter.drawText(QRectF(geometry.center.x() - radius * 0.6, textTop,
                                radius * 1.2, textHeight),
                         Qt::AlignCenter, QStringLiteral("--"));
    }

    painter.setFont(Theme::uiFont(qMax(9, int(radius * 0.092))));
    painter.setPen(m_available ? Theme::textMuted() : Theme::textFaint());
    painter.drawText(QRectF(geometry.center.x() - radius * 0.65,
                            geometry.center.y() + radius * 0.690,
                            radius * 1.30, radius * 0.200),
                     Qt::AlignCenter,
                     m_available ? m_caption
                                 : (m_reason.isEmpty() ? QStringLiteral("无数据") : m_reason));
    painter.restore();
}
