#pragma once
#include <QString>
#include <QVector>
#include <QPair>
#include <functional>

// Letra sincronizada desde lrclib.net (gratis, sin API key).
namespace lyrics {

using Line = QPair<double, QString>;   // (segundos, frase)
using Lines = QVector<Line>;

// Pasa la letra con timestamps `[mm:ss.xx] frase` a [(seg, frase), ...].
Lines parseSynced(const QString& text);

// Índice de la línea que toca en `positionMs`, o -1 si no hay ninguna.
int currentLine(const Lines& lines, double positionMs, double durationMs);

// Busca la mejor coincidencia y devuelve sus líneas. `found` distingue "no hay
// nada sincronizado" (false) de "hay letra pero sin tiempos" (true, lines vacío).
void fetch(const QString& title, const QString& artist, const QString& album,
           double durationMs, std::function<void(Lines, bool found)> cb);

}
