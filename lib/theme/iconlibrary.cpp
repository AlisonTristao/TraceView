#include "iconlibrary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPixmapCache>
#include <algorithm>

#include "iconutils.h"

namespace traceview {

namespace {

constexpr char kLucidePrefix[] = "lucide:";
constexpr char kLucideResource[] = ":/icons/lucide/lucide.json";

// tools/import_lucide.py keeps only each icon's inner markup; this is the
// root every lucide-static SVG shares. The attributes sit on a <g> rather
// than the <svg> root because QSvgRenderer reliably inherits presentation
// attributes from a group (resources/icons/ribbon/*.svg do the same).
// `currentColor` in a few bodies (filled dots) is rewritten in body() below;
// the color itself is irrelevant, only the alpha shape survives tinting.
constexpr char kSvgHeader[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
    "<g fill=\"none\" stroke=\"#000000\" stroke-width=\"2\" stroke-linecap=\"round\" "
    "stroke-linejoin=\"round\">";
constexpr char kSvgFooter[] = "</g></svg>";

QString nameOf(const QString& id) {
    return id.startsWith(QLatin1String(kLucidePrefix)) ? id.mid(int(qstrlen(kLucidePrefix)))
                                                       : QString();
}

}  // namespace

IconLibrary& IconLibrary::instance() {
    static IconLibrary library;
    return library;
}

void IconLibrary::ensureLoaded() const {
    if (m_loaded) {
        return;
    }
    m_loaded = true;

    QFile file(QString::fromLatin1(kLucideResource));
    if (!file.open(QIODevice::ReadOnly)) {
        // Not linked in (e.g. a unit test against traceview_theme alone):
        // an empty library, every id unknown, every caller on its fallback.
        return;
    }
    const QJsonObject icons = QJsonDocument::fromJson(file.readAll()).object().value("icons").toObject();
    m_entries.reserve(icons.size());
    for (auto it = icons.constBegin(); it != icons.constEnd(); ++it) {
        const QJsonObject object = it.value().toObject();
        Entry entry;
        entry.name = it.key();
        for (const QJsonValue& tag : object.value("t").toArray()) {
            entry.tags.append(tag.toString());
        }
        entry.body = object.value("b").toString().toUtf8();
        entry.body.replace("currentColor", "#000000");
        m_entries.append(entry);
    }
    std::sort(m_entries.begin(), m_entries.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });
    for (int i = 0; i < m_entries.size(); ++i) {
        m_indexByName.insert(m_entries[i].name, i);
    }
}

const IconLibrary::Entry* IconLibrary::find(const QString& id) const {
    ensureLoaded();
    const auto it = m_indexByName.constFind(nameOf(id));
    return it == m_indexByName.constEnd() ? nullptr : &m_entries[it.value()];
}

QStringList IconLibrary::ids() const {
    ensureLoaded();
    QStringList result;
    result.reserve(m_entries.size());
    for (const Entry& entry : m_entries) {
        result.append(QLatin1String(kLucidePrefix) + entry.name);
    }
    return result;
}

bool IconLibrary::contains(const QString& id) const {
    return find(id) != nullptr;
}

QString IconLibrary::displayName(const QString& id) const {
    const Entry* entry = find(id);
    return entry ? QString(entry->name).replace(QLatin1Char('-'), QLatin1Char(' ')) : QString();
}

bool IconLibrary::matches(const QString& id, const QString& query) const {
    const Entry* entry = find(id);
    if (!entry) {
        return false;
    }
    const QStringList words = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(words.cbegin(), words.cend(), [entry](const QString& word) {
        if (entry->name.contains(word, Qt::CaseInsensitive)) {
            return true;
        }
        return std::any_of(entry->tags.cbegin(), entry->tags.cend(), [&word](const QString& tag) {
            return tag.contains(word, Qt::CaseInsensitive);
        });
    });
}

QPixmap IconLibrary::pixmap(const Entry& entry, const QColor& color, int size) const {
    const QString key = QStringLiteral("iconlibrary:%1:%2:%3")
                            .arg(entry.name)
                            .arg(color.rgba(), 0, 16)
                            .arg(size);
    QPixmap result;
    if (!QPixmapCache::find(key, &result)) {
        result = renderTintedSvg(kSvgHeader + entry.body + kSvgFooter, color, size);
        QPixmapCache::insert(key, result);
    }
    return result;
}

QIcon IconLibrary::icon(const QString& id, const QColor& color, int size) const {
    const Entry* entry = find(id);
    return entry ? QIcon(pixmap(*entry, color, size)) : QIcon();
}

QIcon IconLibrary::icon(const QString& id, const QColor& color, const QColor& checkedColor,
                        int size) const {
    const Entry* entry = find(id);
    if (!entry) {
        return QIcon();
    }
    QIcon result;
    result.addPixmap(pixmap(*entry, color, size), QIcon::Normal, QIcon::Off);
    result.addPixmap(pixmap(*entry, checkedColor, size), QIcon::Normal, QIcon::On);
    return result;
}

}  // namespace traceview
