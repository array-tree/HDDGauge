#pragma once

#include "DiskTypes.h"

#include <QHash>
#include <QString>
#include <QVector>

/// Builds the list of physical drives the UI can pick from.
///
/// Design note: the drive list is driven by the set of PDH "PhysicalDisk"
/// instances, because those are the exact keys used for the live counters. A
/// query-only handle (desired access == 0) can then be opened on
/// \\.\PhysicalDriveN without elevation to enrich each entry with model,
/// serial, bus type and capacity (IOCTL_DISK_GET_DRIVE_GEOMETRY_EX works with
/// zero access; IOCTL_DISK_GET_LENGTH_INFO does not).
class DriveEnumerator
{
public:
    /// physical drive number -> "C:, D:"
    static QHash<int, QString> volumeLetters();

    /// Probe every candidate. An empty candidate list means "scan 0..7".
    static QVector<DriveDescriptor> enumerate(const QVector<int>& candidates, QString* note);

    /// Fill in the fields of a single descriptor (used by both enumerate() and
    /// the self-test path).
    static void probeDevice(DriveDescriptor* descriptor);
};
