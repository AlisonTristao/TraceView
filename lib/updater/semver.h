#pragma once

#include <QString>

#include <optional>

namespace traceview {

// A parsed MAJOR.MINOR.PATCH version, ignoring any leading "v" and any
// trailing pre-release/build metadata -- "v2.4.0-beta.1" and "2.4.0" both
// parse to {2, 4, 0}. GitHub release tag_names in this project follow
// CONTRIBUTING.md's "tag vx.y.z" convention, but the API response is never
// guaranteed clean input.
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

std::optional<SemVer> parseSemVer(const QString& text);

inline bool operator<(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return a.major < b.major;
    if (a.minor != b.minor) return a.minor < b.minor;
    return a.patch < b.patch;
}

inline bool operator==(const SemVer& a, const SemVer& b) {
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

}  // namespace traceview
