# Limpeza do AUREA — resultado de 13/10/2026

**Liberados 122.7 GB (114.3 GiB) reais no volume C:.**

Foram removidos 68 caminhos expressamente autorizados: builds C++/Android antigos, caches locais, dois builds Claude, dados de emuladores temporários, buffers de renderização sem compressão e APKs de instrumentação duplicados. Não houve falha nem exclusão fora da lista aprovada.

A pasta principal passou de 117.86 GiB para cerca de 32.79 GiB lógicos. A liberação total também inclui os outros onze worktrees. As medidas lógicas de diretórios não são equivalentes ao espaço físico do disco, especialmente para imagens esparsas de emulador.

## Integridade

- 2773 arquivos protegidos conferidos por SHA-256: zero diferenças ou ausências novas.
- Estado do Git idêntico ao registro anterior à limpeza.
- Os arquivos novos de implementação Android/iOS/C++ e relatórios continuam presentes.
- Todos os doze checkouts mantêm sua entrada `.git`; nenhum worktree foi removido ou desregistrado.
- APK da timeline preservado: SHA-256 `ECB19A9F445EA741478780D951A6103293AFD20F1C2A181AD9AE682C236C5C1B`.
- Entrega build-2183 Android arm64/arm32 e IPA unsigned preservada, assim como build-2182.
- Chaves, configurações locais, FFmpeg instalado, third_party, projetos, mídias e evidências REA preservados.
- Backup de 24/09 e AVDs globais existentes não foram alterados.

## Registros

- `PROPOSTA.md`: lista apresentada e aprovada pelo usuário.
- `proposed-directories.json`: os 68 caminhos aprovados.
- `deleted-paths.json`: resultado por caminho, incluindo os candidatos já ausentes.
- `deletion-start.json` e `deletion-result.json`: espaço livre imediatamente antes/depois.
- `protected-files-before.json` e `preservation-result.json`: conferência de integridade.
- `workspace-before.json` e `workspace-after.json`: inventários sem percorrer reparse points.

O restante da pasta principal contém fontes, dependências, mídias, referências, diagnósticos e histórico de entregas preservados. Uma nova compilação recriará seus intermediários; não foram removidos SDKs nem ferramentas instaladas.
