#include "ConfigTextUtils.h"

#include <QLatin1Char>

#include <algorithm>

namespace ConfigTextUtils {

QString stripInlineComment(QString line)
{
    const int hashPos = line.indexOf(QLatin1Char('#'));
    const int semicolonPos = line.indexOf(QLatin1Char(';'));
    int cutPos = -1;
    if (hashPos >= 0) {
        cutPos = hashPos;
    }
    if (semicolonPos >= 0) {
        cutPos = cutPos < 0 ? semicolonPos : std::min(cutPos, semicolonPos);
    }
    return cutPos >= 0 ? line.left(cutPos).trimmed() : line.trimmed();
}

} // namespace ConfigTextUtils
