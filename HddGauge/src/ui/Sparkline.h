#pragma once

#include <QVector>
#include <QWidget>

/// Rolling history of the utilisation, drawn as a filled line chart.
///
/// The buffer is a *time* window, not a sample count: the sampling interval is
/// user-configurable (100 ms .. 2 s), so a fixed number of samples would mean a
/// different amount of history for every setting. Points are stamped with the
/// sample time and everything older than the window is dropped, which also
/// keeps the x axis honest if a tick is ever missed.
class Sparkline : public QWidget
{
    Q_OBJECT

public:
    explicit Sparkline(QWidget* parent = nullptr);

    /// Length of the visible history. Default 60 s.
    void setWindowSeconds(int seconds);
    int  windowSeconds() const { return m_windowMs / 1000; }

    void addSample(double percent, qint64 timestampMs);
    void reset();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Point
    {
        Point() = default;
        Point(qint64 stamp, double v) : ms(stamp), value(v) {}

        qint64 ms = 0;
        double value = 0.0;
    };

    void prune();

    QVector<Point> m_points;
    int m_windowMs = 60000;

    /// Upper bound on retained points; the window normally prunes first, this
    /// only guards against a pathological sampling rate.
    static const int kMaxPoints = 6000;
};
