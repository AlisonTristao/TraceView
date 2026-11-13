#include <QSignalSpy>
#include <QtTest>

#include "theme/doublespinbox.h"

using traceview::DoubleSpinBox;

class TestDoubleSpinBox : public QObject {
    Q_OBJECT

private slots:
    void acceptsBothDecimalSeparators_data();
    void acceptsBothDecimalSeparators();
    void suffixKeepsItsOwnSeparator();
    void stepsAdaptively();
    void commitsOnlyWhenEditingFinishes();
};

namespace {

// Types `text` over the whole field and commits it with Enter.
void typeAndCommit(DoubleSpinBox& spin, const QString& text) {
    spin.selectAll();
    QTest::keyClicks(&spin, text);
    QTest::keyClick(&spin, Qt::Key_Return);
}

}  // namespace

void TestDoubleSpinBox::acceptsBothDecimalSeparators_data() {
    QTest::addColumn<QString>("localeName");
    QTest::addColumn<QString>("typed");

    QTest::newRow("en_US, dot") << "en_US" << "0.5";
    QTest::newRow("en_US, comma") << "en_US" << "0,5";
    QTest::newRow("pt_BR, dot") << "pt_BR" << "0.5";
    QTest::newRow("pt_BR, comma") << "pt_BR" << "0,5";
}

void TestDoubleSpinBox::acceptsBothDecimalSeparators() {
    QFETCH(QString, localeName);
    QFETCH(QString, typed);

    DoubleSpinBox spin;
    spin.setLocale(QLocale(localeName));
    spin.setRange(-100'000.0, 100'000.0);
    spin.setDecimals(2);
    spin.setValue(100.0);

    typeAndCommit(spin, typed);
    QCOMPARE(spin.value(), 0.5);
}

void TestDoubleSpinBox::suffixKeepsItsOwnSeparator() {
    DoubleSpinBox spin;
    spin.setLocale(QLocale(QStringLiteral("en_US")));
    spin.setRange(0.0, 100.0);
    spin.setDecimals(2);
    spin.setSuffix(QStringLiteral(" m,s"));

    typeAndCommit(spin, QStringLiteral("1,25"));
    QCOMPARE(spin.value(), 1.25);
    QVERIFY(spin.text().endsWith(QStringLiteral(" m,s")));
}

void TestDoubleSpinBox::stepsAdaptively() {
    DoubleSpinBox spin;
    spin.setRange(-100'000.0, 100'000.0);
    spin.setDecimals(2);

    spin.setValue(1.0);
    spin.stepUp();
    QCOMPARE(spin.value(), 1.1);

    spin.setValue(0.5);
    spin.stepUp();
    QCOMPARE(spin.value(), 0.51);

    spin.setValue(100.0);
    spin.stepUp();
    QCOMPARE(spin.value(), 110.0);
}

void TestDoubleSpinBox::commitsOnlyWhenEditingFinishes() {
    DoubleSpinBox spin;
    spin.setLocale(QLocale(QStringLiteral("en_US")));
    spin.setRange(-100'000.0, 100'000.0);
    spin.setDecimals(2);
    spin.setValue(100.0);
    QSignalSpy changed(&spin, &QDoubleSpinBox::valueChanged);

    spin.selectAll();
    QTest::keyClicks(&spin, QStringLiteral("0.5"));
    QCOMPARE(changed.count(), 0);

    QTest::keyClick(&spin, Qt::Key_Return);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().first().toDouble(), 0.5);
}

QTEST_MAIN(TestDoubleSpinBox)
#include "test_doublespinbox.moc"
