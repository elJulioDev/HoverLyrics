#pragma once
#include <QWidget>
#include <QPoint>
#include <QString>
#include "config.h"
#include "lyrics.h"
#include "spotify.h"

class QLabel;
class QMovie;
class QTimer;

// La ventana emergente con la letra. Dos timers: uno consulta Spotify (red) y
// otro recalcula la línea (local, sin red). Como la letra ya trae los tiempos,
// entre consulta y consulta la posición se extrapola y la red solo corrige.
class LyricWindow : public QWidget {
public:
    LyricWindow(config::Config cfg, Spotify* spotify, QWidget* parent = nullptr);
    void start();

protected:
    void moveEvent(QMoveEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onPoll();
    void onTick();
    void render(bool visible, const QString& title = QString(), const QString& line = QString());
    void loadImage(const QString& path);
    void reroll();
    void nextProfile();
    void throttleError(const QString& message);
    QPoint centered() const;

    config::Config cfg;
    Spotify* spotify;
    config::Palette palette;

    QLabel* iconLabel = nullptr;
    QLabel* textLabel = nullptr;
    QMovie* movie = nullptr;
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
