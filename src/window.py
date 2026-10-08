"""La ventana emergente con la letra.
Hay dos hilos y cada uno tiene su responsabilidad:
- **Red** (`_follow`): consulta Spotify cada `POLL_MS` y, cuando cambia la
  canción, baja la letra y decodifica una imagen. Deja el resultado en
  `self.pending` (bajo `self.lock`) y **nunca** toca widgets.
- **UI** (`_tick`, el hilo de Tk): cada `TICK_MS` mira ese estado, calcula qué
  línea toca y dibuja. La animación y los clics también corren acá.
El hilo aparte es porque una petición HTTP puede tardar cientos de ms y
bloquearía la animación. Y como la letra ya trae los tiempos de cada línea, la
ventana extrapola la posición entre consulta y consulta: la red solo corrige.
"""
import threading
import time
import tkinter as tk
from . import config as config_mod
from . import images
from . import system
from .lyrics import current_line, find_lyrics
from .spotify import playback

POLL_MS = 400              # cada cuánto se le pregunta a Spotify (red)
TICK_MS = 100              # cada cuánto se recalcula la línea (local, sin red)
OFFLINE_RETRY_S = 2.0      # si falla la red, reintentar más espaciado y sin spam
ERROR_REPEAT_S = 30        # no repetir el mismo error antes de esto
DRAG_HOLD_S = 1.0          # no tocar la ventana mientras la está arrastrando
PLACE_GRACE_S = 0.5        # margen para ignorar la colocación inicial del WM
MOVE_TOLERANCE_PX = 2      # diferencia que todavía consideramos "no la movió"
REROLL_POLL_MS = 20        # cada cuánto se mira si terminó el clic en el gif
REROLL_TIMEOUT_S = 5       # si el hilo del clic se cuelga, no esperar para siempre
LABEL_SLACK = 8            # aire extra del texto para que no se corte al envolver
WAITING = ("Spotify", "Esperando a Spotify…")   # ventana por defecto sin letra

def throttle(last_message, last_at, message):
    """Imprime `message` la primera vez y después cada ERROR_REPEAT_S.
    Sin esto, un corte de red o de DNS llena la terminal con el mismo error
    varias veces por segundo.
    """
    now = time.monotonic()
    if message != last_message or now - last_at >= ERROR_REPEAT_S:
        print("·", message)
        return message, now
    return last_message, last_at

def titlebar_offset(root):
    """Cuánto desplaza el gestor de ventanas una ventana respecto al `+x+y`.
    `geometry("+x+y")` pide una posición y `winfo_rootx/rooty` devuelve otra
    (difieren en la barra de título y los bordes). Se mide una vez con una
    ventana sonda; sirve para distinguir un arrastre del usuario y para
    recordar dónde la dejó.
    """
    probe = tk.Toplevel(root)
    system.no_focus(probe)
    probe.geometry("20x20+400+300")
    probe.update()
    offset = (probe.winfo_rootx() - 400, probe.winfo_rooty() - 300)
    probe.destroy()
    return offset

class LyricWindow:
    """Muestra la letra de lo que suena, una línea por vez.
    En cada cambio de línea cierra la ventana y abre una nueva (es el efecto
    pedido). El clic izquierdo sobre el gif cambia la imagen por otra al azar
    del perfil, y el derecho pasa al siguiente perfil de config.json.
    """
    def __init__(self, root, config, auth):
        self.root = root
        self.config = config
        self.auth = auth

        # ajustes, leídos una sola vez desde config.json
        win = config["ventana"]
        self.ancho, self.alto = int(win["ancho"]), int(win["alto"])
        self.fuente = system.resolve_font(*win["fuente"])
        self.palette = config_mod.colors(config)     # una vez: evita syscalls por línea
        self.icon_px = int(win["icono_px"])
        self.icon_pad = int(win["icono_margen"])
        self.icon_dy = int(win["icono_dy"])
        self.text_gap = int(win["texto_gap"])
        self.text_pad = int(win["texto_margen"])
        self.pad_y = int(win["margen_y"])
        self.topmost = bool(win.get("siempre_arriba", True))
        self.deco = titlebar_offset(root)

        # widgets (todavía no hay ventana)
        self.popup = None
        self.icon = None
        self.text = None

        # imagen
        self.current = None       # ruta de la imagen que se está mostrando
        self.frames = []          # PhotoImage de la imagen actual
        self.delays = []          # ms que dura cada frame
        self.frame_i = 0
        self._anim = None         # id del `after` de la animación, o None
        self._show_image(*self._decode(images.pick_image(config)))
        self.track_key = None     # canción para la que está cargada la imagen

        # posición / arrastre
        self.user_pos = None      # (x, y) elegido por el usuario; None = centrado
        self.free_until = 0.0     # no tocar la ventana hasta este instante
        self.placed = None        # (rootx, rooty) en que la pusimos nosotros
        self.created = 0.0        # cuándo se creó la ventana actual

        # estado de reproducción (lo escribe la red, lo lee la UI)
        self.shown = None         # (título, línea) que está en pantalla, o None
        self.pending = {}         # último estado que dejó el hilo de red
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self._deadline = time.monotonic() * 1000.0 + TICK_MS

        threading.Thread(target=self._follow, args=(self.stop,), daemon=True).start()
        self._tick()

    # Imagen
    def _decode(self, path):
        """Decodifica una imagen. PIL puro: se puede llamar desde cualquier hilo.
        Devuelve `(ruta, (frames, delays))`, listo para pasárselo a
        `_show_image`.
        """
        return path, images.decode(path, self.icon_px, self.palette["fondo"])

    def _show_image(self, path, decoded):
        """Pone una imagen ya decodificada en la ventana (solo hilo de Tk)."""
        self.current = path
        self.frames = images.to_tk(decoded[0])
        self.delays = decoded[1]
        self.frame_i = 0
        self._anim_stop()
        if self.icon is not None and self.icon.winfo_exists():
            self.icon.configure(image=self.frames[0])
            self._anim_start()

    def _reroll(self, *_):
        """Clic izquierdo en el gif: otra imagen al azar del mismo perfil."""
        if self.popup is None:
            return
        avoid = self.current
        box = {}

        def work():
            """Decodifica en un hilo aparte (el gif más pesado tarda ~100 ms)."""
            try:
                box["image"] = self._decode(images.pick_image(self.config, avoid))
            except Exception as exc:
                print("· no pude cambiar la imagen (%s)" % exc)
                box["image"] = None

        threading.Thread(target=work, daemon=True).start()
        self._await_reroll(box, time.time() + REROLL_TIMEOUT_S)

    def _next_profile(self, *_):
        """Clic derecho en el gif: pasa al siguiente perfil de config.json."""
        if self.popup is None:
            return
        names = list(self.config.get("perfiles") or {})
        if not names:
            return
        current = self.config.get("perfil")
        index = names.index(current) if current in names else -1
        self.config["perfil"] = names[(index + 1) % len(names)]     # da la vuelta
        config_mod.save(self.config)
        print("· perfil '%s' -> %s" % (
            self.config["perfil"], self.config["perfiles"][self.config["perfil"]]))
        self._reroll()

    def _await_reroll(self, box, deadline):
        """Espera el resultado del hilo del clic sin bloquear la animación."""
        if "image" in box:
            if box["image"]:
                self._show_image(*box["image"])
            return
        if time.time() < deadline:
            self.root.after(REROLL_POLL_MS, self._await_reroll, box, deadline)

    # Ventana
    def render(self, want):
        """Muestra `(título, línea)`, o cierra la ventana si `want` es None.

        Cada cambio cierra y vuelve a abrir la ventana. Si el usuario la está
        arrastrando no se toca nada hasta que suelte.
        """
        if time.time() < self.free_until:
            return
        if want == self.shown:
            return
        self.shown = want
        if self.popup is not None:
            self.popup.destroy()
            self.popup = self.icon = self.text = None
            self.placed = None
            self._anim_stop()
        if want is None:
            return

        title, line = want
        bg, fg = self.palette["fondo"], self.palette["texto"]
        self.popup = tk.Toplevel(self.root)
        self.popup.title(title)
        self.popup.configure(bg=bg)
        if self.topmost:
            self.popup.attributes("-topmost", True)
        system.no_focus(self.popup)
        self.popup.resizable(False, False)
        self.popup.protocol("WM_DELETE_WINDOW", self.quit)
        self.popup.bind("<Configure>", self._moved)
        self.icon = tk.Label(self.popup, bg=bg, image=self.frames[0], bd=0)
        self.icon.pack(side="left", padx=(self.icon_pad, self.text_gap),
                       pady=(self.pad_y - self.icon_dy, self.pad_y + self.icon_dy))
        self.icon.configure(cursor="hand2")
        self.icon.bind("<Button-1>", self._reroll)          # otra imagen al azar
        self.icon.bind("<Button-3>", self._next_profile)    # siguiente perfil

        self.text = tk.Label(
            self.popup, bg=bg, fg=fg, text=line, font=self.fuente, justify="left",
            anchor="w",
            wraplength=(self.ancho - self.icon_pad - self.icon_px
                        - self.text_gap - self.text_pad - LABEL_SLACK))
        self.text.pack(side="left", fill="both", expand=True,
                       padx=(0, self.text_pad), pady=self.pad_y)

        x, y = self.user_pos if self.user_pos is not None else self._centered()
        self.popup.geometry("%dx%d+%d+%d" % (self.ancho, self.alto, x, y))
        self.placed = (x + self.deco[0], y + self.deco[1])
        self.created = time.time()
        self.frame_i = 0
        self._anim_start()

    def _centered(self):
        """Posición centrada en la pantalla."""
        return ((self.root.winfo_screenwidth() - self.ancho) // 2,
                (self.root.winfo_screenheight() - self.alto) // 2)

    def _moved(self, event):
        """El usuario movió la ventana: recordarla y no cerrarla mientras
        la mueve."""
        if event.widget is not self.popup or self.placed is None:
            return
        if time.time() - self.created < PLACE_GRACE_S:
            return
        pos = (self.popup.winfo_rootx(), self.popup.winfo_rooty())
        if (abs(pos[0] - self.placed[0]) <= MOVE_TOLERANCE_PX
                and abs(pos[1] - self.placed[1]) <= MOVE_TOLERANCE_PX):
            return
        self.placed = pos
        self.user_pos = (pos[0] - self.deco[0], pos[1] - self.deco[1])
        self.free_until = time.time() + DRAG_HOLD_S

    def quit(self, *_):
        """La X de la ventana detiene todo el script."""
        self.stop.set()
        self.root.quit()

    # Animación
    # Solo corre cuando hay ventana y la imagen tiene más de un frame: con un
    # png fijo no queda ningún timer dando vueltas.
    def _anim_start(self):
        if self._anim is None and self.icon is not None and len(self.frames) > 1:
            self.frame_i = 1 % len(self.frames)
            self._anim = self.root.after(self.delays[0], self._frame)

    def _anim_stop(self):
        if self._anim is not None:
            self.root.after_cancel(self._anim)
            self._anim = None

    def _frame(self):
        self._anim = None
        if self.icon is None or not self.icon.winfo_exists() or len(self.frames) <= 1:
            return
        self.icon.configure(image=self.frames[self.frame_i])
        delay = self.delays[self.frame_i]
        self.frame_i = (self.frame_i + 1) % len(self.frames)
        self._anim = self.root.after(delay, self._frame)

    # Seguimiento
    def _follow(self, stop):
        """Hilo de red: mantiene `self.pending` con lo último de Spotify.
        No manda "la línea a mostrar" sino el estado crudo (posición y cuándo se
        consultó): así la UI calcula la línea por su cuenta y no hay que esperar
        a la red entre línea y línea.
        Si la red falla no se publica nada: se deja el último estado como está
        (la letra sigue avanzando con la música, que sigue sonando) y se
        reintenta más espaciado, sin repetir el error en cada intento.
        """
        track_key = title = lines = image = None
        error = None            # último error, para no repetirlo
        error_at = 0.0
        while not stop.is_set():
            started = time.monotonic()
            try:
                state = playback(self.auth)
            except Exception as exc:
                error, error_at = throttle(
                    error, error_at, "error consultando Spotify: %s" % exc)
                stop.wait(OFFLINE_RETRY_S)
                continue
            if error is not None:
                print("· Spotify volvió a responder")
                error = None
            # el dato es de más o menos la mitad del viaje, no de cuando llegó
            now = (started + time.monotonic()) / 2

            snapshot = {"key": None, "playing": False, "at": now}
            if state and state.get("is_playing"):
                item = state.get("item") or {}
                if item.get("name") and item.get("type") == "track":
                    key = item.get("id") or "%s|%s" % (
                        item["name"], (item.get("artists") or [{}])[0].get("name", ""))
                    if key != track_key:                 # canción nueva
                        track_key = key
                        title = item["name"]
                        artist = (item.get("artists") or [{}])[0].get("name", "")
                        album = (item.get("album") or {}).get("name")
                        duracion = item.get("duration_ms") or 0
                        lines = find_lyrics(title, artist, album, duracion)
                        # decodificar acá (PIL puro) deja casi libre el hilo de UI
                        image = self._decode(images.pick_image(self.config))
                        estado = "letra sincronizada" if lines else "sin letra"
                        print("♪ %s — %s | %s" % (title, artist, estado))
                    snapshot = {
                        "key": track_key, "playing": True, "at": now,
                        "title": title, "lines": lines, "image": image,
                        "position": state.get("progress_ms") or 0,
                        "duration": duracion,
                    }
            else:
                track_key, lines, image = None, None, None

            with self.lock:
                self.pending = snapshot
            stop.wait(POLL_MS / 1000.0)

    def _tick(self):
        """Hilo de UI: calcula la línea con lo último que dijo Spotify y dibuja."""
        with self.lock:
            snapshot = self.pending

        key = snapshot.get("key")
        if key != self.track_key:
            self.track_key = key
            if snapshot.get("image") is not None:
                self._show_image(*snapshot["image"])

        want = WAITING
        if snapshot.get("playing"):
            lines = snapshot.get("lines")
            if lines is None:
                want = (snapshot["title"], "♪  sin letra sincronizada")
            else:
                # extrapolar desde la última consulta: la red solo corrige
                elapsed = (time.monotonic() - snapshot["at"]) * 1000.0
                index = current_line(lines, snapshot["position"] + elapsed,
                                     snapshot["duration"])
                if index is not None:
                    want = (snapshot["title"], lines[index][1] or "♪")
        self.render(want)

        # cadencia fija: si el trabajo de este tick se pasó del tiempo, el
        # siguiente se agenda antes para no ir arrastrando el retraso
        now = time.monotonic() * 1000.0
        self._deadline += TICK_MS
        if self._deadline < now:
            self._deadline = now + TICK_MS
        self.root.after(max(1, round(self._deadline - now)), self._tick)
