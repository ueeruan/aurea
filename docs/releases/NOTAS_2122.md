# Build 2122 — edição por quadro, texto 3D e efeitos

## Mudanças

- Curvas: painel limitado a 280 dp/pt, cores do tema, abas diretas Easing/Value/Speed. Acesso recupera Y/Z de posição e parâmetros de forma animados; uma chave abre a orientação para criar o segundo ponto.
- Vínculos: normalização do comando Nenhum, preservação da perspectiva 3D herdada e compensação de rotação/escala/pivô com keyframes ao desvincular. Partículas em espaço mundo acompanham o nulo como conjunto.

- Slip, Roll início/fim e Slide no menu da camada e na busca, com passo de 1–3600 quadros, escolha explícita dos vizinhos e um passo de desfazer por operação. Operações inválidas não alteram o histórico.
- Aparar e dividir mantêm o instante da fonte em velocidade alterada/reversa e preservam o relógio dos keyframes. A compensação usa a curva de remapeamento existente e fica editável.
- Texto 3D: rotação X/Y/Z em torno do centro de cada letra, intervalo de letras, espaçamento, Cylinder e Twist. Não há animação automática.
- Cinematic Metal: textura procedural de metal envelhecido, materiais separados para frente/lateral/chanfro. Mapas imutáveis compartilhados entre edições do histórico.
- Stripes, Radial Rays, Grid e Parenting Helper com parâmetros e keyframes. Nomes dos efeitos em inglês; busca mantém aliases em português.
- Catálogo Android cresce além de 64 efeitos; os novos efeitos não somem por limite fixo do buffer.

## Verificação

Compilação Android x86_64 passou. Motor: ClipEdit 7 testes/1.354 verificações; ClipTime 19/1.206; Engine 55/1.602; Scene3D 35/36.288; Timeline 22/2.165; Clipboard 3/48; Precomp 5/75. Zero falhas nessas execuções. Text3D 27/77.312, Parenting Helper 2/29, padrões GPU 1/51 e EffectGraph 13/240 também passaram.

Os testes do importador/GPU preservam resolução e FPS escolhidos para o projeto. Uma expectativa antiga de adotar o formato do primeiro vídeo foi atualizada para a regra já implementada no build anterior.

Auditorias Swift/bridge, recursos, tipos e Xcode passaram. Vinte e um testes de interface iOS estão preparados; execução nativa desta versão ainda pendente. Evidência de render real: `build/pro-editor-ui/cinematic-metal-lighting.png` e `text3d-letter-rotation-x.png`.

Vínculos: 11 testes/506 verificações; desvinculação móvel: 6 cenários estáticos/com uma chave/com animação em 2D/3D; partículas: 13 testes/281 verificações, incluindo comparação de pixels com nulo animado. Testes JVM verificam dimensão do painel e escolha de trilha. Nenhum teste de interface usou o emulador durante a sessão de edição do usuário.

## Limites

- Cylinder organiza letras rígidas numa superfície cilíndrica; não faz deformação contínua dos vértices de cada letra. Até 256 glifos separados.
- O material depende de fonte, luzes e ambiente. Equivalência visual com Element 3D/After Effects não foi comprovada.
- Parenting Helper permite ponderar rotação/escala; não inclui Auto Rotate.
- Slip/Roll/Slide requerem margem de mídia e vizinhos encostados. Curvas de remapeamento preexistentes continuam com sua interpolação; não são substituídas por curvas lineares.
- Validação física em iPhone/Samsung e os cinco projetos completos do roteiro profissional continuam pendentes. Este build não conclui os 60 itens do pedido.

Fontes de referência: [Premiere Slip](https://helpx.adobe.com/premiere/desktop/edit-projects/trim-clips/perform-slip-edits.html), [Alight Motion Stripes](https://guide.alightmotion.com/effects/stripes), [Radial Rays](https://guide.alightmotion.com/effects/radial-rays), [Grid](https://guide.alightmotion.com/effects/grid), [Parenting](https://support.alightmotion.com/hc/en-us/articles/10536997444369-Layer-Parenting-and-Null-Objects). Implementações nativas próprias; não há cópia de plugins AEX/FFX.
