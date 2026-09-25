#pragma once

#include <QColor>
#include <QFont>
#include <QString>

/// Single place for every colour and the application style sheet.
namespace Theme {

inline QColor background()   { return QColor(0x0B, 0x0F, 0x14); }
inline QColor panel()        { return QColor(0x13, 0x1A, 0x22); }
inline QColor panelAlt()     { return QColor(0x17, 0x1F, 0x29); }
inline QColor border()       { return QColor(0x22, 0x2C, 0x38); }
inline QColor text()         { return QColor(0xE6, 0xEA, 0xF0); }
inline QColor textMuted()    { return QColor(0x86, 0x92, 0xA3); }
inline QColor textFaint()    { return QColor(0x55, 0x60, 0x70); }

inline QColor accent()       { return QColor(0x22, 0xD3, 0xEE); }   // cyan
inline QColor green()        { return QColor(0x34, 0xD3, 0x99); }
inline QColor amber()        { return QColor(0xFB, 0xBF, 0x24); }
inline QColor red()          { return QColor(0xF8, 0x71, 0x71); }
inline QColor violet()       { return QColor(0xA7, 0x8B, 0xFA); }

inline QColor track()        { return QColor(0x1B, 0x24, 0x2F); }
inline QColor trackStrong()  { return QColor(0x27, 0x33, 0x42); }

/// Colour for a 0..100 utilisation value.
inline QColor usageColor(double percent)
{
    if (percent >= 85.0) return red();
    if (percent >= 60.0) return amber();
    return green();
}

QString styleSheet();

QFont uiFont(int pixelSize, bool bold = false);
QFont numberFont(int pixelSize, bool bold = true);

} // namespace Theme
