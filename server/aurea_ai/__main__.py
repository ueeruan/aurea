"""python -m aurea_ai — sobe o servidor.

Uso:
    AUREA_SERVER_TOKEN=... AUREA_ADMIN_TOKEN=... python -m aurea_ai
    python -m aurea_ai --porta 8000 --host 0.0.0.0
"""
from __future__ import annotations

import argparse
import os
import sys


def principal() -> int:
    p = argparse.ArgumentParser(prog="aurea_ai", description="Aurea AI API")
    p.add_argument("--host", default=os.environ.get("AUREA_HOST", "0.0.0.0"))
    p.add_argument("--porta", type=int, default=int(os.environ.get("AUREA_PORT", "8000")))
    p.add_argument("--recarregar", action="store_true", help="recarga ao editar (so em dev)")
    args = p.parse_args()

    try:
        import uvicorn
    except ImportError:
        print("uvicorn ausente: pip install -r requirements.txt", file=sys.stderr)
        return 2

    uvicorn.run("aurea_ai.app:app", host=args.host, port=args.porta,
                reload=args.recarregar, log_level="info", ws_ping_interval=20,
                ws_ping_timeout=20)
    return 0


if __name__ == "__main__":
    raise SystemExit(principal())
