#include "codeblockowner.h"

#include <QLocale>
#include <QRegularExpression>
#include <cmath>

namespace tracelab {

namespace {

QString formatValue(double value) {
    if (std::isnan(value)) {
        return QStringLiteral("--");
    }
    const QLocale locale;
    const double magnitude = std::fabs(value);
    if (magnitude != 0.0 && (magnitude < 1e-3 || magnitude >= 1e5)) {
        return locale.toString(value, 'e', 2);
    }
    return locale.toString(value, 'f', magnitude < 10 ? 3 : 1);
}

}  // namespace

CodeBlockSpec CodeBlockSpec::fromJson(const QJsonObject& json) {
    CodeBlockSpec spec;
    spec.path = json.value(QStringLiteral("path")).toString();
    spec.templateId = json.value(QStringLiteral("template")).toString();
    spec.params = json.value(QStringLiteral("params")).toObject();
    return spec;
}

QJsonObject CodeBlockSpec::toJson() const {
    QJsonObject json;
    if (!path.isEmpty()) {
        json.insert(QStringLiteral("path"), path);
    } else if (!templateId.isEmpty()) {
        json.insert(QStringLiteral("template"), templateId);
    }
    if (!params.isEmpty()) {
        json.insert(QStringLiteral("params"), params);
    }
    return json;
}

void appendSignalRows(QVector<SignalRow>* rows, const QString& title, const QStringList& names,
                      const QVector<double>& values, const QString& scalarDeclaration,
                      const QString& arrayDeclaration, bool showValues) {
    if (names.isEmpty()) {
        return;
    }
    rows->append({title, QString(), true});
    static const QRegularExpression element(QStringLiteral("^(.*)\\[(\\d+)\\]$"));
    for (int i = 0; i < names.size();) {
        const QRegularExpressionMatch m = element.match(names[i]);
        QString name = names[i];
        int count = 1;
        bool array = false;
        if (m.hasMatch() && m.captured(2).toInt() == 0) {
            name = m.captured(1);
            array = true;
            while (i + count < names.size() &&
                   names[i + count] == QStringLiteral("%1[%2]").arg(name).arg(count)) {
                ++count;
            }
        }
        QString value;
        if (showValues) {
            QStringList parts;
            for (int e = 0; e < count; ++e) {
                parts.append(formatValue(values.value(i + e, qQNaN())));
                if (parts.size() == 4 && count > 5) {
                    parts.append(QStringLiteral("..."));
                    break;
                }
            }
            value = parts.join(QLatin1Char(' '));
        }
        const QString label = array ? QStringLiteral("%1[%2]").arg(name).arg(count) : name;
        const QString declaration =
            array ? arrayDeclaration.arg(name).arg(count) : scalarDeclaration.arg(name);
        rows->append({QStringLiteral("  %1  %2").arg(label, -10).arg(value), declaration, false});
        i += count;
    }
}

}  // namespace tracelab
