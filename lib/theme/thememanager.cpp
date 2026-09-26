#include "traceview/thememanager.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

#include "palettes.h"
#include "stylesheet.h"

namespace traceview {

namespace {
constexpr const char* kSettingsKey = "appearance/theme";
constexpr const char* kDefaultThemeId = "dark";
constexpr const char* kFrameSettingsKey = "appearance/frameStyle";
constexpr const char* kCustomPalettesKey = "appearance/customPalettes";
constexpr const char* kDataColorsKey = "appearance/dataColors";
constexpr const char* kDensityKey = "appearance/density";
constexpr const char* kCardHeaderKey = "appearance/cardHeader";
constexpr const char* kCanvasKey = "appearance/canvas";
constexpr const char* kMotionKey = "appearance/motion";
}  // namespace

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager() {
    registerTheme(makeDarkPalette());
    registerTheme(makeLightPalette());
    registerTheme(makeWoodPalette());
    registerTheme(makeBlackPalette());
    registerTheme(makeMatrixPalette());
    registerTheme(makeSynthwavePalette());
    registerTheme(makeAmberPalette());
    registerTheme(makeArcticPalette());
    registerTheme(makeSakuraPalette());

    const QSettings settings;
    const QJsonArray custom =
        QJsonDocument::fromJson(settings.value(kCustomPalettesKey).toByteArray()).array();
    for (const QJsonValue& value : custom) {
        const ThemePalette palette = paletteFromJson(value.toObject(), makeDarkPalette());
        if (isCustomPaletteId(palette.id)) {
            registerTheme(palette);
        }
    }

    const QString savedId = settings.value(kSettingsKey, kDefaultThemeId).toString();
    const int idx = indexOf(savedId);
    m_currentIndex = idx >= 0 ? idx : 0;
    m_frameStyle = frameStyleFromId(settings.value(kFrameSettingsKey).toString());
    m_dataColors = dataColorsFromId(settings.value(kDataColorsKey).toString());
    m_density = densityFromId(settings.value(kDensityKey).toString());
    m_cardHeader = cardHeaderFromId(settings.value(kCardHeaderKey).toString());
    m_canvas = canvasFromId(settings.value(kCanvasKey).toString());
    m_motion = motionFromId(settings.value(kMotionKey).toString());
}

const ThemePalette& ThemeManager::currentTheme() const {
    return m_themes[m_currentIndex];
}

QVector<ThemePalette> ThemeManager::availableThemes() const {
    return m_themes;
}

void ThemeManager::registerTheme(const ThemePalette& palette) {
    if (indexOf(palette.id) >= 0) {
        return;
    }
    m_themes.append(palette);
}

void ThemeManager::setTheme(const QString& id) {
    const int idx = indexOf(id);
    if (idx < 0) {
        return;
    }
    m_currentIndex = idx;

    QSettings settings;
    settings.setValue(kSettingsKey, id);

    applyCurrentTheme();
    emit appearanceChanged();
}

const FrameStyle& ThemeManager::currentFrameStyle() const {
    return frameStyle(m_frameStyle);
}

void ThemeManager::setFrameStyle(const QString& id) {
    const FrameStyleId style = frameStyleFromId(id);
    if (style == m_frameStyle) {
        return;
    }
    m_frameStyle = style;
    QSettings settings;
    settings.setValue(kFrameSettingsKey, frameStyleIdString(style));
    applyCurrentTheme();
    emit frameStyleChanged();
    emit appearanceChanged();
}

void ThemeManager::commitAppearance(const char* key, const QString& value) {
    QSettings settings;
    settings.setValue(key, value);
    applyCurrentTheme();
    emit appearanceChanged();
}

void ThemeManager::setDataColors(const QString& id) {
    const DataColorsId value = dataColorsFromId(id);
    if (value != m_dataColors) {
        m_dataColors = value;
        commitAppearance(kDataColorsKey, dataColorsIdString(value));
    }
}

QColor ThemeManager::seriesColor(int index, const QColor& own) const {
    return dataSeriesColor(m_dataColors, index, own, currentTheme());
}

const Density& ThemeManager::currentDensity() const {
    return density(m_density);
}

void ThemeManager::setDensity(const QString& id) {
    const DensityId value = densityFromId(id);
    if (value != m_density) {
        m_density = value;
        commitAppearance(kDensityKey, densityIdString(value));
    }
}

CardHeaderId ThemeManager::cardHeader() const {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (m_cardHeader == CardHeaderId::Hover) {
        return CardHeaderId::Line;
    }
#endif
    return m_cardHeader;
}

void ThemeManager::setCardHeader(const QString& id) {
    const CardHeaderId value = cardHeaderFromId(id);
    if (value != m_cardHeader) {
        m_cardHeader = value;
        commitAppearance(kCardHeaderKey, cardHeaderIdString(value));
    }
}

void ThemeManager::setCanvas(const QString& id) {
    const CanvasId value = canvasFromId(id);
    if (value != m_canvas) {
        m_canvas = value;
        commitAppearance(kCanvasKey, canvasIdString(value));
    }
}

void ThemeManager::setMotion(const QString& id) {
    const MotionId value = motionFromId(id);
    if (value != m_motion) {
        m_motion = value;
        commitAppearance(kMotionKey, motionIdString(value));
    }
}

void ThemeManager::saveCustomTheme(const ThemePalette& palette) {
    if (!isCustomPaletteId(palette.id)) {
        return;
    }
    const int existing = indexOf(palette.id);
    if (existing >= 0) {
        m_themes[existing] = palette;
    } else {
        m_themes.append(palette);
    }
    persistCustomThemes();
    emit themesChanged();
    if (m_themes[m_currentIndex].id == palette.id) {
        applyCurrentTheme();
        emit appearanceChanged();
    }
}

void ThemeManager::removeCustomTheme(const QString& id) {
    const int index = indexOf(id);
    if (index < 0 || !isCustomPaletteId(id)) {
        return;
    }
    const QString currentId = currentTheme().id;
    m_themes.removeAt(index);
    persistCustomThemes();
    emit themesChanged();
    if (currentId == id) {
        m_currentIndex = qMax(0, indexOf(QString::fromLatin1(kDefaultThemeId)));
        QSettings settings;
        settings.setValue(kSettingsKey, currentTheme().id);
        applyCurrentTheme();
        emit appearanceChanged();
    } else {
        m_currentIndex = indexOf(currentId);
    }
}

void ThemeManager::persistCustomThemes() const {
    QJsonArray custom;
    for (const ThemePalette& palette : m_themes) {
        if (isCustomPaletteId(palette.id)) {
            custom.append(paletteToJson(palette));
        }
    }
    QSettings settings;
    settings.setValue(kCustomPalettesKey, QJsonDocument(custom).toJson(QJsonDocument::Compact));
}

void ThemeManager::applyCurrentTheme() {
    if (auto* app = qApp) {
        app->setStyleSheet(scaleStyleSheetRadii(buildStyleSheet(currentTheme()),
                                                currentFrameStyle().controlRadiusScale));
    }
    emit themeChanged(currentTheme());
}

int ThemeManager::indexOf(const QString& id) const {
    for (int i = 0; i < m_themes.size(); ++i) {
        if (m_themes[i].id == id) {
            return i;
        }
    }
    return -1;
}

}  // namespace traceview
