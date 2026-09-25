// Probe 2: physical drive enumeration, model/size, ATA IDENTIFY nominal RPM, SMART temperature
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <winternl.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static void L(const std::string& s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); }

static std::string fmt(const char* f, ...) {
    char buf[1024]; va_list a; va_start(a, f); vsnprintf(buf, sizeof buf, f, a); va_end(a);
    return std::string(buf);
}

static bool IsAdmin() {
    BOOL admin = FALSE; PSID sid = 0; SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0,0,0,0,0,0, &sid)) {
        CheckTokenMembership(0, sid, &admin);
        FreeSid(sid);
    }
    return admin != FALSE;
}

// ATA IDENTIFY DEVICE -> word 217 = nominal media rotation rate
static bool AtaIdentifyRpm(HANDLE h, unsigned short* rpm, bool* nonRotating)
{
    struct {
        ATA_PASS_THROUGH_DIRECT apt;
        unsigned short id[256];
    } buf;
    ZeroMemory(&buf, sizeof(buf));
    buf.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    buf.apt.AtaFlags = ATA_FLAGS_DATA_IN;
    buf.apt.DataTransferLength = 512;
    buf.apt.TimeOutValue = 5;
    buf.apt.DataBuffer = buf.id;
    buf.apt.CurrentTaskFile[6] = 0xEC;   // IDENTIFY DEVICE
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_ATA_PASS_THROUGH_DIRECT, &buf, sizeof(buf), &buf, sizeof(buf), &ret, 0))
        return false;
    unsigned short w = buf.id[217];
    *nonRotating = (w == 0x0001);
    *rpm = (w == 0x0001 || w == 0x0000) ? 0 : w;
    return true;
}

// SMART READ DATA -> attribute 194 = temperature
static bool SmartTemp(HANDLE h, int* tempC)
{
    struct {
        ATA_PASS_THROUGH_DIRECT apt;
        unsigned char data[512];
    } buf;
    ZeroMemory(&buf, sizeof(buf));
    buf.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    buf.apt.AtaFlags = ATA_FLAGS_DATA_IN;
    buf.apt.DataTransferLength = 512;
    buf.apt.TimeOutValue = 5;
    buf.apt.DataBuffer = buf.data;
    buf.apt.CurrentTaskFile[0] = 0xB0;   // SMART
    buf.apt.CurrentTaskFile[1] = 0x00;   // logical sector count high -> features
    buf.apt.CurrentTaskFile[2] = 0x00;
    buf.apt.CurrentTaskFile[3] = 0x00;
    buf.apt.CurrentTaskFile[4] = 0xD0;   // SMART READ DATA -> LBA low
    buf.apt.CurrentTaskFile[5] = 0x00;   // device
    buf.apt.CurrentTaskFile[6] = 0xD0;   // command -> SMART
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_ATA_PASS_THROUGH_DIRECT, &buf, sizeof(buf), &buf, sizeof(buf), &ret, 0))
        return false;
    for (int i = 0; i < 30; ++i) {
        const unsigned char* a = buf.data + 2 + i * 12;
        if (a[0] == 194) { *tempC = a[5]; return true; }
    }
    return false;
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    L(fmt("isAdmin=%d  sizeof(void*)=%d", (int)IsAdmin(), (int)sizeof(void*)));

    // ---- 1. logical volume -> physical drive number ----
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[8]; wsprintfW(root, L"\\\\.\\%c:", L'A' + i);
        HANDLE h = CreateFileW(root, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        if (h == INVALID_HANDLE_VALUE) { L(fmt("  %c: open FAILED err=%lu", 'A'+i, GetLastError())); continue; }
        STORAGE_DEVICE_NUMBER sdn; DWORD ret = 0;
        if (DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, 0, 0, &sdn, sizeof sdn, &ret, 0))
            L(fmt("  %c: -> PhysicalDrive%lu  partition=%lu", 'A'+i, sdn.DeviceNumber, sdn.PartitionNumber));
        else
            L(fmt("  %c: GET_DEVICE_NUMBER err=%lu", 'A'+i, GetLastError()));
        CloseHandle(h);
    }

    // ---- 2. physical drives ----
    for (int n = 0; n < 8; ++n) {
        std::string path = fmt("\\\\.\\PhysicalDrive%d", n);
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        if (h == INVALID_HANDLE_VALUE) {
            L(fmt("PhysicalDrive%d: open FAILED err=%lu", n, GetLastError()));
            continue;
        }
        L(fmt("PhysicalDrive%d: opened OK", n));

        // size
        GET_LENGTH_INFORMATION gli; DWORD ret = 0;
        if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, 0, 0, &gli, sizeof gli, &ret, 0))
            L(fmt("   size = %.2f GB", gli.Length.QuadPart / 1073741824.0));

        // model / bus type
        BYTE buf[1024]; ZeroMemory(buf, sizeof buf);
        STORAGE_PROPERTY_QUERY q; ZeroMemory(&q, sizeof q);
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf, sizeof buf, &ret, 0)) {
            STORAGE_DEVICE_DESCRIPTOR* d = (STORAGE_DEVICE_DESCRIPTOR*)buf;
            const char* vendor = d->VendorIdOffset ? (char*)buf + d->VendorIdOffset : "";
            const char* product = d->ProductIdOffset ? (char*)buf + d->ProductIdOffset : "";
            const char* rev = d->ProductRevisionOffset ? (char*)buf + d->ProductRevisionOffset : "";
            L(fmt("   vendor='%s' product='%s' rev='%s' busType=%d removable=%d",
                  vendor, product, rev, (int)d->BusType, (int)d->RemovableMedia));
        } else {
            L(fmt("   QUERY_PROPERTY err=%lu", GetLastError()));
        }

        // ATA IDENTIFY -> nominal RPM
        unsigned short rpm = 0; bool nonRot = false;
        if (AtaIdentifyRpm(h, &rpm, &nonRot))
            L(fmt("   ATA IDENTIFY OK: word217 -> nonRotating=%d nominalRpm=%u", (int)nonRot, rpm));
        else
            L(fmt("   ATA IDENTIFY FAILED err=%lu", GetLastError()));

        // SMART temperature
        int t = -1;
        if (SmartTemp(h, &t)) L(fmt("   SMART attr194 temperature = %d C", t));
        else                 L(fmt("   SMART READ DATA FAILED err=%lu", GetLastError()));

        CloseHandle(h);
    }
    L("done");
    return 0;
}
