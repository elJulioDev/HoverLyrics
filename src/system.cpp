#include "system.h"
#include <QGuiApplication>
#include <QStyleHints>
#include <QFontDatabase>
#include <QWidget>
#include <QCoreApplication>

#if defined(Q_OS_LINUX)
#include <QtGui/qguiapplication_platform.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

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

void makeStealth(QWidget* widget) {
#if defined(Q_OS_LINUX)
    if (QGuiApplication::platformName() != QLatin1String("xcb")) return;
    if (!qGuiApp) return;
    auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->display()) return;
    Display* display = x11->display();
    const Window window = static_cast<Window>(widget->winId());

    auto atom = [display](const char* name) { return XInternAtom(display, name, False); };

    // Tipo "utility": la barra de tareas no la lista por defecto.
    Atom utility = atom("_NET_WM_WINDOW_TYPE_UTILITY");
    XChangeProperty(display, window, atom("_NET_WM_WINDOW_TYPE"), XA_ATOM, 32,
                    PropModeReplace, reinterpret_cast<unsigned char*>(&utility), 1);

    // Y además pedimos explícitamente que la salteen la barra y el paginador.
    Atom states[2] = {
        atom("_NET_WM_STATE_SKIP_TASKBAR"),
        atom("_NET_WM_STATE_SKIP_PAGER"),
    };
    XChangeProperty(display, window, atom("_NET_WM_STATE"), XA_ATOM, 32,
                    PropModeReplace, reinterpret_cast<unsigned char*>(states), 2);
    XFlush(display);
#endif
}

}
