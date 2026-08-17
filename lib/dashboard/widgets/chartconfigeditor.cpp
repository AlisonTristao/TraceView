#include "chartconfigeditor.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kNameColumn = 0;
constexpr int kFieldIdColumn = 1;
constexpr int kColorColumn = 2;
constexpr int kStyleColumn = 3;
constexpr int kRemoveColumn = 4;
constexpr int kColumnCount = 5;

// String ids are what's persisted in the config JSON — stable across
// re-orderings of the combo items; the matching display labels are built
// locally in addSeriesRow() (via tr()) instead of a parallel static list,
// since translated text can't be produced at static-init time (no
// QCoreApplication/translators exist yet when namespace-scope objects like
// this one are constructed).
const QStringList kStyleIds = {"solid", "dashed", "dotted", "dashdot", "cross", "asterisk"};

// A spin box's up/down buttons sit in a chrome strip on the right that the
// stylesheet declares as `padding-right: 20px` (see stylesheet.cpp) — but
// with that QSS applied, Qt's CC_SpinBox reserves noticeably more than the
// declared 20px for that strip (confirmed by A/B testing this widget with
// the stylesheet on vs. off: the same computed width that fit every digit
// with room to spare unstyled clipped the last character once styled).
// kExtraChromeSlack is that measured gap between the declared and actual
// reservation, not a guess — drop it only after re-verifying against the
// stylesheet, since it's tied to that QSS's box model, not to Qt's own.
int spinBoxWidthFor(const QFont& font, const QString& widestText) {
    constexpr int kLeftPadding = 6;        // stylesheet.cpp: "padding: 3px 6px"
    constexpr int kButtonChrome = 20;      // stylesheet.cpp: "padding-right: 20px"
    constexpr int kBorder = 2;             // stylesheet.cpp: "border: 1px solid" (both sides)
    constexpr int kExtraChromeSlack = 34;  // measured Qt/QSS shortfall — see comment above
    return QFontMetrics(font).horizontalAdvance(widestText) + kLeftPadding + kButtonChrome + kBorder
           + kExtraChromeSlack;
}

// A small inline sub-label (e.g. "Ts", "Min") riding a shared row — kept to
// its own text width and explicitly vertically centered so it lines up with
// the combo/spin box beside it instead of drifting to the row's top edge.
QLabel* smallLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

QColor swatchColor(QPushButton* button) {
    return button->property("swatchColor").value<QColor>();
}

void setSwatchColor(QPushButton* button, const QColor& color) {
    button->setProperty("swatchColor", color);
    button->setStyleSheet(QString("background-color: %1;").arg(color.name()));
}

} // namespace

ChartConfigEditor::ChartConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_sourceIdEdit = new QLineEdit(this);
    m_sourceIdEdit->setPlaceholderText(tr("0x11223344"));
    m_sourceIdEdit->setToolTip(tr("BTP source_id this chart reads from (hex or decimal)."));

    m_topicIdEdit = new QLineEdit(this);
    m_topicIdEdit->setPlaceholderText(tr("0x0101"));
    m_topicIdEdit->setToolTip(tr("BTP topic_id (TELEMETRY.md) this chart's series bind fields of."));

    m_xAxisModeCombo = new QComboBox(this);
    m_xAxisModeCombo->addItem(tr("Samples"), "samples");
    m_xAxisModeCombo->addItem(tr("Time"), "time");

    m_sampleTimeLabel = smallLabel(tr("Ts"), this);
    m_sampleTimeSpin = new QDoubleSpinBox(this);
    m_sampleTimeSpin->setRange(0.001, 100'000.0);
    m_sampleTimeSpin->setDecimals(2);
    m_sampleTimeSpin->setValue(100.0);
    m_sampleTimeSpin->setSuffix(tr(" ms"));
    m_sampleTimeSpin->setFixedWidth(spinBoxWidthFor(m_sampleTimeSpin->font(), "100000.00 ms"));
    m_sampleTimeSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sampleTimeSpin->setToolTip(
        tr("Time between samples (Ts), not the time a frame arrives — elapsed time for N samples is Ts * N."));

    m_xLimitSpin = new QSpinBox(this);
    m_xLimitSpin->setRange(1, 1'000'000);
    m_xLimitSpin->setValue(500);
    // Real suffix is set by updateAxisRowsVisibility() based on X Axis
    // mode; "pts" here is just the longer of the two candidates, so the
    // fixed width below is sized for whichever one ends up showing.
    m_xLimitSpin->setSuffix(tr(" pts"));
    m_xLimitSpin->setFixedWidth(spinBoxWidthFor(m_xLimitSpin->font(), "1000000 pts"));
    m_xLimitSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_xLimitSpin->setToolTip(
        tr("How much history the chart keeps before older data scrolls off — in samples or seconds, matching X Axis."));

    m_yAxisModeCombo = new QComboBox(this);
    m_yAxisModeCombo->addItem(tr("Auto"), "auto");
    m_yAxisModeCombo->addItem(tr("Fixed"), "fixed");

    m_yMinSpin = new QDoubleSpinBox(this);
    m_yMinSpin->setRange(-100'000.0, 100'000.0);
    m_yMinSpin->setDecimals(2);
    m_yMinSpin->setValue(0.0);
    m_yMinSpin->setFixedWidth(spinBoxWidthFor(m_yMinSpin->font(), "-100000.00"));
    m_yMinSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_yMaxSpin = new QDoubleSpinBox(this);
    m_yMaxSpin->setRange(-100'000.0, 100'000.0);
    m_yMaxSpin->setDecimals(2);
    m_yMaxSpin->setValue(100.0);
    m_yMaxSpin->setFixedWidth(spinBoxWidthFor(m_yMaxSpin->font(), "-100000.00"));
    m_yMaxSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_yUnitEdit = new QLineEdit(this);
    m_yUnitEdit->setPlaceholderText(tr("V, °C, %..."));
    m_yUnitEdit->setToolTip(tr("Unit label shown alongside the Y axis."));

    m_gridCheck = new QCheckBox(this);
    m_gridCheck->setChecked(true);
    m_gridCheck->setToolTip(tr("Show the min/mid/max gridlines across the plot."));

    // Ts and Min/Max ride along their sibling field's row instead of each
    // owning a full row of their own. Every widget is vertically centered
    // explicitly so combo/label/spin line up on one baseline regardless of
    // the row's own height.
    auto* xAxisRow = new QHBoxLayout();
    xAxisRow->addWidget(m_xAxisModeCombo, 1, Qt::AlignVCenter);
    xAxisRow->addWidget(m_sampleTimeLabel, 0, Qt::AlignVCenter);
    xAxisRow->addWidget(m_sampleTimeSpin, 0, Qt::AlignVCenter);

    // Range has no combo to carry the stretch — both spin boxes stay capped
    // and a stretch after each pair spreads Min/Max evenly across the row
    // instead of leaving one dead gap at the end.
    m_yRangeRow = new QHBoxLayout();
    m_yRangeRow->addWidget(smallLabel(tr("Min"), this), 0, Qt::AlignVCenter);
    m_yRangeRow->addWidget(m_yMinSpin, 0, Qt::AlignVCenter);
    m_yRangeRow->addStretch(1);
    m_yRangeRow->addWidget(smallLabel(tr("Max"), this), 0, Qt::AlignVCenter);
    m_yRangeRow->addWidget(m_yMaxSpin, 0, Qt::AlignVCenter);
    m_yRangeRow->addStretch(1);

    m_formLayout = new QFormLayout();
    m_formLayout->setContentsMargins(0, 0, 0, 0);
    m_formLayout->setHorizontalSpacing(6);
    m_formLayout->setVerticalSpacing(6);
    // Without this, a field row holding a single widget (Y Axis, Unit) only
    // grows to fill the column on styles whose default policy already says
    // so — Fusion does, some platform styles don't. Forcing it here makes
    // every field fill its column the same way regardless of platform.
    m_formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->addRow(tr("Source"), m_sourceIdEdit);
    m_formLayout->addRow(tr("Topic"), m_topicIdEdit);
    m_formLayout->addRow(tr("X Axis"), xAxisRow);
    m_formLayout->addRow(tr("Limit"), m_xLimitSpin);
    m_formLayout->addRow(tr("Y Axis"), m_yAxisModeCombo);
    m_formLayout->addRow(tr("Range"), m_yRangeRow);
    m_formLayout->addRow(tr("Unit"), m_yUnitEdit);
    m_formLayout->addRow(tr("Grid"), m_gridCheck);

    auto* divider = new QFrame(this);
    divider->setObjectName("sectionDivider");
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);

    m_seriesTable = new QTableWidget(0, kColumnCount, this);
    m_seriesTable->setHorizontalHeaderLabels({tr("Name"), tr("Field ID"), tr("Color"), tr("Style"), ""});
    m_seriesTable->verticalHeader()->setVisible(false);
    // Every data column stays Interactive (the header's default) so the user
    // can drag any of them wider — Name included, for series with long
    // names — at the expense of the others. Only Remove is pinned to its
    // button's size: it never needs to grow or shrink.
    m_seriesTable->horizontalHeader()->setSectionResizeMode(kRemoveColumn, QHeaderView::ResizeToContents);
    m_seriesTable->setColumnWidth(kNameColumn, 150);
    m_seriesTable->setColumnWidth(kFieldIdColumn, spinBoxWidthFor(m_seriesTable->font(), "65535"));
    m_seriesTable->setColumnWidth(kColorColumn, 56);
    m_seriesTable->setColumnWidth(kStyleColumn, 110);
    m_seriesTable->setSelectionMode(QAbstractItemView::NoSelection);
    // Default row height leaves the combo/spin/swatch cell widgets cramped
    // (they're stretched to fill the row, so a taller row is a taller —
    // easier to read and click — widget).
    m_seriesTable->verticalHeader()->setDefaultSectionSize(36);
    m_seriesTable->setMinimumHeight(160);

    m_addSeriesButton = new QPushButton(tr("+ Add series"), this);
    connect(m_addSeriesButton, &QPushButton::clicked, this, [this]() {
        QJsonObject series;
        series["fieldId"] = m_seriesTable->rowCount() + 1;
        addSeriesRow(series);
        emitChanged();
    });

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_addSeriesButton);
    buttonRow->addStretch(1);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 8, 0, 0);
    mainLayout->addLayout(m_formLayout);
    mainLayout->addWidget(divider);
    mainLayout->addWidget(m_seriesTable);
    mainLayout->addLayout(buttonRow);

    connect(m_sourceIdEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_topicIdEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_xAxisModeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateAxisRowsVisibility();
        emitChanged();
    });
    connect(m_sampleTimeSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_xLimitSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_yAxisModeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateAxisRowsVisibility();
        emitChanged();
    });
    connect(m_yMinSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_yMaxSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_yUnitEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_gridCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
    connect(m_seriesTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) { emitChanged(); });

    updateAxisRowsVisibility();
}

void ChartConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;

    m_sourceIdEdit->setText(config.value("sourceId").toString("0"));
    m_topicIdEdit->setText(config.value("topicId").toString("0"));

    const QJsonObject xAxis = config.value("xAxis").toObject();
    m_xAxisModeCombo->setCurrentIndex(xAxis.value("mode").toString("samples") == "time" ? 1 : 0);
    m_sampleTimeSpin->setValue(xAxis.value("sampleTimeMs").toDouble(100.0));
    m_xLimitSpin->setValue(xAxis.value("limit").toInt(500));

    const QJsonObject yAxis = config.value("yAxis").toObject();
    m_yAxisModeCombo->setCurrentIndex(yAxis.value("mode").toString("auto") == "fixed" ? 1 : 0);
    m_yMinSpin->setValue(yAxis.value("min").toDouble(0.0));
    m_yMaxSpin->setValue(yAxis.value("max").toDouble(100.0));
    m_yUnitEdit->setText(yAxis.value("unit").toString());
    m_gridCheck->setChecked(yAxis.value("grid").toBool(true));

    m_seriesTable->setRowCount(0);
    for (const QJsonValue& value : config.value("series").toArray()) {
        addSeriesRow(value.toObject());
    }

    updateAxisRowsVisibility();
    m_updating = false;
}

QJsonObject ChartConfigEditor::config() const {
    QJsonObject cfg;
    cfg["sourceId"] = m_sourceIdEdit->text().trimmed().isEmpty() ? QStringLiteral("0") : m_sourceIdEdit->text();
    cfg["topicId"] = m_topicIdEdit->text().trimmed().isEmpty() ? QStringLiteral("0") : m_topicIdEdit->text();

    QJsonObject xAxis;
    xAxis["mode"] = m_xAxisModeCombo->currentData().toString();
    xAxis["sampleTimeMs"] = m_sampleTimeSpin->value();
    xAxis["limit"] = m_xLimitSpin->value();
    cfg["xAxis"] = xAxis;

    QJsonObject yAxis;
    yAxis["mode"] = m_yAxisModeCombo->currentData().toString();
    yAxis["min"] = m_yMinSpin->value();
    yAxis["max"] = m_yMaxSpin->value();
    yAxis["unit"] = m_yUnitEdit->text();
    yAxis["grid"] = m_gridCheck->isChecked();
    cfg["yAxis"] = yAxis;

    QJsonArray seriesArray;
    for (int row = 0; row < m_seriesTable->rowCount(); ++row) {
        QJsonObject series;
        const QTableWidgetItem* nameItem = m_seriesTable->item(row, kNameColumn);
        series["name"] = nameItem ? nameItem->text() : QString();

        if (auto* fieldIdSpin = qobject_cast<QSpinBox*>(m_seriesTable->cellWidget(row, kFieldIdColumn))) {
            series["fieldId"] = fieldIdSpin->value();
        }
        if (auto* colorButton = qobject_cast<QPushButton*>(m_seriesTable->cellWidget(row, kColorColumn))) {
            series["color"] = swatchColor(colorButton).name();
        }
        if (auto* styleCombo = qobject_cast<QComboBox*>(m_seriesTable->cellWidget(row, kStyleColumn))) {
            series["style"] = kStyleIds.value(styleCombo->currentIndex(), kStyleIds.first());
        }
        seriesArray.append(series);
    }
    cfg["series"] = seriesArray;
    return cfg;
}

void ChartConfigEditor::addSeriesRow(const QJsonObject& series) {
    const bool wasUpdating = m_updating;
    m_updating = true;

    const int row = m_seriesTable->rowCount();
    m_seriesTable->insertRow(row);

    auto* nameItem = new QTableWidgetItem(series.value("name").toString(tr("Series %1").arg(row + 1)));
    m_seriesTable->setItem(row, kNameColumn, nameItem);

    auto* fieldIdSpin = new QSpinBox();
    fieldIdSpin->setRange(0, 65535);
    fieldIdSpin->setValue(series.value("fieldId").toInt(row + 1));
    fieldIdSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(fieldIdSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    m_seriesTable->setCellWidget(row, kFieldIdColumn, fieldIdSpin);

    auto* colorButton = new QPushButton();
    colorButton->setFixedWidth(40);
    // No "color" key means this is a brand-new row (Add series button, or an
    // old save predating this field) -- default it to the theme's series
    // palette instead of a hardcoded blue so a new series' color already
    // fits the active theme and differs from the previous row. Existing rows
    // with an explicit color are untouched.
    const QVector<QColor>& seriesPalette = ThemeManager::instance().currentTheme().series;
    const QColor fallback = seriesPalette.isEmpty() ? QColor("#3B82F6") : seriesPalette[row % seriesPalette.size()];
    setSwatchColor(colorButton, QColor(series.value("color").toString(fallback.name())));
    connect(colorButton, &QPushButton::clicked, this, [this, colorButton]() {
        const QColor chosen = QColorDialog::getColor(swatchColor(colorButton), this, tr("Series Color"));
        if (!chosen.isValid()) {
            return;
        }
        setSwatchColor(colorButton, chosen);
        emitChanged();
    });
    m_seriesTable->setCellWidget(row, kColorColumn, colorButton);

    auto* styleCombo = new QComboBox();
    // Built locally (rather than a static list) so tr() can produce a real
    // translation — see the kStyleIds comment above for why.
    const QStringList styleLabels = {tr("Solid"),    tr("Dashed"), tr("Dotted"),
                                      tr("Dash-Dot"), tr("Cross"),  tr("Asterisk")};
    styleCombo->addItems(styleLabels);
    styleCombo->setCurrentIndex(qMax(0, kStyleIds.indexOf(series.value("style").toString("solid"))));
    connect(styleCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    m_seriesTable->setCellWidget(row, kStyleColumn, styleCombo);

    auto* removeButton = new QPushButton("✕");
    removeButton->setFixedWidth(28);
    removeButton->setToolTip(tr("Remove series"));
    connect(removeButton, &QPushButton::clicked, this, [this, removeButton]() {
        for (int r = 0; r < m_seriesTable->rowCount(); ++r) {
            if (m_seriesTable->cellWidget(r, kRemoveColumn) == removeButton) {
                m_seriesTable->removeRow(r);
                break;
            }
        }
        emitChanged();
    });
    m_seriesTable->setCellWidget(row, kRemoveColumn, removeButton);

    m_updating = wasUpdating;
}

void ChartConfigEditor::updateAxisRowsVisibility() {
    // Ts rides the X Axis row alongside a combo that must stay visible, so
    // it's shown/hidden directly rather than via the whole-row
    // QFormLayout::setRowVisible() used below for Min/Max (which own their
    // row outright).
    const bool timeMode = m_xAxisModeCombo->currentData().toString() == "time";
    m_sampleTimeLabel->setVisible(timeMode);
    m_sampleTimeSpin->setVisible(timeMode);
    // Limit stays visible either way — only the unit it's counting changes.
    // Its fixed width (see construction) is sized for " pts", the longer of
    // the two, so switching to " s" never needs a relayout.
    m_xLimitSpin->setSuffix(timeMode ? tr(" s") : tr(" pts"));

    m_formLayout->setRowVisible(m_yRangeRow, m_yAxisModeCombo->currentData().toString() == "fixed");
}

void ChartConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

} // namespace traceview
