#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <functional>
#include <vector>

class QNetworkAccessManager;
class QUrlQuery;
class QTcpServer;

// Spotify: login OAuth PKCE, refresco de token y estado de reproducción.
// No se usa el Client Secret: PKCE alcanza para una app de escritorio.
class Spotify : public QObject {
public:
    using PlaybackCb = std::function<void(const QJsonObject& state, const QString& error)>;

    explicit Spotify(QString clientId, QObject* parent = nullptr);

    // Estado actual, o {} con error vacío si no hay nada sonando.
    void playback(PlaybackCb cb);

private:
    void ensureToken(std::function<void(QString)> cb);
    void requestPlayback(PlaybackCb cb, bool retry);
    void postToken(const QUrlQuery& body, std::function<void(QJsonObject, QString)> cb);
    void login(std::function<void(bool)> done);
    void finishLogin(bool ok);
    void store(const QJsonObject& data);

    QString clientId;
    QString accessToken;
    QString refreshToken;
    qint64 expiresAt = 0;                 // segundos epoch
    QNetworkAccessManager* net;
    QTcpServer* server = nullptr;
    QString pendingVerifier;
    QString pendingState;
    std::function<void(bool)> loginDone;
    bool loadingToken = false;
    bool loginActive = false;
    bool authBroken = false;              // login falló: no reintentar en bucle
    std::vector<std::function<void(QString)>> tokenWaiters;
};
