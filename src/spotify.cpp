#include "spotify.h"
#include "config.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QFile>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDesktopServices>
#include <QTimer>
#include <QDateTime>
#include <QHostAddress>
#include <iostream>
#include <memory>

static const char* AUTH_URL = "https://accounts.spotify.com/authorize";
static const char* TOKEN_URL = "https://accounts.spotify.com/api/token";
static const char* PLAYER_URL = "https://api.spotify.com/v1/me/player";
static const char* REDIRECT_URI = "http://127.0.0.1:8888/callback";
static const char* SCOPE = "user-read-playback-state";

Spotify::Spotify(QString cid, QObject* parent)
    : QObject(parent), clientId(std::move(cid)), net(new QNetworkAccessManager(this)) {
    QFile file(config::tokenPath());
    if (file.exists()) {
        (void)file.open(QIODevice::ReadOnly);
        const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
        accessToken = saved.value("access_token").toString();
        refreshToken = saved.value("refresh_token").toString();
        expiresAt = static_cast<qint64>(saved.value("expires_at").toDouble());
    }
}

void Spotify::store(const QJsonObject& data) {
    accessToken = data.value("access_token").toString();
    expiresAt = QDateTime::currentMSecsSinceEpoch() / 1000
        + static_cast<qint64>(data.value("expires_in").toDouble(3600));
    const QString refreshed = data.value("refresh_token").toString();
    if (!refreshed.isEmpty()) refreshToken = refreshed;

    const QJsonObject out{
        {"access_token", accessToken},
        {"expires_at", static_cast<double>(expiresAt)},
        {"refresh_token", refreshToken},
    };
    QFile file(config::tokenPath());
    (void)file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(QJsonDocument(out).toJson());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
}

void Spotify::postToken(const QUrlQuery& body, std::function<void(QJsonObject, QString)> cb) {
    QNetworkRequest req{QUrl(TOKEN_URL)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply* reply = net->post(req, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, [reply, cb]() {
        reply->deleteLater();
        const QString error = reply->error() == QNetworkReply::NoError
            ? QString() : reply->errorString();
        cb(QJsonDocument::fromJson(reply->readAll()).object(), error);
    });
}

void Spotify::ensureToken(std::function<void(QString)> cb) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    if (!accessToken.isEmpty() && now < expiresAt - 60) {
        cb(accessToken);
        return;
    }
    if (authBroken) {
        cb(QString());
        return;
    }
    tokenWaiters.push_back(std::move(cb));
    if (loadingToken) return;
    loadingToken = true;

    auto finish = [this](QString token) {
        loadingToken = false;
        auto waiters = std::move(tokenWaiters);
        tokenWaiters.clear();
        for (auto& waiter : waiters) waiter(token);
    };
    auto doLogin = [this, finish]() {
        login([this, finish](bool ok) { finish(ok ? accessToken : QString()); });
    };

    if (!refreshToken.isEmpty()) {
        QUrlQuery body;
        body.addQueryItem("grant_type", "refresh_token");
        body.addQueryItem("refresh_token", refreshToken);
        body.addQueryItem("client_id", clientId);
        postToken(body, [this, finish, doLogin](QJsonObject out, QString error) {
            if (error.isEmpty() && out.contains("access_token")) {
                store(out);
                finish(accessToken);
            } else {
                std::cout << "· no pude refrescar el token: " << error.toStdString() << "\n";
                doLogin();
            }
        });
        return;
    }
    doLogin();
}

void Spotify::login(std::function<void(bool)> done) {
    loginActive = true;
    loginDone = std::move(done);

    auto randomUrl = [](int bytes) {
        QByteArray buffer(bytes, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(
            reinterpret_cast<quint32*>(buffer.data()), bytes / 4);
        return QString::fromLatin1(buffer.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    pendingVerifier = randomUrl(64);
    const QString challenge = QString::fromLatin1(
        QCryptographicHash::hash(pendingVerifier.toUtf8(), QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    pendingState = randomUrl(16);

    server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, 8888)) {
        std::cout << "El puerto 8888 está ocupado (" << server->errorString().toStdString()
                  << "). Ciérralo y reintenta.\n";
        finishLogin(false);
        return;
    }

    auto buffer = std::make_shared<QByteArray>();
    connect(server, &QTcpServer::newConnection, this, [this, buffer]() {
        QTcpSocket* socket = server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;

            const QByteArray head = buffer->left(headerEnd);
            const int firstSpace = head.indexOf(' ');
            const int secondSpace = head.indexOf(' ', firstSpace + 1);
            const QByteArray target = (firstSpace >= 0 && secondSpace > firstSpace)
                ? head.mid(firstSpace + 1, secondSpace - firstSpace - 1) : QByteArray();

            const QUrl url("http://127.0.0.1" + QString::fromLatin1(target));
            const QUrlQuery query(url);
            const QString code = query.queryItemValue("code");
            const QString error = query.queryItemValue("error");
            const QString state = query.queryItemValue("state");

            const QByteArray html =
                "<h2>Listo. Cierra esta pestaña y vuelve a la terminal.</h2>";
            const QByteArray response = "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: " + QByteArray::number(html.size())
                + "\r\nConnection: close\r\n\r\n" + html;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
            if (server) server->close();

            if (!error.isEmpty()) {
                std::cout << "Autorización rechazada: " << error.toStdString() << "\n";
                finishLogin(false);
                return;
            }
            if (code.isEmpty()) {
                std::cout << "No llegó la respuesta. ¿Está " << REDIRECT_URI
                          << " en los Redirect URIs de tu app?\n";
                finishLogin(false);
                return;
            }
            if (state != pendingState) {
                std::cout << "state inválido, cancelado por seguridad.\n";
                finishLogin(false);
                return;
            }

            QUrlQuery body;
            body.addQueryItem("grant_type", "authorization_code");
            body.addQueryItem("code", code);
            body.addQueryItem("redirect_uri", REDIRECT_URI);
            body.addQueryItem("client_id", clientId);
            body.addQueryItem("code_verifier", pendingVerifier);
            postToken(body, [this](QJsonObject out, QString err) {
                if (err.isEmpty() && out.contains("access_token")) {
                    store(out);
                    finishLogin(true);
                } else {
                    std::cout << "· no pude obtener el token: " << err.toStdString() << "\n";
                    finishLogin(false);
                }
            });
        });
    });

    std::cout << "· Abriendo el navegador para autorizar Spotify…\n";
    QUrlQuery authQuery;
    authQuery.addQueryItem("client_id", clientId);
    authQuery.addQueryItem("response_type", "code");
    authQuery.addQueryItem("redirect_uri", REDIRECT_URI);
    authQuery.addQueryItem("scope", SCOPE);
    authQuery.addQueryItem("state", pendingState);
    authQuery.addQueryItem("code_challenge_method", "S256");
    authQuery.addQueryItem("code_challenge", challenge);
    QUrl authUrl(AUTH_URL);
    authUrl.setQuery(authQuery);
    QDesktopServices::openUrl(authUrl);

    QTimer::singleShot(300000, this, [this]() {
        if (loginActive) {
            std::cout << "· se agotó el tiempo de espera del login\n";
            finishLogin(false);
        }
    });
}

void Spotify::finishLogin(bool ok) {
    if (!loginActive) return;
    loginActive = false;
    if (!ok) authBroken = true;
    if (server) {
        server->close();
        server->deleteLater();
        server = nullptr;
    }
    auto done = std::move(loginDone);
    loginDone = nullptr;
    if (done) done(ok);
}

void Spotify::requestPlayback(PlaybackCb cb, bool retry) {
    ensureToken([this, cb, retry](QString token) {
        if (token.isEmpty()) {
            cb(QJsonObject(), "sin token");
            return;
        }
        QNetworkRequest req{QUrl(PLAYER_URL)};
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        req.setHeader(QNetworkRequest::UserAgentHeader, "HoverLyrics/1.0");
        QNetworkReply* reply = net->get(req);
        connect(reply, &QNetworkReply::finished, [this, reply, cb, retry]() {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            const QNetworkReply::NetworkError err = reply->error();
            const QString errorString = reply->errorString();
            reply->deleteLater();

            if (code == 401) {
                accessToken.clear();
                expiresAt = 0;
                if (retry) requestPlayback(cb, false);
                else cb(QJsonObject(), "401");
                return;
            }
            if (code == 204 || code == 404 || data.isEmpty()) {
                cb(QJsonObject(), QString());
                return;
            }
            if (err != QNetworkReply::NoError) {
                cb(QJsonObject(), errorString);
                return;
            }
            cb(QJsonDocument::fromJson(data).object(), QString());
        });
    });
}

void Spotify::playback(PlaybackCb cb) {
    requestPlayback(std::move(cb), true);
}
