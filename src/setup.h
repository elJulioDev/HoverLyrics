#pragma once
#include <QString>

// Primer arranque: ventana con el tutorial para crear la app de Spotify y pegar
// el Client ID. Devuelve el ID (ya guardado en .env), o vacío si se canceló.
namespace setup {

QString clientIdDialog();

}
