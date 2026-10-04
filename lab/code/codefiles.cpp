#include "codefiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace tracelab::code {

namespace {

constexpr char kOriginPrefix[] = "// tracelab: template ";

QString normalized(QString text) {
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    while (text.endsWith(QLatin1Char('\n'))) {
        text.chop(1);
    }
    return text;
}

QString withoutOrigin(const QString& source) {
    if (!source.startsWith(QLatin1String(kOriginPrefix))) {
        return source;
    }
    const int newline = source.indexOf(QLatin1Char('\n'));
    return newline < 0 ? QString() : source.mid(newline + 1);
}

}  // namespace

CodeLibrary& CodeLibrary::instance() {
    static CodeLibrary library;
    return library;
}

void CodeLibrary::registerTemplate(const CodeTemplate& codeTemplate) {
    if (!findTemplate(codeTemplate.id)) {
        m_templates.append(codeTemplate);
    }
}

QVector<CodeTemplate> CodeLibrary::templates(BlockKind kind) const {
    QVector<CodeTemplate> list;
    for (const CodeTemplate& t : m_templates) {
        if (t.kind == kind) {
            list.append(t);
        }
    }
    return list;
}

const CodeTemplate* CodeLibrary::findTemplate(const QString& id) const {
    for (const CodeTemplate& t : m_templates) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

QString CodeLibrary::templateSource(const QString& id) const {
    const CodeTemplate* t = findTemplate(id);
    return t ? withoutOrigin(readFile(t->resourcePath)) : QString();
}

QString CodeLibrary::skeletonSource(BlockKind kind) {
    return readFile(QStringLiteral(":/tracelab/code/skeletons/%1.c")
                        .arg(kind == BlockKind::Plant    ? QStringLiteral("plant")
                             : kind == BlockKind::Filter ? QStringLiteral("filter")
                                                         : QStringLiteral("controller")));
}

QString CodeLibrary::kindFolder(BlockKind kind) {
    switch (kind) {
        case BlockKind::Plant:
            return QStringLiteral("plants");
        case BlockKind::Filter:
            return QStringLiteral("filters");
        case BlockKind::Controller:
            break;
    }
    return QStringLiteral("controllers");
}

QString CodeLibrary::userDirectory(BlockKind kind) {
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QDir::homePath();
    }
    const QString path = documents + QStringLiteral("/TraceLab/") + kindFolder(kind);
    QDir().mkpath(path);
    return path;
}

QStringList CodeLibrary::userFiles(BlockKind kind) {
    QStringList paths;
    const QFileInfoList entries =
        QDir(userDirectory(kind)).entryInfoList({QStringLiteral("*.c")}, QDir::Files, QDir::Time);
    for (const QFileInfo& entry : entries) {
        paths.append(entry.absoluteFilePath());
    }
    return paths;
}

QString CodeLibrary::createFile(BlockKind kind, const QString& templateId, const QString& baseName,
                                QString* error) const {
    QString source;
    QString stem = baseName;
    if (templateId.isEmpty()) {
        source = skeletonSource(kind);
        if (stem.isEmpty()) {
            stem = kind == BlockKind::Plant    ? QStringLiteral("my_plant")
                   : kind == BlockKind::Filter ? QStringLiteral("my_filter")
                                               : QStringLiteral("my_controller");
        }
    } else {
        if (!findTemplate(templateId)) {
            if (error) {
                *error = QStringLiteral("unknown template %1").arg(templateId);
            }
            return QString();
        }
        source = withOrigin(templateSource(templateId), templateId);
        if (stem.isEmpty()) {
            stem = templateId.section(QLatin1Char('/'), -1);
        }
    }
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    const QString directory = userDirectory(kind);
    QString path = directory + QLatin1Char('/') + stem + QStringLiteral(".c");
    for (int n = 2; QFile::exists(path); ++n) {
        path = directory + QStringLiteral("/%1_%2.c").arg(stem).arg(n);
    }
    if (!writeFile(path, source, error)) {
        return QString();
    }
    return path;
}

QString CodeLibrary::originOf(const QString& source) {
    if (!source.startsWith(QLatin1String(kOriginPrefix))) {
        return QString();
    }
    const int newline = source.indexOf(QLatin1Char('\n'));
    return source.mid(int(sizeof kOriginPrefix) - 1,
                      (newline < 0 ? source.size() : newline) - (int(sizeof kOriginPrefix) - 1))
        .trimmed();
}

QString CodeLibrary::withOrigin(const QString& source, const QString& templateId) {
    const QString body = withoutOrigin(source);
    if (templateId.isEmpty()) {
        return body;
    }
    return QLatin1String(kOriginPrefix) + templateId + QLatin1Char('\n') + body;
}

bool CodeLibrary::isModified(const QString& source) const {
    const QString id = originOf(source);
    if (id.isEmpty() || !findTemplate(id)) {
        return false;
    }
    return normalized(withoutOrigin(source)) != normalized(templateSource(id));
}

QString CodeLibrary::originalOf(const QString& source) const {
    const QString id = originOf(source);
    if (id.isEmpty() || !findTemplate(id)) {
        return QString();
    }
    return withOrigin(templateSource(id), id);
}

QString CodeLibrary::readFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool CodeLibrary::writeFile(const QString& path, const QString& text, QString* error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(text.toUtf8()) < 0 ||
        !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

}  // namespace tracelab::code
