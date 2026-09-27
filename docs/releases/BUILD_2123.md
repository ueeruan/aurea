# Build 2123

Pacotes em preparação. Android `com.aurea.aurea`, ARMv7 e ARM64, mínimo API 26. iOS `com.aurea.aurea`, ARM64, mínimo iOS 16.3, IPA sem assinatura para sideload.

## Evidência antes da publicação

- Efeitos MediaLab: 7 testes / 100.187 verificações, zero falhas, Vulkan real no host. Inclui benchmark 720p com readback, não medição de celular.
- Tracking: 10 entradas / 173 verificações, zero falhas. MotionGeometry: 11 entradas / 1.965 verificações, zero falhas. Benchmarks opt-in não contam como executados.
- Os seis shaders novos traduzidos para Metal 2.1 e GLES 3.1. Verificadores locais de APIs Swift/ObjC, tipos, projeto Xcode, parâmetros e recursos compartilhados: zero problemas.
- Android: compilação debug anterior e 124 testes JVM passaram. Release 2123 e suíte completa do motor em execução; atualizar este registro ao concluir.
- Emulador Android reservado ao usuário. O teste novo de tracking compila, mas sua execução conectada aguarda disponibilidade.

## Limites conhecidos

Não equivale à conclusão integral do Prompt 4 ou à reprodução idêntica dos plugins desktop. Ver `PROMPT04_TRACKING_PROGRESS_2026-09-26.md` e `MEDIA_LAB_EFFECTS_2026-09-26.md` em `docs/architecture` para recursos faltantes e evidência. A51 5G e iPhone físicos não estão disponíveis nesta máquina. A mitigação Samsung não comprova a resolução do fechamento relatado.

Não distribuir arquivos anteriores como se fossem 2123. Registrar commit, validação dos artefatos, tamanhos, SHA-256 e execução Apple após a geração.
