#!/usr/bin/env python3
"""Launch CoreFSM Studio using only the Python standard library."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from studio.core import StudioWorkspace  # noqa: E402
from studio.server import serve  # noqa: E402


def main(argv=None):
    parser = argparse.ArgumentParser(description="Entorno local de ingeniería para CoreFSM")
    parser.add_argument("--port", type=int, default=8765, help="puerto local (8765 por defecto; usa 0 para uno libre)")
    parser.add_argument("--no-browser", action="store_true", help="no abrir el navegador automáticamente")
    parser.add_argument("--repo", default=None, help="raíz alternativa del repositorio, útil para pruebas")
    args = parser.parse_args(argv)
    workspace = StudioWorkspace(args.repo)
    serve(workspace, port=args.port, open_browser=not args.no_browser)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
