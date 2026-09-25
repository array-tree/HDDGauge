#pragma once

#include <QString>

namespace SelfTest {

/// Runs the whole collection pipeline head-less and returns a text report.
/// Used by `HddGauge.exe --selftest [report.txt]` so the data layer can be
/// verified without a visible window or an interactive session.
int run(const QString& reportPath);

} // namespace SelfTest
