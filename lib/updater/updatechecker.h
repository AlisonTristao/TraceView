#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <optional>

#include "updater/semver.h"
#include "updater/updateinfo.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace traceview {

// The result of comparing one GitHub /releases LIST response against a local
// version: either nothing newer was found (hasUpdate false, info unset) or
// info describes the update.
struct CheckResult {
    bool hasUpdate = false;
    UpdateInfo info;
};

// Parses `body` (the raw JSON array GitHub's releases LIST endpoint returns)
// and compares its first (newest) entry's tag against `localVersion`. Returns
// std::nullopt (with `errorReason` set, if given) when the body has no
// releases or the first entry's tag doesn't parse as a version -- both are
// treated as a check failure by UpdateChecker, not "up to date". Kept as a
// free function, separate from UpdateChecker's own network round trip, so
// this parsing/comparison logic is unit-testable without a live server -- see
// tests/test_updatechecker.cpp.
std::optional<CheckResult> parseReleasesResponse(const QByteArray& body,
                                                  const SemVer& localVersion,
                                                  QString* errorReason = nullptr);

// Polls GitHub Releases for a newer TraceView build than this one.
//
// Uses GET /repos/AlisonTristao/TraceView/releases (the LIST endpoint) and
// reads only the first entry, not GET .../releases/latest -- GitHub's
// "latest" endpoint explicitly ignores prereleases and drafts, and the first
// releases published with this feature are marked prerelease while the
// mechanism itself is validated in practice (see CONTRIBUTING.md). The list
// is already sorted newest-first regardless of that flag, so this keeps
// working unchanged once releases stop being marked prerelease.
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // Starts an async check. Resolves to exactly one of the signals below,
    // including on a network error -- callers never need a timeout of their
    // own. A check already in flight is not restarted; the caller gets the
    // one signal the request already running will emit.
    void checkForUpdates();

    bool checkInFlight() const;

signals:
    // info.version is newer than traceview::kVersion.
    void updateAvailable(const UpdateInfo& info);
    // The latest published release is not newer than this build.
    void upToDate();
    void checkFailed(const QString& reason);

private:
    QNetworkAccessManager* m_manager;
    QNetworkReply* m_reply = nullptr;
};

}  // namespace traceview
