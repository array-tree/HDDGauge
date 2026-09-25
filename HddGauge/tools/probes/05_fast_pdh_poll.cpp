// Probe 5: can the \PhysicalDisk(*) counters be sampled faster than 1 Hz?
//
// The dashboard originally sampled at 1 Hz with the justification that "PDH
// rate counters are differential". That is only half true: PDH computes a rate
// from the time stamps of the *last two* PdhCollectQueryData calls, so the
// interval is arbitrary in principle. What has to be checked is whether the
// driver-backed PhysicalDisk counters stay accurate and free of invalid status
// codes when collected every 100-250 ms.
//
// Method: keep the target disk genuinely busy from a background thread
// (real writes + FlushFileBuffers, so it is not just cache), then sample the
// counter set at several intervals. If the counters are interval independent,
// the mean busy% and the mean bytes/sec must agree across intervals.
//
// Build (from tools/probes):
//   g++ -std=c++11 -O2 -o p5.exe 05_fast_pdh_poll.cpp -lpdh -lwinmm
//
// Usage:
//   p5.exe [targetDevice=0] [perIntervalSeconds=3] [ioFile=<path>]
//
// The I/O file must live on the target disk for the numbers to be meaningful.

#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define NOMINMAX

#include <windows.h>
#include <mmsystem.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
static void L(const std::string& s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }

static std::string fmt(const char* f, ...)
{
    char b[1024];
    va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
    return std::string(b);
}

// MinGW 5.3's libpdh.a does not export PdhAddEnglishCounterW -> resolve at runtime.
typedef PDH_STATUS (WINAPI *PfnAddEnglish)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
static PfnAddEnglish g_addEnglish = 0;
static PDH_STATUS addEnglish(PDH_HQUERY q, LPCWSTR path, DWORD_PTR user, PDH_HCOUNTER* h)
{
    if (!g_addEnglish) {
        HMODULE m = GetModuleHandleW(L"pdh.dll");
        if (!m) m = LoadLibraryW(L"pdh.dll");
        if (m) g_addEnglish = (PfnAddEnglish)GetProcAddress(m, "PdhAddEnglishCounterW");
    }
    return g_addEnglish ? g_addEnglish(q, path, user, h) : (PDH_STATUS)PDH_CSTATUS_NO_OBJECT;
}

static int deviceFromInstance(const std::wstring& name)
{
    size_t i = 0;
    while (i < name.size() && name[i] >= L'0' && name[i] <= L'9') ++i;
    if (i == 0) return -1;
    int value = 0;
    for (size_t k = 0; k < i; ++k)
        value = value * 10 + int(name[k] - L'0');
    return value;
}

// ---------------------------------------------------------------------------
// one wildcard counter read: value for the target disk + status accounting
// ---------------------------------------------------------------------------
struct ArrayRead
{
    bool     ok = false;
    int      instances = 0;
    int      badStatus = 0;      // CStatus != VALID/NEW
    DWORD    worstStatus = 0;
    std::wstring worstName;
    bool     haveTarget = false;
    double   target = 0.0;
};

static ArrayRead readArray(PDH_HCOUNTER counter, int targetDevice)
{
    ArrayRead r;
    if (!counter) return r;

    DWORD size = 0, count = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, 0);
    if (st != PDH_MORE_DATA || size == 0) {
        r.worstStatus = (DWORD)st;
        return r;
    }

    std::vector<unsigned char> buf(size_t(size) + 64);
    PDH_FMT_COUNTERVALUE_ITEM_W* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    st = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items);
    if (st != ERROR_SUCCESS) {
        r.worstStatus = (DWORD)st;
        return r;
    }

    r.ok = true;
    for (DWORD i = 0; i < count; ++i) {
        std::wstring name = items[i].szName ? items[i].szName : L"";
        if (deviceFromInstance(name) < 0) continue;   // _Total
        ++r.instances;
        const DWORD cs = items[i].FmtValue.CStatus;
        if (cs != PDH_CSTATUS_VALID_DATA && cs != PDH_CSTATUS_NEW_DATA) {
            ++r.badStatus;
            r.worstStatus = cs;
            r.worstName = name;
        }
        if (deviceFromInstance(name) == targetDevice) {
            r.haveTarget = true;
            r.target = items[i].FmtValue.doubleValue;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// background load generator
// ---------------------------------------------------------------------------
static std::atomic<bool> g_ioStop(false);
static std::atomic<long long> g_ioBytes(0);
static std::wstring g_ioPath;

static void ioThread()
{
    const DWORD chunk = 1u << 20;                 // 1 MiB
    std::vector<char> buf(chunk);
    for (DWORD i = 0; i < chunk; ++i)
        buf[i] = char(i * 31 + (i >> 8));

    while (!g_ioStop.load()) {
        HANDLE h = CreateFileW(g_ioPath.c_str(), GENERIC_WRITE, 0, 0,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == INVALID_HANDLE_VALUE) {
            L(fmt("  !! cannot open I/O file (err=%lu)", GetLastError()));
            return;
        }
        for (int pass = 0; pass < 48 && !g_ioStop.load(); ++pass) {   // ~48 MiB per file
            DWORD written = 0;
            if (!WriteFile(h, buf.data(), chunk, &written, 0) || written != chunk)
                break;
            // Force the write all the way to the platters, otherwise the
            // physical disk counters would only see cache traffic.
            FlushFileBuffers(h);
            g_ioBytes += written;
        }
        CloseHandle(h);
    }
}

// ---------------------------------------------------------------------------
// sampling run
// ---------------------------------------------------------------------------
struct Stats
{
    int    samples = 0;
    int    collectFail = 0;
    DWORD  firstCollectFail = 0;
    int    invalidValues = 0;
    double sumBusy = 0, sumRead = 0, sumWrite = 0, sumQueue = 0, sumLatency = 0;
    double minDeltaMs = 1e9, maxDeltaMs = 0, sumDeltaMs = 0;
    double maxCollectMs = 0, sumCollectMs = 0;
    long long ioBytes = 0;
};

static double qpcMs(const LARGE_INTEGER& freq, const LARGE_INTEGER& a, const LARGE_INTEGER& b)
{
    return double(b.QuadPart - a.QuadPart) * 1000.0 / double(freq.QuadPart);
}

static Stats runInterval(int intervalMs, int durationSec, int targetDevice)
{
    Stats s;

    PDH_HQUERY q = 0;
    PDH_HCOUNTER hIdle = 0, hRead = 0, hWrite = 0, hQueue = 0, hLat = 0;
    if (PdhOpenQueryW(0, 0, &q) != ERROR_SUCCESS) { L("  !! PdhOpenQuery failed"); return s; }
    addEnglish(q, L"\\PhysicalDisk(*)\\% Idle Time", 0, &hIdle);
    addEnglish(q, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", 0, &hRead);
    addEnglish(q, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", 0, &hWrite);
    addEnglish(q, L"\\PhysicalDisk(*)\\Avg. Disk Queue Length", 0, &hQueue);
    addEnglish(q, L"\\PhysicalDisk(*)\\Avg. Disk sec/Transfer", 0, &hLat);

    PdhCollectQueryData(q);                    // baseline
    Sleep(intervalMs);

    LARGE_INTEGER freq, prev, now, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    const long long ioBefore = g_ioBytes.load();
    QueryPerformanceCounter(&t0);
    const int wanted = durationSec * 1000 / intervalMs;

    for (int i = 0; i < wanted; ++i) {
        Sleep(intervalMs);

        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        const PDH_STATUS st = PdhCollectQueryData(q);
        QueryPerformanceCounter(&b);

        if (st != ERROR_SUCCESS) {
            ++s.collectFail;
            if (!s.firstCollectFail) s.firstCollectFail = (DWORD)st;
        } else {
            const ArrayRead idle  = readArray(hIdle,  targetDevice);
            const ArrayRead rd    = readArray(hRead,  targetDevice);
            const ArrayRead wr    = readArray(hWrite, targetDevice);
            const ArrayRead queue = readArray(hQueue, targetDevice);
            const ArrayRead lat   = readArray(hLat,   targetDevice);

            if (idle.ok && idle.haveTarget) {
                ++s.samples;
                s.sumBusy += (100.0 - idle.target);
                s.sumRead += rd.target;
                s.sumWrite += wr.target;
                s.sumQueue += queue.target;
                s.sumLatency += lat.target;
                s.invalidValues += idle.badStatus + rd.badStatus + wr.badStatus
                                   + queue.badStatus + lat.badStatus;
            }
        }

        const double collectMs = qpcMs(freq, a, b);
        s.sumCollectMs += collectMs;
        if (collectMs > s.maxCollectMs) s.maxCollectMs = collectMs;

        QueryPerformanceCounter(&now);
        const double delta = qpcMs(freq, prev, now);
        prev = now;
        s.sumDeltaMs += delta;
        if (delta < s.minDeltaMs) s.minDeltaMs = delta;
        if (delta > s.maxDeltaMs) s.maxDeltaMs = delta;
    }
    QueryPerformanceCounter(&t1);
    s.ioBytes = g_ioBytes.load() - ioBefore;

    PdhCloseQuery(q);
    return s;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    const int targetDevice   = (argc > 1) ? atoi(argv[1]) : 0;
    const int perIntervalSec = (argc > 2) ? atoi(argv[2]) : 3;

    char cwd[MAX_PATH] = {0};
    GetCurrentDirectoryA(MAX_PATH, cwd);
    g_ioPath = L"pdh_load.tmp";
    if (argc > 3) {
        std::string p = argv[3];
        g_ioPath.assign(p.begin(), p.end());
    }

    timeBeginPeriod(1);   // Qt does the same; without it Sleep() quantises to ~15.6 ms

    L("=== probe 5: high-rate PDH polling ===");
    L(fmt("target PhysicalDrive%d, %d s per interval, cwd=%s", targetDevice, perIntervalSec, cwd));
    L(fmt("io file: %ls", g_ioPath.c_str()));

    // --- instance names, so the reader can sanity-check the mapping ---------
    {
        DWORD cchCounter = 0, cchInstance = 0;
        PdhEnumObjectItemsW(0, 0, L"PhysicalDisk", 0, &cchCounter, 0, &cchInstance,
                            PERF_DETAIL_WIZARD, 0);
        if (cchInstance > 0) {
            std::vector<wchar_t> inst(cchInstance + 2, 0), cnt(cchCounter + 2, 0);
            if (PdhEnumObjectItemsW(0, 0, L"PhysicalDisk", cnt.data(), &cchCounter,
                                    inst.data(), &cchInstance, PERF_DETAIL_WIZARD, 0) == ERROR_SUCCESS) {
                std::string joined;
                for (const wchar_t* p = inst.data(); p && *p; p += wcslen(p) + 1)
                    joined += fmt("'%ls' ", p);
                L("instances: " + joined);
            }
        }
    }

    std::thread io(ioThread);
    Sleep(500);
    L(fmt("load generator running (%.1f MiB written so far)", double(g_ioBytes.load()) / 1048576.0));
    L("");

    const int intervals[] = { 100, 200, 250, 500, 1000 };
    const int n = int(sizeof(intervals) / sizeof(intervals[0]));

    std::vector<Stats> results;
    L("interval | samples | collectFail | invalidVal | meanBusy% | meanRead MB/s | meanWrite MB/s | "
      "meanQueue | meanLatency ms | tick ms(avg/min/max) | collect ms(avg/max)");
    L("---------+---------+-------------+------------+-----------+---------------+----------------+"
      "-----------+----------------+----------------------+-------------------");
    for (int i = 0; i < n; ++i) {
        const Stats s = runInterval(intervals[i], perIntervalSec, targetDevice);
        results.push_back(s);

        const double d = s.samples ? double(s.samples) : 1.0;
        L(fmt("%6d ms| %7d | %11d | %10d | %8.2f%% | %13.2f | %14.2f | %9.2f | %14.3f | "
              "%6.1f/%6.1f/%6.1f | %8.2f/%6.2f",
              intervals[i], s.samples, s.collectFail, s.invalidValues,
              s.sumBusy / d, s.sumRead / d / 1048576.0, s.sumWrite / d / 1048576.0,
              s.sumQueue / d, s.sumLatency / d * 1000.0,
              s.sumDeltaMs / (s.samples ? s.samples : 1), s.minDeltaMs, s.maxDeltaMs,
              s.sumCollectMs / (s.samples ? s.samples : 1), s.maxCollectMs));
        if (s.firstCollectFail)
            L(fmt("         ^ first PdhCollectQueryData failure status = 0x%08X", s.firstCollectFail));
    }

    L("");
    // --- verdict: compare every interval against the 1 Hz reference ---------
    L("=== agreement vs the 1 s reference (write MB/s) ===");
    const Stats& ref = results[n - 1];
    const double refWrite = ref.samples ? ref.sumWrite / ref.samples : 0.0;
    const double refBusy  = ref.samples ? ref.sumBusy / ref.samples : 0.0;
    bool allGood = true;
    for (int i = 0; i < n - 1; ++i) {
        const Stats& s = results[i];
        const double w = s.samples ? s.sumWrite / s.samples : 0.0;
        const double b = s.samples ? s.sumBusy / s.samples : 0.0;
        const double dw = refWrite > 0 ? (w - refWrite) / refWrite * 100.0 : 0.0;
        const double db = refBusy  > 0 ? (b - refBusy)  / refBusy  * 100.0 : 0.0;
        const bool ok = s.collectFail == 0 && s.samples > 0 && std::fabs(dw) < 35.0;
        if (!ok) allGood = false;
        L(fmt("  %4d ms: write %+7.1f%%   busy %+7.1f%%   -> %s",
              intervals[i], dw, db, ok ? "OK" : "CHECK"));
    }
    if (ref.samples == 0)
        L("  !! the 1 s reference produced no samples - the load may have ended early");

    g_ioStop = true;
    io.join();
    DeleteFileW(g_ioPath.c_str());
    timeEndPeriod(1);

    L("");
    L(allGood ? "VERDICT: sub-second polling looks safe (no invalid data, means agree)."
              : "VERDICT: at least one interval disagreed - keep the sampling interval at >= 1 s.");
    return allGood ? 0 : 1;
}
