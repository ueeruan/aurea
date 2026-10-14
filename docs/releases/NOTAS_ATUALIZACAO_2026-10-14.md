# AUREA 0.1.9 (build 2185) — notas de atualização (14/10/2026)

Base: 0.1.8 BETA 3 (build 2184). Branch `claude/fix-export-vivo-2185`.
Android: APK arm64 e arm32. iOS: IPA sem assinatura (CI do GitHub).

## Correções

- **Export preso em "Preparando" em celulares com GPU PowerVR** (vivo V2026, Helio P35):
  - o driver gráfico levava dezenas de segundos para preparar cada shader;
  - agora os shaders são preparados só quando um efeito precisa deles;
  - o export reaproveita o que a prévia já preparou na GPU;
  - a prévia para de desenhar assim que o export começa.

  O encoder de software já existia (OpenH264/Kvazaar + o do Android).
- **Diagnóstico de fechamentos** (só Android) lista os codificadores de vídeo do aparelho e
  se o app roda em 32 ou 64 bits.
- **Texto do track matte** agora diz que ele usa a camada **de baixo**, que é como o app
  funciona.

## Novidades

### Máscaras e seleção (Android e iOS)
- **Barra ao segurar camadas:** *Agrupar*, *Agrupar e mascarar*, *Agrupar e excluir* e
  *Apagar* no canto superior direito; o `›` mostra as outras ações.
- **Seleção:**
  - segurar uma camada no palco põe ou tira ela da seleção;
  - arrastar o dedo pelas pílulas da timeline soma camadas;
  - as escolhidas mostram ✓, e tocar no ✓ tira a camada.
- **Máscara de recorte por camada:** no menu ⋯ da camada, *Criar/Liberar máscara de
  recorte*. A camada aparece só dentro da camada de baixo, e a timeline mostra ↳ na pílula.
  - Liberar uma camada libera também as recortadas logo acima.
  - Mover uma camada recortada para fora da cadeia tira o recorte.
  - Projetos antigos abrem sem nada recortado (formato da timeline: versão 48).

### Efeitos
- **Seções recolhíveis** com interruptor e dicas em Saber, Lens Flare/Scene Flare,
  Deep Glow, Turbulence, 3D Shatter e Extrusão. Cada seção lembra se ficou aberta ou fechada.
- **Alça no preview para parâmetros de ponto:** toque no nome do ponto e arraste no preview.
  Se o ponto for animado, a alça cria keyframe.
- **Deslizar para a esquerda** num efeito fechado apaga, com *Desfazer* no aviso.
- **Extrusão 3D:** *Reflexo do ambiente*, *Metal* e *Rugosidade*, usando o HDRI/estúdio da
  cena como os modelos 3D. Projetos antigos abrem sem reflexo.
- **3D Shatter de verdade:** na cena 3D as peças usam a câmera, a profundidade e as luzes
  e caem para o chão do mundo. Adicionar o efeito coloca a camada no 3D.

### Timeline e Início
- Os keyframes do parâmetro em edição aparecem fortes e os dos outros, esmaecidos; o keyframe
  sob o cursor fica na cor de destaque.
- Camadas novas surgem suavemente.
- No iOS, a rolagem vertical volta ao lugar ao sair do modo compacto. No Android ela já
  voltava; só foi corrigido um caso em que a rolagem ainda em movimento zerava a posição.
- Início:
  - *Selecionar* no menu ⋯ do projeto;
  - etiquetas de fps, proporção, resolução e tamanho em cada projeto da lista (no celular).

## Validação

- **Motor (host Windows/Vulkan):**
  - Testes novos passam: máscara de recorte (13), seções de efeito (4), reflexo da extrusão,
    Shatter na cena, regra de agrupar e mascarar, e o reaproveitamento de shaders e
    pipelines no export.
  - Uma suíte completa rodou antes de as três frentes entrarem: as mesmas 28 falhas que o
    BETA 3 já tinha, mais `PreRenderCache.CorruptFrameHeader…`, que falhou nas rodadas
    completas deste branch e passa sozinho (não é código desta branch). Depois de juntar as
    três frentes rodaram só os grupos principais, sem falhas.
- **Android:** compila; os testes JVM passaram em cada frente (420–428).
- **iOS:**
  - checagens estáticas do CI passam;
  - o IPA é compilado no CI do GitHub;
  - os testes de simulador **não** rodaram nesta build.
- **Em aparelho:** nada foi validado ainda (vivo, Android e iPhone). Também não foram
  executados os renderizadores GLES e Metal da máscara de recorte.

## Pendências conhecidas

- O editor de gradiente não foi feito: nenhum efeito usa parâmetro de gradiente ainda.
- Compilar um pipeline novo pela primeira vez nesse driver PowerVR ainda segura a thread de
  render por alguns segundos.
- A decisão de segurar a pílula continua sendo **travar** a camada; na referência, segurar
  seleciona.
