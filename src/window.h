#pragma once
#include <QObject>
#include <QPoint>
#include <QString>
#include "config.h"
#include "lyrics.h"
#include "spotify.h"

class QLabel;
class QMovie;
class QTimer;
class QWidget;

// Controlador: dos timers (uno consulta Spotify, otro recalcula la línea) y la
// ventana. Como la letra ya trae los tiempos, entre consulta y consulta la
// posición se extrapola y la red solo corrige.
//
// La ventana se destruye y se vuelve a crear con cada línea (el efecto pedido):
// por eso el controlador no es la ventana, sino un objeto que la arma.
class LyricWindow : public QObject {
public:
    LyricWindow(config::Config cfg, Spotify* spotify, QObject* parent = nullptr);
    void start();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Popup;

    void onPoll();
    void onTick();
    void render(bool visible, const QString& title = QString(), const QString& line = QString());
    void rebuild(const QString& title, const QString& line);
    void destroy();
    void setImage(const QString& path);
    void reroll();
    void nextProfile();
    void throttleError(const QString& message);
    QPoint centered(QWidget* window) const;

    config::Config cfg;
    Spotify* spotify;
    config::Palette palette;

    Popup* popup = nullptr;
    QString currentImage;

    QTimer* pollTimer = nullptr;
    QTimer* tickTimer = nullptr;

    // estado de reproducción (lo escribe la red, lo lee el timer de la UI)
    bool playing = false;
    QString trackKey;
    QString trackTitle;
    bool linesReady = false;
    bool lyricsFound = false;
    lyrics::Lines lines;
    double positionMs = 0;
    double durationMs = 0;
    double atMs = 0;            // instante (monótono) al que corresponde positionMs

    // qué hay en pantalla
    bool shown = false;
    QString shownTitle;
    QString shownLine;

    // posición / arrastre
    bool haveUserPos = false;
    QPoint userPos;
    QPoint placed;
    qint64 createdMs = 0;
    qint64 freeUntilMs = 0;

    QString lastError;
    qint64 lastErrorAt = 0;
};
