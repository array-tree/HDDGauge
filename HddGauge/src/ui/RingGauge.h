#pragma once

#include "../core/DiskTypes.h"
#include "GaugeBase.h"

#include <QString>

/// The thin ring wrapped around the main dial.
///
/// Two interchangeable readings, exactly as requested:
///   * EquivalentRpm — derived "how fast would the platters have to spin"
///     figure, clearly labelled as a derived value.
///   * TransferRate  — measured read/write throughput in MB/s, two bands.
class RingGauge : public GaugeBase
{
    Q_OBJECT

public:
    explicit RingGauge(QWidget* parent = nullptr);

    void setMode(RingMode mode);
    RingMode mode() const { return m_mode; }

    /// Nominal spindle speed known for the selected drive.
    void setMedia(int nominalRpm, bool rotating, const QString& rpmSource);

    void setEquivalentRpm(double rpm);
    void setTransfer(double readBps, double writeBps);

    double scaleMaximum() const { return m_max; }
    QString scaleUnit() const;

protected:
    void paintGauge(QPainter& painter, const Geom& geometry) override;

private:
    void rebuildScale();

    RingMode m_mode = RingMode::EquivalentRpm;
    int      m_nominalRpm = 0;
    bool     m_rotating = true;
    QString  m_rpmSource;

    double   m_transferCeiling = 100.0;   // MB/s, auto-ranged with decay
    double   m_readBps = 0.0;
    double   m_writeBps = 0.0;
};
