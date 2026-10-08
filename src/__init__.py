"""Partes de la app, separadas por responsabilidad.

- `config`:  `.env`, `config.json`, perfiles y temas
- `system`:  lo que cambia entre Windows, Linux y macOS
- `spotify`: login OAuth PKCE y estado de reproducción
- `lyrics`:  letra sincronizada desde lrclib.net
- `images`:  elegir y decodificar las imágenes de los perfiles
- `window`:  la ventana de Tk (hilos de red y de UI)
"""
