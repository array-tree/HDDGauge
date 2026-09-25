#pragma once

#include "GaugeBase.h"

/// The primary dial: 0..100 % disk busy, with the digital readout sitting in
/// the bottom gap of the 270 deg sweep (the needle can never reach there).
class UsageGauge : public GaugeBase
{
    Q_OBJECT

public:
    explicit UsageGauge(QWidget* parent = nullptr);

    void setBusy(double percent);
    void setAvailable(bool available, const QString& reason = QString());
    void setCaption(const QString& caption);

protected:
    void paintGauge(QPainter& painter, const Geom& geometry) override;

private:
    bool    m_available = false;
    QString m_reason;
    QString m_caption;
};
