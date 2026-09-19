# O 3D NOVO — Diligent + Assimp no núcleo C++, com o 3D na timeline

Plano de trabalho para substituir o motor 3D. Escrito depois de medir o
que existe, em 18/09/2026. **Nada aqui foi implementado ainda.**

---

## 1. O que existe hoje (medido, não estimado)

| peça | onde | tamanho |
| --- | --- | --- |
| Renderer 3D | `flutter_scene: ^0.23.0` (pacote externo) | — |
| Porta do renderer para o palco | `application/scene3d_gpu.dart` (`Scene3DGpu` / `Scene3DGpuView`) | 1.350 linhas |
| Modelo da cena (nós, materiais, câmera, luzes, keyframes) | `domain/scene3d.dart` | 2.265 linhas |
| Estúdio A (a porta do `+`) | `presentation/am/scene3d_studio.dart` | 2.492 linhas |
| Estúdio B (a ficha da cena) | `presentation/am/scene3d_sheet.dart` | 3.161 linhas |
| Arquivos que mencionam `Scene3D`/`SceneNode` | 61 de `lib/` | — |

**`filament` não existe nas dependências nativas do projeto** (o único
resultado da busca é um binário de cache do Gradle). Quem desenha o 3D
hoje é o `flutter_scene`. "Remover o Filament" hoje é remover o
`flutter_scene` — e com ele as 9.268 linhas acima.

**A porta já existe e está no lugar certo:** o palco chama
`Scene3DGpuView` atrás de `Scene3DGpu.indisponivel`
(`preview_stage.dart:7403`). É por aí que o novo backend entra — e é o
que permite trocar o motor sem mexer no editor.

## 2. O que já existe a favor (e não é pouco)

`packages/aurea_render` — **núcleo de renderização em C++ já em
produção**, 23 fontes:

```
avaliador_da_timeline.cpp/.h   ← o avaliador de timeline JÁ EXISTE
backend_vulkan.cpp/.h          ← superfície Vulkan
compositor.cpp/.h              ← composição de camadas
gerenciador_de_recursos.cpp    ← cache de recursos (textura, shader)
gerenciador_de_shaders.cpp
fila_de_comandos.h
relogio_do_quadro.cpp          ← relógio por quadro
jni_android.cpp                ← ponte Android
particulas.cpp/.h
nucleo.cpp/.h, api.cpp
```

- Build hook nativo em `packages/aurea_render/hook/build.dart`.
- ABI Dart **versão 5**, em `lib/aurea_render.dart`, com a regra escrita:
  *"não há frame atravessando a ponte"* — estado → comando → C++ → GPU →
  superfície.
- Outros quatro hooks nativos já provam o caminho: `aurea_core`,
  `aurea_meshopt`, `aurea_timecore`, `aurea_tracker2`.

**O que ele NÃO é:** um renderizador 3D. Não tem malha, glTF, PBR, depth
nem esqueleto. É o compositor **2D**. Ou seja: a fundação existe, o
andar 3D não.

## 3. A decisão de arquitetura

**Um renderer só, não dois.** `aurea_render` ganha um backend 3D dentro
dele (Diligent + Assimp), em vez de nascer um `aurea_3d` separado. Os
motivos, na ordem em que pesam:

1. O 3D precisa compor com o 2D **no mesmo quadro** (a timeline do
   pedido tem `Modelo A 3D` embaixo de `Texto 2D`). Dois renderizadores
   separados só se compõem por textura intermediária: um passe a mais,
   uma cópia de tela a mais, e o 2D e o 3D passam a ter dois donos do
   mesmo quadro.
2. O `avaliador_da_timeline.cpp` já existe e já recebe o estado do
   frame. O 3D tem de ser mais um tipo de camada que ele resolve — não
   um segundo avaliador.
3. Um `substituir o backend de desenho` cabe no ABI que já está
   versionado, em vez de exigir uma segunda ponte FFI com as próprias
   regras.

**Diligent Engine** substitui o `backend_vulkan.cpp` (é o renderer:
Vulkan/Metal/D3D12, com PBR, depth e transparência prontos) e **Assimp**
entra como importador (GLB/GLTF, FBX, OBJ). Ambos são C++ e vão para
dentro do hook de build, do mesmo jeito que o `aurea_meshopt` já faz.

## 4. A separação que o pedido descreve — e por que ela é a certa

```
Importar modelo 3D  →  Asset 3D  →  Adicionar na timeline  →  Clip 3D
                                                               ↓
                              transform + material + animação + câmera + luz
                                                               ↓
                                            Preview (o palco do editor)
                                                               ↓
                                                       Render / Export
```

O que muda de verdade em relação a hoje:

- **O 3D sai do estúdio e vira camada.** Hoje a cena é UM objeto
  (`Scene3D`) com nós dentro, e a timeline tem UMA camada que a
  representa. No alvo, cada modelo é uma `Layer3D` com `startTime`,
  `duration` e as próprias propriedades animáveis — exatamente como uma
  forma ou um texto. A timeline passa a controlar posição, rotação,
  escala, visibilidade, material, keyframes, entrada e saída.
- **A câmera e as luzes também são camadas.** Não são campos de uma
  cena: são clips com duração, que entram e saem. Uma câmera que acaba
  em 00:05 devolve a vista à câmera padrão depois disso.
- **O renderer não guarda estado de cena.** Ele monta a cada quadro a
  partir do que o avaliador entrega: quais `Layer3D` estão ativas, com
  que transformação, que material, em que quadro da animação própria, com
  que câmera e que luzes. Sem estado paralelo, não há cena que "ficou
  para trás".
- **Um espaço 3D só.** Múltiplos modelos coexistem e interagem porque
  compartilham a mesma matriz de vista e o mesmo depth buffer.

## 5. Fases, com critério de aceite em cada uma

O critério é sempre o mesmo formato: **o que se mede, e com o quê.**

### Fase 0 — o backend novo atrás da porta (nada antigo sai)
- `aurea_render` ganha o 3D: Diligent compilado para Android (NDK/arm64)
  e o `backend_vulkan.cpp` passa a ter um irmão 3D.
- Assimp compilado junto (GLB/GLTF, FBX, OBJ) e um importador C++ que
  devolve malha + materiais + esqueleto + animações para o
  `gerenciador_de_recursos`.
- Aceite: um cubo e um GLB real desenhados no palco do editor, com
  depth correto (o cubo tapa o que está atrás), medido por
  `lerPixels` na bancada — o mesmo caminho de prova que o 2D já usa.
- **O 3D antigo continua no ar.** Nada é apagado nesta fase.

### Fase 1 — o 3D virou camada
- `Layer3D` no `layer.dart` (start, duration, transform, material,
  visibilidade, animação, esqueleto), com keyframes nas mesmas curvas do
  resto do editor.
- `Camera3D` e `Luz` como tipos de camada.
- `avaliador_da_timeline` resolve as camadas 3D ativas do quadro.
- Editor: os controles de transformação do painel valem para a `Layer3D`
  como valem para uma forma; o palco mostra o resultado.
- Aceite: duas `Layer3D` e uma câmera na timeline, cada uma com o
  próprio keyframe, sobrevivendo a salvar/fechar/reabrir
  (`project_store`), com um teste de ida e volta no JSON.

### Fase 2 — importação de verdade
- `importar 3D` → asset no projeto (caminho + hash + metadados), com
  cache por hash para não reimportar.
- Aceite: GLB, GLTF, FBX e OBJ importados de arquivo real, cada um com
  um teste que confere vértices/materiais/texturas/esqueleto/animações
  lidos, sem depender do desenho.

### Fase 3 — PBR, texturas, skeletal animation, transparência
- Aceite por imagem de referência: o mesmo asset renderizado aqui e por
  um renderizador de referência, comparados pixel a pixel na bancada
  (é o método que já se usa nos efeitos de cor).

### Fase 4 — exportação
- O mesmo avaliador alimenta o `export_engine`; o que se vê é o que sai.
- Aceite: exportar a composição com 3D + 2D e conferir o quadro
  exportado contra o quadro do palco.

### Fase 5 — a demolição
- Só aqui saem `flutter_scene`, `scene3d_gpu.dart`, `domain/scene3d.dart`
  e os dois estúdios, com a prova de que nada os alcança.
- Aceite: `flutter analyze` limpo e a suíte sem nenhum vermelho novo.

## 6. Riscos e bloqueios reais

1. **iOS não se prova nesta máquina.** Não há macOS aqui; o IPA sai pelo
   GitHub Actions. Diligent e Assimp precisam de compilação para arm64
   no CI, e o Podfile/hook tem de aprender os dois. O Android dá para
   provar aqui; o iPhone, só no CI — e "só no CI" já custou caro neste
   projeto antes.
2. **Tamanho.** Diligent + Assimp + os formatos que o Assimp traz são
   dezenas de MB de biblioteca. O split-per-abi e o teto do APK precisam
   ser remedidos, e provavelmente o Assimp entra com os importadores
   ligados um por um (sem os formatos que não se usa).
3. **Ordem destrutiva.** Apagar o 3D atual antes de o novo desenhar
   deixa o app sem 3D e sem como validar. Por isso a Fase 0 mantém o
   antigo no ar e a Fase 5 é a última.
4. **A decisão anterior do dono era a oposta** ("manter o flutter_scene e
   corrigir a ponte", P0 de 16/09). Este plano a reverte; está escrito
   aqui para quem ler depois saber que a reversão foi pedida.

## 7. Como começar (a primeira sessão de código)

1. `packages/aurea_render`: CMake do hook ganha Diligent e Assimp como
   dependências (buscar, compilar, linkar) — **só isso**, com o build do
   Android verde no emulador.
2. Uma cena mínima: cubo com PBR, depth ligado, desenhado numa
   superfície de teste, provada por `lerPixels`.
3. Só depois disso começa a Fase 1.

O tamanho do passo 1 é a razão deste documento: as duas bibliotecas
sozinhas são a sessão inteira.
