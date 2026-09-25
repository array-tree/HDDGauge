// Probe 3: what can a NON-elevated process still read from a physical drive?
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <cstdio>
#include <string>

static void L(const std::string& s){ std::printf("%s\n", s.c_str()); std::fflush(stdout); }
static std::string fmt(const char* f, ...){ char b[1024]; va_list a; va_start(a,f); vsnprintf(b,sizeof b,f,a); va_end(a); return std::string(b); }

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    struct Access { const char* name; DWORD access; DWORD share; };
    Access modes[] = {
        { "GENERIC_READ|WRITE", GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE },
        { "GENERIC_READ",       GENERIC_READ,               FILE_SHARE_READ|FILE_SHARE_WRITE },
        { "0 (query only)",     0,                          FILE_SHARE_READ|FILE_SHARE_WRITE },
    };

    for (int n = 0; n < 3; ++n) {
        for (const Access& m : modes) {
            std::string path = fmt("\\\\.\\PhysicalDrive%d", n);
            HANDLE h = CreateFileA(path.c_str(), m.access, m.share, 0, OPEN_EXISTING, 0, 0);
            if (h == INVALID_HANDLE_VALUE) {
                L(fmt("PD%d [%-18s] open FAILED err=%lu", n, m.name, GetLastError()));
                continue;
            }
            std::string line = fmt("PD%d [%-18s] open OK |", n, m.name);

            DWORD ret = 0;
            GET_LENGTH_INFORMATION gli;
            line += DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, 0,0,&gli,sizeof gli,&ret,0)
                    ? fmt(" size=%.1fGB", gli.Length.QuadPart/1073741824.0) : fmt(" size=ERR%lu", GetLastError());

            BYTE buf[1024]; ZeroMemory(buf,sizeof buf);
            STORAGE_PROPERTY_QUERY q; ZeroMemory(&q,sizeof q);
            q.PropertyId = StorageDeviceProperty; q.QueryType = PropertyStandardQuery;
            if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q,sizeof q, buf,sizeof buf,&ret,0)) {
                STORAGE_DEVICE_DESCRIPTOR* d = (STORAGE_DEVICE_DESCRIPTOR*)buf;
                line += fmt(" model='%s' bus=%d",
                            d->ProductIdOffset ? (char*)buf+d->ProductIdOffset : "?", (int)d->BusType);
            } else {
                line += fmt(" model=ERR%lu", GetLastError());
            }

            // ATA IDENTIFY (RPM)
            struct { ATA_PASS_THROUGH_DIRECT apt; unsigned short id[256]; } ab;
            ZeroMemory(&ab, sizeof ab);
            ab.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
            ab.apt.AtaFlags = ATA_FLAGS_DATA_IN;
            ab.apt.DataTransferLength = 512;
            ab.apt.TimeOutValue = 5;
            ab.apt.DataBuffer = ab.id;
            ab.apt.CurrentTaskFile[6] = 0xEC;
            if (DeviceIoControl(h, IOCTL_ATA_PASS_THROUGH_DIRECT, &ab, sizeof ab, &ab, sizeof ab, &ret, 0))
                line += fmt(" rpm(word217)=%u", ab.id[217]);
            else
                line += fmt(" rpm=ERR%lu", GetLastError());

            L(line);
            CloseHandle(h);
        }
    }
    L("done");
    return 0;
}
