// Punto de entrada: ventana emergente con la letra de lo que suena en Spotify.
// Uso:  ./hoverlyrics    (--selftest corre los chequeos sin ventana)
#include <QApplication>
#include <QCoreApplication>
#include <iostream>
#include "config.h"
#include "images.h"
#include "lyrics.h"
#include "system.h"
#include "spotify.h"
#include "window.h"
#include "throttle.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FALLO: " #cond "\n"; ++failures; } } while (0)

// Chequeos de la lógica pura: sin red y sin ventana.
static int selftest() {
    // Letra: parseo y qué línea toca en cada momento.
    const lyrics::Lines lines = lyrics::parseSynced(
        "[00:10.56]hola\n[01:02.30]mundo\nbasura\n");
    CHECK(lines.size() == 2);
    CHECK(qFuzzyCompare(lines[0].first, 10.56));
    CHECK(qFuzzyCompare(lines[1].first, 62.30));
    CHECK(lines[0].second == "hola" && lines[1].second == "mundo");
    CHECK(lyrics::currentLine(lines, 5000, 120000) == -1);    // antes de la 1ª
    CHECK(lyrics::currentLine(lines, 10560, 120000) == 0);
    CHECK(lyrics::currentLine(lines, 62000, 120000) == 0);    // dura hasta la 2ª
    CHECK(lyrics::currentLine(lines, 62300, 120000) == 1);
    CHECK(lyrics::currentLine(lines, 120000, 120000) == -1);  // pasó el final
    CHECK(lyrics::currentLine({}, 1000, 1000) == -1);         // sin letra

    // Config: se completa sola y los temas se resuelven bien.
    const auto base = config::normalize(nlohmann::ordered_json::object());
    CHECK(base["ventana"]["ancho"].get<int>() == 440);
    nlohmann::ordered_json partial;
    partial["ventana"]["ancho"] = 500;
    const auto filled = config::normalize(partial);
    CHECK(filled["ventana"]["ancho"].get<int>() == 500);
    CHECK(filled["perfil"].get<std::string>() == "default");

    config::Config claro;
    claro.ventana.tema = "claro";
    CHECK(config::colors(claro).fondo == "#f5f5f5");
    config::Config oscuro;
    oscuro.ventana.tema = "oscuro";
    CHECK(config::colors(oscuro).texto == "#f2f2f2");
    config::Config custom;
    custom.ventana.tema = "claro";
    custom.ventana.colorFondo = "#123456";
    const config::Palette palette = config::colors(custom);
    CHECK(palette.fondo == "#123456" && palette.texto == "#1a1a1a");

    // Imágenes: elección de perfil y caída a default.
    CHECK(images::folderImages("").isEmpty());
    CHECK(images::folderImages("no/existe").isEmpty());
    {
        config::Config directo;
        directo.perfil = "d";
        directo.perfilOrden = {"d"};
        directo.perfiles["d"] = "gifs/default";
        CHECK(images::pick(directo).contains("/default/"));

        config::Config caida;
        caida.perfil = "x";
        caida.perfilOrden = {"default"};
        caida.perfiles["default"] = "gifs/default";
        CHECK(images::pick(caida).contains("/default/"));

        config::Config vacio;
        CHECK(images::pick(vacio).isEmpty());
    }

    // Fuente por sistema.
    CHECK(platform::resolveFont("DejaVu Sans", 11).pointSize() == 11);

    // Un corte de red no puede repetir el mismo error en cada reintento.
    {
        QString last;
        qint64 at = 0;
        CHECK(throttle(last, at, "falla A", 0));
        CHECK(!throttle(last, at, "falla A", 100));
        CHECK(throttle(last, at, "falla B", 200));
        CHECK(throttle(last, at, "falla B", 200 + 30001));
    }

    if (failures == 0) {
        std::cout << "selftest ok\n";
        return 0;
    }
    std::cerr << failures << " fallo(s)\n";
    return 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--selftest") {
        QCoreApplication app(argc, argv);
        return selftest();
    }

#if defined(Q_OS_LINUX)
    // La versión Python (Tk) corre sobre X11/XWayland, y con eso el "siempre
    // arriba", la posición y el tamaño se comportan igual. Wayland le da la
    // espalda al topmost y a mover la ventana, así que si hay XWayland usamos
    // xcb (se puede forzar otro backend con QT_QPA_PLATFORM).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

    // Los tamaños de config.json son píxeles reales, como en la versión Python.
    // Qt por defecto los escala según el DPI del monitor (acá Xft.dpi=144 daba
    // 1.5x y la ventana salía más grande); desactivamos esa escala para que
    // 1 px de config sea 1 px real. La fuente sigue usando el DPI del monitor.
    // Para volver a la escala de Qt: QT_ENABLE_HIGHDPI_SCALING=1.
    if (qEnvironmentVariableIsEmpty("QT_ENABLE_HIGHDPI_SCALING")) {
        qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    }

    QApplication app(argc, argv);
    config::Config cfg = config::load();
    const config::Palette palette = config::colors(cfg);
    const QString clientId = config::loadClientId();
    if (clientId.isEmpty()) {
        std::cerr << "Sin Client ID no hay nada que hacer.\n";
        return 1;
    }

    std::cout << "· perfil '" << cfg.perfil.toStdString() << "' | tema '"
              << cfg.ventana.tema.toStdString() << "' (" << palette.texto.toStdString()
              << " sobre " << palette.fondo.toStdString() << ")\n";

    Spotify spotify(clientId);
    LyricWindow window(std::move(cfg), &spotify);
    window.start();

    std::cout << "· escuchando a Spotify… (Ctrl+C para salir)\n";
    return app.exec();
}
