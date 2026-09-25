#include "DriveEnumerator.h"
#include "BusTypes.h"
#include "SmartProbe.h"
#include "WinUtil.h"

#include <QCoreApplication>
#include <QSet>

#include <vector>

#ifndef IOCTL_DISK_GET_DRIVE_GEOMETRY_EX
#  define IOCTL_DISK_GET_DRIVE_GEOMETRY_EX \
      CTL_CODE(IOCTL_DISK_BASE, 0x0028, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

namespace {

wu::Handle openPhysicalDrive(int deviceNumber, DWORD access)
{
    const QString path = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(deviceNumber);
    return wu::Handle(::CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()),
                                    access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr));
}

} // namespace

QHash<int, QString> DriveEnumerator::volumeLetters()
{
    QHash<int, QString> map;

    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (DWORD(1) << i)) == 0)
            continue;

        wchar_t root[8] = { L'\\', L'\\', L'.', L'\\', wchar_t(L'A' + i), L':', 0, 0 };
        wu::Handle handle(::CreateFileW(root, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, 0, nullptr));
        if (!handle.valid())
            continue;

        STORAGE_DEVICE_NUMBER sdn;
        ZeroMemory(&sdn, sizeof(sdn));
        DWORD returned = 0;
        if (::DeviceIoControl(handle.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER,
                              nullptr, 0, &sdn, sizeof(sdn), &returned, nullptr)) {
            QString& letters = map[int(sdn.DeviceNumber)];
            if (!letters.isEmpty())
                letters += QStringLiteral(", ");
            letters += QString(QChar(L'A' + i)) + QLatin1Char(':');
        }
    }
    return map;
}

void DriveEnumerator::probeDevice(DriveDescriptor* descriptor)
{
    if (!descriptor)
        return;

    // ---- unprivileged pass: model / serial / bus / capacity ----------------
    wu::Handle handle = openPhysicalDrive(descriptor->deviceNumber, 0);
    if (!handle.valid()) {
        const DWORD code = ::GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
            descriptor->accessNote = QStringLiteral("无法打开设备：%1").arg(wu::winError(code));
        return;
    }

    DWORD returned = 0;
    {
        std::vector<unsigned char> buffer(1024);
        if (::DeviceIoControl(handle.get(), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                              nullptr, 0, buffer.data(), DWORD(buffer.size()), &returned, nullptr)) {
            const DISK_GEOMETRY_EX* geometry = reinterpret_cast<const DISK_GEOMETRY_EX*>(buffer.data());
            descriptor->sizeBytes = quint64(geometry->DiskSize.QuadPart);
        }
    }

    {
        std::vector<unsigned char> buffer(2048, 0);
        STORAGE_PROPERTY_QUERY query;
        ZeroMemory(&query, sizeof(query));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        if (::DeviceIoControl(handle.get(), IOCTL_STORAGE_QUERY_PROPERTY,
                              &query, sizeof(query), buffer.data(), DWORD(buffer.size()),
                              &returned, nullptr)) {
            const STORAGE_DEVICE_DESCRIPTOR* desc =
                reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
            const char* base = reinterpret_cast<const char*>(buffer.data());
            if (desc->VendorIdOffset)
                descriptor->vendor = wu::fixedAscii(base + desc->VendorIdOffset);
            if (desc->ProductIdOffset)
                descriptor->model = wu::fixedAscii(base + desc->ProductIdOffset);
            if (desc->ProductRevisionOffset)
                descriptor->revision = wu::fixedAscii(base + desc->ProductRevisionOffset);
            if (desc->SerialNumberOffset)
                descriptor->serial = wu::fixedAscii(base + desc->SerialNumberOffset);
            descriptor->busType = int(desc->BusType);
            descriptor->busName = wu::busName(descriptor->busType);
            descriptor->removable = desc->RemovableMedia != 0;
            descriptor->queried = true;
        }
    }

    handle.close();

    // ---- media type inference ---------------------------------------------
    if (descriptor->busType == bus::Nvme) {
        descriptor->solidState = true;
        descriptor->mediaKnown = true;
        descriptor->rpmSource = QStringLiteral("NVMe 总线");
    }

    // ---- media type and nominal RPM ---------------------------------------
    // There is no privileged pass any more: the shipped build runs with the
    // rights of the invoking user, so ATA IDENTIFY — the only authoritative
    // source of a spindle's nominal rate — is out of reach, and so is S.M.A.R.T.
    // What follows is a model-string guess, and `rpmSource` says exactly that on
    // screen instead of dressing it up as a hardware reading.
    if (!descriptor->mediaKnown) {
        const int hint = SmartProbe::solidStateHint(descriptor->vendor, descriptor->model);
        if (hint == 1) {
            descriptor->solidState = true;
            descriptor->mediaKnown = true;
            descriptor->rpmSource = QStringLiteral("型号推断");
        } else if (hint == 0) {
            descriptor->solidState = false;
            descriptor->mediaKnown = true;
        } else if (descriptor->busType == bus::Sata || descriptor->busType == bus::Ata ||
                   descriptor->busType == bus::Usb || descriptor->busType == bus::Raid) {
            // Most likely rotating media; keep mediaKnown false so the UI can
            // say "按机械盘处理" instead of pretending to know.
            descriptor->solidState = false;
        }
    }

    if (!descriptor->solidState && descriptor->nominalRpm <= 0) {
        const int rpm = SmartProbe::rpmFromModelName(descriptor->vendor, descriptor->model);
        if (rpm > 0) {
            descriptor->nominalRpm = rpm;
            descriptor->rpmSource = QStringLiteral("型号推断（非硬件读数）");
        }
    }

    descriptor->vendor = descriptor->vendor.trimmed();
    descriptor->model = descriptor->model.trimmed();
}

QVector<DriveDescriptor> DriveEnumerator::enumerate(const QVector<int>& candidates, QString* note)
{
    QSet<int> unique;
    if (candidates.isEmpty()) {
        for (int i = 0; i < 8; ++i)
            unique.insert(i);
    } else {
        for (int c : candidates) {
            if (c >= 0)
                unique.insert(c);
        }
    }

    QList<int> sorted = unique.toList();
    std::sort(sorted.begin(), sorted.end());

    const QHash<int, QString> letters = volumeLetters();

    QVector<DriveDescriptor> result;

    for (int device : sorted) {
        DriveDescriptor descriptor;
        descriptor.deviceNumber = device;
        descriptor.devicePath = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(device);
        descriptor.letters = letters.value(device);

        probeDevice(&descriptor);

        if (!descriptor.queried && descriptor.letters.isEmpty() && !descriptor.accessNote.isEmpty())
            continue;   // nothing at all -> device does not exist
        if (!descriptor.queried && descriptor.letters.isEmpty() && descriptor.accessNote.isEmpty())
            continue;

        result.append(descriptor);
    }

    if (note) {
        if (result.isEmpty())
            *note = QStringLiteral("未发现任何物理磁盘");
        else
            note->clear();
    }

    return result;
}
