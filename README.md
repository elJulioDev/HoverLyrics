# HoverLyrics

![licencia](https://img.shields.io/badge/licencia-MIT-blue)
![python](https://img.shields.io/badge/python-3.9%2B-blue)
![plataformas](https://img.shields.io/badge/plataforma-Linux%20%7C%20Windows%20%7C%20macOS-lightgrey)

Ventana emergente que muestra la **letra de la canción que está sonando en
Spotify**, una línea por vez: la ventana se abre, se cierra al terminar la línea
y se vuelve a abrir con la siguiente. A la izquierda va un gif (o png, webp,
jpg…) que sale de una carpeta según el **perfil** elegido.

![La ventana de HoverLyrics: el nombre de la canción en la barra de título, el gif del perfil a la izquierda y una línea de la letra](captura.png)

*(El nombre de la canción va en la barra de título y abajo aparece una línea de
la letra con el gif del perfil.)*

## Índice

- [Qué hace](#qué-hace)
- [Requisitos](#requisitos)
- [Crear la app de Spotify](#crear-la-app-de-spotify)
- [Uso](#uso)
- [Estructura](#estructura)
- [config.json](#configjson)
- [Cómo funciona](#cómo-funciona)
- [Diferencias por sistema](#diferencias-por-sistema)
- [Verificación](#verificación)
- [Licencia](#licencia)

## Qué hace

- Muestra **una línea de la letra por ventana**, sincronizada con lo que suena
  (`progress_ms` de Spotify + los timestamps de lrclib).
- El gif de la izquierda sale de la carpeta del **perfil** activo. Elegís el
  perfil en `config.json` y podés cambiarlo al vuelo con el **clic derecho**.
- **Clic izquierdo** en el gif → otra imagen al azar del mismo perfil.
- La ventana tiene **tema claro, oscuro, del sistema o colores propios**, y se
  puede arrastrar (recuerda dónde la dejaste).
- **No roba el foco** al aparecer, así no te interrumpe mientras hacés otra cosa.

## Requisitos

- **Python 3.9+** con `tkinter`.
- Una app de Spotify (gratis, ver abajo).
- [uv](https://docs.astral.sh/uv/) para las dependencias (`Pillow`). Si no lo
  tenés, con `pip install pillow` también anda.

> [!NOTE]
> El login con Spotify está hecho con la librería estándar (OAuth PKCE), así que
> la única dependencia es Pillow, que la instala `uv` sola.

> [!WARNING]
> En Linux, el Python que `uv` se descarga trae un Tk sin Xft: solo ve la fuente
> `fixed` y **el texto sale diminuto**. Por eso el proyecto usa tu Python del
> sistema, que sí ve las fuentes. Si igual te pasa, corré `python3 main.py`: el
> script avisa por terminal cuando lo detecta.

> [!WARNING]
> En Linux, `tkinter` no viene con Python: hay que instalarlo aparte
> (`sudo apt install python3-tk`, o el equivalente de tu distro). En Windows y
> macOS viene con el instalador oficial de Python.

## Crear la app de Spotify

1. Entrá a <https://developer.spotify.com/dashboard> e iniciá sesión.
2. **Create app**. El nombre y la descripción son lo que quieras.
3. En **Redirect URIs** agregá exactamente esta, sin nada más:

   ```
   http://127.0.0.1:8888/callback
   ```

4. En **APIs used** marcá **Web API**.
5. Guardá y copiá el **Client ID**.

> [!IMPORTANT]
> El **Client Secret no se usa**: el login es OAuth PKCE, que para una app de
> escritorio funciona solo con el Client ID. No lo pegues en ningún lado.

La primera vez el script te pide el Client ID por terminal y lo guarda en
`.env`, junto al proyecto.

## Uso

```bash
uv run main.py
```

La primera vez `uv` crea el entorno, instala Pillow y abre el navegador para que
autorices la app. Después, con Spotify reproduciendo algo:

| Acción | Qué hace |
|---|---|
| **Arrastrar** la ventana | la mueve; la posición se recuerda |
| **Clic izquierdo** en el gif | otra imagen al azar del perfil activo |
| **Clic derecho** en el gif | pasa al siguiente perfil y lo guarda |
| **X** de la ventana, o `Ctrl+C` | cierra el script |

> [!TIP]
> Si ya tenés Pillow instalado y no querés usar `uv`, `python3 main.py` funciona
> igual.

## Estructura

```
main.py          entrada: --selftest y arranque
pyproject.toml   dependencia de Pillow (para uv)
captura.png      la captura de arriba

src/
  config.py      .env, config.json, perfiles, temas
  system.py      diferencias entre Windows, Linux y macOS
  spotify.py     login OAuth PKCE y estado de reproducción
  lyrics.py      letra sincronizada (lrclib.net)
  images.py      elegir y decodificar las imágenes de los perfiles
  window.py      la ventana de Tk

gifs/            una carpeta por perfil
```

Al primer arranque se crean tres archivos más, en la carpeta del proyecto:

| archivo | qué guarda |
|---|---|
| `config.json` | los perfiles y los ajustes de la ventana |
| `.env` | el Client ID de Spotify |
| `token.json` | el token de la sesión |

## config.json

Todo lo editable vive en un solo archivo:

```json
{
  "perfil": "default",
  "perfiles": {
    "default": "gifs/default",
    "sad": "gifs/sad",
    "romantic": "gifs/romantic",
    "fun": "gifs/fun"
  },
  "ventana": {
    "tema": "oscuro",
    "colores": {},
    "ancho": 440,
    "alto": 92,
    "fuente": ["DejaVu Sans", 11],
    "icono_px": 45,
    "icono_margen": 20,
    "icono_dy": 4,
    "texto_gap": 22,
    "texto_margen": 12,
    "margen_y": 10,
    "siempre_arriba": true
  }
}
```

Si al archivo le falta alguna clave, se completa con el valor por defecto y se
escribe de vuelta, así siempre ves todas las opciones disponibles.

### Perfiles

Cada perfil es un nombre a elección que apunta a una carpeta. Ponés las
imágenes donde quieras y elegís el perfil con `"perfil"`; no dependen de la
canción ni del nombre de la carpeta.

Acepta cualquier formato que abra Pillow: `.gif`, `.webp` (también animados),
`.png`, `.jpg`, `.jpeg`, `.bmp`, `.tiff`.

- `icono_px` es la **caja** donde entran: cada imagen se escala manteniendo la
  proporción (sin deformarse) y se centra. Al agrandar usa NEAREST, para que los
  emojis pixel art no se emborronen; al achicar, LANCZOS.
- Cada gif se reproduce **a su propio ritmo** (se respeta la duración de cada
  frame, que puede variar entre frames).
- Se elige **una imagen al azar por canción** y se mantiene hasta que cambie el
  tema. Si el perfil no existe o su carpeta está vacía, cae a `default`.

> [!TIP]
> Para agregar un perfil nuevo, creá la carpeta dentro de `gifs/` y sumá la
> entrada en `"perfiles"`. El clic derecho va rotando en el orden en que estén
> escritos.

### Tema y colores

`"tema"` acepta `"oscuro"`, `"claro"` o `"sistema"` (detecta el tema del
escritorio).

Para colores propios, llená `"colores"`; pisan lo que traiga el tema:

```json
"tema": "sistema",
"colores": { "fondo": "#2b0a3d", "texto": "#ffd166" }
```

El resto son medidas en píxeles: `ancho`/`alto` de la ventana, `icono_px` (lado
de la caja de la imagen), `icono_margen` (al borde izquierdo), `icono_dy` (cuánto
se sube la imagen para centrarla con el texto), `texto_gap` (espacio entre
imagen y letra), `texto_margen`, `margen_y`, `fuente` (`[familia, tamaño]`) y
`siempre_arriba`.

> [!NOTE]
> Los cambios se aplican al reiniciar el script. Si traés un `config.json` de
> otro sistema y la fuente no existe, cae sola a la del sistema.

## Cómo funciona

Hay **dos hilos**, cada uno con una responsabilidad:

- **Red**: consulta Spotify cada 400 ms y, cuando cambia la canción, baja la
  letra de [lrclib.net](https://lrclib.net) y decodifica la imagen. Nunca toca la
  ventana.
- **UI** (el hilo de Tk): cada **100 ms** calcula qué línea toca y dibuja. Acá
  corren también la animación y los clics.

El hilo aparte es porque una petición HTTP puede tardar cientos de ms y
bloquearía la animación. Y como la letra ya trae el tiempo de cada línea, la
ventana **extrapola la posición** entre consulta y consulta: la red solo
corrige. Por eso una línea aparece con ≤100 ms de atraso y no hay que esperar a
la respuesta de Spotify en cada cambio.

> [!NOTE]
> Si se corta la conexión, la ventana no se cierra (queda la última línea), se
> reintenta cada 2 s en vez de cada 400 ms y el error se imprime una sola vez
> cada 30 s. Cuando vuelve, avisa y se resincroniza.

> [!WARNING]
> El login usa el puerto **8888**. Si está ocupado, la autorización falla:
> liberalo y reintentá.

> [!NOTE]
> Spotify dejó de exponer el género del artista (`genres: null`) y los
> audio-features (`valence`/`energy`: 403 para apps nuevas), así que no hay
> forma de deducir el estado de ánimo de una canción desde su API. Por eso los
> perfiles se eligen a mano.

## Diferencias por sistema

Todo está aislado en `src/system.py`; el resto del código es igual en los tres.

| | Linux (X11) | Windows | macOS |
|---|---|---|---|
| **No robar foco** | tipo `utility` (EWMH) | estilo `WS_EX_NOACTIVATE` | sin equivalente, no se aplica |
| **Tema `sistema`** | KDE (`kdeglobals`) o GNOME (`gsettings`) | registro `AppsUseLightTheme` | `AppleInterfaceStyle` |
| **Fuente por defecto** | DejaVu Sans | Segoe UI | Helvetica Neue |
| **tkinter** | `python3-tk` del sistema | instalador de Python | instalador de Python |

> [!NOTE]
> La detección del tema es *best effort*: si no puede leer la configuración del
> escritorio, usa el tema claro. El soporte de Windows está implementado con
> red de seguridad (si algo de Win32 falla, la app sigue andando y solo pierde la
> funcionalidad de no robar el foco), pero todavía no se probó en una máquina
> Windows.

## Verificación

```bash
uv run main.py --selftest
```

Corre los chequeos de la lógica pura —sin red y sin abrir la ventana— del parseo
de la letra, el timing de las líneas, el encaje de las imágenes, la config, los
temas, los perfiles y el manejo de errores de red.

## Licencia

MIT — ver [`LICENSE`](LICENSE). Hecho por **Alexis González**
([@elJulioDev](https://github.com/elJulioDev)).
