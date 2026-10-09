#include "window.h"
#include "images.h"
#include "system.h"
#include "throttle.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QMovie>
#include <QScreen>
#include <QTimer>
#include <QMoveEvent>
#include <QMouseEvent>
#include <QImageReader>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>

static const int POLL_MS = 400;
static const int TICK_MS = 100;
static const qint64 ERROR_REPEAT_MS = 30000;
static const qint64 DRAG_HOLD_MS = 1000;
static const qint64 PLACE_GRACE_MS = 500;
static const int MOVE_TOLERANCE_PX = 2;
static const QString WAITING_TITLE = "Spotify";
static const QString WAITING_LINE = "Esperando a Spotify…";

static qint64 monotonicMs() {
    static QElapsedTimer s_clock = [] { QElapsedTimer timer; timer.start(); return timer; }();
    return s_clock.elapsed();
}

LyricWindow::LyricWindow(config::Config config, Spotify* spotifyClient, QWidget* parent)
    : QWidget(parent), cfg(std::move(config)), spotify(spotifyClient) {
    palette = config::colors(cfg);

    Qt::WindowFlags flags = Qt::Tool | Qt::WindowDoesNotAcceptFocus;
    if (cfg.ventana.siempreArriba) flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(cfg.ventana.ancho, cfg.ventana.alto);
    setStyleSheet(QString("QWidget{background:%1;} QLabel{background:transparent;color:%2;}")
                      .arg(palette.fondo, palette.texto));

    iconLabel = new QLabel(this);
    iconLabel->setFixedSize(cfg.ventana.iconoPx, cfg.ventana.iconoPx);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setCursor(Qt::PointingHandCursor);
    iconLabel->installEventFilter(this);

    textLabel = new QLabel(this);
    textLabel->setWordWrap(true);
    textLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    textLabel->setFont(platform::resolveFont(cfg.ventana.fuenteFamilia, cfg.ventana.fuenteTam));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(cfg.ventana.iconoMargen, cfg.ventana.margenY,
                               cfg.ventana.textoMargen, cfg.ventana.margenY);
    layout->setSpacing(cfg.ventana.textoGap);
    layout->addWidget(iconLabel);
    layout->addWidget(textLabel, 1);

    loadImage(images::pick(cfg));
}

void LyricWindow::start() {
    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, [this]() { onPoll(); });
    pollTimer->start(POLL_MS);

    tickTimer = new QTimer(this);
    connect(tickTimer, &QTimer::timeout, this, [this]() { onTick(); });
    tickTimer->start(TICK_MS);

    render(true, WAITING_TITLE, WAITING_LINE);
}

void LyricWindow::onPoll() {
    const qint64 startMs = monotonicMs();
    spotify->playback([this, startMs](const QJsonObject& state, const QString& error) {
        if (!error.isEmpty()) {
            throttleError("error consultando Spotify: " + error);
            return;
        }
        // el dato es de más o menos la mitad del viaje, no de cuando llegó
        const double at = (startMs + monotonicMs()) / 2.0;

        if (state.isEmpty() || !state.value("is_playing").toBool()) {
            playing = false;
            trackKey.clear();
            linesReady = false;
            lines.clear();
            return;
        }
        const QJsonObject item = state.value("item").toObject();
        const QString name = item.value("name").toString();
        if (name.isEmpty() || item.value("type").toString() != "track") {
            playing = false;
            return;
        }

        const QJsonArray artists = item.value("artists").toArray();
        const QString artist = artists.isEmpty() ? QString()
            : artists.first().toObject().value("name").toString();
        QString key = item.value("id").toString();
        if (key.isEmpty()) key = name + "|" + artist;

        if (key != trackKey) {
            trackKey = key;
            trackTitle = name;
            const QString album = item.value("album").toObject().value("name").toString();
            durationMs = item.value("duration_ms").toDouble();
            linesReady = false;
            lyricsFound = false;
            lines.clear();
            loadImage(images::pick(cfg));   // una imagen al azar por canción

            const QString capturedKey = key;
            lyrics::fetch(name, artist, album, durationMs,
                          [this, capturedKey](lyrics::Lines fetched, bool found) {
                if (capturedKey != trackKey) return;   // canción vieja: descartar
                lines = std::move(fetched);
                lyricsFound = found;
                linesReady = true;
            });
            std::cout << "♪ " << name.toStdString() << " — " << artist.toStdString()
                      << " | buscando letra\n";
        }
        playing = true;
        positionMs = state.value("progress_ms").toDouble();
        atMs = at;
    });
}

void LyricWindow::onTick() {
    if (!playing) {
        render(true, WAITING_TITLE, WAITING_LINE);
        return;
    }
    if (!linesReady) return;                 // todavía no llegó la letra
    if (lines.isEmpty()) {
        if (!lyricsFound) render(true, trackTitle, "♪  sin letra sincronizada");
        else render(false);
        return;
    }
    const double position = positionMs + (monotonicMs() - atMs);
    const int index = lyrics::currentLine(lines, position, durationMs);
    if (index < 0) render(false);
    else render(true, trackTitle, lines[index].second.isEmpty() ? "♪" : lines[index].second);
}

void LyricWindow::render(bool visible, const QString& title, const QString& line) {
    if (monotonicMs() < freeUntilMs) return;   // la está arrastrando: no tocar
    if (!visible && !shown) return;
    if (visible && shown && title == shownTitle && line == shownLine) return;

    shown = visible;
    if (!visible) {
        hide();
        if (movie) movie->stop();
        return;
    }
    shownTitle = title;
    shownLine = line;
    setWindowTitle(title);
    textLabel->setText(line);

    move(haveUserPos ? userPos : centered());
    placed = pos();
    createdMs = monotonicMs();
    show();
    raise();
    if (movie) {
        movie->stop();
        movie->start();
    }
}

QPoint LyricWindow::centered() const {
    const QRect screen = this->screen()->availableGeometry();
    return QPoint(screen.x() + (screen.width() - width()) / 2,
                  screen.y() + (screen.height() - height()) / 2);
}

void LyricWindow::loadImage(const QString& path) {
    delete movie;
    movie = nullptr;
    currentImage = path;
    if (path.isEmpty()) {
        iconLabel->setMovie(nullptr);
        iconLabel->clear();
        return;
    }
    movie = new QMovie(path, QByteArray(), this);
    if (!movie->isValid()) {
        std::cout << "· no pude abrir " << path.toStdString() << "\n";
        delete movie;
        movie = nullptr;
        return;
    }
    QSize frame = QImageReader(path).size();
    if (!frame.isValid()) frame = movie->currentPixmap().size();
    if (frame.isValid() && frame.width() > 0 && frame.height() > 0) {
        const int box = cfg.ventana.iconoPx;
        const double scale = qMin(static_cast<double>(box) / frame.width(),
                                  static_cast<double>(box) / frame.height());
        movie->setScaledSize(QSize(qMax(1, qRound(frame.width() * scale)),
                                   qMax(1, qRound(frame.height() * scale))));
    }
    iconLabel->setMovie(movie);
    if (shown) movie->start();
}

void LyricWindow::reroll() {
    loadImage(images::pick(cfg, currentImage));
}

void LyricWindow::nextProfile() {
    if (cfg.perfilOrden.isEmpty()) return;
    const int index = cfg.perfilOrden.indexOf(cfg.perfil);
    const QString next = cfg.perfilOrden[(index + 1) % cfg.perfilOrden.size()];
    config::setPerfil(cfg, next);
    config::save(cfg);
    std::cout << "· perfil '" << next.toStdString() << "' -> "
              << cfg.perfiles.value(next).toStdString() << "\n";
    reroll();
}

bool LyricWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == iconLabel && event->type() == QEvent::MouseButtonPress) {
        const auto* press = static_cast<QMouseEvent*>(event);
        if (press->button() == Qt::LeftButton) {
            reroll();
            return true;
        }
        if (press->button() == Qt::RightButton) {
            nextProfile();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LyricWindow::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    const qint64 now = monotonicMs();
    if (now - createdMs < PLACE_GRACE_MS) return;
    const QPoint moved = event->pos();
    if (qAbs(moved.x() - placed.x()) <= MOVE_TOLERANCE_PX
        && qAbs(moved.y() - placed.y()) <= MOVE_TOLERANCE_PX) {
        return;
    }
    placed = moved;
    userPos = moved;
    haveUserPos = true;
    freeUntilMs = now + DRAG_HOLD_MS;
}

void LyricWindow::throttleError(const QString& message) {
    const qint64 now = monotonicMs();
    if (throttle(lastError, lastErrorAt, message, now, ERROR_REPEAT_MS)) {
        std::cout << "· " << message.toStdString() << "\n";
    }
}
