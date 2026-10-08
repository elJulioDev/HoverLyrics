"""Letra sincronizada desde lrclib.net (gratis, sin API key).
La letra viene con timestamps por línea, que es lo que después usa la ventana
para saber qué línea mostrar en cada momento.
"""
import json
import re
import urllib.parse
import urllib.request

API = "https://lrclib.net/api/search"

def parse_synced(text):
    """Convierte la letra con timestamps en [(segundos, frase), ...].
    Espera líneas tipo `[mm:ss.xx] frase` (lrclib a veces usa `:` en lugar de
    `.` para las centésimas, así que se aceptan las dos).
    """
    lines = []
    for raw in text.splitlines():
        match = re.match(r"\[(\d+):(\d+(?:[.:]\d+)?)\]\s?(.*)", raw)
        if match:
            seconds = int(match.group(1)) * 60 + float(match.group(2).replace(":", "."))
            lines.append((seconds, match.group(3).strip()))
    return lines

def find_lyrics(title, artist, album, duration_ms):
    """La letra sincronizada de la mejor coincidencia, o None si no hay.
    Se buscan candidatos por título + artista y se elige el que mejor puntúa:
    cuánto se parecen los nombres y la duración, y —lo más importante— que
    tenga letra sincronizada.
    """
    consulta = urllib.parse.urlencode({"track_name": title, "artist_name": artist})
    req = urllib.request.Request(
        API + "?" + consulta, headers={"User-Agent": "HoverLyrics/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            hits = json.load(resp)
    except Exception:
        return None
    hits = [h for h in hits if h.get("syncedLyrics") and not h.get("instrumental")]
    if not hits:
        return None
    def score(hit):
        total = 3
        if (hit.get("trackName") or "").casefold() == title.casefold():
            total += 4
        if (hit.get("artistName") or "").casefold() == (artist or "").casefold():
            total += 2
        if album and (hit.get("albumName") or "").casefold() == album.casefold():
            total += 1
        misma_duracion = (hit.get("duration") and duration_ms
                          and abs(hit["duration"] - duration_ms / 1000) <= 3)
        if misma_duracion:
            total += 2
        return total

    return parse_synced(max(hits, key=score)["syncedLyrics"])

def current_line(lines, position_ms, duration_ms):
    """Qué índice de `lines` toca en `position_ms`, o None.
    Cada línea dura hasta que empieza la siguiente; la última hasta que termina
    la canción. Antes de la primera (o después del final) no hay nada que
    mostrar, y por eso devuelve None.
    """
    if not lines:
        return None
    position = position_ms / 1000
    if position < lines[0][0]:
        return None
    index = 0
    for i, (start, _) in enumerate(lines):
        if start <= position:
            index = i
        else:
            break
    end = lines[index + 1][0] if index + 1 < len(lines) else duration_ms / 1000
    return index if position < end else None
