"""Imágenes de los perfiles: elegir una al azar y prepararla para Tk.
La decodificación (PIL) se puede hacer fuera del hilo de Tk: es la parte más
cara y así no traba la animación. Crear el PhotoImage sí o sí va en el hilo de
Tk, pero es mucho más liviano.
"""
import functools
import random
from PIL import Image, ImageTk
from .config import ROOT

IMAGE_EXTS = {".gif", ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff"}
FALLBACK_DELAY = 100

def folder_images(folder):
    """Las imágenes de una carpeta, ordenadas (vacío si no existe)."""
    if not folder:
        return []
    path = ROOT / folder
    if not path.is_dir():
        return []
    return sorted(p for p in path.iterdir() if p.suffix.lower() in IMAGE_EXTS)

def pick_image(config, avoid=None):
    """Una imagen al azar de la carpeta del perfil elegido.
    Si ese perfil no existe o su carpeta está vacía, prueba con "default" y
    después con cualquier otro perfil que sí tenga imágenes. `avoid` (una ruta)
    se salta, para que al pedir otra no repita la que ya se está mostrando.
    """
    profiles = config.get("perfiles") or {}
    for name in [config.get("perfil"), "default"] + list(profiles):
        images = folder_images(profiles.get(name))
        if images:
            options = [p for p in images if p != avoid] or images
            return random.choice(options)
    return None

@functools.lru_cache(maxsize=8)
def _decode(path, size, bg):
    """Decodifica y escala todas las partes. Devuelve (frames PIL, demoras).
    Cacheado: decodificar un gif grande es lo más caro del programa y varias
    canciones pueden caer en la misma imagen.
    """
    if path is None:
        return [Image.new("RGB", (size, size), bg)], [FALLBACK_DELAY]
    image = Image.open(path)
    frames, delays = [], []
    for i in range(getattr(image, "n_frames", 1)):
        image.seek(i)
        frames.append(_fit(image, size, bg))
        delay = image.info.get("duration") or FALLBACK_DELAY
        delays.append(max(20, min(int(delay), 2000)))
    return frames, delays

def _fit(image, size, bg):
    """Encaja la imagen en una caja de size x size SIN deformarla.
    Mantiene la proporción (escala lo que más sobresale) y centra el resultado
    sobre el fondo de la ventana. Al agrandar usa NEAREST para no emborronar los
    gifs de emoji, que son pixel art; al achicar, LANCZOS.
    """
    art = image.convert("RGBA")
    scale = min(size / art.width, size / art.height)
    frame_size = (max(1, round(art.width * scale)), max(1, round(art.height * scale)))
    art = art.resize(frame_size, Image.NEAREST if scale > 1 else Image.LANCZOS)
    flat = Image.new("RGB", (size, size), bg)            # sobre el fondo de la ventana
    flat.paste(art, ((size - frame_size[0]) // 2, (size - frame_size[1]) // 2), art)
    return flat

def decode(path, size, bg):
    """(frames PIL, demoras) de la imagen, encajada en una caja de size x size.
    Devuelve copias de lo cacheado para que nadie pueda romper la cache. Se
    puede llamar desde cualquier hilo.
    """
    frames, delays = _decode(path, size, bg)
    return list(frames), list(delays)

def to_tk(frames):
    """PhotoImage: solo se puede llamar desde el hilo de Tk."""
    return [ImageTk.PhotoImage(frame) for frame in frames]
