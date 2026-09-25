#include "InfoBar.h"
#include "Theme.h"

#include "../core/WinUtil.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

InfoBar::InfoBar(QWidget* parent)
    : QWidget(parent)
{
    QGridLayout* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    for (int column = 0; column < 6; ++column)
        grid->setColumnStretch(column, 1);

    addCell(grid, 0, 0, QStringLiteral("型号"),       QStringLiteral("model"),    false);
    addCell(grid, 0, 1, QStringLiteral("容量"),       QStringLiteral("capacity"), false);
    addCell(grid, 0, 2, QStringLiteral("接口"),       QStringLiteral("bus"),      false);
    addCell(grid, 0, 3, QStringLiteral("介质"),       QStringLiteral("media"),    false);
    addCell(grid, 0, 4, QStringLiteral("标称转速"),   QStringLiteral("rpm"),      false);
    addCell(grid, 0, 5, QStringLiteral("温度"),       QStringLiteral("temp"),     false);

    addCell(grid, 1, 0, QStringLiteral("读取"),       QStringLiteral("read"),     true);
    addCell(grid, 1, 1, QStringLiteral("写入"),       QStringLiteral("write"),    true);
    addCell(grid, 1, 2, QStringLiteral("队列深度"),   QStringLiteral("queue"),    true);
    addCell(grid, 1, 3, QStringLiteral("响应时间"),   QStringLiteral("latency"),  true);
    addCell(grid, 1, 4, QStringLiteral("盘符"),       QStringLiteral("letters"),  false);
    addCell(grid, 1, 5, QStringLiteral("数据源"),     QStringLiteral("source"),   false);
}

QLabel* InfoBar::addCell(QGridLayout* grid, int row, int column,
                         const QString& caption, const QString& key, bool monospace)
{
    QWidget* cell = new QWidget(this);
    cell->setObjectName(QStringLiteral("Card"));

    QVBoxLayout* layout = new QVBoxLayout(cell);
    layout->setContentsMargins(10, 7, 10, 8);
    layout->setSpacing(2);

    QLabel* captionLabel = new QLabel(caption, cell);
    captionLabel->setObjectName(QStringLiteral("Caption"));

    QLabel* valueLabel = new QLabel(QStringLiteral("--"), cell);
    valueLabel->setObjectName(monospace ? QStringLiteral("ValueMono") : QStringLiteral("Value"));
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Columns size to their content (the default policy), but a pathologically
    // long model string ("WDC PC SN520 SDAPMUW-128G-1001") must not be able to
    // widen the whole window: cap each value and rely on the tool tip for the
    // rest. Ignoring the size hint entirely was tried first and squeezed every
    // column down to the caption width, clipping even "TOSHIBA MQ04ABF100".
    valueLabel->setMaximumWidth(230);

    layout->addWidget(captionLabel);
    layout->addWidget(valueLabel);

    grid->addWidget(cell, row, column);

    m_cells.insert(key, valueLabel);
    return valueLabel;
}

void InfoBar::setText(const QString& key, const QString& text)
{
    QLabel* label = m_cells.value(key, nullptr);
    if (!label || label->text() == text)
        return;
    label->setText(text);
    label->setToolTip(text);
}

void InfoBar::clearDynamic()
{
    static const char* kDynamic[] = { "read", "write", "queue", "latency", "source" };
    for (const char* key : kDynamic)
        setText(QLatin1String(key), QStringLiteral("--"));
}

void InfoBar::setDescriptor(const DriveDescriptor& descriptor)
{
    m_haveDescriptor = true;

    setText(QStringLiteral("model"), descriptor.model.isEmpty()
                                         ? QStringLiteral("未知")
                                         : descriptor.model);
    setText(QStringLiteral("capacity"), descriptor.sizeBytes > 0
                                            ? wu::formatBytes(descriptor.sizeBytes)
                                            : QStringLiteral("需管理员权限"));
    setText(QStringLiteral("bus"), descriptor.busName.isEmpty()
                                       ? QStringLiteral("未知")
                                       : descriptor.busName);
    setText(QStringLiteral("media"), descriptor.mediaText());

    QString rpmText;
    if (descriptor.solidState) {
        rpmText = QStringLiteral("—");
    } else if (descriptor.nominalRpm > 0) {
        rpmText = QStringLiteral("%1 RPM").arg(descriptor.nominalRpm);
    } else {
        rpmText = QStringLiteral("未知 (按 7200 估算)");
    }
    setText(QStringLiteral("rpm"), rpmText);

    if (descriptor.temperatureC != INT_MIN)
        setText(QStringLiteral("temp"), QStringLiteral("%1 °C").arg(descriptor.temperatureC));
    else if (!descriptor.smartNote.isEmpty())
        setText(QStringLiteral("temp"), descriptor.smartNote.left(24));
    else if (!descriptor.accessNote.isEmpty())
        setText(QStringLiteral("temp"), QStringLiteral("需管理员权限"));
    else
        setText(QStringLiteral("temp"), QStringLiteral("不支持"));

    setText(QStringLiteral("letters"),
            descriptor.letters.isEmpty() ? QStringLiteral("—") : descriptor.letters);
}

void InfoBar::setSamplingIntervalMs(int intervalMs)
{
    m_intervalMs = intervalMs;
}

void InfoBar::setSample(const DiskSample& sample)
{
    if (!m_haveDescriptor)
        return;

    setText(QStringLiteral("read"), wu::formatRate(sample.readBps));
    setText(QStringLiteral("write"), wu::formatRate(sample.writeBps));
    setText(QStringLiteral("queue"), QString::number(sample.queueLength, 'f', 2));

    if (sample.latencySec > 0.0)
        setText(QStringLiteral("latency"), QStringLiteral("%1 ms").arg(sample.latencySec * 1000.0, 0, 'f', 2));
    else
        setText(QStringLiteral("latency"), QStringLiteral("—"));

    QString source = sample.valid ? sample.source : QStringLiteral("无数据");
    if (m_intervalMs > 0)
        source += QStringLiteral(" · %1").arg(m_intervalMs < 1000
                                                  ? QStringLiteral("%1 ms").arg(m_intervalMs)
                                                  : QStringLiteral("%1 s").arg(m_intervalMs / 1000.0, 0, 'f',
                                                                               m_intervalMs % 1000 ? 1 : 0));
    setText(QStringLiteral("source"), source);
}
