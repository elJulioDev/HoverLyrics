"""Spotify: login OAuth PKCE y estado de reproducción.
No se usa el Client Secret: PKCE alcanza para una app de escritorio, así que
con el Client ID sobra.
"""
import base64
import hashlib
import json
import os
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from .config import ROOT

TOKEN_FILE = ROOT / "token.json"
REDIRECT_URI = "http://127.0.0.1:8888/callback"
AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
PLAYER_URL = "https://api.spotify.com/v1/me/player"
SCOPE = "user-read-playback-state"

def _post_token(data):
    """POST al endpoint de tokens de Spotify y devuelve el JSON."""
    body = urllib.parse.urlencode(data).encode()
    req = urllib.request.Request(
        TOKEN_URL, data=body,
        headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.load(resp)

class Auth:
    """El token de Spotify: lo carga de token.json, lo refresca o hace el login.
    El login es el flujo PKCE: se abre el navegador, se levanta un servidor
    local que recibe el `code` y se canjea por un token. De ahí en adelante
    alcanza con el refresh token.
    """

    def __init__(self, client_id):
        self.client_id = client_id
        self.refresh_token = None
        self.access_token = None
        self.expires_at = 0.0
        if TOKEN_FILE.exists():
            saved = json.loads(TOKEN_FILE.read_text())
            self.refresh_token = saved.get("refresh_token")
            self.access_token = saved.get("access_token")
            self.expires_at = saved.get("expires_at", 0.0)

    def _store(self, data):
        """Guarda la respuesta del endpoint de tokens y actualiza el estado."""
        self.access_token = data["access_token"]
        self.expires_at = time.time() + data.get("expires_in", 3600)
        if data.get("refresh_token"):
            self.refresh_token = data["refresh_token"]
        TOKEN_FILE.write_text(json.dumps({
            "access_token": self.access_token,
            "expires_at": self.expires_at,
            "refresh_token": self.refresh_token,
        }, indent=2))
        if os.name == "posix":
            TOKEN_FILE.chmod(0o600)

    def token(self):
        """Un access token válido, refrescando o haciendo login si hace falta."""
        if self.access_token and time.time() < self.expires_at - 60:
            return self.access_token
        if self.refresh_token:
            try:
                self._store(_post_token({
                    "grant_type": "refresh_token",
                    "refresh_token": self.refresh_token,
                    "client_id": self.client_id,
                }))
                return self.access_token
            except Exception as exc:
                print("· no pude refrescar el token:", exc)
        self._login()
        return self.access_token

    def _login(self):
        """Flujo PKCE: navegador + servidor local en 127.0.0.1:8888."""
        verifier = base64.urlsafe_b64encode(os.urandom(64)).rstrip(b"=").decode()
        challenge = base64.urlsafe_b64encode(
            hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
        state = base64.urlsafe_b64encode(os.urandom(16)).rstrip(b"=").decode()
        got = {}

        class Handler(BaseHTTPRequestHandler):
            """Recibe el redirect de Spotify con el `code`."""

            def do_GET(self):
                got.update(urllib.parse.parse_qs(
                    urllib.parse.urlparse(self.path).query))
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                aviso = "<h2>Listo. Cierra esta pestaña y vuelve a la terminal.</h2>"
                self.wfile.write(aviso.encode())

            def log_message(self, *args):
                pass
        try:
            server = HTTPServer(("127.0.0.1", 8888), Handler)
        except OSError as exc:
            raise SystemExit(
                "El puerto 8888 está ocupado (%s). Ciérralo y reintenta." % exc)
        server.timeout = 300
        threading.Thread(target=server.handle_request, daemon=True).start()

        print("· Abriendo el navegador para autorizar Spotify…")
        webbrowser.open(AUTH_URL + "?" + urllib.parse.urlencode({
            "client_id": self.client_id,
            "response_type": "code",
            "redirect_uri": REDIRECT_URI,
            "scope": SCOPE,
            "state": state,
            "code_challenge_method": "S256",
            "code_challenge": challenge,
        }))

        deadline = time.time() + 300
        while not got and time.time() < deadline:
            time.sleep(0.2)
        server.server_close()

        if "error" in got:
            raise SystemExit("Autorización rechazada: %s" % got["error"][0])
        if "code" not in got:
            raise SystemExit(
                "No llegó la respuesta. ¿Está %s en los Redirect URIs de tu app?"
                % REDIRECT_URI)
        if got.get("state", [None])[0] != state:
            raise SystemExit("state inválido, cancelado por seguridad.")
        self._store(_post_token({
            "grant_type": "authorization_code",
            "code": got["code"][0],
            "redirect_uri": REDIRECT_URI,
            "client_id": self.client_id,
            "code_verifier": verifier,
        }))

def playback(auth):
    """Estado de reproducción actual, o None si no hay nada sonando."""
    def request():
        req = urllib.request.Request(PLAYER_URL, headers={
            "Authorization": "Bearer " + auth.token()})
        with urllib.request.urlopen(req, timeout=15) as resp:
            body = resp.read()
            return json.loads(body) if body else None
    try:
        return request()
    except urllib.error.HTTPError as exc:
        if exc.code == 401:
            auth.access_token, auth.expires_at = None, 0.0
            return request()
        if exc.code in (204, 404):
            return None
        raise
