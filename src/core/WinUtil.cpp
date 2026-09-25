#include "WinUtil.h"
#include "BusTypes.h"

#include <QCoreApplication>

namespace wu {

QString winError(DWORD code)
{
    if (code == 0)
        return QStringLiteral("成功");
    LPWSTR buffer = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text;
    if (n && buffer) {
        text = QString::fromWCharArray(buffer, int(n)).trimmed();
        LocalFree(buffer);
    }
    if (text.isEmpty())
        text = QStringLiteral("未知错误");
    return QStringLiteral("%1 (0x%2)").arg(text, QString::number(code, 16).rightJustified(8, QLatin1Char('0')).toUpper());
}

QString lastError()
{
    return winError(::GetLastError());
}

QString fixedAscii(const char* s)
{
    if (!s)
        return QString();
    QString out = QString::fromLatin1(s);
    while (!out.isEmpty() && (out.at(out.size() - 1) == QLatin1Char(' ') || out.at(out.size() - 1) == QLatin1Char('\0')))
        out.chop(1);
    return out.trimmed();
}

QString busName(int busType)
{
    switch (busType) {
    case bus::Scsi:              return QStringLiteral("SCSI");
    case bus::Atapi:             return QStringLiteral("ATAPI");
    case bus::Ata:               return QStringLiteral("ATA");
    case bus::Ieee1394:          return QStringLiteral("1394");
    case bus::Ssa:               return QStringLiteral("SSA");
    case bus::Fibre:             return QStringLiteral("Fibre");
    case bus::Usb:               return QStringLiteral("USB");
    case bus::Raid:              return QStringLiteral("RAID");
    case bus::IScsi:             return QStringLiteral("iSCSI");
    case bus::Sas:               return QStringLiteral("SAS");
    case bus::Sata:              return QStringLiteral("SATA");
    case bus::Sd:                return QStringLiteral("SD");
    case bus::Mmc:               return QStringLiteral("MMC");
    case bus::Virtual:           return QStringLiteral("Virtual");
    case bus::FileBackedVirtual: return QStringLiteral("虚拟磁盘");
    case bus::Spaces:            return QStringLiteral("存储空间");
    case bus::Nvme:              return QStringLiteral("NVMe");
    case bus::Scm:               return QStringLiteral("SCM");
    case bus::Ufs:               return QStringLiteral("UFS");
    default:                     return QStringLiteral("未知(%1)").arg(busType);
    }
}

bool isElevated()
{
    BOOL admin = FALSE;
    PSID sid = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (::AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                   0, 0, 0, 0, 0, 0, &sid)) {
        ::CheckTokenMembership(nullptr, sid, &admin);
        ::FreeSid(sid);
    }
    return admin != FALSE;
}

QString formatBytes(quint64 bytes)
{
    const double b = double(bytes);
    const double gb = b / 1024.0 / 1024.0 / 1024.0;
    if (gb >= 1000.0)
        return QStringLiteral("%1 TB").arg(gb / 1024.0, 0, 'f', 2);
    if (gb >= 1.0)
        return QStringLiteral("%1 GB").arg(gb, 0, 'f', 2);
    return QStringLiteral("%1 MB").arg(b / 1024.0 / 1024.0, 0, 'f', 1);
}

QString formatRate(double bytesPerSecond)
{
    const double mb = bytesPerSecond / 1024.0 / 1024.0;
    if (mb >= 1000.0)
        return QStringLiteral("%1 GB/s").arg(mb / 1024.0, 0, 'f', 2);
    if (mb >= 10.0)
        return QStringLiteral("%1 MB/s").arg(mb, 0, 'f', 1);
    if (mb >= 0.1)
        return QStringLiteral("%1 MB/s").arg(mb, 0, 'f', 2);
    return QStringLiteral("%1 KB/s").arg(bytesPerSecond / 1024.0, 0, 'f', 1);
}

} // namespace wu
