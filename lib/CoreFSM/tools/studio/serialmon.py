"""Puente con el puerto serie para el monitor de Studio.

pyserial es opcional a propósito.  Studio arranca con el Python que trae
PlatformIO, que ya lo incluye, pero también tiene que funcionar con un Python
del sistema pelado: en ese caso el monitor se desactiva y lo dice, en lugar de
impedir que la aplicación abra.

El hilo lector solo acumula líneas en una cola; la interfaz las pide por
índice.  Así ninguna petición HTTP se queda esperando al puerto, y recargar el
navegador no pierde el historial.
"""

from __future__ import annotations

import re
import threading
import time
from collections import deque
from pathlib import Path


MAX_LINES = 4000


def _import_serial():
    try:
        import serial                      # noqa: F401
        from serial.tools import list_ports  # noqa: F401
        return serial, list_ports
    except Exception:
        return None, None


class SerialMonitor:
    """Una sola conexión abierta a la vez: es un banco de pruebas, no un SCADA."""

    def __init__(self):
        self._lock = threading.RLock()
        self._port = None
        self._thread = None
        self._stop = threading.Event()
        self._lines = deque(maxlen=MAX_LINES)
        self._base = 0          # líneas ya descartadas por el maxlen
        self._next = 0          # índice de la próxima línea
        self._info = {}

    # ------------------------------------------------------------------
    @staticmethod
    def available():
        serial, _ = _import_serial()
        return serial is not None

    @staticmethod
    def list_ports():
        _, list_ports = _import_serial()
        if list_ports is None:
            return None
        result = []
        for port in list_ports.comports():
            description = (port.description or "").strip()
            if description.lower() in ("n/a", "unknown"):
                description = ""
            result.append({"device": port.device, "description": description})
        return result

    # ------------------------------------------------------------------
    def connect(self, port, baud):
        serial, _ = _import_serial()
        if serial is None:
            raise RuntimeError("pyserial no está disponible en este Python")
        self.disconnect()
        with self._lock:
            try:
                handle = serial.Serial(port=port, baudrate=int(baud), timeout=0.2)
            except Exception as exc:
                raise RuntimeError(_explain(exc, port))
            self._port = handle
            self._info = {"port": port, "baud": int(baud), "since": time.time()}
            self._stop.clear()
            self._lines.clear()
            self._base = 0
            self._next = 0
            self._thread = threading.Thread(target=self._reader, daemon=True)
            self._thread.start()
        return dict(self._info)

    def disconnect(self):
        with self._lock:
            self._stop.set()
            handle, self._port = self._port, None
        if handle is not None:
            try:
                handle.close()
            except Exception:
                pass
        thread, self._thread = self._thread, None
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=1.0)

    @property
    def connected(self):
        return self._port is not None

    def send(self, text):
        with self._lock:
            if self._port is None:
                raise RuntimeError("No hay ningún puerto conectado")
            self._port.write(text.encode("utf-8", "replace"))
            self._port.flush()

    def read(self, cursor):
        """Devuelve las líneas a partir de `cursor` y el nuevo cursor."""
        with self._lock:
            start = max(int(cursor or 0), self._base)
            offset = start - self._base
            lines = list(self._lines)[offset:] if offset < len(self._lines) else []
            return {"lines": lines, "cursor": self._next, "connected": self.connected,
                    "info": dict(self._info)}

    # ------------------------------------------------------------------
    def _reader(self):
        pending = b""
        while not self._stop.is_set():
            handle = self._port
            if handle is None:
                break
            try:
                chunk = handle.read(256)
            except Exception as exc:
                self._push("*** puerto cerrado: %s" % exc)
                break
            if not chunk:
                continue
            pending += chunk
            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                self._push(raw.decode("utf-8", "replace").rstrip("\r"))
        with self._lock:
            handle, self._port = self._port, None
        if handle is not None:
            try:
                handle.close()
            except Exception:
                pass

    def _push(self, line):
        with self._lock:
            if len(self._lines) == self._lines.maxlen:
                self._base += 1
            self._lines.append(line)
            self._next += 1


def _explain(exc, port):
    text = str(exc)
    if "PermissionError" in text or "Acceso denegado" in text or "Access is denied" in text:
        return ("No se puede abrir %s: lo tiene ocupado otro programa. Cierra el monitor serie "
                "de PlatformIO o del IDE de Arduino y vuelve a intentarlo." % port)
    if "could not open port" in text.lower() or "FileNotFoundError" in text:
        return ("No existe el puerto %s. Comprueba que la placa está conectada y que el driver "
                "(CH340 o CP210x) está instalado." % port)
    return "No se ha podido abrir %s: %s" % (port, text)


def suggested_baud(project_dir):
    """La velocidad del monitor declarada en platformio.ini.

    Abrir el monitor a una velocidad distinta a la de Serial.begin() produce
    caracteres basura, que es el primer susto de cualquiera. Si el proyecto ya
    lo dice, no hay razón para preguntarlo.
    """
    path = Path(project_dir) / "platformio.ini"
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"(?mi)^\s*monitor_speed\s*=\s*(\d+)", text)
    return int(match.group(1)) if match else None


MONITOR = SerialMonitor()
