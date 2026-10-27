#!/usr/bin/env python3
"""Bugs 0.3.4 (seleção de mídia), checagem estática do iOS.

Bug 4: a prévia do navegador de mídia mostrava a mídia escolhida ANTES. No
`onChange(of:)` de um parâmetro (API de um argumento) o valor capturado é o
anterior; quem carrega precisa receber a mídia nova pelo parâmetro e ignorar
respostas de outra mídia.

Bug 5: áudio não aparecia em "Outros arquivos e nuvem" da faixa de mídia; o
seletor tem de pedir áudio e o lote tem de criar camada de áudio.
"""
import pathlib
import re
import sys

APP = pathlib.Path(__file__).resolve().parents[1] / "app"
browser = (APP / "MediaBrowserView.swift").read_text(encoding="utf-8")
editor = (APP / "EditorView.swift").read_text(encoding="utf-8")
layout = (APP / "EditorLayout.swift").read_text(encoding="utf-8")
model = (APP / "AureaModel.swift").read_text(encoding="utf-8")

problems = []

def need(condition, message):
    if not condition:
        problems.append(message)

# Bug 4: nada de `onChange(of: asset) { _ in ... }` que recarrega pelo `asset` capturado.
need(not re.search(r"\.onChange\(of: asset\)\s*\{\s*_\s+in", browser),
     "MediaBrowserView: onChange(of: asset) ignora o valor novo (usa o asset ANTERIOR)")
need(re.search(r"(?<![A-Za-z])(load|loadFrames)\(\)", browser) is None,
     "MediaBrowserView: carregamento sem a mídia explícita")
need(".id(asset.localIdentifier)" in browser, "MediaBrowserView: a prévia precisa de identidade por mídia")
for name in ("OmegaAssetThumb", "OmegaMediaPreview", "OmegaTrimBar"):
    start = browser.find(f"struct {name}")
    end = browser.find("\nstruct ", start + 1)
    end2 = browser.find("\nprivate struct ", start + 1)
    end = min(e for e in (end, end2, len(browser)) if e > 0)
    body = browser[start:end]
    need(start >= 0 and "wanted == id" in body, f"{name}: resposta de outra mídia não é ignorada")

# Bug 5: Arquivos com áudio e o lote fazendo camada de áudio.
need("case mediaFiles" in layout and "self == .mediaFiles" in layout, "ShellAddPicker.mediaFiles ausente ou fora de isFile")
need("omegaRequestPicker(model, shell, .mediaFiles)" in browser, "faixa de mídia: Arquivos não usa .mediaFiles")
need(re.search(r"\[\.image, \.movie, \.audio\]", browser) is not None, "OmegaPickedMedia.types sem áudio")
need("case .mediaFiles: return OmegaPickedMedia.types" in editor, "seletor de arquivos da mídia sem os tipos de áudio")
need("case .mediaFiles: model.importMediaBatch" in editor, "escolha de .mediaFiles não vai para o lote")
need("audio ? importer.importAudio(" in model, "importMediaBatch não cria camada de áudio")
for ext in ("mp3", "m4a", "wav"):
    need(f'"{ext}"' in browser, f"OmegaPickedMedia sem a extensão {ext}")

if problems:
    print("check_media_selection: FALHOU")
    for p in problems:
        print(" -", p)
    sys.exit(1)
print("check_media_selection: ok")
