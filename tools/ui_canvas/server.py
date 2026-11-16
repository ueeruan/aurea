"""Canvas da UI real do AUREA: espelha a tela do Android (emulador ou aparelho) via adb
e repassa toques, arrastos, rolagem e teclas. Uso: python tools/ui_canvas/server.py [porta]

tools/ui_canvas/status.json define as telas (serial + rótulo) e o texto do painel lateral,
atualizado a cada mudança de UI."""
import json
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
ADB = os.environ.get("AUREA_ADB", r"D:\Users\Ruan\AppData\Local\Android\Sdk\platform-tools\adb.exe")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

# Um quadro por aparelho; serial "" = único aparelho conectado.
_frames = {}
_lock = threading.Lock()


def adb(serial, *args, timeout=10):
    cmd = [ADB] + (["-s", serial] if serial else []) + list(args)
    return subprocess.run(cmd, capture_output=True, timeout=timeout)


def grab_loop(serial):
    # Uma captura contínua por aparelho; os clientes só leem o último quadro.
    frame = _frames[serial]
    while True:
        try:
            r = adb(serial, "exec-out", "screencap", "-p", timeout=8)
            with _lock:
                if r.returncode == 0 and r.stdout[:4] == b"\x89PNG":
                    frame.update(png=r.stdout, t=time.time(), err="")
                else:
                    frame["err"] = (r.stderr or b"sem aparelho").decode("utf-8", "replace").strip()
        except Exception as e:  # adb travado ou ausente
            with _lock:
                frame["err"] = str(e)
            time.sleep(1)
        time.sleep(0.05)


def frame_for(serial):
    with _lock:
        if serial not in _frames:
            _frames[serial] = {"png": b"", "t": 0.0, "err": ""}
            threading.Thread(target=grab_loop, args=(serial,), daemon=True).start()
        return dict(_frames[serial])


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                return self.send(200, f.read(), "text/html; charset=utf-8")
        if self.path.startswith("/frame"):
            q = parse_qs(urlparse(self.path).query)
            f = frame_for(q.get("s", [""])[0])
            png, err = f["png"], f["err"]
            if not png:
                return self.send(503, err.encode(), "text/plain; charset=utf-8")
            return self.send(200, png, "image/png")
        if self.path.startswith("/meta"):
            with _lock:
                meta = {"errs": {k: v["err"] for k, v in _frames.items() if v["err"]}}
            try:
                with open(os.path.join(HERE, "status.json"), encoding="utf-8") as f:
                    meta["status"] = json.load(f)
            except Exception:
                meta["status"] = None
            return self.send(200, json.dumps(meta).encode(), "application/json")
        self.send(404, b"", "text/plain")

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            p = json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return self.send(400, b"json", "text/plain")
        kind, serial = p.get("kind"), p.get("s", "")
        if kind == "tap":
            adb(serial, "shell", "input", "tap", str(int(p["x"])), str(int(p["y"])))
        elif kind == "swipe":
            adb(serial, "shell", "input", "swipe", *[str(int(p[k])) for k in ("x1", "y1", "x2", "y2", "ms")])
        elif kind == "key":
            adb(serial, "shell", "input", "keyevent", str(p["code"]))
        elif kind == "text":
            adb(serial, "shell", "input", "text", str(p["text"]).replace(" ", "%s"))
        else:
            return self.send(400, b"kind", "text/plain")
        self.send(200, b"ok", "text/plain")


if __name__ == "__main__":
    print(f"AUREA UI canvas em http://localhost:{PORT}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
