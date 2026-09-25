#include "SmartProbe.h"

#include <QLatin1String>

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
