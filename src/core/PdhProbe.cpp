#include "PdhProbe.h"
#include "WinUtil.h"

#include <QByteArray>
#include <QHash>

#include <pdhmsg.h>
#include <vector>

namespace {

typedef PDH_STATUS (WINAPI *PfnAddEnglishCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);

// Not every SDK header spells these out, and the numeric values are stable.
const PDH_STATUS kPdhInvalidData = PDH_STATUS(0xC0000BC6L);
const PDH_STATUS kPdhNoData      = PDH_STATUS(0x800007D5L);

/// "No new data for this tick" rather than "the counter is broken".
bool isTransientStatus(PDH_STATUS status)
{
    return status == kPdhInvalidData || status == kPdhNoData;
}

/// pdh.dll exports this on Vista+, but MinGW 5.3's import library does not
/// carry the symbol — so bind it at runtime.
PfnAddEnglishCounterW addEnglishCounterFn()
{
    static PfnAddEnglishCounterW fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE mod = ::GetModuleHandleW(L"pdh.dll");
        if (!mod)
            mod = ::LoadLibraryW(L"pdh.dll");
        if (mod)
            fn = reinterpret_cast<PfnAddEnglishCounterW>(::GetProcAddress(mod, "PdhAddEnglishCounterW"));
    }
    return fn;
}

/// PdhGetFormattedCounterArrayW on a wildcard counter -> { instance name : value }.
bool readCounterArray(PDH_HCOUNTER counter, QHash<QString, double>* out)
{
    if (!counter)
        return false;

    DWORD size = 0;
    DWORD count = 0;
    PDH_STATUS status = ::PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr);
    if (status != PDH_STATUS(PDH_MORE_DATA) || size == 0)
        return false;

    std::vector<unsigned char> buffer(size_t(size) + 64);
    PDH_FMT_COUNTERVALUE_ITEM_W* items =
        reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());

    status = ::PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items);
    if (status != ERROR_SUCCESS)
        return false;

    for (DWORD i = 0; i < count; ++i) {
        const QString name = QString::fromWCharArray(items[i].szName);
        const DWORD cs = items[i].FmtValue.CStatus;
        const double value = (cs == PDH_CSTATUS_VALID_DATA || cs == PDH_CSTATUS_NEW_DATA)
                                 ? items[i].FmtValue.doubleValue
                                 : 0.0;
        out->insert(name, value);
    }
    return true;
}

bool addWildcard(PDH_HQUERY query, const wchar_t* path, PDH_HCOUNTER* counter, QString* error)
{
    PfnAddEnglishCounterW add = addEnglishCounterFn();
    if (!add) {
        if (error)
            *error = QStringLiteral("pdh.dll 未提供 PdhAddEnglishCounterW");
        return false;
    }
    const PDH_STATUS status = add(query, path, 0, counter);
    if (status != ERROR_SUCCESS) {
        if (error)
            *error = QStringLiteral("添加计数器 %1 失败: %2")
                         .arg(QString::fromWCharArray(path), wu::winError(DWORD(status)));
        return false;
    }
    return true;
}

} // namespace

int deviceNumberFromPdhInstance(const QString& instanceName)
{
    const QString name = instanceName.trimmed();
    if (name.isEmpty() || name.startsWith(QLatin1Char('_')))
        return -1;

    int i = 0;
    while (i < name.size() && name.at(i).isDigit())
        ++i;
    if (i == 0)
        return -1;

    bool ok = false;
    const int value = name.left(i).toInt(&ok);
    return ok ? value : -1;
}

PdhProbe::PdhProbe() = default;

PdhProbe::~PdhProbe()
{
    stop();
}

bool PdhProbe::start(QString* error)
{
    stop();

    const PDH_STATUS openStatus = ::PdhOpenQueryW(nullptr, 0, &m_query);
    if (openStatus != ERROR_SUCCESS) {
        if (error)
            *error = QStringLiteral("PdhOpenQuery 失败: %1").arg(wu::winError(DWORD(openStatus)));
        m_query = nullptr;
        return false;
    }

    bool ok = true;
    ok = addWildcard(m_query, L"\\PhysicalDisk(*)\\% Idle Time",        &m_idle,    error) && ok;
    ok = addWildcard(m_query, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", &m_read,   error) && ok;
    ok = addWildcard(m_query, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec",&m_write,  error) && ok;
    ok = addWildcard(m_query, L"\\PhysicalDisk(*)\\Avg. Disk Queue Length", &m_queue, error) && ok;
    ok = addWildcard(m_query, L"\\PhysicalDisk(*)\\Avg. Disk sec/Transfer", &m_latency, error) && ok;

    if (!ok) {
        stop();
        if (error)
            *error = QStringLiteral("性能计数器不可用（可能需要以管理员身份运行 diskperf -y 启用磁盘计数器）");
        return false;
    }

    // Prime: rate counters need a baseline collection before they mean anything.
    ::PdhCollectQueryData(m_query);
    m_error.clear();
    return true;
}

void PdhProbe::stop()
{
    if (m_query) {
        ::PdhCloseQuery(m_query);
        m_query = nullptr;
    }
    m_idle = m_read = m_write = m_queue = m_latency = nullptr;
    m_transient = false;
}

bool PdhProbe::poll(QVector<PdhReading>* out, QString* error)
{
    m_transient = false;
    if (out)
        out->clear();
    if (!m_query) {
        if (error)
            *error = QStringLiteral("性能计数器查询未启动");
        return false;
    }

    const PDH_STATUS status = ::PdhCollectQueryData(m_query);
    if (status != ERROR_SUCCESS) {
        if (isTransientStatus(status))
            m_transient = true;
        if (error) {
            *error = QStringLiteral("PdhCollectQueryData 失败: %1").arg(wu::winError(DWORD(status)));
        }
        return false;
    }

    QHash<QString, double> idle, read, write, queue, latency;
    readCounterArray(m_idle, &idle);
    readCounterArray(m_read, &read);
    readCounterArray(m_write, &write);
    readCounterArray(m_queue, &queue);
    readCounterArray(m_latency, &latency);

    if (idle.isEmpty()) {
        // Either the machine really has no physical-disk instances, or the
        // collection succeeded but carried no fresh data. Treat it as a
        // transient miss so a single starved tick never flips the UI into a
        // "counter broken" state.
        m_transient = true;
        if (error)
            *error = QStringLiteral("性能计数器本次未返回物理磁盘数据");
        return false;
    }

    if (!out)
        return true;

    for (auto it = idle.constBegin(); it != idle.constEnd(); ++it) {
        const int device = deviceNumberFromPdhInstance(it.key());
        if (device < 0)
            continue;   // _Total and friends

        PdhReading r;
        r.deviceNumber = device;
        r.idlePercent  = it.value();
        r.readBps      = read.value(it.key(), 0.0);
        r.writeBps     = write.value(it.key(), 0.0);
        r.queueLength  = queue.value(it.key(), 0.0);
        r.latencySec   = latency.value(it.key(), 0.0);
        if (r.readBps < 0.0)   r.readBps = 0.0;
        if (r.writeBps < 0.0)  r.writeBps = 0.0;
        out->append(r);
    }

    return true;
}
