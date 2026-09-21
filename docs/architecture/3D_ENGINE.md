# Motor 3D

## Estado: estrutura de dados e comandos, renderização ausente

Os comandos `SceneLoadModel`, `SceneSetCamera`, `SceneAddLight`,
`SceneSetMaterialParam` e companhia existem e devolvem `NotImplemented`. A
`Composition` já guarda os ajustes de cena (sombras, ambiente, pós-processo) e
`Layer` já carrega `Model3DData`, `CameraData` e `LightData`.

O renderer 3D **não existe**.

## Por que não um motor externo

A decisão está tomada e vale registrada: um motor 3D de terceiros seria um
segundo compositor dentro do Aurea, com o seu próprio pipeline de cor, o seu
próprio tratamento de tempo e a sua própria noção de frame.

O resultado previsível: preview e export divergindo, correção de cor aplicada
duas vezes, e um objeto 3D que não respeita o motion blur da timeline.

A cena 3D é um **passe do MESMO FrameGraph** do compositor:

```
Aurea Renderer
├── Video
├── Image
├── Vector
├── Text
├── Particles
└── Scene3D          ← participa do mesmo grafo
```

## Formato

**glTF 2.0 / GLB** é o formato runtime principal. Importação adicional de FBX,
OBJ, USDZ, DAE e STL, convertidos na importação para:

```
AureaSceneAsset
```

### Pipeline de importação

```
modelo de origem
  ↓ parse
  ↓ validação
  ↓ otimização de malha (meshoptimizer)
  ↓ geração de LOD
  ↓ otimização de textura
  ↓ metadados
AureaSceneAsset
```

Para FBX, `ufbx` (permissivo) — não depender do FBX SDK em runtime.

## Material

PBR com, no mínimo:

```
Base Color · Metallic · Roughness · Normal · Ambient Occlusion
Emissive · Opacity · Alpha Mask · Double-sided
```

Avançado, quando presente no asset: clearcoat, sheen, transmission, IOR,
specular, volume, anisotropy.

## Iluminação

Directional, point, spot, ambient, environment (HDRI/IBL).

Um modelo importado precisa parecer **visualmente correto imediatamente** — sem
o usuário ter que caçar a luz certa. Como: ambiente padrão neutro quando a cena
não tem HDRI, e exposição calibrada.

### IBL

```
HDRI
  ↓
Environment
  ↓
Diffuse irradiance
  ↓
Specular prefilter
  ↓
BRDF LUT
```

Gerado uma vez por HDRI e guardado em cache. As LUTs não dependem do HDRI, então
são geradas uma vez na vida do app.

## Sombras

Shadow maps com PCF, e arquitetura para PCSS e cascaded shadow maps.

`ShadowSettings` é POR COMPOSIÇÃO, não por luz: uma cena não precisa de dois
orçamentos de shadow map. O padrão do preview é 1 cascata e 1024 px; o export
usa a configuração final.

## Pós-processamento

SSAO, bloom, tone mapping, depth of field, fog, color grade, vignette.

Todos com **degradação de preview automática**: o mesmo mecanismo dos efeitos —
o preview usa uma configuração mais barata, o export usa a final.

## Animação

Skeleton, skinning, bones, animation clips, morph targets, blend shapes,
animação de câmera.

O usuário escolhe um clipe do modelo e o controla na timeline: `Model3DData`
guarda `animationClip`, `timeScale` e o tempo vem da timeline, como qualquer
outra camada animada.

## Texto 3D

Texto 3D **real**, não uma textura plana no espaço 3D.

Extrusão, bevel, profundidade, material, metallic, roughness, reflexo de
ambiente, iluminação, sombras. Isso significa geometria gerada a partir das
curvas da fonte (FreeType → contorno → extrusão → malha), não um quad.

## Texturas

```
PNG/JPEG/etc
  ↓ pré-processamento no asset
  ↓ KTX2 / Basis Universal
textura comprimida na GPU
```

Mipmaps sempre. Nunca carregar uma textura 4K completa para um objeto que ocupa
100 px na tela — para isso existem os LODs de textura.

## Streaming e LOD

```
metadados
  ↓
LOD baixo
  ↓
materiais básicos
  ↓
texturas de mip baixo
  ↓
LOD alto
  ↓
mips mais altos
```

O objeto aparece rápido e melhora progressivamente. Um modelo grande **não pode**
congelar o app.

`Asset::ModelInfo` guarda `lodTriangleCounts` e `lodCount`. `Model3DData::forcedLod`
permite forçar um LOD (-1 = automático por tamanho na tela).

## Culling

Frustum, backface e occlusion. Arquitetura preparada para culling por GPU com
draw indireto — importante para partículas, vegetação e múltiplos objetos.

`PipelineDesc` e `DrawCall` já têm os campos de draw indireto
(`indirect`, `indirectBuffer`, `drawCount`) porque retrofitar isso depois seria
uma reescrita do grafo.

## Câmera

Camada de câmera com posição, rotação, orientação, FOV, distância focal,
distância de foco, abertura e planos near/far.

`Composition::active_camera()` define qual vale — só uma por vez.

Gizmos X/Y/Z e orbit/pan/dolly na UI.

## Ordem de implementação

1. `MetalBackend`/`VulkanBackend` (bloqueio comum a tudo);
2. pipeline de render 3D básico no FrameGraph: malha, material, luz;
3. importador glTF → `AureaSceneAsset`;
4. câmera e gizmos;
5. IBL e sombras;
6. animação (skinning);
7. LOD e streaming;
8. texto 3D;
9. pós-processamento.
