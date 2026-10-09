#include "lyrics.h"
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>

namespace lyrics {

static const char* API = "https://lrclib.net/api/search";

Lines parseSynced(const QString& text) {
    // lrclib a veces usa `:` en lugar de `.` para las centésimas: se aceptan las dos.
    static const QRegularExpression re(QStringLiteral(R"(\[(\d+):(\d+(?:[.:]\d+)?)\]\s?(.*))"));
    Lines lines;
    for (const QString& raw : text.split('\n')) {
        const QRegularExpressionMatch m = re.match(raw);
        if (m.hasMatch()) {
            const double seconds = m.captured(1).toInt() * 60
                + QString(m.captured(2)).replace(':', '.').toDouble();
            lines.append({seconds, m.captured(3).trimmed()});
        }
    }
    return lines;
}

int currentLine(const Lines& lines, double positionMs, double durationMs) {
    if (lines.isEmpty()) return -1;
    const double position = positionMs / 1000.0;
    if (position < lines[0].first) return -1;
    int index = 0;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].first <= position) index = i;
        else break;
    }
    const double end = (index + 1 < lines.size())
        ? lines[index + 1].first : durationMs / 1000.0;
    return position < end ? index : -1;
}

void fetch(const QString& title, const QString& artist, const QString& album,
           double durationMs, std::function<void(Lines, bool)> cb) {
    static QNetworkAccessManager* net = new QNetworkAccessManager(QCoreApplication::instance());
    QUrl url(API);
    QUrlQuery query;
    query.addQueryItem("track_name", title);
    query.addQueryItem("artist_name", artist);
    url.setQuery(query);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "HoverLyrics/1.0");

    QNetworkReply* reply = net->get(req);
    QObject::connect(reply, &QNetworkReply::finished, [reply, title, artist, album, durationMs, cb]() {
        reply->deleteLater();
        Lines result;
        bool found = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isArray()) {
                QJsonObject best;
                int bestScore = -1;
                bool have = false;
                for (const QJsonValue& value : doc.array()) {
                    const QJsonObject hit = value.toObject();
                    if (hit.value("syncedLyrics").toString().isEmpty()) continue;
                    if (hit.value("instrumental").toBool()) continue;

                    int score = 3;
                    if (hit.value("trackName").toString().compare(title, Qt::CaseInsensitive) == 0) score += 4;
                    if (hit.value("artistName").toString().compare(artist, Qt::CaseInsensitive) == 0) score += 2;
                    if (!album.isEmpty()
                        && hit.value("albumName").toString().compare(album, Qt::CaseInsensitive) == 0) score += 1;
                    const double dur = hit.value("duration").toDouble();
                    if (dur > 0 && durationMs > 0 && qAbs(dur - durationMs / 1000.0) <= 3) score += 2;

                    if (score > bestScore) {
                        bestScore = score;
                        best = hit;
                        have = true;
                    }
                }
                if (have) {
                    found = true;
                    result = parseSynced(best.value("syncedLyrics").toString());
                }
            }
        }
        cb(result, found);
    });
}

}
