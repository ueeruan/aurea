"""Configuracao do servidor Aurea AI.

Tudo por variavel de ambiente. Nenhum segredo tem valor padrao util: sem a
variavel o servidor recusa iniciar em modo `producao` e grita no log em modo
`dev`.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field


def _int(nome: str, padrao: int) -> int:
    try:
        return int(os.environ.get(nome, "") or padrao)
    except ValueError:
        return padrao


def _bool(nome: str, padrao: bool) -> bool:
    v = (os.environ.get(nome) or "").strip().lower()
    if not v:
        return padrao
    return v in ("1", "true", "yes", "on", "sim")


@dataclass(frozen=True)
class Config:
    # --- identidade ---
    service: str = "aurea-ai"
    version: int = 1
    engine: str = "minimax-h3"

    # --- tokens ---
    # Tokens que o APP usa. Vive tambem no Colab.
    # Um por tester: e o que separa o historico de cada um, e o que permite
    # revogar um aparelho perdido sem trocar o token de todos.
    server_tokens: tuple[str, ...] = field(default_factory=lambda: tuple(
        t.strip() for t in (os.environ.get("AUREA_SERVER_TOKENS")
                            or os.environ.get("AUREA_SERVER_TOKEN") or "").split(",")
        if t.strip()))
    # Token administrativo. NUNCA sai do Colab.
    admin_token: str = field(default_factory=lambda: os.environ.get("AUREA_ADMIN_TOKEN", ""))
    # Token de escrita do documento de discovery (GitHub).
    discovery_token: str = field(default_factory=lambda: os.environ.get("AUREA_DISCOVERY_TOKEN", ""))

    # --- discovery ---
    discovery_repo: str = field(default_factory=lambda: os.environ.get("AUREA_DISCOVERY_REPO", ""))
    discovery_branch: str = field(default_factory=lambda: os.environ.get("AUREA_DISCOVERY_BRANCH", "main"))
    discovery_path: str = "discovery/aurea-h3.json"
    heartbeat_seconds: int = field(default_factory=lambda: _int("AUREA_HEARTBEAT_SECONDS", 20))

    # --- comfyui ---
    comfy_url: str = field(default_factory=lambda: os.environ.get("AUREA_COMFY_URL", "http://127.0.0.1:8188"))
    comfy_timeout_s: int = field(default_factory=lambda: _int("AUREA_COMFY_TIMEOUT_S", 30))
    # Teto de tempo de UM job de geracao. H3 em A100 leva minutos.
    job_timeout_s: int = field(default_factory=lambda: _int("AUREA_JOB_TIMEOUT_S", 1800))
    models_dir: str = field(default_factory=lambda: os.environ.get("AUREA_MODELS_DIR", "/content/models"))
    workflows_dir: str = field(default_factory=lambda: os.environ.get("AUREA_WORKFLOWS_DIR", "workflows"))

    # --- fila ---
    max_gpu_jobs: int = field(default_factory=lambda: max(1, _int("AUREA_MAX_GPU_JOBS", 1)))
    max_queue: int = field(default_factory=lambda: _int("AUREA_MAX_QUEUE", 20))
    job_ttl_s: int = field(default_factory=lambda: _int("AUREA_JOB_TTL_S", 3600))

    # --- uploads ---
    max_upload_bytes: int = field(default_factory=lambda: _int("AUREA_MAX_UPLOAD_MB", 12) * 1024 * 1024)
    upload_dir: str = field(default_factory=lambda: os.environ.get("AUREA_UPLOAD_DIR", "/content/aurea_assets"))

    # --- limites ---
    rate_limit_per_min: int = field(default_factory=lambda: _int("AUREA_RATE_LIMIT_PER_MIN", 30))
    max_prompt_chars: int = 2000
    cors_origins: str = field(default_factory=lambda: os.environ.get("AUREA_CORS_ORIGINS", ""))

    # --- modo ---
    # "dev" imprime avisos; "producao" recusa iniciar sem os tokens.
    mode: str = field(default_factory=lambda: os.environ.get("AUREA_MODE", "dev"))
    # Endpoint publico usado no discovery quando nao ha tunel (teste local).
    public_url: str = field(default_factory=lambda: os.environ.get("AUREA_PUBLIC_URL", ""))

    def exigir_tokens(self) -> list[str]:
        """Devolve a lista do que falta. Vazia = tudo pronto."""
        falta = []
        if not self.server_tokens:
            falta.append("AUREA_SERVER_TOKEN")
        if not self.admin_token:
            falta.append("AUREA_ADMIN_TOKEN")
        return falta


_CONFIG: Config | None = None


def config() -> Config:
    global _CONFIG
    if _CONFIG is None:
        _CONFIG = Config()
    return _CONFIG


def reset_config_for_tests() -> None:
    """So para os testes: refaz o Config lendo o ambiente de novo."""
    global _CONFIG
    _CONFIG = None
