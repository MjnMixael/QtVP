#include "Theme.h"

#include <QApplication>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>

namespace {

const char* const SettingKey = "appearance/theme";

QString& nativeStyle()
{
    static QString name;
    return name;
}

// The Windows 10 style has no dark palette, so use Fusion there while dark is in effect.
// The Windows 11 style handles both schemes itself.
void updateStyle()
{
    if (nativeStyle().compare("windowsvista", Qt::CaseInsensitive) != 0)
        return;

    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QString wanted = dark ? QStringLiteral("Fusion") : nativeStyle();
    if (QApplication::style()->name().compare(wanted, Qt::CaseInsensitive) != 0)
        QApplication::setStyle(wanted);
}

} // namespace

Theme::Mode Theme::saved()
{
    const QString value = QSettings().value(SettingKey).toString();
    if (value == "light")
        return Mode::Light;
    if (value == "dark")
        return Mode::Dark;
    return Mode::System;
}

void Theme::save(Mode mode)
{
    const char* value = mode == Mode::Light ? "light" : mode == Mode::Dark ? "dark" : "system";
    QSettings().setValue(SettingKey, QString::fromLatin1(value));
}

void Theme::apply(Mode mode)
{
    // First call: remember the platform style and follow OS changes while on System
    if (nativeStyle().isEmpty()) {
        nativeStyle() = QApplication::style()->name();
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] { updateStyle(); });
    }

    const Qt::ColorScheme scheme = mode == Mode::Light ? Qt::ColorScheme::Light
        : mode == Mode::Dark ? Qt::ColorScheme::Dark
        : Qt::ColorScheme::Unknown;   // Unknown means follow the OS
    QGuiApplication::styleHints()->setColorScheme(scheme);
    updateStyle();
}
