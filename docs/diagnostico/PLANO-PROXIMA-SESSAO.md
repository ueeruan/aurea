# Plano para a próxima sessão (diagnóstico de 18/10/2026)

Base: branch `claude/release-2191` (0.2.3, commit `7f3d4ab9`), pasta `D:\Users\Ruan\Documents\Aureabeta-vivo`.
Tudo abaixo veio de leitura de código: nada foi compilado, testado em host ou visto em aparelho.
Detalhes, com arquivo:linha, em `export-falhas.md` e `efeitos-imagem-feed.md`.

## 1. Export falhando em todos os aparelhos (fazer nesta ordem)

| # | Causa | Status | Correção |
|---|---|---|---|
| C1 | O export exige TODOS os assets do projeto, não só os usados (`ExportRecovery.cpp:240-268`). Uma URI `content://` morta em qualquer asset, inclusive de mídia já substituída, dá erro 18. Assets nunca saem do projeto; "Substituir mídia" cria outro asset. | provado | Resolver só os assets alcançáveis pela composição exportada; pular imagens já carregadas; "Substituir mídia" revincula em vez de criar asset; coletar órfãos ao salvar. Teste de host com asset morto não usado. |
| C4 | PreRender e export dividem o mesmo contexto. Exportar com o PreRender rodando dá erro 5, e nenhuma tela cancela o PreRender antes. O PreRender também ignora o aparar. | provado | Cancelar e esperar o PreRender no início do export (motor), nas duas plataformas. |
| C2 | Duração sem teto, que gruda e é herdada. A auto-rolagem da timeline vira catraca ao arrastar ou aparar perto da borda (`TimelineController.kt:204,1392-1402`; iOS `TimelineView.swift:1946`). Camadas novas nascem com `end = duração`. É a explicação provável do "Frame 0 de 522520" (4,8 h a 30 fps). | código provado, execução inferida | Limitar a auto-rolagem ao fim de navegação do início do gesto + margem; camadas novas terminam no fim do conteúdo; teto de sanidade no export (ex.: 3 h) com mensagem; mostrar a duração na tela Exportar. |
| C5 | No quadro 0 a tela não mostra o estágio (decodificando, shaders, encoder): lento parece travado. No iOS o watchdog não roda em "preparando" e o Cancelar fica desativado. | provado | Publicar a fase atual e mostrar "Preparando o primeiro quadro…" nas duas telas; Cancelar sempre ativo. |
| C3 | Todo export recopia todos os `content://` (120 s por arquivo, depois erro 26). `export-sources` nunca é limpa e acaba em erro 28. A cópia só serve à recuperação, que está desligada. | provado | Não copiar quando a recuperação está desligada; limpar `export-sources` com teto. |
| C6 | Duração do contêiner não é checada na importação. | inferido | Validar a duração da mídia contra o número de quadros ou de amostras na importação. |

Para confirmar o print do usuário: `.aurea` dele, "Relatar um problema", ou logcat com
`video importado:`, `export-ffmpeg frame=0` e `export start failed`.

## 2. "Imagem inapropriada" ao abrir Efeitos

É a foto de amostra das prévias de efeito (`android/app/src/main/assets/previa_efeitos.jpg`,
igual em `engine/platform/ios/app/`), gerada por IA. O motor desenha todo cartão de efeito em
cima dela, e não dá para desligar. Não é anúncio nem feed.

- **P0:** trocar por uma imagem neutra (pode ser de IA, com assunto central e cantos escuros),
  igual byte a byte nas duas plataformas. Subir a chave de cache `-compare-v2` para `-compare-v3`
  (`EditorStore.kt:977`, `EffectPreviewStore.swift:34`) e ajustar os testes que dependem da foto
  (lista no diagnóstico).
- **P1:** no iOS, cair na cartela procedural se a foto faltar (hoje zera as prévias).
- **P2:** ajuste "Prévias de efeitos" ligado/desligado nas duas plataformas.
- **P3:** a Comunidade não tem denunciar, bloquear nem ocultar (risco de loja; não é em Efeitos).

## 3. Pedido "vídeos de exemplo para cada efeito"

Hoje não há nenhum asset por efeito: só a foto e a renderização sob demanda (cerca de 220 efeitos).
Plano: o motor gera de 12 a 16 quadros por efeito a partir de 3 ou 4 cenas próprias; a UI anima só
o cartão tocado e a ficha; cache em disco com teto de cerca de 24 MB; repetir quando vier `Busy`.
Descartados: MP4 embutido (cerca de 33 a 66 MB) e download do servidor (volta o risco de conteúdo
impróprio).

## Pendências herdadas (0.2.3)

- A inclinação do Puppet continua sem correção. O solver em banda não mudou nada no cenário medido;
  ver `docs/releases/0.2.3.md` e `build/test_puppet_solver.cpp.medicao`.
- A 0.2.3 não tem APK nem IPA (pedido) e o Swift dela não foi compilado.
