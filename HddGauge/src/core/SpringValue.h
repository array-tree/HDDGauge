#pragma once

#include <QtGlobal>

#include <cmath>

// A critically-damped spring used to drive every needle.
//
// Gauges must never jump: a raw 1 Hz sample rendered directly reads as a
// flickering stick. Running the sample through a spring at 60 fps gives the
// needle mass, which is what makes the dial feel like a real instrument.
class SpringValue
{
public:
    SpringValue() = default;

    void setFrequency(double hz) { m_omega = 2.0 * kPi * hz; }

    /// New destination for the needle.
    void setTarget(double t) { m_target = t; }

    /// Jump straight to a value (used on first sample / device switch).
    void snapTo(double v) { m_value = v; m_target = v; m_velocity = 0.0; }

    double value() const { return m_value; }
    double target() const { return m_target; }

    /// Integrate one step; returns true while the needle is still moving.
    bool step(double dt)
    {
        if (dt <= 0.0)
            return true;
        if (dt > 0.25)
            dt = 0.25;

        const double k = 2.0 * m_omega;
        const double accel = -m_omega * m_omega * (m_value - m_target) - k * m_velocity;
        m_velocity += accel * dt;
        m_value += m_velocity * dt;

        if (qAbs(m_value - m_target) < 1e-3 && qAbs(m_velocity) < 1e-2) {
            m_value = m_target;
            m_velocity = 0.0;
            return false;
        }
        return true;
    }

    void setFrequencyIfIdle(double hz) { setFrequency(hz); }

private:
    static constexpr double kPi = 3.14159265358979323846;

    double m_value = 0.0;
    double m_target = 0.0;
    double m_velocity = 0.0;
    double m_omega = 2.0 * kPi * 1.7;   // ~1.7 Hz settle
};
