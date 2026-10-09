#include "images.h"
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QRandomGenerator>

namespace images {

const QStringList IMAGE_EXTS = {
    ".gif", ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff"
};

QStringList folderImages(const QString& folder) {
    if (folder.isEmpty()) return {};
    // Los gifs pueden vivir con los datos (portable) o ir empaquetados junto al
    // ejecutable (AppImage), así que se prueban las dos carpetas.
    for (const QString& base : {config::root(), QCoreApplication::applicationDirPath()}) {
        const QDir dir(base + "/" + folder);
        if (!dir.exists()) continue;
        QStringList out;
        const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo& info : entries) {
            if (IMAGE_EXTS.contains("." + info.suffix().toLower())) {
                out << info.absoluteFilePath();
            }
        }
        if (!out.isEmpty()) return out;
    }
    return {};
}

QString pick(const config::Config& cfg, const QString& avoid) {
    QStringList order;
    auto add = [&order](const QString& name) {
        if (!name.isEmpty() && !order.contains(name)) order << name;
    };
    add(cfg.perfil);
    add("default");
    for (const QString& name : cfg.perfilOrden) add(name);

    for (const QString& name : order) {
        const QStringList images = folderImages(cfg.perfiles.value(name));
        if (images.isEmpty()) continue;
        QStringList options;
        for (const QString& path : images) {
            if (path != avoid) options << path;
        }
        if (options.isEmpty()) options = images;
        return options[QRandomGenerator::global()->bounded(options.size())];
    }
    return {};
}

}
