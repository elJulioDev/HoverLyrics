"""Configuración: credenciales (.env), perfiles y ventana (config.json), temas."""
import json
import os
from pathlib import Path
from . import system

ROOT = Path(__file__).resolve().parent.parent
ENV_FILE = ROOT / ".env"
CONFIG_FILE = ROOT / "config.json"
CLIENT_ID_KEY = "SPOTIFY_CLIENT_ID"

# Paletas base; "colores" en config.json las puede pisar una por una.
TEMAS = {
    "oscuro": {"fondo": "#1e1e1e", "texto": "#f2f2f2"},
    "claro": {"fondo": "#f5f5f5", "texto": "#1a1a1a"},
}

# Lo que se escribe en config.json cuando falta. "ventana" son las medidas en
# píxeles y los colores; todo es editable sin tocar el código.
DEFAULT_CONFIG = {
    "perfil": "default",
    "perfiles": {
        "default": "gifs/default",
        "sad": "gifs/sad",
        "romantic": "gifs/romantic",
        "fun": "gifs/fun",
    },
    "ventana": {
        "tema": "oscuro",           # oscuro | claro | sistema
        "colores": {},              # custom: {"fondo": "#..", "texto": "#.."}
        "ancho": 440,
        "alto": 92,
        "fuente": system.default_font(),
        "icono_px": 45,
        "icono_margen": 20,
        "icono_dy": 4,
        "texto_gap": 22,
        "texto_margen": 12,
        "margen_y": 10,
        "siempre_arriba": True,
    },
}

def load():
    """Lee config.json (o lo crea con el ejemplo). Devuelve siempre un dict
    completo: lo que falte se rellena con DEFAULT_CONFIG y se guarda, así el
    archivo siempre muestra todas las opciones disponibles."""
    data = {}
    if CONFIG_FILE.exists():
        try:
            data = json.loads(CONFIG_FILE.read_text())
        except Exception as exc:
            print("· config.json inválido (%s), uso los valores por defecto" % exc)
    else:
        print("· creando config.json con los valores por defecto")
    before = json.loads(json.dumps(data))
    config = normalize(data)
    if config != before:
        save(config)
    return config

def save(config):
    """Escribe config.json (lo usa el clic derecho al cambiar de perfil)."""
    CONFIG_FILE.write_text(json.dumps(config, indent=2, ensure_ascii=False) + "\n")

def normalize(data):
    """Rellena lo que falte sin pisar lo que ya hay (ni claves desconocidas)."""
    config = json.loads(json.dumps(data)) if data else {}
    config.setdefault("perfil", DEFAULT_CONFIG["perfil"])
    config.setdefault("perfiles", DEFAULT_CONFIG["perfiles"])
    window = dict(DEFAULT_CONFIG["ventana"])
    window.update(config.get("ventana") or {})
    config["ventana"] = window
    return config

def colors(config):
    """Colores finales de la ventana según el tema y los colores custom."""
    window = config["ventana"]
    name = str(window.get("tema", "oscuro")).lower()
    if name == "sistema":
        name = "oscuro" if system.system_is_dark() else "claro"
    palette = dict(TEMAS.get(name, TEMAS["oscuro"]))
    palette.update(window.get("colores") or {})
    return palette

def env_value(key, hint):
    """Lee `key` de .env o del entorno; si no está, la pide y la agrega a .env."""
    text = ENV_FILE.read_text() if ENV_FILE.exists() else ""
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            name, value = line.split("=", 1)
            if name.strip() == key:
                return value.strip()
    value = os.environ.get(key, "").strip()
    if value:
        return value
    print(hint)
    value = input("%s: " % key).strip()
    with ENV_FILE.open("a") as fh:
        if text and not text.endswith("\n"):
            fh.write("\n")
        fh.write("%s=%s\n" % (key, value))
    return value

def load_client_id():
    """El Client ID de la app de Spotify (del .env o preguntándolo)."""
    pista = "Necesito el Client ID de tu app de Spotify (developer.spotify.com)."
    client_id = env_value(CLIENT_ID_KEY, pista)
    if not client_id:
        raise SystemExit("Sin Client ID no hay nada que hacer.")
    return client_id
