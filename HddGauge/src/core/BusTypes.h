#pragma once

/// Numeric STORAGE_BUS_TYPE values.
///
/// MinGW 5.3's winioctl.h stops at BusTypeFileBackedVirtual (15) and does not
/// declare BusTypeSpaces / BusTypeNvme / BusTypeSCM / BusTypeUfs, so the enum
/// is reproduced here rather than depending on the SDK vintage.
namespace bus {

enum Type {
    Unknown            = 0,
    Scsi               = 1,
    Atapi              = 2,
    Ata                = 3,
    Ieee1394           = 4,
    Ssa                = 5,
    Fibre              = 6,
    Usb                = 7,
    Raid               = 8,
    IScsi              = 9,
    Sas                = 10,
    Sata               = 11,
    Sd                 = 12,
    Mmc                = 13,
    Virtual            = 14,
    FileBackedVirtual  = 15,
    Spaces             = 16,
    Nvme               = 17,
    Scm                = 18,
    Ufs                = 19
};

} // namespace bus
