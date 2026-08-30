"""Local HTTP server for CoreFSM Studio."""

from __future__ import annotations

import json
import mimetypes
from pathlib import Path
import threading
from urllib.parse import parse_qs, unquote, urlparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .core import StudioError, StudioWorkspace
from .serialmon import MONITOR, SerialMonitor, suggested_baud


STATIC_ROOT = Path(__file__).with_name("static")
MAX_BODY = 2 * 1024 * 1024


class StudioRequestHandler(BaseHTTPRequestHandler):
    workspace = None
    server_version = "CoreFSMStudio/0.1"

    def log_message(self, fmt, *args):
        # Keep the console useful: static asset noise does not help the user.
        if self.path.startswith("/api/") or int(args[1]) >= 400:
            super().log_message(fmt, *args)

    def do_GET(self):
        try:
            route = urlparse(self.path)
            if route.path == "/api/bootstrap":
                return self._json(200, self.workspace.bootstrap())
            parts = self._api_parts(route.path)
            if parts[:1] == ["serial"]:
                return self._json(200, self._serial_get(parts, parse_qs(route.query)))
            if len(parts) == 2 and parts[0] == "projects":
                return self._json(200, self.workspace.load_project(parts[1]))
            if len(parts) == 3 and parts[0] == "projects" and parts[2] == "file":
                query = parse_qs(route.query)
                relative = (query.get("path") or [""])[0]
                return self._json(200, self.workspace.read_file(parts[1], relative))
            return self._static(route.path)
        except StudioError as exc:
            return self._json(exc.status, exc.as_dict())
        except Exception as exc:
            return self._json(500, {"error": "internal_error", "message": str(exc)})

    def do_POST(self):
        try:
            parts = self._api_parts(urlparse(self.path).path)
            payload = self._body()
            if parts[:1] == ["serial"]:
                return self._json(200, self._serial_post(parts, payload))
            if parts == ["projects"]:
                value = self.workspace.create_project(
                    payload.get("name"), payload.get("board", "nano"),
                    payload.get("preset", "starter"), payload.get("displayName"))
                return self._json(201, value)
            if len(parts) == 3 and parts[0] == "projects" and parts[2] == "actions":
                return self._json(200, self.workspace.run_action(parts[1], payload.get("action")))
            raise StudioError("Ruta API no encontrada", "not_found", 404)
        except StudioError as exc:
            return self._json(exc.status, exc.as_dict())
        except Exception as exc:
            return self._json(500, {"error": "internal_error", "message": str(exc)})

    def do_PUT(self):
        try:
            parts = self._api_parts(urlparse(self.path).path)
            payload = self._body()
            if len(parts) == 3 and parts[0] == "projects" and parts[2] == "model":
                return self._json(200, self.workspace.save_model(parts[1], payload.get("model") or {}))
            if len(parts) == 3 and parts[0] == "projects" and parts[2] == "file":
                value = self.workspace.write_file(
                    parts[1], payload.get("path"), payload.get("content", ""), payload.get("revision"))
                return self._json(200, value)
            raise StudioError("Ruta API no encontrada", "not_found", 404)
        except StudioError as exc:
            return self._json(exc.status, exc.as_dict())
        except Exception as exc:
            return self._json(500, {"error": "internal_error", "message": str(exc)})

    # ------------------------------------------------------------------
    # Monitor serie
    #
    # Vive aquí y no en StudioWorkspace porque no toca el sistema de archivos
    # del proyecto: es un recurso del proceso, uno solo, compartido por todas
    # las pestañas que tenga abiertas el usuario.
    # ------------------------------------------------------------------
    def _serial_get(self, parts, query):
        if parts == ["serial", "ports"]:
            ports = SerialMonitor.list_ports()
            if ports is None:
                raise StudioError(
                    "El monitor necesita pyserial y este Python no lo tiene",
                    "serial_unavailable", 503)
            value = {"ports": ports, "connected": MONITOR.connected}
            project = (query.get("project") or [""])[0]
            if project:
                try:
                    baud = suggested_baud(self.workspace._project_path(project))
                except StudioError:
                    baud = None
                if baud:
                    value["suggestedBaud"] = baud
            return value
        if parts == ["serial", "poll"]:
            try:
                cursor = int((query.get("cursor") or ["0"])[0])
            except ValueError:
                cursor = 0
            return MONITOR.read(cursor)
        raise StudioError("Ruta API no encontrada", "not_found", 404)

    def _serial_post(self, parts, payload):
        if not SerialMonitor.available():
            raise StudioError("El monitor necesita pyserial y este Python no lo tiene",
                              "serial_unavailable", 503)
        if parts == ["serial", "connect"]:
            port = str(payload.get("port") or "").strip()
            if not port:
                raise StudioError("Falta el puerto", "invalid_request", 422)
            try:
                return {"connected": True, "info": MONITOR.connect(port, payload.get("baud", 9600))}
            except RuntimeError as exc:
                raise StudioError(str(exc), "serial_open_failed", 409)
        if parts == ["serial", "disconnect"]:
            MONITOR.disconnect()
            return {"connected": False}
        if parts == ["serial", "send"]:
            text = str(payload.get("text") or "")
            if len(text) > 512:
                raise StudioError("Mensaje demasiado largo", "invalid_request", 413)
            try:
                MONITOR.send(text)
            except RuntimeError as exc:
                raise StudioError(str(exc), "serial_not_connected", 409)
            return {"sent": len(text)}
        raise StudioError("Ruta API no encontrada", "not_found", 404)

    @staticmethod
    def _api_parts(path):
        if not path.startswith("/api/"):
            return []
        return [unquote(part) for part in path[5:].split("/") if part]

    def _body(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            raise StudioError("Content-Length inválido", "invalid_request", 400)
        if length > MAX_BODY:
            raise StudioError("Petición demasiado grande", "request_too_large", 413)
        raw = self.rfile.read(length)
        if not raw:
            return {}
        try:
            value = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            raise StudioError("El cuerpo no contiene JSON válido", "invalid_json", 400)
        if not isinstance(value, dict):
            raise StudioError("Se esperaba un objeto JSON", "invalid_json", 400)
        return value

    def _json(self, status, value):
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def _static(self, route):
        relative = "index.html" if route in ("", "/") else route.lstrip("/")
        candidate = (STATIC_ROOT / relative).resolve()
        try:
            candidate.relative_to(STATIC_ROOT.resolve())
        except ValueError:
            raise StudioError("Ruta estática no permitida", "unsafe_path", 403)
        if not candidate.is_file():
            # A client-side route should still open the application shell.
            candidate = STATIC_ROOT / "index.html"
        body = candidate.read_bytes()
        mime = mimetypes.guess_type(candidate.name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", mime + ("; charset=utf-8" if mime.startswith("text/") or mime in ("application/javascript", "application/json") else ""))
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)


class StudioServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def create_server(workspace=None, host="127.0.0.1", port=8765):
    workspace = workspace or StudioWorkspace()
    handler = type("BoundStudioRequestHandler", (StudioRequestHandler,), {"workspace": workspace})
    return StudioServer((host, port), handler)


def serve(workspace=None, host="127.0.0.1", port=8765, open_browser=True):
    server = create_server(workspace, host, port)
    actual_port = server.server_address[1]
    url = "http://%s:%d" % (host, actual_port)
    print("\nCoreFSM Studio está listo en %s" % url)
    print("Pulsa Ctrl+C para cerrarlo.\n")
    if open_browser:
        import webbrowser
        timer = threading.Timer(0.35, lambda: webbrowser.open(url))
        timer.daemon = True
        timer.start()
    try:
        server.serve_forever(poll_interval=0.3)
    except KeyboardInterrupt:
        print("\nCerrando CoreFSM Studio...")
    finally:
        MONITOR.disconnect()
        server.server_close()


__all__ = ["StudioServer", "create_server", "serve"]
