#include "applogic/Version.h"

#include "gayospace_version.h"

namespace gayospace {

QString versionString()
{
    return QStringLiteral("%1 (build %2)").arg(
        QLatin1String(kVersion), QLatin1String(kBuildId));
}

}  // namespace gayospace
