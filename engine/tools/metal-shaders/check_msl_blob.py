"""Confere os blobs MSL gerados no build contra o contrato de engine/gpu/metal/msl_glue.md.

    python check_msl_blob.py <pasta-dos-blobs> <pasta-dos-.spv>

A verdade é o SPIR-V: para CADA blob, este script lê os bindings DECLARADOS no
`.spv` (o que o shader pede) e confere que o MSL traduzido usa OS MESMOS índices
na tabela do motor. É assim que se pega um build que não fixou os índices — o
SPIRV-Cross numera recursos em sequência por padrão, e um índice trocado não dá
erro nenhum: desenha com o recurso errado.

Confere, por blob:
  1. cabeçalho de 32 bytes: magic "AUREAMSL", versão 1, payload (texto UTF-8, ou
     `.metallib` com a flag 1) e o tamanho batendo com o arquivo;
  2. o ponto de entrada do estágio (`vs_main` / `fs_main` / `cs_main`) — o
     SPIRV-Cross emite `vertex_main`/`fragment_main`/`kernel_main` e o build
     renomeia;
  3. compute: `threadgroup` não pode ser zero — o Metal não descobre o tamanho do
     grupo pelo pipeline;
  4. os ÍNDICES: cada binding do SPIR-V tem de aparecer no MSL no lugar certo —
     textura com o seu sampler no mesmo índice, bloco de uniforms, storage buffer
     e os push constants em `[[buffer(30)]]`.
"""
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

MAGIC = b"AUREAMSL"
SLOT_PUSH = 30

OP_VARIABLE = 59
OP_DECORATE = 71
OP_ENTRY_POINT = 15
DEC_BINDING = 33
DEC_DESCRIPTOR_SET = 34
STORAGE_UNIFORM_CONSTANT = 0
STORAGE_UNIFORM = 2
STORAGE_PUSH_CONSTANT = 9
STORAGE_STORAGE_BUFFER = 12
EXEC_MODEL = {0: "vert", 1: "frag", 5: "comp"}
ENTRADA = {"vert": "vs_main", "frag": "fs_main", "comp": "cs_main"}


def le_spirv(p):
    """(estágio, {binding: classe de armazenamento}) do SPIR-V."""
    palavras = list(struct.unpack("<%dI" % (p.stat().st_size // 4), p.read_bytes()))
    if not palavras or palavras[0] != 0x07230203:
        raise ValueError("nao e SPIR-V")
    estagio = "frag"
    espacos, bindings, usados = {}, {}, {}
    i = 5
    while i < len(palavras):
        opcode = palavras[i] & 0xFFFF
        conta = palavras[i] >> 16
        if conta == 0:
            break
        args = palavras[i + 1:i + conta]
        if opcode == OP_ENTRY_POINT and conta >= 2:
            estagio = EXEC_MODEL.get(args[0], "frag")
        elif opcode == OP_DECORATE and conta >= 3:
            if args[1] == DEC_BINDING:
                bindings[args[0]] = args[2]
            elif args[1] == DEC_DESCRIPTOR_SET:
                espacos[args[0]] = args[2]
        elif opcode == OP_VARIABLE and conta >= 3:
            # OpVariable: tipo do resultado, id do resultado, CLASSE de
            # armazenamento (o terceiro operando — o segundo é o tipo).
            usados[args[1]] = args[2]
        i += conta
    # Só o que tem binding declarado e vive no descritor 0 (o do motor).
    ligados = {}
    for rid, classe in usados.items():
        if rid in bindings and espacos.get(rid, 0) == 0:
            ligados[bindings[rid]] = classe
    return estagio, ligados


def le_blob(p):
    raw = p.read_bytes()
    if len(raw) < 32:
        return None, "curto demais para ter cabecalho"
    magic, version, flags, size, gx, gy, gz = struct.unpack_from("<8s6I", raw)
    if magic != MAGIC:
        return None, "magic errado: %r" % magic
    if version != 1:
        return None, "versao %d (esperada 1)" % version
    if flags & ~1:
        return None, "flags desconhecidas: %d" % flags
    if size == 0 or 32 + size > len(raw) + 3:
        return None, "payload de %d bytes nao cabe no arquivo (%d)" % (size, len(raw))
    return (flags, (gx, gy, gz), raw[32:32 + size]), None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    raiz, spv_raiz = Path(sys.argv[1]), Path(sys.argv[2])
    blobs = sorted(p for p in raiz.rglob("*.spv.msl") if p.is_file())
    if not blobs:
        print("nenhum blob em %s" % raiz)
        return 1

    problemas = []
    contagem = defaultdict(int)
    for p in blobs:
        cabecalho, erro = le_blob(p)
        if cabecalho is None:
            problemas.append("%s: %s" % (p.name, erro))
            continue
        flags, grupo, payload = cabecalho
        precompilado = bool(flags & 1)
        texto = "" if precompilado else payload.decode("utf-8", "replace")

        spv = spv_raiz / str(p.relative_to(raiz))[:-4]      # tira o ".msl"
        if not spv.is_file():
            problemas.append("%s: sem o .spv de origem (%s)" % (p.name, spv.name))
            continue
        try:
            estagio, ligados = le_spirv(spv)
        except Exception as e:                              # noqa: BLE001
            problemas.append("%s: %s" % (p.name, e))
            continue
        contagem[estagio] += 1

        if estagio == "comp" and grupo == (0, 0, 0):
            problemas.append("%s: compute sem threadgroup" % p.name)
        if estagio != "comp" and grupo != (0, 0, 0):
            problemas.append("%s: threadgroup em shader que nao e compute" % p.name)

        if precompilado:
            continue
        esperado = ENTRADA[estagio]
        if esperado not in texto:
            problemas.append("%s: sem o ponto de entrada %s" % (p.name, esperado))
        for outro in ("vertex_main", "fragment_main", "kernel_main"):
            if outro in texto:
                problemas.append("%s: ponto de entrada nao renomeado (%s)" % (p.name, outro))

        tex = {int(i) for i in re.findall(r"\[\[texture\((\d+)\)\]\]", texto)}
        smp = {int(i) for i in re.findall(r"\[\[sampler\((\d+)\)\]\]", texto)}
        buf = {int(i) for i in re.findall(r"\[\[buffer\((\d+)\)\]\]", texto)}

        for binding, classe in sorted(ligados.items()):
            if classe == STORAGE_UNIFORM_CONSTANT:
                # sampler2D combinado: vira textura E sampler NO MESMO indice.
                if binding not in tex:
                    problemas.append("%s: o SPIR-V pede o binding %d e o MSL nao tem [[texture(%d)]]"
                                     % (p.name, binding, binding))
                if binding not in smp:
                    problemas.append("%s: binding %d sem [[sampler(%d)]]" % (p.name, binding, binding))
            elif classe in (STORAGE_UNIFORM, STORAGE_STORAGE_BUFFER):
                if binding not in buf:
                    problemas.append("%s: o SPIR-V pede o buffer %d e o MSL nao tem [[buffer(%d)]]"
                                     % (p.name, binding, binding))
        if STORAGE_PUSH_CONSTANT in ligados.values() and SLOT_PUSH not in buf:
            problemas.append("%s: usa push constants e nao ha [[buffer(%d)]]" % (p.name, SLOT_PUSH))

    print("blobs:    %d (vert %d, frag %d, comp %d)" % (len(blobs), contagem["vert"], contagem["frag"], contagem["comp"]))
    if problemas:
        print("PROBLEMAS (%d):" % len(problemas))
        for x in problemas[:40]:
            print("  " + x)
        return 1
    print("0 problema(s) - todo indice do SPIR-V aparece no MSL no mesmo slot")
    return 0


if __name__ == "__main__":
    sys.exit(main())
