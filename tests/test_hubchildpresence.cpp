#include <QtTest>

#include "devices/device.h"

using traceview::HubChildPresence;
using traceview::hubChildPresence;

namespace {

// The combine rule MainWindow::reconcileHubChildPresence applies once per tick
// for a connected hub child (device.h). MainWindow feeds it the two liveness
// signals it read that tick; this pins how they combine, debounce and stick.
//
// kDebounce mirrors the caller's kOfflineTicksToConfirm.
constexpr int kDebounce = 5;

// A tick: apply the rule, carrying the previous verdict forward.
HubChildPresence step(bool frameFresh, bool hubReadable, bool hubOnline,
                      const HubChildPresence& prev) {
    return hubChildPresence(frameFresh, hubReadable, hubOnline, prev, kDebounce);
}

class TestHubChildPresence : public QObject {
    Q_OBJECT

private slots:
    void freshConnectIsUnknownNotOnline();
    void robotFrameAloneIsLiveAndKnown();
    void hubPeersFallbackWhenNoFrame();
    void hubPeersOfflineIsKnownButNotOnline();
    void losingEverythingIsDebouncedThenAmber();
    void knownIsStickyAcrossASilence();
    void staleHubViewCannotManufactureOnline();
    void frameOverridesAHubOfflineVerdict();
};

// Just connected: no frame has arrived and the hub.peers view is not readable
// yet. Not "live" -- that would be a green dot on no evidence -- and not
// "known" either, so the card says "locating" rather than "no data".
void TestHubChildPresence::freshConnectIsUnknownNotOnline() {
    const HubChildPresence v = step(/*frame=*/false, /*hubReadable=*/false, /*hubOnline=*/false, {});
    QVERIFY(!v.online);
    QVERIFY(!v.known);
    QCOMPARE(v.offlineTicks, 1);
}

// A frame from the robot passed AEAD open this tick: end-to-end proof, no
// dependence on the dongle's hub.peers view at all.
void TestHubChildPresence::robotFrameAloneIsLiveAndKnown() {
    const HubChildPresence v = step(/*frame=*/true, /*hubReadable=*/false, /*hubOnline=*/false, {});
    QVERIFY(v.online);
    QVERIFY(v.known);
    QCOMPARE(v.offlineTicks, 0);
}

// Nothing subscribed so no telemetry, but the dongle vouches for the robot.
void TestHubChildPresence::hubPeersFallbackWhenNoFrame() {
    const HubChildPresence v = step(/*frame=*/false, /*hubReadable=*/true, /*hubOnline=*/true, {});
    QVERIFY(v.online);
    QVERIFY(v.known);
}

// The dongle's view is readable and says offline: a verdict (known), just not
// a live one. Never escalates past amber here -- red is the caller's job and
// only when the dongle link itself drops.
void TestHubChildPresence::hubPeersOfflineIsKnownButNotOnline() {
    const HubChildPresence v = step(/*frame=*/false, /*hubReadable=*/true, /*hubOnline=*/false, {});
    QVERIFY(!v.online);
    QVERIFY(v.known);
}

// Established green, then both signals vanish (robot stopped AND the hub.peers
// view went unreadable): green holds for the debounce window, then drops.
void TestHubChildPresence::losingEverythingIsDebouncedThenAmber() {
    HubChildPresence v = step(true, true, true, {});
    QVERIFY(v.online);

    for (int tick = 1; tick < kDebounce; ++tick) {
        v = step(false, false, false, v);
        QVERIFY2(v.online, qPrintable(QString("still green at debounce tick %1").arg(tick)));
    }
    v = step(false, false, false, v);
    QVERIFY(!v.online);   // debounce spent
    QVERIFY(v.known);     // ...but we still know it -- "no data", not "locating"
}

// Once any verdict has been reached, `known` never falls back to false, so the
// card never returns to "locating" after a mid-session silence.
void TestHubChildPresence::knownIsStickyAcrossASilence() {
    HubChildPresence v = step(true, false, false, {});
    QVERIFY(v.known);
    for (int i = 0; i < 20; ++i) {
        v = step(false, false, false, v);
    }
    QVERIFY(v.known);
    QVERIFY(!v.online);
}

// The debounce grace only protects an already-established online. Starting
// from "never online", a readable-but-offline hub view stays offline every
// tick -- the grace must not paint a transient green.
void TestHubChildPresence::staleHubViewCannotManufactureOnline() {
    HubChildPresence v{};
    for (int i = 0; i < kDebounce + 2; ++i) {
        v = step(false, true, false, v);
        QVERIFY2(!v.online, qPrintable(QString("must not flash green at tick %1").arg(i)));
    }
}

// The dongle says offline (its channel-C STATUS view can lag or flake) but the
// robot's own sealed frames are arriving: the frame wins, the dot is green.
void TestHubChildPresence::frameOverridesAHubOfflineVerdict() {
    const HubChildPresence v = step(/*frame=*/true, /*hubReadable=*/true, /*hubOnline=*/false, {});
    QVERIFY(v.online);
    QVERIFY(v.known);
    QCOMPARE(v.offlineTicks, 0);
}

}  // namespace

QTEST_MAIN(TestHubChildPresence)
#include "test_hubchildpresence.moc"
