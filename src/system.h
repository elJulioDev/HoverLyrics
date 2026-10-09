#pragma once
#include <QString>
#include <QFont>

class QWidget;

// Diferencias entre sistemas operativos. Con Qt casi todo se reduce a una API
// cross-platform; queda la familia de fuente por defecto y el "no robar foco".
// (no se llama `system`: choca con `int system(const char*)` de la libc)
namespace platform {

// La familia que se ve nativa en cada sistema.
QString defaultFontFamily();

// Si la familia configurada no existe acá, cae a la del sistema: así el mismo
// config.json se puede llevar de un sistema a otro.
QFont resolveFont(const QString& family, int size);

// ¿El escritorio está en modo oscuro? (Qt 6.5+ lo sabe en las tres plataformas)
bool isDark();

// Que el gestor de ventanas no le dé el foco al abrirla.
void noFocus(QWidget* widget);

}
