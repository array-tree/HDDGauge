#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>

namespace Theme {

QString styleSheet()
{
    return QStringLiteral(R"QSS(
QWidget {
    background: transparent;
    color: #E6EAF0;
    font-family: "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", sans-serif;
    font-size: 12px;
}
QWidget#RootPanel {
    background: #0B0F14;
    border: 1px solid #1E2733;
    border-radius: 14px;
}
QWidget#Card {
    background: #131A22;
    border: 1px solid #1F2937;
    border-radius: 10px;
}
QLabel#Title {
    font-size: 15px;
    font-weight: 600;
    color: #E6EAF0;
}
QLabel#Caption {
    color: #6C7787;
    font-size: 11px;
}
QLabel#Value {
    color: #E6EAF0;
    font-size: 13px;
    font-weight: 600;
}
QLabel#ValueMono {
    color: #E6EAF0;
    font-size: 13px;
    font-weight: 600;
    font-family: "Consolas", "Cascadia Mono", monospace;
}
QLabel#Status {
    color: #868FA0;
    font-size: 11px;
}
QLabel#StatusWarn {
    color: #FBBF24;
    font-size: 11px;
}
QComboBox {
    background: #171F29;
    border: 1px solid #2A3542;
    border-radius: 7px;
    padding: 4px 8px;
    min-height: 20px;
    color: #E6EAF0;
}
QComboBox:hover { border-color: #22D3EE; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #8A95A5;
    width: 0; height: 0;
    margin-right: 6px;
}
QComboBox QAbstractItemView {
    background: #131A22;
    border: 1px solid #2A3542;
    selection-background-color: #1E3A45;
    selection-color: #22D3EE;
    outline: none;
    padding: 4px;
}
QPushButton, QToolButton {
    background: #171F29;
    border: 1px solid #2A3542;
    border-radius: 7px;
    padding: 4px 10px;
    color: #C9D2DE;
}
QPushButton:hover, QToolButton:hover { border-color: #22D3EE; color: #E6EAF0; }
QPushButton:checked, QToolButton:checked {
    background: #10333C;
    border-color: #22D3EE;
    color: #22D3EE;
}
QMenu {
    background: #131A22;
    border: 1px solid #2A3542;
    padding: 5px;
}
QMenu::item { padding: 5px 22px 5px 14px; border-radius: 5px; }
QMenu::item:selected { background: #1E3A45; color: #22D3EE; }
QMenu::separator { height: 1px; background: #232E3B; margin: 4px 8px; }
QToolTip {
    background: #131A22;
    color: #E6EAF0;
    border: 1px solid #2A3542;
    padding: 4px 6px;
}
)QSS");
}

QFont uiFont(int pixelSize, bool bold)
{
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QFont numberFont(int pixelSize, bool bold)
{
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

} // namespace Theme
