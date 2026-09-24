"""Contrato puro: workflow, discovery, modelos. Sem servidor no meio."""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import pytest

RAIZ = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RAIZ))
sys.path.insert(0, str(RAIZ / "scripts"))

from aurea_ai.contract import DiscoveryDoc, dimensoes  # noqa: E402
from aurea_ai.models import conferir, faltando, ler_manifesto, registrar_manifesto  # noqa: E402
from aurea_ai.workflows import Biblioteca, Workflow, WorkflowInvalido  # noqa: E402


# ---------------------------------------------------------------------------
# Workflows
# ---------------------------------------------------------------------------

def test_os_dois_workflows_carregam():
    b = Biblioteca(RAIZ / "workflows")
    b.carregar()
    assert set(b.modos()) == {"text_to_video", "image_to_video"}
    for wf in b.todos():
        assert wf.class_names()
        assert wf.modelos_exigidos()


def test_o_bloco_interno_nao_vai_para_o_comfyui():
    wf = Workflow.carregar(RAIZ / "workflows" / "h3_t2v.json")
    assert "_aurea" not in json.dumps(wf.grafo)
    assert "_aurea" not in json.dumps(wf.montar({"prompt": "x"}))


def test_o_mapa_aponta_para_campos_que_existem():
    """Um alvo errado no mapa so apareceria no meio de um job, na A100."""
    for modo in ("h3_t2v.json", "h3_i2v.json"):
        wf = Workflow.carregar(RAIZ / "workflows" / modo)
        for chave, alvo in wf.mapa.items():
            node_id, campo = alvo.split(".", 1)
            assert node_id in wf.grafo, f"{modo}.{chave}: node {node_id} nao existe"
            assert campo in wf.grafo[node_id]["inputs"], \
                f"{modo}.{chave}: campo {campo} nao existe no node {node_id}"


def test_montar_nao_mexe_no_arquivo_original():
    wf = Workflow.carregar(RAIZ / "workflows" / "h3_t2v.json")
    antes = json.dumps(wf.grafo, sort_keys=True)
    wf.montar({"prompt": "outro", "seed": 7})
    assert json.dumps(wf.grafo, sort_keys=True) == antes


def test_montar_recusa_chave_fora_do_mapa():
    wf = Workflow.carregar(RAIZ / "workflows" / "h3_t2v.json")
    with pytest.raises(WorkflowInvalido):
        wf.montar({"caminho_do_servidor": "/etc/passwd"})


def test_workflow_sem_mapa_e_recusado(tmp_path):
    ruim = tmp_path / "ruim.json"
    ruim.write_text(json.dumps({"1": {"class_type": "X", "inputs": {}}}), encoding="utf-8")
    with pytest.raises(WorkflowInvalido):
        Workflow.carregar(ruim)


def test_workflow_com_json_quebrado_e_recusado(tmp_path):
    ruim = tmp_path / "ruim.json"
    ruim.write_text("{ isso nao e json", encoding="utf-8")
    with pytest.raises(WorkflowInvalido):
        Workflow.carregar(ruim)


def test_mapa_apontando_para_node_inexistente_falha_ao_montar():
    grafo = {"1": {"class_type": "X", "inputs": {"a": 1}}}
    wf = Workflow("teste", grafo, {"prompt": "9.a"}, {})
    with pytest.raises(WorkflowInvalido) as e:
        wf.montar({"prompt": "oi"})
    assert "9" in str(e.value)


def test_campo_inexistente_no_node_falha_ao_montar():
    grafo = {"1": {"class_type": "X", "inputs": {"a": 1}}}
    wf = Workflow("teste", grafo, {"seed": "1.b"}, {})
    with pytest.raises(WorkflowInvalido) as e:
        wf.montar({"seed": 3})
    assert "b" in str(e.value)


def test_indice_em_lista_funciona():
    grafo = {"1": {"class_type": "X", "inputs": {"image": ["antiga.png", 0]}}}
    wf = Workflow("teste", grafo, {"imagem": "1.image[0]"}, {})
    saida = wf.montar({"imagem": "nova.png"})
    assert saida["1"]["inputs"]["image"] == ["nova.png", 0]


def test_indice_fora_da_lista_falha():
    grafo = {"1": {"class_type": "X", "inputs": {"image": ["a.png", 0]}}}
    wf = Workflow("teste", grafo, {"imagem": "1.image[5]"}, {})
    with pytest.raises(WorkflowInvalido):
        wf.montar({"imagem": "x.png"})


def test_i2v_tem_o_carregador_de_imagem_e_o_t2v_nao():
    t2v = Workflow.carregar(RAIZ / "workflows" / "h3_t2v.json")
    i2v = Workflow.carregar(RAIZ / "workflows" / "h3_i2v.json")
    assert "LoadImage" in i2v.class_names()
    assert "LoadImage" not in t2v.class_names()


# ---------------------------------------------------------------------------
# Dimensoes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("aspecto,resolucao,esperado", [
    ("16:9", "standard", (1344, 768)),
    ("9:16", "standard", (768, 1344)),
    ("1:1", "standard", (768, 768)),
    ("4:5", "standard", (768, 960)),
    ("16:9", "preview", (896, 512)),
    ("16:9", "high", (1792, 1024)),
])
def test_dimensoes_sao_multiplo_de_32(aspecto, resolucao, esperado):
    l, a = dimensoes(aspecto, resolucao)
    assert (l, a) == esperado
    assert l % 32 == 0 and a % 32 == 0


# ---------------------------------------------------------------------------
# Discovery
# ---------------------------------------------------------------------------

def _doc(**troca) -> DiscoveryDoc:
    base = {"endpoint": "https://algo.trycloudflare.com", "online": True,
            "updatedAt": int(time.time())}
    base.update(troca)
    return DiscoveryDoc(**base)


def test_documento_recente_e_valido():
    assert _doc().valido() is True


def test_servico_errado_e_recusado():
    """Um documento de outro servico na mesma pasta nao pode passar."""
    d = _doc()
    object.__setattr__(d, "service", "outra-coisa")
    assert d.valido() is False


def test_endpoint_sem_https_e_recusado():
    assert _doc(endpoint="http://1.2.3.4:8000").valido() is False


def test_offline_e_recusado():
    assert _doc(online=False).valido() is False


def test_batida_velha_e_recusada():
    assert _doc(updatedAt=int(time.time()) - 91).valido() is False
    assert _doc(updatedAt=int(time.time()) - 89).valido() is True


def test_documento_inclui_o_que_o_app_mostra():
    d = _doc(gpu="NVIDIA A100-SXM4-80GB")
    corpo = d.como_dict()
    assert corpo["service"] == "aurea-h3"
    assert corpo["model"] == "MiniMax-H3"
    assert "text_to_video" in corpo["capabilities"]


# ---------------------------------------------------------------------------
# Modelos
# ---------------------------------------------------------------------------

def _pesos(tmp_path: Path, nomes: list[str]) -> Path:
    for n in nomes:
        (tmp_path / n).write_bytes(b"x" * 64)
    return tmp_path


def test_sem_manifesto_nao_bloqueia(tmp_path):
    nomes = ["a.safetensors"]
    d = _pesos(tmp_path, nomes)
    situacoes = conferir(d, nomes)
    assert situacoes[0].aviso == "sem manifesto"
    assert faltando(situacoes) == []      # avisa, mas nao impede


def test_arquivo_ausente_bloqueia(tmp_path):
    assert faltando(conferir(tmp_path, ["a.safetensors"])) == ["a.safetensors"]


def test_arquivo_vazio_bloqueia(tmp_path):
    (tmp_path / "a.safetensors").write_bytes(b"")
    assert faltando(conferir(tmp_path, ["a.safetensors"])) == ["a.safetensors"]


def test_manifesto_registrado_e_respeitado(tmp_path):
    nomes = ["a.safetensors"]
    _pesos(tmp_path, nomes)
    registrar_manifesto(tmp_path, nomes)
    assert ler_manifesto(tmp_path)["a.safetensors"]["tamanho"] == 64
    assert faltando(conferir(tmp_path, nomes)) == []


def test_arquivo_truncado_depois_do_manifesto_bloqueia(tmp_path):
    nomes = ["a.safetensors"]
    _pesos(tmp_path, nomes)
    registrar_manifesto(tmp_path, nomes)
    (tmp_path / "a.safetensors").write_bytes(b"x" * 10)
    situacoes = conferir(tmp_path, nomes)
    assert "tamanho difere" in situacoes[0].problema
    assert faltando(situacoes) == ["a.safetensors"]


def test_arquivo_trocado_com_mesmo_tamanho_bloqueia(tmp_path):
    """O caso que so o hash pega: download interrompido e retomado errado."""
    nomes = ["a.safetensors"]
    _pesos(tmp_path, nomes)
    registrar_manifesto(tmp_path, nomes)
    (tmp_path / "a.safetensors").write_bytes(b"y" * 64)
    assert conferir(tmp_path, nomes)[0].problema == "hash difere"


def test_manifesto_ilegivel_nao_derruba(tmp_path):
    (tmp_path / "a.safetensors").write_bytes(b"x" * 64)
    (tmp_path / "manifest.json").write_text("{ nao e json", encoding="utf-8")
    assert faltando(conferir(tmp_path, ["a.safetensors"])) == []


# ---------------------------------------------------------------------------
# Verificador de workflow
# ---------------------------------------------------------------------------

def test_verificador_aponta_a_classe_que_falta():
    from verificar_workflow import conferir as conferir_nodes
    b = Biblioteca(RAIZ / "workflows")
    b.carregar()
    faltam = conferir_nodes(b, {"UNETLoader"})
    nomes = {c for _, c in faltam}
    assert "MiniMaxH3Sampler" in nomes
    assert "UNETLoader" not in nomes


def test_verificador_aprova_quando_tudo_existe():
    from verificar_workflow import conferir as conferir_nodes
    b = Biblioteca(RAIZ / "workflows")
    b.carregar()
    tudo = set()
    for wf in b.todos():
        tudo |= wf.class_names()
    assert conferir_nodes(b, tudo) == []
