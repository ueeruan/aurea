# Aurea 0.3.6 BETA (versionCode 2203) — AutoRig 3D, Motion Capture e correções da 0.3.5

Android: AAB (arm64-v8a + armeabi-v7a). iOS: IPA sem assinatura (CI do GitHub).

Obrigado a **hanszx_** pela colaboração na nova interface, e a quem mandou os relatos da 0.3.5
(TOXXIC BOY, Frostko, وشـو؟, Shawn Wesley, FakhruL, BkhanggDesu, Angry^^, M4YKO e MIYUKI).

## Google Play (pt-BR)

```
Novidades da 0.3.6 BETA:
• AutoRig 3D (BETA): esqueleto automático para personagens em T-pose
• Motion Capture (BETA): transforme um vídeo em animação 3D
• Ambiente da cena 3D refeito, mais simples
• Textura 3D fácil de trocar, até com uma camada da timeline
• Keyframes, corte, zoom e grade da barra lateral corrigidos
• Painel de efeitos rolando no iPhone
• MKV mais leve e com áudio limpo
• Exportação mais confiável
```

## Google Play (en-US)

```
New in 0.3.6 BETA:
• AutoRig 3D (BETA): automatic skeleton for T-pose characters
• Motion Capture (BETA): turn a video into 3D animation
• Simpler, redesigned 3D scene environment
• Easy 3D texture swap, even from a timeline layer
• Keyframes, split, sidebar zoom and grid fixed
• Effects panel scrolls on iPhone
• Smoother MKV playback with clean audio
• More reliable export
```

## Versão completa

### Novidades
- **AutoRig 3D (BETA)** — aba **Esqueleto** no painel 3D de um modelo importado:
  - "AutoRig 3D" cria um esqueleto humanoide (quadril, coluna, peito, pescoço, cabeça, braços,
    antebraços, mãos, pernas, canelas e pés) com skinning automático, com progresso e Cancelar.
  - Modelos que já trazem esqueleto (Mixamo, Bip01, Unreal, Rigify, VRM, DAZ) são reconhecidos pelo nome
    dos ossos e podem ir direto para o Motion Capture.
  - **Corrigir esqueleto**: arrastar as juntas na cena 3D ou escolher qual osso do arquivo é cada parte.
  - **IK** de braços e pernas (alvo e polo) e ossos animáveis com keyframes e no editor de curvas. Sai igual
    na prévia e na exportação.
- **Motion Capture (BETA)** — na mesma aba, para um personagem com esqueleto:
  - Importar vídeo da galeria ou gravar pela câmera → Analisar movimento (no próprio aparelho, com progresso
    e Cancelar) → Pré-visualizar → ajustar intensidade, suavização, velocidade e alinhamento → Aplicar à
    timeline. Vira keyframes normais, editáveis, num único desfazer.
  - Suaviza tremores, preenche falhas curtas, prende os pés no chão (foot lock), corrige mãos e pés com IK e
    adapta o movimento às proporções do personagem. Avisa quando braços ou pernas ficaram escondidos.
- **Ambiente da cena 3D refeito**: a aba Ambiente agora tem só céu e fundo, luz ambiente e exposição, chão e
  sombra, em seções espaçadas. Qualidade, tone map e bloom foram para a nova aba **Render**; sombras do objeto
  foram para **Material**.
- **Textura de modelo 3D**: cada material tem uma linha "Textura" com miniatura e Arquivo · Timeline · Remover.
  **Timeline** usa uma camada ou composição do projeto como textura, atualizada a cada quadro (a própria
  camada e o que está dentro da cena 3D ficam de fora).

### Correções
- **Keyframes**: adicionar um keyframe em posição/escala mostrava dois losangos sobrepostos; agora é um só.
  O mesmo "keyframe estranho" aparecia ao entrar no modo de transformação. Os keyframes têm área de toque
  maior e respondem mesmo com a timeline deslizando.
- **Corte**: cortar perto do começo do clipe ou durante o play cortava antes do ponto mostrado (ou não
  cortava). Agora corta no quadro da prévia; cortar durante o play pausa o vídeo. Vale também para aparar
  o início/fim no cabeçote.
- **Barra lateral**: zoom (+, −, 100%) e grade voltaram a responder.
- **Efeitos**: arrastar um efeito para a esquerda volta a remover (com Desfazer), sem atrapalhar a rolagem.
  No iPhone, o painel de efeitos volta a rolar começando o arrasto em cima de um efeito.
- **Texto 3D**: em alguns projetos as configurações não mudavam (material da camada tomando a frente,
  memória esgotada por malhas antigas, projeto antigo com texto por letra, valores inválidos salvos). Quando
  uma mudança é recusada, o app agora diz o motivo.
- **MKV 2K 60fps**: o áudio picotava (tempos do MKV em milissegundos) e perdia para o vídeo; o fps saía
  58,8 em vez de 60. Fontes que o aparelho não decodifica a tempo ganham uma cópia de prévia leve sozinhas.
- **Exportação**: composições com largura ou altura ímpar falhavam em todas as tentativas; agora exportam
  no tamanho válido mais próximo. No Android 15, quando o sistema encerra o tempo de exportação em segundo
  plano, o app avisa em vez de ser fechado.
- **Editor**:
  - Ao abrir um projeto e deslizar, o clipe sumia da tela; a rolagem agora tem limite e segue um eixo por gesto.
  - Tocar fora para fechar a barra de ferramentas não move mais o cabeçote.
  - A Composição não fica mais presa ao ímã da Edição depois de ir e voltar.
  - O clipe selecionado rola para ficar visível acima do painel.
  - Texto vai até 2000 px, com campo para digitar.
  - Voltar, play e avançar centralizados.
  - Ícone de corte fino, marcas da régua sem sobreposição e painéis com altura máxima.

### Limitações conhecidas (BETA)
- AutoRig: só humanoides em T-pose; sem dedos, rosto e clavícula; pesos automáticos podem precisar de ajuste
  em axilas e virilha.
- Motion Capture: vídeo comum não tem profundidade real, então o 3D é aproximado (3D de verdade só no iPhone
  com iOS 17+); partes escondidas são preenchidas ou ficam paradas; câmera frontal espelhada troca esquerda e
  direita; sem dedos e rosto.
- iPhone ainda não abre arquivos MKV.
- Animador de texto no iPhone: a causa ainda não foi encontrada — mande como você adicionou e o que acontece.
