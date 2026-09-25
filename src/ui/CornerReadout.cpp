#include "CornerReadout.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QPainter>

namespace {

const int kCaptionPx = 10;
const int kValuePx   = 14;
const int kRowGap    = 3;
const int kColumnGap = 10;

} // namespace

CornerReadout::CornerReadout(QWidget* parent)
    : QWidget(parent)
{
    // Purely informational: never steal a click from the dial's context menu.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
}

void CornerReadout::setRows(const QVector<Row>& rows)
{
    // The readout is refreshed on every sample, most of which do not move a
    // single digit; re-measuring and repainting only when something changed
    // keeps a 100 ms sampling interval cheap.
    bool same = rows.size() == m_rows.size();
    for (int i = 0; same && i < rows.size(); ++i) {
        same = rows.at(i).caption == m_rows.at(i).caption
            && rows.at(i).value == m_rows.at(i).value
            && rows.at(i).color == m_rows.at(i).color;
    }
    if (same)
        return;

    m_rows = rows;
    updateGeometry();
    update();
}

QSize CornerReadout::sizeHint() const
{
    const QFontMetrics captionMetrics(Theme::uiFont(kCaptionPx));
    const QFontMetrics valueMetrics(Theme::numberFont(kValuePx));

    int width = 0;
    int height = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        width = qMax(width, captionMetrics.width(m_rows.at(i).caption) + kColumnGap
                          + valueMetrics.width(m_rows.at(i).value));
        height += qMax(captionMetrics.height(), valueMetrics.height());
        if (i + 1 < m_rows.size())
            height += kRowGap;
    }
    return QSize(width, height);
}

void CornerReadout::paintEvent(QPaintEvent*)
{
    if (m_rows.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QFont captionFont = Theme::uiFont(kCaptionPx);
    const QFont valueFont = Theme::numberFont(kValuePx);
    const QFontMetrics captionMetrics(captionFont);
    const QFontMetrics valueMetrics(valueFont);

    int y = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows.at(i);
        const int rowHeight = qMax(captionMetrics.height(), valueMetrics.height());
        const QRect line(0, y, width(), rowHeight);

        painter.setFont(captionFont);
        painter.setPen(Theme::textMuted());
        painter.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, row.caption);

        painter.setFont(valueFont);
        painter.setPen(row.color.isValid() ? row.color : Theme::text());
        painter.drawText(line, Qt::AlignRight | Qt::AlignVCenter, row.value);

        y += rowHeight + kRowGap;
    }
}
