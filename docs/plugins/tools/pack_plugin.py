#!/usr/bin/env python3
"""Confere uma pasta de plugin do AUREA e empacota em .aureaplugin.

Formatos 1 (efeitos) e 2 (0.3.4: painéis, script e permissões).

Uso:
    python pack_plugin.py <pasta-do-plugin> [-o saida.aureaplugin]
    python pack_plugin.py --check <pasta-do-plugin>      (só confere)

O pacote é um ZIP (deflate) com o plugin.json na raiz e os arquivos que ele
cita. A conferência repete as regras que o app aplica na instalação (formato,
id, versão, parâmetros, passes, pilha, limites de tamanho) para que o erro
apareça no computador, não no aparelho. A compilação do GLSL e o quadro de
teste de 256x256 só o app faz (docs/plugins/README.md, "Limites").

Só usa a biblioteca padrão do Python 3.8+.
"""
import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

MAX_PARAMS = 16
MAX_PASSES = 8
MAX_INPUTS = 3
MAX_STACK = 16
MAX_SHADER_BYTES = 64 * 1024
MAX_MANIFEST_BYTES = 256 * 1024
MAX_PACKAGE_BYTES = 16 * 1024 * 1024
PARAM_TYPES = {"float", "int", "bool", "enum", "color", "point", "angle", "layer"}
ID_RE = re.compile(r"^[a-z0-9._-]{3,64}$")
VERSION_RE = re.compile(r"^\d{1,5}\.\d{1,5}\.\d{1,5}$")
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,31}$")
RESERVED_PREFIXES = ("gl_", "aurea", "Aurea", "AUREA", "isf_", "IMG_", "vv_")
# Os mesmos nomes que o app recusa como id de parâmetro/entrada (PluginManifest.cpp).
RESERVED = set("""
attribute const uniform varying buffer shared coherent volatile restrict readonly writeonly atomic_uint layout
centroid flat smooth noperspective patch sample break continue do for while switch case default if else
subroutine in out inout float double int void bool true false invariant precise discard return lowp mediump
highp precision struct uint vec2 vec3 vec4 ivec2 ivec3 ivec4 uvec2 uvec3 uvec4 bvec2 bvec3 bvec4 dvec2 dvec3
dvec4 mat2 mat3 mat4 mat2x2 mat2x3 mat2x4 mat3x2 mat3x3 mat3x4 mat4x2 mat4x3 mat4x4 sampler2D sampler3D
samplerCube sampler2DShadow texture texture2D textureLod textureSize texelFetch textureGrad textureOffset
radians degrees sin cos tan asin acos atan sinh cosh tanh asinh acosh atanh pow exp log exp2 log2 sqrt
inversesqrt abs sign floor trunc round roundEven ceil fract mod modf min max clamp mix step smoothstep isnan
isinf length distance dot cross normalize faceforward reflect refract matrixCompMult outerProduct transpose
determinant inverse lessThan lessThanEqual greaterThan greaterThanEqual equal notEqual any all not dFdx dFdy
fwidth fma main uv v_uv o_color source time RENDERSIZE TIME TIMEDELTA FRAMEINDEX DATE PASSINDEX
floatBitsToInt floatBitsToUint intBitsToFloat uintBitsToFloat packUnorm2x16 unpackUnorm2x16 bitfieldExtract
bitCount findLSB findMSB frexp ldexp
""".split())
UNSTACKABLE_PREFIXES = ("aurea.time.", "aurea.audio.", "aurea.control.", "aurea.3d.", "aurea.layout.",
                        "aurea.text", "aurea.shape3d.", "plugin.")
UNSTACKABLE = {"aurea.distort.mesh_warp", "aurea.distort.puppet", "aurea.transform.parenting_helper",
               "aurea.generate.particular", "aurea.key.rotobrush"}


class Invalid(Exception):
    pass


def valid_ident(name):
    return (bool(IDENT_RE.match(name)) and "__" not in name and not name.startswith(RESERVED_PREFIXES)
            and name not in RESERVED)


def check_text(value, field, required=True):
    if value is None:
        if required:
            raise Invalid(f'falta "{field}"')
        return
    if isinstance(value, str):
        if required and not value:
            raise Invalid(f'"{field}" vazio')
        return
    if isinstance(value, dict) and value and all(isinstance(v, str) for v in value.values()):
        return
    raise Invalid(f'"{field}" deve ser texto ou um objeto idioma -> texto')


def check_param(p, index):
    if not isinstance(p, dict):
        raise Invalid(f"parametro {index + 1} deve ser um objeto")
    pid = p.get("id")
    if not isinstance(pid, str) or not valid_ident(pid):
        raise Invalid(f'parametro {index + 1}: id invalido "{pid}" (letras, digitos e _, ate 32, '
                      "sem palavra reservada do GLSL nem prefixo aurea/gl_)")
    kind = p.get("type")
    if kind not in PARAM_TYPES:
        raise Invalid(f'parametro "{pid}": tipo desconhecido "{kind}" (use {", ".join(sorted(PARAM_TYPES))})')
    check_text(p.get("label"), "label", required=False)
    if kind == "layer":
        # 0.3.1: camada ligada (só "shader3d"); "accept" filtra o tipo.
        for a in p.get("accept", []):
            if a not in ("camera", "null", "light", "any"):
                raise Invalid(f'parametro "{pid}": "accept" desconhecido "{a}" (camera, null, light ou any)')
        return pid
    if kind == "enum":
        options = p.get("options")
        if not isinstance(options, list) or not options or len(options) > 32:
            raise Invalid(f'parametro "{pid}": enum precisa de "options" (1 a 32)')
        default = p.get("default", 0)
        if not isinstance(default, (int, float)) or not 0 <= default < len(options):
            raise Invalid(f'parametro "{pid}": "default" fora das opcoes')
    elif kind == "color":
        default = p.get("default")
        if default is not None:
            ok = (isinstance(default, str) and re.match(r"^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$", default)) or (
                isinstance(default, list) and len(default) in (3, 4) and all(isinstance(c, (int, float)) for c in default))
            if not ok:
                raise Invalid(f'parametro "{pid}": cor "default" deve ser "#RRGGBB", "#RRGGBBAA" ou [r, g, b(, a)]')
    elif kind == "point":
        default = p.get("default")
        if default is not None and not (isinstance(default, list) and len(default) == 2):
            raise Invalid(f'parametro "{pid}": ponto "default" deve ser [x, y]')
    elif kind in ("float", "int", "angle"):
        lo = p.get("min", -360 if kind == "angle" else 0)
        hi = p.get("max", 360 if kind == "angle" else (100 if kind == "int" else 1))
        if not (isinstance(lo, (int, float)) and isinstance(hi, (int, float)) and hi > lo):
            raise Invalid(f'parametro "{pid}": "max" deve ser maior que "min"')
        default = p.get("default", 0 if kind == "angle" else lo)
        if not isinstance(default, (int, float)) or not lo <= default <= hi:
            raise Invalid(f'parametro "{pid}": "default" fora de [min, max]')
    return pid


# --- 0.3.4 plugins: formato 2 (interface, script, permissoes) -------------------
ICONS = set("""wand sparkles text star bolt cloud music photo film layers gear brush clock list cube lightbulb
wrench book flag grid camera chart sliders eye folder link speed waveform""".split())
CONTROL_TYPES = {"button", "slider", "ruler", "color", "choice", "text", "list", "image", "label", "toggle"}
FUNC_RE = re.compile(r"^[A-Za-z_$][A-Za-z0-9_$]{0,63}$")
UI_ID_RE = re.compile(r"^[A-Za-z0-9_.-]{1,32}$")
DOMAIN_RE = re.compile(r"^[a-z0-9-]+(\.[a-z0-9-]+)+$")


def safe_file(name):
    return isinstance(name, str) and bool(name) and not name.startswith(("/", "\\")) and ":" not in name \
        and not any(part in ("", ".", "..") for part in re.split(r"[/\\]", name))


def check_v2(m, folder, fmt):
    """Confere "script", "permissions", "strings" e "ui" (o mesmo que o app recusa)."""
    v2 = [k for k in ("ui", "script", "permissions", "strings") if k in m]
    if fmt == 1:
        if v2:
            raise Invalid(f'"{v2[0]}" pede "aureaPlugin": 2')
        return []
    files = []
    script = m.get("script")
    if script is not None:
        if not safe_file(script) or not script.lower().endswith(".js"):
            raise Invalid('"script" deve ser um arquivo .js do pacote (ex.: "main.js")')
        path = folder / script
        if not path.is_file():
            raise Invalid(f"arquivo de script ausente: {script}")
        if path.stat().st_size > 512 * 1024:
            raise Invalid(f"{script} maior que 512 KB")
        files.append(script)
    perms = m.get("permissions", {})
    if not isinstance(perms, dict):
        raise Invalid('"permissions" deve ser um objeto')
    for k in perms:
        if k not in ("network", "clipboard", "files"):
            raise Invalid(f'"permissions": campo desconhecido "{k}"')
    net = perms.get("network", [])
    if not isinstance(net, list) or len(net) > 8:
        raise Invalid('"permissions".network deve ser a lista de dominios (ate 8)')
    for d in net:
        if not isinstance(d, str) or "*" in d:
            raise Invalid('"permissions".network: liste cada dominio; "*" nao e aceito')
        if "/" in d or ":" in d or not DOMAIN_RE.match(d.lower()):
            raise Invalid(f'"permissions".network: "{d}" deve ser so o dominio (sem https://, porta ou caminho)')
    functions = []
    ui = m.get("ui", {})
    if not isinstance(ui, dict):
        raise Invalid('"ui" deve ser um objeto com "panels" e/ou "actions"')
    controls = set()
    panels = ui.get("panels", [])
    if not isinstance(panels, list) or len(panels) > 8:
        raise Invalid('"ui".panels deve ser uma lista (ate 8)')
    panel_ids = {}
    for p in panels:
        pid = p.get("id") if isinstance(p, dict) else None
        if not isinstance(pid, str) or not UI_ID_RE.match(pid) or pid in panel_ids:
            raise Invalid(f'painel com "id" invalido ou repetido: {pid}')
        check_text(p.get("title"), f"{pid}.title")
        if p.get("icon", "wand") not in ICONS:
            raise Invalid(f'painel "{pid}": icone desconhecido (use {", ".join(sorted(ICONS))})')
        place = p.get("placement", "editor.panel")
        place = [place] if isinstance(place, str) else place
        if not place or any(x not in ("editor.panel", "home.tab") for x in place):
            raise Invalid(f'painel "{pid}": "placement" deve ser editor.panel e/ou home.tab')
        panel_ids[pid] = place
        if p.get("onOpen"):
            functions.append(p["onOpen"])
        ctrls = p.get("controls")
        if not isinstance(ctrls, list) or not ctrls or len(ctrls) > 32:
            raise Invalid(f'painel "{pid}": precisa de "controls" (1 a 32)')
        for c in ctrls:
            cid = c.get("id") if isinstance(c, dict) else None
            if not isinstance(cid, str) or not UI_ID_RE.match(cid) or cid in controls:
                raise Invalid(f'controle com "id" invalido ou repetido: {cid}')
            controls.add(cid)
            ctype = c.get("type")
            if ctype not in CONTROL_TYPES:
                raise Invalid(f'controle "{cid}": tipo desconhecido "{ctype}"')
            for ev in ("onClick", "onChange", "onSelect"):
                fn = c.get(ev)
                if fn is not None:
                    if not isinstance(fn, str) or not FUNC_RE.match(fn):
                        raise Invalid(f'controle "{cid}": "{ev}" deve ser o nome de uma funcao')
                    functions.append(fn)
            if ctype == "button" and not c.get("onClick"):
                raise Invalid(f'controle "{cid}": botao precisa de "onClick"')
            if ctype == "choice" and not c.get("options"):
                raise Invalid(f'controle "{cid}": "choice" precisa de "options"')
            if ctype == "image" and c.get("src") is not None:
                src = c["src"]
                if not safe_file(src) or not src.lower().endswith((".png", ".jpg", ".jpeg")) or not (folder / src).is_file():
                    raise Invalid(f'controle "{cid}": imagem ausente ou invalida: {src}')
    actions = ui.get("actions", [])
    if not isinstance(actions, list) or len(actions) > 16:
        raise Invalid('"ui".actions deve ser uma lista (ate 16)')
    for a in actions:
        aid = a.get("id") if isinstance(a, dict) else None
        if not isinstance(aid, str) or not UI_ID_RE.match(aid):
            raise Invalid(f'acao com "id" invalido: {aid}')
        check_text(a.get("title"), f"{aid}.title")
        place = a.get("placement")
        if place not in ("editor.menu", "layer.menu", "layer.toolbar"):
            raise Invalid(f'acao "{aid}": "placement" deve ser editor.menu, layer.menu ou layer.toolbar')
        if place == "layer.toolbar" and "icon" not in a:
            raise Invalid(f'acao "{aid}": botao da barra da camada precisa de "icon"')
        if a.get("icon", "wand") not in ICONS:
            raise Invalid(f'acao "{aid}": icone desconhecido')
        if bool(a.get("call")) == bool(a.get("panel")):
            raise Invalid(f'acao "{aid}": use "call" (funcao) ou "panel" (painel), um dos dois')
        if a.get("call"):
            functions.append(a["call"])
        elif "editor.panel" not in panel_ids.get(a["panel"], []):
            raise Invalid(f'acao "{aid}": o painel "{a["panel"]}" nao existe ou nao tem "editor.panel"')
    if functions and not script:
        raise Invalid(f'a interface chama "{functions[0]}", mas o plugin nao tem "script"')
    if script:
        text = (folder / script).read_text(encoding="utf-8", errors="replace")
        for fn in sorted(set(functions)):
            if not re.search(r"function\s+" + re.escape(fn) + r"\s*\(", text) and not re.search(
                    r"(const|let|var)\s+" + re.escape(fn) + r"\s*=", text):
                raise Invalid(f'a interface chama "{fn}", que o script nao define')
    if m.get("kind") == "panel" and not panels and not actions:
        raise Invalid('plugin "panel" precisa de "ui" (paineis ou acoes)')
    return files


def check(folder):
    folder = Path(folder)
    manifest_path = folder / "plugin.json"
    if not manifest_path.is_file():
        raise Invalid("plugin.json ausente na pasta")
    raw = manifest_path.read_bytes()
    if len(raw) > MAX_MANIFEST_BYTES:
        raise Invalid("plugin.json maior que 256 KB")
    try:
        m = json.loads(raw.decode("utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise Invalid(f"plugin.json invalido: {e}")
    if not isinstance(m, dict):
        raise Invalid("plugin.json invalido: a raiz deve ser um objeto")
    fmt = m.get("aureaPlugin")
    if fmt not in (1, 2):
        raise Invalid('falta "aureaPlugin": 1 ou 2 (versao do formato)')
    if not isinstance(m.get("id"), str) or not ID_RE.match(m["id"]) or m["id"][0] == "." or m["id"][-1] == "." \
            or ".." in m["id"]:
        raise Invalid(f'"id" invalido "{m.get("id")}": use 3 a 64 caracteres a-z, 0-9, ".", "_" ou "-"')
    if not isinstance(m.get("version"), str) or not VERSION_RE.match(m["version"]):
        raise Invalid('"version" deve ser "x.y.z" (ex.: "1.0.0")')
    check_text(m.get("name"), "name")
    check_text(m.get("description"), "description", required=False)
    kind = m.get("kind")
    if kind not in ("shader", "shader3d", "stack", "panel"):
        raise Invalid('"kind" deve ser "shader", "shader3d", "stack" ou "panel"')
    if kind == "panel" and fmt != 2:
        raise Invalid('"kind": "panel" pede "aureaPlugin": 2')
    params = m.get("params", [])
    if not isinstance(params, list):
        raise Invalid('"params" deve ser uma lista')
    if len(params) > MAX_PARAMS:
        raise Invalid(f"{len(params)} parametros (limite {MAX_PARAMS})")
    ids = [check_param(p, i) for i, p in enumerate(params)]
    layers = sum(1 for p in params if isinstance(p, dict) and p.get("type") == "layer")
    if layers and kind != "shader3d":
        raise Invalid('parametro "layer" so em plugin "kind": "shader3d"')
    if layers > 4:
        raise Invalid(f'{layers} parametros "layer" (limite 4)')
    if len(set(ids)) != len(ids):
        raise Invalid("parametro repetido")
    files = ["plugin.json"]
    if kind == "panel" and params:
        raise Invalid('plugin "panel" nao tem "params" (os controles ficam em "ui")')
    files += check_v2(m, folder, fmt)
    if kind == "panel":
        pass
    elif kind in ("shader", "shader3d"):
        passes = m.get("passes")
        if not isinstance(passes, list) or not passes:
            raise Invalid('plugin "shader" precisa de "passes" (1 a 8)')
        if len(passes) > MAX_PASSES:
            raise Invalid(f"{len(passes)} passes (limite {MAX_PASSES})")
        targets = []
        for i, p in enumerate(passes):
            where = f"passe {i + 1}"
            shader = p.get("shader") if isinstance(p, dict) else None
            if not isinstance(shader, str) or not shader or shader.startswith(("/", "\\")) or ":" in shader \
                    or any(part in ("", ".", "..") for part in re.split(r"[/\\]", shader)):
                raise Invalid(f'{where}: caminho de shader invalido "{shader}"')
            path = folder / shader
            if not path.is_file():
                raise Invalid(f"{where}: arquivo de shader ausente: {shader}")
            if path.stat().st_size > MAX_SHADER_BYTES:
                raise Invalid(f"{where}: {shader} maior que 64 KB")
            if "aurea_main" not in path.read_text(encoding="utf-8", errors="replace"):
                raise Invalid(f"{where}: {shader} nao define vec4 aurea_main(vec2 uv)")
            files.append(shader)
            last = i + 1 == len(passes)
            target = p.get("target")
            if target is not None:
                if last:
                    raise Invalid('o ultimo passe nao tem "target": ele e a saida do efeito')
                if not isinstance(target, str) or not valid_ident(target) or target in targets:
                    raise Invalid(f'{where}: "target" invalido ou repetido')
            elif not last:
                raise Invalid(f'{where}: falta "target" (so o ultimo passe escreve a saida)')
            scale = p.get("scale", 1)
            if not isinstance(scale, (int, float)) or not 0.125 <= scale <= 1:
                raise Invalid(f'{where}: "scale" deve estar entre 0.125 e 1')
            inputs = p.get("inputs", {})
            if not isinstance(inputs, dict) or len(inputs) > MAX_INPUTS:
                raise Invalid(f'{where}: "inputs" deve ser um objeto com ate {MAX_INPUTS} entradas')
            for name, src in inputs.items():
                if not valid_ident(name) or name in ids:
                    raise Invalid(f'{where}: entrada com nome invalido "{name}"')
                if src != "source" and src not in targets:
                    raise Invalid(f'{where}: entrada "{name}" le "{src}", que nao e "source" nem o target de um passe anterior')
            if target:
                targets.append(target)
        margin = m.get("margin", 0)
        if not isinstance(margin, (int, float)) or not 0 <= margin <= 1024:
            raise Invalid('"margin" deve estar entre 0 e 1024 (px da camada)')
        if m.get("colorSpace", "linear") not in ("linear", "srgb"):
            raise Invalid('"colorSpace" deve ser "linear" ou "srgb"')
    else:
        stack = m.get("stack")
        if not isinstance(stack, list) or not stack or len(stack) > MAX_STACK:
            raise Invalid(f'plugin "stack" precisa de "stack" (1 a {MAX_STACK} efeitos embutidos)')
        for i, step in enumerate(stack):
            effect = step.get("effect") if isinstance(step, dict) else None
            if not isinstance(effect, str) or not effect.startswith("aurea."):
                raise Invalid(f'stack[{i}]: falta "effect" (chave de um efeito embutido, ex.: aurea.blur.gaussian)')
            if effect.startswith(UNSTACKABLE_PREFIXES) or effect in UNSTACKABLE:
                raise Invalid(f"stack[{i}] ({effect}): este efeito nao pode entrar numa pilha")
            if not isinstance(step.get("set", {}), dict):
                raise Invalid(f'stack[{i}]: "set" deve ser um objeto parametro -> valor')
    # Outros arquivos da pasta (licença, imagens de documentação) vão junto.
    for path in sorted(folder.rglob("*")):
        rel = path.relative_to(folder).as_posix()
        if path.is_file() and rel not in files and not rel.startswith(".") and "/." not in rel:
            files.append(rel)
    return m, files


def pack(folder, output):
    m, files = check(folder)
    output = Path(output)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for rel in files:
            info = zipfile.ZipInfo(rel, date_time=(2026, 1, 1, 0, 0, 0))   # pacote reprodutível
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, (Path(folder) / rel).read_bytes())
    if output.stat().st_size > MAX_PACKAGE_BYTES:
        output.unlink()
        raise Invalid("pacote maior que 16 MB")
    return m, files


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("folder", help="pasta com plugin.json")
    ap.add_argument("-o", "--output", help="arquivo .aureaplugin (padrao: <pasta>.aureaplugin)")
    ap.add_argument("--check", action="store_true", help="so confere, nao empacota")
    args = ap.parse_args()
    folder = Path(args.folder)
    try:
        if args.check:
            m, files = check(folder)
            print(f"ok: {m['id']} {m['version']} ({m['kind']}, {len(files)} arquivos)")
            return 0
        output = Path(args.output) if args.output else folder.with_suffix(".aureaplugin")
        m, files = pack(folder, output)
        print(f"ok: {output} ({m['id']} {m['version']}, {len(files)} arquivos)")
        return 0
    except Invalid as e:
        print(f"erro: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
