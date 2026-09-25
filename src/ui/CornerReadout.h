#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

/// A compact multi-row caption/value readout pinned into a corner of the dial.
///
/// It is painted rather than assembled from QLabels on purpose. The dashboard
/// centres its dial inside a square and leaves that square's corners empty, and
/// a painted child can sit there without joining any layout. A QLabel would feed
/// its text width into the window's minimum size and push the window edge
/// around — the exact failure mode the collapsible detail panel had.
class CornerReadout : public QWidget
{
    Q_OBJECT

public:
    struct Row {
        QString caption;
        QString value;
        QColor  color;
    };

    explicit CornerReadout(QWidget* parent = nullptr);

    void setRows(const QVector<Row>& rows);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<Row> m_rows;
};
