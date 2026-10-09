#include "window.h"
#include "images.h"
#include "system.h"
#include "throttle.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QHBoxLayout>
#include <QMovie>
#include <QScreen>
#include <QTimer>
#include <QEvent>
#include <QMoveEvent>
#include <QMouseEvent>
#include <QImageReader>
#include <QIcon>
#include <QPixmap>
#include <QWidget>
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

// Icono transparente: saca el logo por defecto de la barra de título.
static QIcon blankIcon() {
    static QPixmap pixel = [] { QPixmap p(1, 1); p.fill(Qt::transparent); return p; }();
    return QIcon(pixel);
}

struct LyricWindow::Popup {
    QWidget* widget = nullptr;
    QLabel* icon = nullptr;
    QLabel* text = nullptr;
    QMovie* movie = nullptr;
};

LyricWindow::LyricWindow(config::Config config, Spotify* spotifyClient, QObject* parent)
    : QObject(parent), cfg(std::move(config)), spotify(spotifyClient) {
    palette = config::colors(cfg);
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
            setImage(images::pick(cfg));   // una imagen al azar por canción

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
    if (!visible) {
        if (!shown) return;
        shown = false;
        destroy();
        return;
    }
    if (shown && title == shownTitle && line == shownLine) return;

    shown = true;
    shownTitle = title;
    shownLine = line;
    rebuild(title, line);                      // ventana nueva: cierra la anterior

    QWidget* window = popup->widget;
    // `move()` posiciona el área de cliente; el WM dibuja la barra de título por
    // encima. Por eso guardamos/restauramos la posición de cliente, no la del
    // marco (si no, en cada recreación la ventana sube lo que mide la barra).
    const QPoint target = haveUserPos ? userPos : centered(window);
    window->move(target);
    window->show();
    window->raise();
    createdMs = monotonicMs();
    window->move(target);
    placed = window->frameGeometry().topLeft();
    if (popup->movie) {
        popup->movie->stop();
        popup->movie->start();
    }
}

QPoint LyricWindow::centered(QWidget* window) const {
    const QRect screen = window->screen()->availableGeometry();
    return QPoint(screen.x() + (screen.width() - window->width()) / 2,
                  screen.y() + (screen.height() - window->height()) / 2);
}

void LyricWindow::rebuild(const QString& title, const QString& line) {
    destroy();

    Popup* p = new Popup;
    p->widget = new QWidget;
    Qt::WindowFlags flags = Qt::Tool | Qt::WindowDoesNotAcceptFocus
        | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint;
    if (cfg.ventana.siempreArriba) flags |= Qt::WindowStaysOnTopHint;
    p->widget->setWindowFlags(flags);
    p->widget->setAttribute(Qt::WA_ShowWithoutActivating);
    p->widget->setStyleSheet(QString("QWidget{background:%1;} QLabel{background:transparent;color:%2;}")
                                 .arg(palette.fondo, palette.texto));
    p->widget->setWindowTitle(title);
    p->widget->setWindowIcon(blankIcon());

    p->icon = new QLabel(p->widget);
    p->icon->setFixedSize(cfg.ventana.iconoPx, cfg.ventana.iconoPx);
    p->icon->setAlignment(Qt::AlignCenter);
    p->icon->setCursor(Qt::PointingHandCursor);
    p->icon->installEventFilter(this);

    p->text = new QLabel(p->widget);
    p->text->setWordWrap(true);
    p->text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    p->text->setFont(platform::resolveFont(cfg.ventana.fuenteFamilia, cfg.ventana.fuenteTam));
    p->text->setText(line);

    auto* layout = new QHBoxLayout(p->widget);
    layout->setContentsMargins(cfg.ventana.iconoMargen, cfg.ventana.margenY,
                               cfg.ventana.textoMargen, cfg.ventana.margenY);
    layout->setSpacing(cfg.ventana.textoGap);
    layout->addWidget(p->icon);
    layout->addWidget(p->text, 1);

    p->widget->setFixedSize(cfg.ventana.ancho, cfg.ventana.alto);
    platform::makeStealth(p->widget);   // sin entrada en la barra de tareas
    popup = p;

    setImage(currentImage);   // (re)carga la imagen actual en la ventana nueva
}

void LyricWindow::destroy() {
    if (!popup) return;
    delete popup->widget;
    delete popup;
    popup = nullptr;
}

void LyricWindow::setImage(const QString& path) {
    currentImage = path;
    if (!popup) return;
    delete popup->movie;
    popup->movie = nullptr;
    if (path.isEmpty()) {
        popup->icon->setMovie(nullptr);
        popup->icon->clear();
        return;
    }
    auto* movie = new QMovie(path, QByteArray(), popup->widget);
    if (!movie->isValid()) {
        std::cout << "· no pude abrir " << path.toStdString() << "\n";
        delete movie;
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
    popup->movie = movie;
    popup->icon->setMovie(movie);
    if (popup->widget->isVisible()) movie->start();
}

void LyricWindow::reroll() {
    setImage(images::pick(cfg, currentImage));
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
    if (!popup) return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::Close) {
        qApp->quit();
        return false;
    }
    if (watched == popup->widget && event->type() == QEvent::Move) {
        const qint64 now = monotonicMs();
        if (now - createdMs < PLACE_GRACE_MS) return false;
        const QPoint frame = popup->widget->frameGeometry().topLeft();
        if (qAbs(frame.x() - placed.x()) <= MOVE_TOLERANCE_PX
            && qAbs(frame.y() - placed.y()) <= MOVE_TOLERANCE_PX) {
            return false;
        }
        placed = frame;
        userPos = popup->widget->geometry().topLeft();   // posición de cliente
        haveUserPos = true;
        freeUntilMs = now + DRAG_HOLD_MS;
        return false;
    }
    if (watched == popup->icon && event->type() == QEvent::MouseButtonPress) {
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
    return QObject::eventFilter(watched, event);
}

void LyricWindow::throttleError(const QString& message) {
    const qint64 now = monotonicMs();
    if (throttle(lastError, lastErrorAt, message, now, ERROR_REPEAT_MS)) {
        std::cout << "· " << message.toStdString() << "\n";
    }
}
