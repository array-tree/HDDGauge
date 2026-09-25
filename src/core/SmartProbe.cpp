#include "SmartProbe.h"
#include "WinUtil.h"

#include <QStringList>

// ATA_PASS_THROUGH_EX / IOCTL_ATA_PASS_THROUGH_DIRECT live in ntddscsi.h;
// winioctl.h alone is not enough.
#include <ntddscsi.h>

namespace {

const int kAtaIdentifyDevice = 0xEC;
const int kSmartCommand = 0xB0;
const int kSmartReadData = 0xD0;

struct IdentifyBuffer
{
    ATA_PASS_THROUGH_DIRECT apt;
    unsigned short          words[256];
};

struct SmartBuffer
{
    ATA_PASS_THROUGH_DIRECT apt;
    unsigned char           data[512];
};

bool ataIdentify(HANDLE handle, unsigned short* words, QString* error)
{
    IdentifyBuffer buffer;
    ZeroMemory(&buffer, sizeof(buffer));
    buffer.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    buffer.apt.AtaFlags = ATA_FLAGS_DATA_IN;
    buffer.apt.DataTransferLength = 512;
    buffer.apt.TimeOutValue = 5;
    buffer.apt.DataBuffer = buffer.words;
    buffer.apt.CurrentTaskFile[6] = kAtaIdentifyDevice;

    DWORD returned = 0;
    if (!::DeviceIoControl(handle, IOCTL_ATA_PASS_THROUGH_DIRECT,
                           &buffer, sizeof(buffer), &buffer, sizeof(buffer), &returned, nullptr)) {
        if (error)
            *error = QStringLiteral("ATA IDENTIFY 失败：%1").arg(wu::lastError());
        return false;
    }
    if ((buffer.words[0] & 0x8000) == 0) {
        if (error)
            *error = QStringLiteral("ATA IDENTIFY 返回的数据无效");
        return false;
    }
    CopyMemory(words, buffer.words, 512);
    return true;
}

bool smartReadData(HANDLE handle, unsigned char* data, QString* error)
{
    SmartBuffer buffer;
    ZeroMemory(&buffer, sizeof(buffer));
    buffer.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    buffer.apt.AtaFlags = ATA_FLAGS_DATA_IN;
    buffer.apt.DataTransferLength = 512;
    buffer.apt.TimeOutValue = 5;
    buffer.apt.DataBuffer = buffer.data;
    buffer.apt.CurrentTaskFile[0] = kSmartCommand;
    buffer.apt.CurrentTaskFile[1] = 0x00;   // features (high)
    buffer.apt.CurrentTaskFile[2] = 0x00;
    buffer.apt.CurrentTaskFile[3] = 0x00;
    buffer.apt.CurrentTaskFile[4] = kSmartReadData;
    buffer.apt.CurrentTaskFile[5] = 0x00;
    buffer.apt.CurrentTaskFile[6] = kSmartCommand;

    DWORD returned = 0;
    if (!::DeviceIoControl(handle, IOCTL_ATA_PASS_THROUGH_DIRECT,
                           &buffer, sizeof(buffer), &buffer, sizeof(buffer), &returned, nullptr)) {
        if (error)
            *error = QStringLiteral("SMART READ DATA 失败：%1").arg(wu::lastError());
        return false;
    }
    CopyMemory(data, buffer.data, 512);
    return true;
}

/// SMART attribute table starts at byte 2, 12 bytes per entry.
bool findAttribute(const unsigned char* data, int id, unsigned char* raw6, int* normalized)
{
    for (int i = 0; i < 30; ++i) {
        const unsigned char* entry = data + 2 + i * 12;
        if (entry[0] == 0x00)
            break;
        if (entry[0] == id) {
            if (raw6)
                CopyMemory(raw6, entry + 5, 6);
            if (normalized)
                *normalized = entry[3];
            return true;
        }
    }
    return false;
}

} // namespace

SmartResult SmartProbe::query(int deviceNumber)
{
    SmartResult result;
    result.attempted = true;

    const QString path = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(deviceNumber);
    wu::Handle handle(::CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()),
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr));
    if (!handle.valid()) {
        const DWORD code = ::GetLastError();
        result.needsElevation = (code == ERROR_ACCESS_DENIED);
        result.error = result.needsElevation
                           ? QStringLiteral("需要管理员权限")
                           : wu::winError(code);
        return result;
    }

    QString error;
    unsigned short words[256];
    if (ataIdentify(handle.get(), words, &error)) {
        const unsigned short word217 = words[217];
        if (word217 == 0x0001) {
            result.nonRotating = true;
            result.rpmKnown = true;
            result.nominalRpm = 0;
        } else if (word217 >= 0x0401) {
            result.nominalRpm = int(word217);
            result.rpmKnown = true;
        }
        // word 217 == 0x0000 means "rate not reported" -> leave unknown.
        result.ok = true;
    } else {
        result.error = error;
    }

    unsigned char smartData[512];
    if (smartReadData(handle.get(), smartData, &error)) {
        unsigned char raw[6];
        int normalized = 0;

        if (findAttribute(smartData, 194, raw, &normalized)) {
            result.temperatureC = (raw[0] != 0 || raw[1] != 0) ? int(raw[0]) : int(normalized);
            result.temperatureKnown = true;
        }
        if (findAttribute(smartData, 9, raw, &normalized)) {
            result.powerOnHours = int(raw[0]) | (int(raw[1]) << 8) | (int(raw[2]) << 16) | (int(raw[3]) << 24);
            if (result.powerOnHours < 0)
                result.powerOnHours = normalized;
        }
        if (findAttribute(smartData, 5, raw, &normalized)) {
            result.reallocatedSectors = int(raw[0]) | (int(raw[1]) << 8);
        }
        if (findAttribute(smartData, 197, raw, &normalized)) {
            result.pendingSectors = int(raw[0]) | (int(raw[1]) << 8);
        }
        result.ok = true;
    } else if (result.error.isEmpty()) {
        result.error = error;
    }

    if (!result.ok && result.error.isEmpty())
        result.error = QStringLiteral("设备不支持 ATA 直通指令");
    return result;
}

int SmartProbe::rpmFromModelName(const QString& vendor, const QString& model)
{
    const QString text = (vendor + QLatin1Char(' ') + model).toUpper();

    // Explicit numbers win.
    static const struct { const char* token; int rpm; } kNumeric[] = {
        { "15000", 15000 }, { "10000", 10000 },
        { "7200",  7200  }, { "5900",  5900  },
        { "5400",  5400  }, { "5200",  5200  },
        { "4200",  4200  },
    };
    for (const auto& entry : kNumeric) {
        if (text.contains(QLatin1String(entry.token)))
            return entry.rpm;
    }

    // Family prefixes that do not carry a number.
    static const struct { const char* token; int rpm; } kFamily[] = {
        { "MQ01", 5400 }, { "MQ02", 5400 }, { "MQ03", 5400 }, { "MQ04", 5400 },   // Toshiba 2.5"
        { "MK",   5400 },                                                        // Toshiba 2.5"
        { "WD10SPZX", 5400 }, { "WD20SPZX", 5400 },                              // WD 2.5"
        { "WD5000L", 5400 },  { "WD7500B", 5400 },
        { "ST2000LM", 5400 }, { "ST1000LM", 5400 },                              // Seagate 2.5"
        { "ST4000DM", 5900 }, { "ST8000DM", 5400 },
        { "DT01", 7200 }, { "MG0", 7200 }, { "MD0", 7200 },                      // Toshiba 3.5"
        { "WD10EZEX", 7200 }, { "WD20EZRX", 5400 }, { "WD40EZRZ", 5400 },
        { "ST1000DM", 7200 }, { "ST2000DM", 7200 }, { "ST4000DM004", 5400 },
        { "HTS", 5400 }, { "HDS", 7200 }, { "HUH", 7200 },                       // HGST
    };
    for (const auto& entry : kFamily) {
        if (text.contains(QLatin1String(entry.token)))
            return entry.rpm;
    }

    return 0;
}

int SmartProbe::solidStateHint(const QString& vendor, const QString& model)
{
    const QString text = (vendor + QLatin1Char(' ') + model).toUpper();
    static const char* kSsdTokens[] = {
        "SSD", "NVME", "SOLID", "PC SN", "SN5", "SN7", "MZ-", "MZV", "SAMSUNG MZ",
        "CT", "MX500", "BX500", "SU800", "UV400", "A400", "KINGSTON SA", "SV300",
        "INTEL SSDP", "SANDISK SD", "WD BLUE SA", "WDS", "TOSHIBA THN", "KIOXIA",
    };
    for (const char* token : kSsdTokens) {
        if (text.contains(QLatin1String(token)))
            return 1;
    }
    // Anything that advertises a rotational rate is definitely rotating media.
    if (rpmFromModelName(vendor, model) > 0)
        return 0;
    return -1;   // unknown
}
