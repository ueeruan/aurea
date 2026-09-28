# Aurea beta — build 2111

## Novidades

- **Novidades na abertura**: painel com mudanças e limitações da versão, exibido até ser fechado; pode ser reaberto pela tela inicial nas duas plataformas.
- **Cena 3D por gestos**, sem sliders nem campos: arraste o objeto para mover, 1 dedo no vazio gira a vista, 2 dedos aproximam, toque duplo recentra.
- **Temas**: Aurea, Meia-noite, Grafite, Esmeralda, Ametista e Pôr do sol (Ajustes › Tema), aplicados na hora.
- **Presets de efeito**: "Salvar como preset" em cada efeito; "Meus presets de efeito" no menu dos efeitos.
- **Importar do Alight Motion** (.xml, .amproj, .zip): efeitos mapeados viram preset; conversões com efeitos ignorados ou avisos de perda são recusadas antes de salvar/aplicar. Equivalência visual ainda não comprovada com exportações reais do AM. Leitura fora da interface, limitada a 64 MiB.
- Ações rápidas do dock com nome (Início, Dividir, Fim, Puxar, Som); seta na borda da timeline quando o clipe da linha está fora da tela.

## Correções

- iOS: corrigida no código a chamada incorreta a `pause()` em "Puxar a camada para o cabeçote"; movimento e fluxo de adicionar/fechar/desfazer efeito passaram no simulador: 8/8 testes no run 36203157530 (commit 7bdb5477). A captura da Home teve timeout, portanto o run geral não passou.
- iOS: o preview não redesenha mais o quadro inteiro a cada atualização da tela quando nada mudou; a timeline só se redesenha com mudanças reais.
- Android: menos consultas ao motor durante o play (preview mais estável).
- A área em volta do quadro ficou cinza e o quadro tem contorno: projeto preto não se confunde mais com o fundo.
- Marcas que apareciam "do nada": tocar na régua só move o cabeçote; segurar a âncora abre um rascunho que só vira marca ao salvar; outra camada por cima tem prioridade sobre a âncora.
- O "+" não cobre mais o clipe escolhido.
- Android: existe tratamento para uma falha interna do WebView de anúncios, mas não há reprodução e reteste suficientes para declarar resolvida a falha Unity/WebView após exportar.
- Importação AM: corrigido laço infinito ao gerar nomes únicos para presets duplicados com títulos longos, nas duas plataformas.

## Pendências e limites

- Upscale por IA segue na CPU (modelo de anime/ilustração).
- Tracking, estabilização, optical flow e partículas ainda sem aceitação ampla com cenas reais.
- Reteste de fontes no Samsung afetado e testes em iPhone físico continuam pendentes.
