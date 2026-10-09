#pragma once
#include <QString>
#include <QStringList>
#include "config.h"

// Imágenes de los perfiles: elegir una al azar y prepararla para Qt.
namespace images {

extern const QStringList IMAGE_EXTS;

// Las imágenes de una carpeta (relativa a la raíz), ordenadas; vacío si no existe.
QStringList folderImages(const QString& folder);

// Una imagen al azar del perfil elegido. Si ese perfil no existe o su carpeta
// está vacía, prueba "default" y después cualquier perfil con imágenes.
// `avoid` se salta, para no repetir la que ya se muestra.
QString pick(const config::Config& cfg, const QString& avoid = QString());

}
