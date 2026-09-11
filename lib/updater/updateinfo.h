#pragma once

#include <QString>
#include <QUrl>

namespace traceview {

// What UpdateChecker found on GitHub Releases for this platform. assetUrl/
// assetName/assetSize describe the one release asset matching this OS (see
// UpdateChecker's asset-picking loop) -- Windows picks the "-windows-*.exe"
// asset, Linux the "-linux-*.tar.gz" one. An invalid assetUrl means the
// release has no asset for this platform (nothing to download; only
// releaseUrl is useful then). checksumsUrl is the release's SHA256SUMS.txt,
// used by UpdateDownloader to verify assetUrl before anything is installed.
struct UpdateInfo {
    QString version;        // e.g. "2.5.0", without the tag's leading "v"
    QString releaseNotes;   // the release body, Markdown as GitHub stores it
    QUrl releaseUrl;        // the release's own GitHub page (html_url)
    QUrl assetUrl;          // browser_download_url of the matching installer/archive
    QString assetName;
    qint64 assetSize = 0;
    QUrl checksumsUrl;      // browser_download_url of SHA256SUMS.txt
    bool prerelease = false;
};

}  // namespace traceview
