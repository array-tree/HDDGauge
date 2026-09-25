#pragma once

#include <QString>

/// Model-string heuristics that stand in for what ATA IDENTIFY used to answer.
///
/// HddGauge deliberately runs with the rights of the invoking user, so ATA
/// pass-through — the only authoritative source of a spindle's nominal rotation
/// rate, and of S.M.A.R.T. attribute 194 for temperature — is out of reach. The
/// build no longer embeds a requireAdministrator manifest and no longer asks for
/// elevation, so everything here is a best-effort guess. The UI labels it that
/// way ("型号推断（非硬件读数）") rather than passing it off as a measurement.
class SmartProbe
{
public:
    /// Best-effort nominal RPM from the model string. Returns 0 when nothing can
    /// be inferred.
    static int rpmFromModelName(const QString& vendor, const QString& model);

    /// Cheap SSD/HDD guess purely from the model string.
    static int solidStateHint(const QString& vendor, const QString& model);
};
