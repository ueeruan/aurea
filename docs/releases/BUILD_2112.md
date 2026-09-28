# Build 2112 — timeline, keyframes e pacotes

Código Android: `a4d5fb83`. Código iOS final: `c463bbea`, na branch `codex/build-2112-timeline`. A diferença do último commit isola somente execuções DEBUG com `AUREA_UI_TEST_PROBE=1` das telas de consentimento de anúncios e novidades; não altera o comportamento Release.

## Correções da timeline

- Android e iOS mostram somente os keyframes do grupo de transformação ou parâmetro de efeito em edição. O editor de curva filtra a trilha exata.
- O arrasto da visão geral escolhe uma trilha real, sem mover todas as propriedades sobrepostas. No painel, move apenas o grupo em foco. Os limites consideram vizinhos das trilhas movidas.
- A seleção compara propriedade, efeito e parâmetro, além do tempo; outra propriedade no mesmo instante não herda o destaque.
- Durante scrub e pinça, o frame exibido é o mesmo inteiro enviado ao motor.
- Criar/apagar keyframe usa o playhead apresentado, convertido pelo início e offset da camada, em vez de um detalhe assíncrono anterior.
- Um grupo com apenas alguns eixos marcados pode ser apagado diretamente; ativar controles 3D não exige criar Z para conseguir remover X/Y.
- A interface mantém o destino de seek enquanto aguarda a confirmação do motor, com prazo de dois segundos. Reproduzir e avançar por frame liberam essa espera.
- Adicionar/remover marcador pausa e posiciona o cursor no frame usado pela operação, inclusive no fim da composição.
- No iOS, as linhas filtradas são armazenadas em cache para evitar refiltrar e ordenar os keyframes em cada desenho.

## Verificações

- Android JVM: 115 testes, zero falhas e zero erros; cinco regressões específicas para isolamento, seleção, limites, grade e remoção parcial XYZ.
- Emulador Android: projeto separado `Projeto 11`, marca adicionada no frame 0, aberta na régua e excluída sem desfazer. Keyframes de posição e rotação criados no mesmo instante; posição arrastada sem levar rotação. Após reabrir o projeto, navegação até a posição e remoção direta do grupo X/Y sem Z confirmadas pelo estado dos controles. Evidências XML em `build/aurea-position-removable.xml`, `build/aurea-position-removed.xml`, `build/aurea-rotation-after-drag.xml`, `build/aurea-marker-editor.xml`.
- Auditorias estáticas Swift/API e tipos: zero problemas. Essas auditorias não substituem a compilação Xcode ou testes em aparelho físico.
- Evidência anterior do núcleo e GPU: ver `../architecture/P13_GPU_TEMPORAL_WEBVIEW_2026-09-25.md`.
- Requisitos de instalação e limites de validação: `REQUISITOS_2112.md`.

O IPA desta versão é gerado sem assinatura. O workflow remoto final é `https://github.com/ueeruan/aurea/actions/runs/36211270713`.

Resultado final: o job de geração do IPA passou; o job de paridade do simulador falhou.
Sete dos oito testes de gestos passaram. O cenário de seta do vídeo voltou a falhar
na preparação por ausência do probe DEBUG (`Read-only DEBUG preview probe is missing`),
portanto a correção do travamento original não foi comprovada. O manifesto dos artefatos
registra o hash e o commit específicos deste IPA; os APKs usam o commit Android acima.

No teste intermediário, sete gestos passaram e a preparação de vídeo não apresentou o probe. A árvore de acessibilidade recuperada do xcresult mostrou as telas de consentimento e novidades cobrindo o app. As execuções de gestos passam a isolar essas telas exclusivamente no modo DEBUG de teste. Esse isolamento não comprova o comportamento do SDK de anúncios/WebView em produção.

Samsung/iPhone físicos, fluidez sustentada e os relatos não reproduzidos continuam sem certificação. A mitigação do WebView evita o provedor conhecido; não é uma correção do Chromium. A inferência de upscale iOS permanece em CPU.
