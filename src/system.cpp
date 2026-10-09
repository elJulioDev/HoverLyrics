#include "system.h"
#include <QGuiApplication>
#include <QStyleHints>
#include <QFontDatabase>
#include <QWidget>
#include <QCoreApplication>

namespace platform {

QString defaultFontFamily() {
#if defined(Q_OS_WIN)
    return "Segoe UI";
#elif defined(Q_OS_MAC)
    return "Helvetica Neue";
#else
    return "DejaVu Sans";
#endif
}

QFont resolveFont(const QString& family, int size) {
    // Sin app de GUI (p.ej. --selftest) no se puede consultar la base de fuentes.
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        if (!QFontDatabase::families().contains(family)) {
            return QFont(defaultFontFamily(), size);
        }
    }
    return QFont(family, size);
}

bool isDark() {
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        return false;
    }
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void noFocus(QWidget* widget) {
    widget->setAttribute(Qt::WA_ShowWithoutActivating);
    widget->setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
}

}
