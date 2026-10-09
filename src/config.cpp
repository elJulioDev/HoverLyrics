#include "config.h"
#include "system.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QByteArray>
#include <QCoreApplication>
#include <QStandardPaths>
#include <iostream>

using ojson = nlohmann::ordered_json;
using ojson_ref = const nlohmann::ordered_json&;

namespace config {

static QString s(const ojson_ref o, const char* key, const QString& fallback = QString()) {
    if (o.contains(key) && o[key].is_string()) {
        return QString::fromStdString(o[key].get<std::string>());
    }
    return fallback;
}

static int i(const ojson_ref o, const char* key, int fallback) {
    if (o.contains(key) && o[key].is_number()) {
        return o[key].get<int>();
    }
    return fallback;
}

static bool b(const ojson_ref o, const char* key, bool fallback) {
    if (o.contains(key) && o[key].is_boolean()) {
        return o[key].get<bool>();
    }
    return fallback;
}

QString root() {
    const QString override = qEnvironmentVariable("HOVERLYRICS_HOME");
    if (!override.isEmpty()) return override;

    // AppImage: el ejecutable vive en un squashfs de solo lectura, así que los
    // datos van al lado del .AppImage cuando se puede escribir.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty()) {
        const QString dir = QFileInfo(appImage).absolutePath();
        if (QFileInfo(dir).isWritable()) return dir;
    }

    // ¿Esta carpeta tiene pinta de ser la de datos? (portable, o el repo en dev)
    auto looksLikeHome = [](const QString& dir) {
        return QFileInfo::exists(dir + "/config.json")
            || QFileInfo::exists(dir + "/.env")
            || QFileInfo::exists(dir + "/gifs");
    };

    const QString appDir = QCoreApplication::applicationDirPath();
    if (QFileInfo(appDir).isWritable() && looksLikeHome(appDir)) return appDir;
    const QString cwd = QDir::currentPath();
    if (looksLikeHome(cwd)) return cwd;
    if (QFileInfo(appDir).isWritable()) return appDir;

    // Instalada en un sitio de solo lectura: carpeta de configuración del usuario.
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(data);
    return data;
}
QString configPath() { return root() + "/config.json"; }
QString envPath() { return root() + "/.env"; }
QString tokenPath() { return root() + "/token.json"; }

ojson defaults() {
    ojson ventana;
    ventana["tema"] = "oscuro";
    ventana["colores"] = ojson::object();
    ventana["ancho"] = 440;
    ventana["alto"] = 92;
    ventana["fuente"] = ojson::array({platform::defaultFontFamily().toStdString(), 11});
    ventana["icono_px"] = 45;
    ventana["icono_margen"] = 20;
    ventana["icono_dy"] = 4;
    ventana["texto_gap"] = 22;
    ventana["texto_margen"] = 12;
    ventana["margen_y"] = 10;
    ventana["siempre_arriba"] = true;

    ojson perfiles;
    perfiles["default"] = "gifs/default";
    perfiles["sad"] = "gifs/sad";
    perfiles["romantic"] = "gifs/romantic";
    perfiles["fun"] = "gifs/fun";

    ojson c;
    c["perfil"] = "default";
    c["perfiles"] = perfiles;
    c["ventana"] = ventana;
    return c;
}

// Rellena lo que falte sin pisar lo que ya hay (ni claves desconocidas).
ojson normalize(ojson data) {
    if (!data.is_object()) {
        data = ojson::object();
    }
    const ojson def = defaults();
    if (!data.contains("perfil")) {
        data["perfil"] = def["perfil"];
    }
    if (!data.contains("perfiles") || !data["perfiles"].is_object()) {
        data["perfiles"] = def["perfiles"];
    }
    ojson ventana = def["ventana"];
    if (data.contains("ventana") && data["ventana"].is_object()) {
        for (auto it = data["ventana"].begin(); it != data["ventana"].end(); ++it) {
            ventana[it.key()] = it.value();
        }
    }
    data["ventana"] = ventana;
    return data;
}

Config parse(const ojson& data) {
    Config c;
    c.root = data;
    c.perfil = s(data, "perfil", "default");

    const ojson perfiles = data.value("perfiles", ojson::object());
    for (auto it = perfiles.begin(); it != perfiles.end(); ++it) {
        const QString name = QString::fromStdString(it.key());
        c.perfilOrden << name;
        c.perfiles[name] = it.value().is_string()
            ? QString::fromStdString(it.value().get<std::string>()) : QString();
    }

    const ojson w = data.value("ventana", ojson::object());
    Window& v = c.ventana;
    v.tema = s(w, "tema", "oscuro");
    v.ancho = i(w, "ancho", 440);
    v.alto = i(w, "alto", 92);
    v.iconoPx = i(w, "icono_px", 45);
    v.iconoMargen = i(w, "icono_margen", 20);
    v.iconoDy = i(w, "icono_dy", 4);
    v.textoGap = i(w, "texto_gap", 22);
    v.textoMargen = i(w, "texto_margen", 12);
    v.margenY = i(w, "margen_y", 10);
    v.siempreArriba = b(w, "siempre_arriba", true);

    if (w.contains("colores") && w["colores"].is_object()) {
        v.colorFondo = s(w["colores"], "fondo");
        v.colorTexto = s(w["colores"], "texto");
    }
    if (w.contains("fuente") && w["fuente"].is_array() && w["fuente"].size() >= 2) {
        v.fuenteFamilia = QString::fromStdString(w["fuente"][0].get<std::string>());
        v.fuenteTam = w["fuente"][1].get<int>();
    } else {
        v.fuenteFamilia = platform::defaultFontFamily();
        v.fuenteTam = 11;
    }
    return c;
}

Config load() {
    ojson data;
    QFile file(configPath());
    if (file.exists()) {
        (void)file.open(QIODevice::ReadOnly);
        try {
            data = ojson::parse(file.readAll().toStdString());
        } catch (const std::exception& exc) {
            std::cout << "· config.json inválido (" << exc.what() << "), uso los valores por defecto\n";
            data = ojson::object();
        }
    } else {
        std::cout << "· creando config.json con los valores por defecto\n";
    }
    const ojson before = data;
    Config cfg = parse(normalize(data));
    if (cfg.root != before) {
        save(cfg);
    }
    return cfg;
}

void save(Config& cfg) {
    cfg.root["perfil"] = cfg.perfil.toStdString();
    QFile file(configPath());
    (void)file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(QByteArray::fromStdString(cfg.root.dump(2)));
    file.write("\n");
}

void setPerfil(Config& cfg, const QString& name) {
    cfg.perfil = name;
    cfg.root["perfil"] = name.toStdString();
}

Palette colors(const Config& cfg) {
    QString name = cfg.ventana.tema.toLower();
    if (name == "sistema") {
        name = platform::isDark() ? "oscuro" : "claro";
    }
    Palette p;
    if (name == "claro") {
        p.fondo = "#f5f5f5";
        p.texto = "#1a1a1a";
    } else {
        p.fondo = "#1e1e1e";
        p.texto = "#f2f2f2";
    }
    if (!cfg.ventana.colorFondo.isEmpty()) {
        p.fondo = cfg.ventana.colorFondo;
    }
    if (!cfg.ventana.colorTexto.isEmpty()) {
        p.texto = cfg.ventana.colorTexto;
    }
    return p;
}

QString loadClientId() {
    const QString key = "SPOTIFY_CLIENT_ID";
    QString text;
    QFile env(envPath());
    if (env.exists()) {
        (void)env.open(QIODevice::ReadOnly);
        text = QString::fromUtf8(env.readAll());
    }
    for (const QString& raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        if (line.left(eq).trimmed() == key) {
            const QString value = line.mid(eq + 1).trimmed();
            if (!value.isEmpty()) return value;
        }
    }
    return qEnvironmentVariable(key.toUtf8()).trimmed();
}

// Guarda el Client ID en .env, pisando el que hubiera (sin duplicar líneas).
void saveClientId(const QString& id) {
    const QString key = "SPOTIFY_CLIENT_ID";
    QStringList kept;
    QFile env(envPath());
    if (env.exists()) {
        (void)env.open(QIODevice::ReadOnly);
        for (const QString& raw : QString::fromUtf8(env.readAll()).split('\n')) {
            const QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith(key + "=")) continue;
            kept << line;
        }
        env.close();
    }
    kept << key + "=" + id;
    QFile out(envPath());
    (void)out.open(QIODevice::WriteOnly | QIODevice::Truncate);
    out.write((kept.join('\n') + "\n").toUtf8());
}

}
