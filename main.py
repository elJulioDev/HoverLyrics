"""Punto de entrada: ventana emergente con la letra de lo que suena en Spotify.
Uso:  uv run main.py     (o: python3 main.py)
"""
import sys

try:
    import tkinter as tk
except ImportError:
    raise SystemExit(
        "Falta tkinter.\n"
        "  Linux:   sudo apt install python3-tk   (o el de tu distro)\n"
        "  Windows: reinstalá Python marcando 'tcl/tk and IDLE'")

from PIL import Image
from src import config, images, lyrics, spotify, system
from src.window import ERROR_REPEAT_S, LyricWindow, throttle

def check_lyrics():
    """Parseo de la letra y qué línea toca en cada momento."""
    lines = lyrics.parse_synced("[00:10.56]hola\n[01:02.30]mundo\nbasura\n")
    assert lines == [(10.56, "hola"), (62.30, "mundo")], lines
    # antes de la 1ª
    assert lyrics.current_line(lines, 5000, 120000) is None
    assert lyrics.current_line(lines, 10560, 120000) == 0
    # dura hasta la 2ª
    assert lyrics.current_line(lines, 62000, 120000) == 0
    assert lyrics.current_line(lines, 62300, 120000) == 1
    # pasó el final
    assert lyrics.current_line(lines, 120000, 120000) is None
    # sin letra
    assert lyrics.current_line([], 1000, 1000) is None

def check_config():
    """La config se completa sola y los temas se resuelven bien."""
    assert config.normalize({})["ventana"]["ancho"] == 440
    assert config.normalize({"ventana": {"ancho": 500}})["ventana"]["ancho"] == 500
    assert config.normalize({"ventana": {"ancho": 500}})["perfil"] == "default"
    assert config.colors({"ventana": {"tema": "claro"}})["fondo"] == "#f5f5f5"
    assert config.colors({"ventana": {"tema": "oscuro"}})["texto"] == "#f2f2f2"
    sistema = config.colors({"ventana": {"tema": "sistema"}})
    assert sistema["fondo"] in ("#1e1e1e", "#f5f5f5")
    con_custom = {"tema": "claro", "colores": {"fondo": "#123456"}}
    custom = config.colors({"ventana": con_custom})
    assert custom["fondo"] == "#123456" and custom["texto"] == "#1a1a1a"

def check_images():
    """Elección de perfil y encaje sin deformar."""
    assert images.folder_images("") == []
    assert images.folder_images("no/existe") == []
    directo = {"perfil": "d", "perfiles": {"d": "gifs/default"}}
    assert images.pick_image(directo).parent.name == "default"
    caida = {"perfil": "x", "perfiles": {"default": "gifs/default"}}
    assert images.pick_image(caida).parent.name == "default"
    assert images.pick_image({"perfiles": {}}) is None

    frames, delays = images.decode(None, 30, "#000000")
    assert len(frames) == 1 and len(delays) == 1

    # 60x20 en una caja de 40 -> 40x13, centrado y sin deformarse
    # (_fit es privada, pero es la cuenta que interesa cuidar y así se prueba
    #  sin tener que armar imágenes de prueba en disco)
    wide = images._fit(Image.new("RGBA", (60, 20), (255, 0, 0, 255)), 40, "#000000")
    box = wide.getbbox()
    assert (box[2] - box[0], box[3] - box[1]) == (40, 13), box
    assert (box[0], box[1]) == (0, (40 - 13) // 2), box
    # 20x60 -> 13x40, centrado
    tall = images._fit(Image.new("RGBA", (20, 60), (255, 0, 0, 255)), 40, "#000000")
    box = tall.getbbox()
    assert (box[2] - box[0], box[3] - box[1]) == (13, 40), box
    assert box[0] == (40 - 13) // 2, box

def check_platform():
    """Diferencias por sistema operativo."""
    assert system.default_font()[1] == 11
    assert system.resolve_font("DejaVu Sans", 11)[1] == 11

def check_errors():
    """Un corte de red no puede repetir el mismo error en cada reintento."""
    import io
    import time
    from contextlib import redirect_stdout

    salida = io.StringIO()
    with redirect_stdout(salida):
        mensaje, cuando = throttle(None, 0.0, "falla A")
        mensaje, cuando = throttle(mensaje, cuando, "falla A")     # repetido: se calla
        mensaje, cuando = throttle(mensaje, cuando, "falla B")     # distinto: sale
        # pasado el rato, el mismo error vuelve a salir
        throttle(mensaje, time.monotonic() - ERROR_REPEAT_S - 1, "falla B")
    texto = salida.getvalue()
    assert texto.count("falla A") == 1, texto
    assert texto.count("falla B") == 2, texto

def selftest():
    """Chequeos de la lógica pura: sin red y sin ventana.
    Se corre con `--selftest` para no tener que abrir la app.
    """
    check_lyrics()
    check_config()
    check_images()
    check_platform()
    check_errors()
    print("selftest ok")

def main():
    """Arranca la app: config, login y ventana."""
    if "--selftest" in sys.argv:
        selftest()
        return

    cfg = config.load()
    palette = config.colors(cfg)
    auth = spotify.Auth(config.load_client_id())

    root = tk.Tk()
    # el root no se ve, solo crea la ventana
    root.withdraw()
    system.check_fonts()
    # icono transparente: intenta quitar el logo por defecto de la barra de título.
    # La referencia queda en el root para que el recolector no se lo lleve.
    root._blank_icon = tk.PhotoImage(width=1, height=1)
    root.iconphoto(True, root._blank_icon)

    print("· perfil '%s' | tema '%s' (%s sobre %s)" % (
        cfg["perfil"], cfg["ventana"]["tema"], palette["texto"], palette["fondo"]))
    app = LyricWindow(root, cfg, auth)
    print("· escuchando a Spotify… (Ctrl+C para salir)")
    try:
        root.mainloop()
    except KeyboardInterrupt:
        pass
    finally:
        # frena el hilo de red
        app.stop.set()
        root.destroy()

if __name__ == "__main__":
    main()
