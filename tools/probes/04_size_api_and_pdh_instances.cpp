// Probe 4: size APIs under 0-access handle + PDH PhysicalDisk instance names
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winioctl.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdio>
#include <string>
#include <vector>

#ifndef IOCTL_STORAGE_READ_CAPACITY
#define IOCTL_STORAGE_READ_CAPACITY CTL_CODE(IOCTL_STORAGE_BASE, 0x0512, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

static void L(const std::string& s){ std::printf("%s\n", s.c_str()); std::fflush(stdout); }
static std::string fmt(const char* f, ...){ char b[1024]; va_list a; va_start(a,f); vsnprintf(b,sizeof b,f,a); va_end(a); return std::string(b); }

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

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // ---------- A. PDH PhysicalDisk instances ----------
    DWORD cchCounter = 0, cchInstance = 0;
    PDH_STATUS st = PdhEnumObjectItemsW(0, 0, L"PhysicalDisk", 0, &cchCounter, 0, &cchInstance,
                                        PERF_DETAIL_WIZARD, 0);
    L(fmt("PdhEnumObjectItems(PhysicalDisk) probe status=0x%08X counterChars=%lu instanceChars=%lu",
          (unsigned)st, cchCounter, cchInstance));
    if (cchInstance > 0) {
        std::vector<wchar_t> inst(cchInstance + 2, 0), cnt(cchCounter + 2, 0);
        st = PdhEnumObjectItemsW(0, 0, L"PhysicalDisk", cnt.data(), &cchCounter,
                                 inst.data(), &cchInstance, PERF_DETAIL_WIZARD, 0);
        L(fmt("PdhEnumObjectItems status=0x%08X", (unsigned)st));
        std::string joined;
        for (const wchar_t* p = inst.data(); p && *p; p += wcslen(p) + 1)
            joined += fmt("'%ls' ", p);
        L("  instances: " + joined);

        std::string counters;
        for (const wchar_t* p = cnt.data(); p && *p; p += wcslen(p) + 1) {
            char mb[256]; WideCharToMultiByte(CP_ACP, 0, p, -1, mb, sizeof mb, 0, 0);
            counters += fmt("'%s' ", mb);
        }
        L("  counters(first 12): " + counters.substr(0, 600));
    }

    // wildcard counter -> instance names + values
    PDH_HQUERY q = 0; PDH_HCOUNTER h = 0;
    if (PdhOpenQueryW(0, 0, &q) == ERROR_SUCCESS) {
        PDH_STATUS s = addEnglish(q, L"\\PhysicalDisk(*)\\% Idle Time", 0, &h);
        L(fmt("AddEnglishCounter(%% Idle Time wildcard) status=0x%08X", (unsigned)s));
        if (s == ERROR_SUCCESS) {
            PdhCollectQueryData(q); Sleep(1100); PdhCollectQueryData(q);
            DWORD sz = 0, n = 0;
            if (PdhGetFormattedCounterArrayW(h, PDH_FMT_DOUBLE, &sz, &n, 0) == PDH_MORE_DATA && sz) {
                std::vector<unsigned char> buf(sz);
                PDH_FMT_COUNTERVALUE_ITEM_W* it = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
                if (PdhGetFormattedCounterArrayW(h, PDH_FMT_DOUBLE, &sz, &n, it) == ERROR_SUCCESS) {
                    for (DWORD i = 0; i < n; ++i) {
                        char nb[128]; WideCharToMultiByte(CP_ACP, 0, it[i].szName, -1, nb, sizeof nb, 0, 0);
                        L(fmt("  instance '%s' idle=%.2f  -> busy=%.2f%%", nb,
                              it[i].FmtValue.doubleValue, 100.0 - it[i].FmtValue.doubleValue));
                    }
                }
            } else {
                L("  PdhGetFormattedCounterArray probe failed");
            }
        }
        PdhCloseQuery(q);
    }

    // ---------- B. which size IOCTL works with 0 access ----------
    for (int n = 0; n < 3; ++n) {
        std::string path = fmt("\\\\.\\PhysicalDrive%d", n);
        HANDLE h2 = CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        if (h2 == INVALID_HANDLE_VALUE) { L(fmt("PD%d: 0-access open FAILED err=%lu", n, GetLastError())); continue; }
        DWORD ret = 0;

        STORAGE_READ_CAPACITY src; ZeroMemory(&src, sizeof src);
        src.Version = sizeof(STORAGE_READ_CAPACITY);
        if (DeviceIoControl(h2, IOCTL_STORAGE_READ_CAPACITY, 0,0,&src,sizeof src,&ret,0))
            L(fmt("PD%d: IOCTL_STORAGE_READ_CAPACITY OK -> %.2f GB (block=%lu)",
                  n, src.DiskLength.QuadPart / 1073741824.0, src.BlockLength));
        else
            L(fmt("PD%d: IOCTL_STORAGE_READ_CAPACITY ERR=%lu", n, GetLastError()));

        std::vector<unsigned char> gbuf(1024);
        if (DeviceIoControl(h2, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 0,0, gbuf.data(), (DWORD)gbuf.size(), &ret, 0)) {
            DISK_GEOMETRY_EX* g = (DISK_GEOMETRY_EX*)gbuf.data();
            L(fmt("PD%d: IOCTL_DISK_GET_DRIVE_GEOMETRY_EX OK -> %.2f GB", n, g->DiskSize.QuadPart / 1073741824.0));
        } else {
            L(fmt("PD%d: IOCTL_DISK_GET_DRIVE_GEOMETRY_EX ERR=%lu", n, GetLastError()));
        }

        // serial number + bus type via StorageDeviceProperty
        std::vector<unsigned char> dbuf(2048); ZeroMemory(dbuf.data(), dbuf.size());
        STORAGE_PROPERTY_QUERY pq; ZeroMemory(&pq, sizeof pq);
        pq.PropertyId = StorageDeviceProperty; pq.QueryType = PropertyStandardQuery;
        if (DeviceIoControl(h2, IOCTL_STORAGE_QUERY_PROPERTY, &pq,sizeof pq, dbuf.data(),(DWORD)dbuf.size(),&ret,0)) {
            STORAGE_DEVICE_DESCRIPTOR* d = (STORAGE_DEVICE_DESCRIPTOR*)dbuf.data();
            L(fmt("PD%d: model='%s' serial='%s' bus=%d",
                  n,
                  d->ProductIdOffset ? (char*)dbuf.data()+d->ProductIdOffset : "",
                  d->SerialNumberOffset ? (char*)dbuf.data()+d->SerialNumberOffset : "",
                  (int)d->BusType));
        }
        CloseHandle(h2);
    }
    L("done");
    return 0;
}

