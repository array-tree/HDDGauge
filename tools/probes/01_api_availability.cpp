#include <windows.h>
#include <winioctl.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <ntddscsi.h>
#include <cstdio>
#include <cstddef>

#define T(x) do { std::printf("%s\n", x); std::fflush(stdout); } while(0)
#define P(fmt, ...) do { std::printf(fmt "\n", __VA_ARGS__); std::fflush(stdout); } while(0)

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    T("start");

#ifdef IOCTL_ATA_PASS_THROUGH
    T("OK   IOCTL_ATA_PASS_THROUGH");
#else
    T("MISS IOCTL_ATA_PASS_THROUGH");
#endif

#ifdef IOCTL_ATA_PASS_THROUGH_DIRECT
    T("OK   IOCTL_ATA_PASS_THROUGH_DIRECT");
#else
    T("MISS IOCTL_ATA_PASS_THROUGH_DIRECT");
#endif

#ifdef IOCTL_STORAGE_QUERY_PROPERTY
    T("OK   IOCTL_STORAGE_QUERY_PROPERTY");
#else
    T("MISS IOCTL_STORAGE_QUERY_PROPERTY");
#endif

#ifdef IOCTL_STORAGE_GET_DEVICE_NUMBER
    T("OK   IOCTL_STORAGE_GET_DEVICE_NUMBER");
#else
    T("MISS IOCTL_STORAGE_GET_DEVICE_NUMBER");
#endif

#ifdef IOCTL_DISK_PERFORMANCE
    T("OK   IOCTL_DISK_PERFORMANCE");
#else
    T("MISS IOCTL_DISK_PERFORMANCE");
#endif

#ifdef IOCTL_DISK_PERFORMANCE_OFF
    T("OK   IOCTL_DISK_PERFORMANCE_OFF");
#else
    T("MISS IOCTL_DISK_PERFORMANCE_OFF");
#endif

    P("sizeof DISK_PERFORMANCE=%d", (int)sizeof(DISK_PERFORMANCE));
    P("offset IdleTime=%d", (int)offsetof(DISK_PERFORMANCE, IdleTime));
    P("offset ReadTime=%d", (int)offsetof(DISK_PERFORMANCE, ReadTime));
    P("offset WriteTime=%d", (int)offsetof(DISK_PERFORMANCE, WriteTime));
    P("offset QueryTime=%d", (int)offsetof(DISK_PERFORMANCE, QueryTime));
    P("sizeof STORAGE_DEVICE_NUMBER=%d", (int)sizeof(STORAGE_DEVICE_NUMBER));
    P("sizeof STORAGE_PROPERTY_QUERY=%d", (int)sizeof(STORAGE_PROPERTY_QUERY));
    P("sizeof STORAGE_DEVICE_DESCRIPTOR=%d", (int)sizeof(STORAGE_DEVICE_DESCRIPTOR));
    P("sizeof ATA_PASS_THROUGH_EX=%d", (int)sizeof(ATA_PASS_THROUGH_EX));

    HMODULE hPdh = LoadLibraryW(L"pdh.dll");
    P("hPdh=%p", (void*)hPdh);
    FARPROC pAddEn = hPdh ? GetProcAddress(hPdh, "PdhAddEnglishCounterW") : 0;
    P("PdhAddEnglishCounterW=%p", (void*)pAddEn);

    typedef PDH_STATUS (WINAPI *Pfn)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
    Pfn addEn = (Pfn)pAddEn;
    if (addEn) {
        PDH_HQUERY q = 0; PDH_HCOUNTER c = 0;
        PDH_STATUS s1 = PdhOpenQueryW(0, 0, &q);
        P("PdhOpenQueryW status=0x%08X", (unsigned)s1);
        PDH_STATUS s2 = addEn(q, L"\\PhysicalDisk(_Total)\\% Idle Time", 0, &c);
        P("PdhAddEnglishCounterW status=0x%08X", (unsigned)s2);
        PDH_STATUS s3 = PdhCollectQueryData(q);
        P("PdhCollectQueryData#1 status=0x%08X", (unsigned)s3);
        Sleep(1000);
        PDH_STATUS s4 = PdhCollectQueryData(q);
        P("PdhCollectQueryData#2 status=0x%08X", (unsigned)s4);
        PDH_FMT_COUNTERVALUE v; ZeroMemory(&v, sizeof(v));
        PDH_STATUS s5 = PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE, 0, &v);
        P("PdhGetFormattedCounterValue status=0x%08X statusField=0x%08X value=%.3f",
          (unsigned)s5, (unsigned)v.CStatus, v.doubleValue);
        PdhCloseQuery(q);
    }

    P("sizeof(void*)=%d", (int)sizeof(void*));
    T("done");
    return 0;
}
