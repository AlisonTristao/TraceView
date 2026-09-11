#include "updater/semver.h"

#include <QRegularExpression>

namespace traceview {

std::optional<SemVer> parseSemVer(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^v?(\\d+)\\.(\\d+)\\.(\\d+)"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    SemVer version;
    version.major = match.captured(1).toInt();
    version.minor = match.captured(2).toInt();
    version.patch = match.captured(3).toInt();
    return version;
}

}  // namespace traceview
