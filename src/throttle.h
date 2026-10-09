#pragma once
#include <QString>

// Imprime un mensaje la primera vez y después cada `repeatMs`. Sin esto, un
// corte de red llena la terminal con el mismo error varias veces por segundo.
// Devuelve true si corresponde imprimir. Pura para poder probarla.
inline bool throttle(QString& last, qint64& lastAt, const QString& message,
                     qint64 now, qint64 repeatMs = 30000) {
    if (message != last || now - lastAt >= repeatMs) {
        last = message;
        lastAt = now;
        return true;
    }
    return false;
}
