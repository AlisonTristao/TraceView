#include <QApplication>
#include <QLineEdit>
#include <QtTest>

#include "core/keyboardreshowguard.h"

namespace {

// Records autoSipEnabled() as QLineEdit's own key handlers would see it --
// keyReleaseEvent() asks for the soft keyboard only while it is on.
class RecordingLineEdit : public QLineEdit {
public:
    bool sipOnPress = false;
    bool sipOnRelease = true;

protected:
    void keyPressEvent(QKeyEvent* event) override {
        sipOnPress = qApp->autoSipEnabled();
        QLineEdit::keyPressEvent(event);
    }
    void keyReleaseEvent(QKeyEvent* event) override {
        sipOnRelease = qApp->autoSipEnabled();
        QLineEdit::keyReleaseEvent(event);
    }
};

class TestKeyboardReshowGuard : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qApp->setAutoSipEnabled(true);
        traceview::installKeyboardReshowGuard();
    }

    void keyReleaseDoesNotAskForTheKeyboardAgain() {
        RecordingLineEdit edit;
        edit.setText(QStringLiteral("abc"));

        QTest::keyClick(&edit, Qt::Key_Backspace);

        QCOMPARE(edit.text(), QStringLiteral("ab"));
        QVERIFY(edit.sipOnPress);
        QVERIFY(!edit.sipOnRelease);
    }

    void autoSipComesBackAfterTheRelease() {
        RecordingLineEdit edit;

        QTest::keyClick(&edit, Qt::Key_CapsLock);
        QVERIFY(!edit.sipOnRelease);
        QCoreApplication::processEvents();

        // A tap on a field still raises the keyboard through autoSip.
        QVERIFY(qApp->autoSipEnabled());
    }
};

}  // namespace

QTEST_MAIN(TestKeyboardReshowGuard)
#include "test_keyboardreshowguard.moc"
