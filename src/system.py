"""Diferencias entre sistemas operativos: tema del escritorio, fuente y foco."""
import subprocess
import sys
from pathlib import Path
WINDOWS = sys.platform == "win32"
LINUX = sys.platform.startswith("linux")
MAC = sys.platform == "darwin"

def default_font():
    """La familia que se ve nativa en cada sistema."""
    if WINDOWS:
        return ["Segoe UI", 11]
    if MAC:
        return ["Helvetica Neue", 11]
    return ["DejaVu Sans", 11]

def families():
    """Las familias de fuentes que ve Tk, o None si todavía no hay ventana."""
    try:
        from tkinter import font as tkfont
        return set(tkfont.families())
    except Exception:
        return None

def resolve_font(family, size):
    """Si la familia configurada no existe acá, cae a la del sistema.
    Sirve para que el mismo config.json se pueda llevar de un sistema a otro.
    """
    disponibles = families()
    if disponibles is not None and family not in disponibles:
        preferida = default_font()[0]
        if preferida in disponibles:
            family = preferida
    return (family, int(size))

def check_fonts():
    """Avisa si Tk no ve las fuentes del sistema.

    Le pasa al Python que descarga `uv` en Linux: su Tk viene sin Xft, ve solo
    la fuente "fixed" y el texto queda diminuto. No se puede arreglar desde el
    script, así que al menos lo decimos claro.
    """
    disponibles = families()
    if disponibles is not None and len(disponibles) <= 2:
        print("· ATENCIÓN: Tk casi no ve fuentes (%s): el texto va a salir diminuto."
              % ", ".join(sorted(disponibles)))
        print("  Es del Tk que trae el Python de uv, no del script. Solución:")
        print("  usá el Python del sistema (`python3 main.py`).")

def no_focus(window):
    """Intenta que el gestor de ventanas no le dé el foco al abrirla.
    En X11 con el tipo "utility"; en Windows con el estilo extendido
    WS_EX_NOACTIVATE. En macOS no hay equivalente simple y se deja como está.
    """
    try:
        if WINDOWS:
            _windows_no_activate(window)
        elif LINUX:
            window.attributes("-type", "utility")
    except Exception as exc:
        print("· no pude evitar el foco automático: %s" % exc)

def _windows_no_activate(window):
    """Marca la ventana como "no activable" con los estilos extendidos de Win32.
    `import ctypes` va acá adentro porque el módulo solo existe en Windows.
    """
    import ctypes
    user32 = ctypes.windll.user32
    hwnd = window.winfo_id()
    owner = user32.GetParent(hwnd)
    if owner and owner != user32.GetDesktopWindow():
        hwnd = owner
    GWL_EXSTYLE = -20
    WS_EX_NOACTIVATE = 0x08000000
    WS_EX_TOOLWINDOW = 0x00000080
    style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
    nuevo = style | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
    user32.SetWindowLongW(hwnd, GWL_EXSTYLE, nuevo)

def system_is_dark():
    """¿El escritorio está en modo oscuro?"""
    if WINDOWS:
        return _windows_is_dark()
    if MAC:
        return _mac_is_dark()
    return _linux_is_dark()

def _windows_is_dark():
    """Del registro: AppsUseLightTheme (0 = oscuro). Se importa winreg acá
    porque el módulo solo existe en Windows."""
    try:
        import winreg
        key = winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize")
        value, _ = winreg.QueryValueEx(key, "AppsUseLightTheme")
        return value == 0
    except Exception:
        return False

def _mac_is_dark():
    """`defaults read -g AppleInterfaceStyle` devuelve "Dark" si está oscuro."""
    try:
        out = subprocess.run(["defaults", "read", "-g", "AppleInterfaceStyle"],
                             capture_output=True, text=True, timeout=2).stdout
        return "dark" in out.lower()
    except Exception:
        return False

def _linux_is_dark():
    """KDE por la luminancia del fondo de ventana; GNOME por `gsettings`."""
    kdeglobals = Path.home() / ".config" / "kdeglobals"
    if kdeglobals.exists():
        background = _kde_window_background(kdeglobals.read_text(errors="ignore"))
        if background:
            red, green, blue = (int(x) for x in background.split(",")[:3])
            return (0.299 * red + 0.587 * green + 0.114 * blue) < 128
    try:
        out = subprocess.run(
            ["gsettings", "get", "org.gnome.desktop.interface", "color-scheme"],
            capture_output=True, text=True, timeout=2).stdout
        if out.strip():
            return "dark" in out.lower()
    except Exception:
        pass
    return False

def _kde_window_background(text):
    """BackgroundNormal ("R,G,B") de la sección [Colors:Window] de kdeglobals."""
    section = ""
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section == "Colors:Window" and line.startswith("BackgroundNormal="):
            return line.split("=", 1)[1].strip()
    return None
