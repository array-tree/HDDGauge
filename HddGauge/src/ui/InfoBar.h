#pragma once

#include "../core/DiskTypes.h"

#include <QHash>
#include <QWidget>

class QGridLayout;
class QLabel;

/// Fixed grid of caption/value readouts below the dial.
class InfoBar : public QWidget
{
    Q_OBJECT

public:
    explicit InfoBar(QWidget* parent = nullptr);

    void setDescriptor(const DriveDescriptor& descriptor);
    void setSample(const DiskSample& sample);
    void clearDynamic();

    /// Sampling period, appended to the "数据源" cell so the readout always
    /// states how old a number is (the interval is user-configurable).
    void setSamplingIntervalMs(int intervalMs);

private:
    QLabel* addCell(QGridLayout* grid, int row, int column,
                    const QString& caption, const QString& key, bool monospace);
    void setText(const QString& key, const QString& text);

    QHash<QString, QLabel*> m_cells;
    bool m_haveDescriptor = false;
    int  m_intervalMs = 0;
};
