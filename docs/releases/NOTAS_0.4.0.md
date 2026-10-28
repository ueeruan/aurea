# Aurea Beta 0.4.0 — Fase 1 (build 2204) · 28/10/2026

Android (AAB, Google Play) e iPhone (IPA). Mesmas mudanças nas duas plataformas.

## Novidades

### Toque leve (desempenho em celulares antigos)
Enquanto o dedo arrasta qualquer controle (alça de curva, slider, mover/girar/escalar,
cor, keyframe), a prévia desenha um **rascunho mais leve** e volta à qualidade normal
no quadro seguinte ao soltar. O motor decide pela classe do aparelho:

| Aparelho | Durante o arrasto |
|---|---|
| Fraco (ex.: 2017) | reduções a 1/4 (motion blur, partículas, sombras, SSAO, blur, flow) e metade da resolução |
| Médio | reduções a 1/4, resolução igual |
| Bom | reduções a 1/2 |
| Topo de linha | nada muda |

Os quadros de rascunho não entram na medição do preview automático, então a
resolução não "sobe" errado depois do gesto. O export não muda (qualidade cheia sempre).

### 7 estilos novos de texto 3D
Em Texto 3D › Material › Materiais prontos: **Inflado** (puffy), **Retrô 80** (frente
cromada, lateral magenta, chanfro ciano aceso), **Borda neon** (só o chanfro brilha),
**Bloco** (extrusão longa tipo "sombra longa"), **Gelo**, **Pedra** (esculpida, chanfro reto)
e **Ouro rosé**. Cada estilo ajusta também profundidade e chanfro; texto, fonte e
animação continuam os seus.

## Correções
- **Editor de curvas**: a alça de baixo (perto do 0) às vezes "não mexia" porque a faixa
  ◀ rótulo ▶ ficava por cima da base do gráfico e pegava o toque. A faixa agora fica
  abaixo do gráfico. Os sliders de Quantidade/Oscilações/Amortecimento seguem o dedo
  (antes o polegar podia voltar enquanto o motor respondia).
- **Edição (não Composição)**: com um painel aberto a prévia cai para 30 % da altura e a
  linha do tempo fica com 72 dp no mínimo; em telas baixas as abas dos painéis tinham
  ~150 dp e não dava para mexer.
- **Exportação**: um lado menor pedido ímpar (ou valor inválido vindo da ponte) não abre
  mais o encoder com tamanho gigante.
- **Motion Capture**: pé encurtado no 2D não vira mais o eixo do joelho (canela reta).

## Validação
- Motor: testes de host (`DevicePolicy.InteractionDraftByTier`, `MaterialPreview.PresetBalls…`
  com 14 materiais) — ver resultado no commit.
- Android: AAB release assinado com a chave de upload.
- iPhone: IPA gerado pelo workflow `build-ipa.yml` (sem assinatura, como nas anteriores).
- **Não validado em aparelho** nesta rodada: o "Toque leve" e os novos estilos precisam
  de teste manual num Android antigo e no iPhone.

## Fora desta fase (próximas)
- Export em pipeline (decode → render → encode sobrepostos; `exportPipelineDepth`).
- Máscaras com efeitos de tipografia (texto como máscara/track matte animado).
