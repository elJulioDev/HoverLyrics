#pragma once
#include <QString>
#include <QStringList>
#include <QHash>
#include <nlohmann/json.hpp>

// Configuración: credenciales (.env), perfiles y ventana (config.json), temas.
namespace config {

struct Window {
    QString tema = "oscuro";     // oscuro | claro | sistema
    QString colorFondo;          // custom ("" = usa el del tema)
    QString colorTexto;
    int ancho = 440;
    int alto = 92;
    QString fuenteFamilia;
    int fuenteTam = 11;
    int iconoPx = 45;
    int iconoMargen = 20;
    int iconoDy = 4;
    int textoGap = 22;
    int textoMargen = 12;
    int margenY = 10;
    bool siempreArriba = true;
};

struct Config {
    QString perfil = "default";
    QStringList perfilOrden;                 // orden en que se escribieron
    QHash<QString, QString> perfiles;        // nombre -> carpeta
    Window ventana;
    nlohmann::ordered_json root;             // documento completo (claves de más incluidas)
};

struct Palette {
    QString fondo;
    QString texto;
};

// La carpeta de datos: donde viven config.json/.env/token.json y gifs/.
// Es portable: por defecto va junto al ejecutable (o al .AppImage); la variable
// HOVERLYRICS_HOME la pisa, y si el destino es de solo lectura cae a la carpeta
// de configuración del usuario.
QString root();
QString configPath();
QString envPath();
QString tokenPath();

nlohmann::ordered_json defaults();
nlohmann::ordered_json normalize(nlohmann::ordered_json data);
Config parse(const nlohmann::ordered_json& data);
Config load();
void save(Config& cfg);
void setPerfil(Config& cfg, const QString& name);

Palette colors(const Config& cfg);
QString loadClientId();
void saveClientId(const QString& id);

}
