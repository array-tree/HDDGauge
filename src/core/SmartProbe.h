#pragma once

#include <QString>
#include <climits>

/// Everything S.M.A.R.T. / ATA IDENTIFY could tell us about one drive.
struct SmartResult
{
    bool    attempted = false;
    bool    ok = false;
    bool    needsElevation = false;
    bool    nonRotating = false;
    int     nominalRpm = 0;          // 0 => not reported
    bool    rpmKnown = false;
    int     temperatureC = INT_MIN;
    bool    temperatureKnown = false;
    int     powerOnHours = -1;
    int     reallocatedSectors = -1;
    int     pendingSectors = -1;
    QString error;
};

/// ATA pass-through access to physical drives.
///
/// ATA IDENTIFY DEVICE word 217 is the only authoritative source of the
/// spindle's *nominal* rotation rate; S.M.A.R.T. attribute 194 gives the
/// temperature. Both require an elevated process.
class SmartProbe
{
public:
    static SmartResult query(int deviceNumber);

    /// Best-effort nominal RPM when ATA IDENTIFY is unavailable (USB bridges,
    /// virtual disks, no elevation). Returns 0 when nothing can be inferred.
    static int rpmFromModelName(const QString& vendor, const QString& model);

    /// Cheap SSD/HDD guess purely from the model string.
    static int solidStateHint(const QString& vendor, const QString& model);
};
