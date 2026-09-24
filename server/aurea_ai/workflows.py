"""Carrega os workflows versionados e troca so os inputs.

O grafo do ComfyUI fica num arquivo (`workflows/h3_t2v.json`), nunca em codigo.
O que o codigo precisa saber e *onde* cada valor mora — isso esta no proprio
arquivo, na chave reservada `_aurea.inputs`, no formato:

    "prompt": "6.text"
    "seed":   "3.seed"
    "largura": "5.width"

Assim, se o pack de nodes do H3 mudar de nome de classe ou de id, o operador
edita o JSON e o servidor nao precisa ser tocado. `class_names()` existe para
conferir esses nomes contra o `/object_info` do ComfyUI antes de gerar.
"""
from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

CHAVE_RESERVADA = "_aurea"


class WorkflowInvalido(RuntimeError):
    pass


class Workflow:
    def __init__(self, nome: str, grafo: dict, mapa: dict[str, str], meta: dict) -> None:
        self.nome = nome
        self.grafo = grafo
        self.mapa = mapa
        self.meta = meta

    # -- leitura ----------------------------------------------------------
    @classmethod
    def carregar(cls, caminho: Path) -> "Workflow":
        if not caminho.is_file():
            raise WorkflowInvalido(f"workflow ausente: {caminho}")
        try:
            bruto = json.loads(caminho.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            raise WorkflowInvalido(f"{caminho.name}: JSON invalido ({e})") from e
        if not isinstance(bruto, dict):
            raise WorkflowInvalido(f"{caminho.name}: a raiz precisa ser um objeto")

        meta = bruto.pop(CHAVE_RESERVADA, {})
        if not isinstance(meta, dict):
            raise WorkflowInvalido(f"{caminho.name}: `{CHAVE_RESERVADA}` precisa ser um objeto")
        mapa = meta.get("inputs", {})
        if not isinstance(mapa, dict) or not mapa:
            raise WorkflowInvalido(f"{caminho.name}: `{CHAVE_RESERVADA}.inputs` vazio")

        # Sem restos de _aurea aninhados: o grafo vai inteiro para o ComfyUI.
        cls._limpar(bruto)
        if not bruto:
            raise WorkflowInvalido(f"{caminho.name}: grafo vazio")
        return cls(caminho.stem, bruto, mapa, meta)

    @staticmethod
    def _limpar(no: Any) -> None:
        if isinstance(no, dict):
            no.pop(CHAVE_RESERVADA, None)
            for v in no.values():
                Workflow._limpar(v)
        elif isinstance(no, list):
            for v in no:
                Workflow._limpar(v)

    def class_names(self) -> set[str]:
        return {n["class_type"] for n in self.grafo.values()
                if isinstance(n, dict) and isinstance(n.get("class_type"), str)}

    def modelos_exigidos(self) -> list[str]:
        return list(self.meta.get("models", []))

    # -- montagem ---------------------------------------------------------
    def montar(self, valores: dict[str, Any]) -> dict:
        """Copia o grafo e escreve cada valor no lugar mapeado.

        Chave desconhecida em `valores` = erro: preferimos falhar alto a gerar
        com o prompt padrao do arquivo por engano.
        """
        grafo = copy.deepcopy(self.grafo)
        for chave, valor in valores.items():
            if chave not in self.mapa:
                raise WorkflowInvalido(f"{self.nome}: '{chave}' nao esta no mapa de inputs")
            self._escrever(grafo, self.mapa[chave], valor, chave)
        return grafo

    def _escrever(self, grafo: dict, alvo: str, valor: Any, chave: str) -> None:
        if "." not in alvo:
            raise WorkflowInvalido(f"{self.nome}.{chave}: alvo '{alvo}' precisa ser '<node>.<campo>'")
        node_id, campo = alvo.split(".", 1)
        # Um nivel de indice: "<node>.image[0]" para entradas em lista.
        indice: int | None = None
        if campo.endswith("]") and "[" in campo:
            campo, _, resto = campo.partition("[")
            try:
                indice = int(resto[:-1])
            except ValueError as e:
                raise WorkflowInvalido(f"{self.nome}.{chave}: indice invalido em '{alvo}'") from e

        node = grafo.get(node_id)
        if not isinstance(node, dict):
            raise WorkflowInvalido(f"{self.nome}.{chave}: node '{node_id}' nao existe no grafo")
        inputs = node.setdefault("inputs", {})
        if campo not in inputs:
            # Escrever campo inexistente criaria um no invalido no ComfyUI.
            raise WorkflowInvalido(f"{self.nome}.{chave}: node '{node_id}' nao tem o campo '{campo}'")

        if indice is None:
            inputs[campo] = valor
        else:
            atual = inputs[campo]
            if not isinstance(atual, list) or indice >= len(atual):
                raise WorkflowInvalido(f"{self.nome}.{chave}: '{campo}' nao aceita indice {indice}")
            atual[indice] = valor


class Biblioteca:
    """Os workflows disponiveis, carregados uma vez."""

    def __init__(self, diretorio: Path) -> None:
        self.diretorio = diretorio
        self._por_modo: dict[str, Workflow] = {}

    def carregar(self) -> None:
        for modo, arquivo in (("text_to_video", "h3_t2v.json"),
                              ("image_to_video", "h3_i2v.json")):
            self._por_modo[modo] = Workflow.carregar(self.diretorio / arquivo)

    def do_modo(self, modo: str) -> Workflow:
        if modo not in self._por_modo:
            raise WorkflowInvalido(f"modo '{modo}' sem workflow")
        return self._por_modo[modo]

    def modos(self) -> list[str]:
        return list(self._por_modo)

    def todos(self) -> list[Workflow]:
        return list(self._por_modo.values())
