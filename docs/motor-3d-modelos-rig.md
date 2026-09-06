# Motor 3D: modelos, rig e animação

## Como usar

1. Abra **Cena 3D → Objetos → Importar GLB / glTF / OBJ / FBX**.
2. Selecione um modelo. Para glTF/OBJ, selecione também `.bin`, `.mtl` e
   texturas quando o seletor do iOS não der acesso à pasta inteira.
3. Selecione o objeto e toque em **Animar modelo / Rig**.
4. Escolha um clipe para reproduzir, ajuste a velocidade ou a repetição.
5. Para uma nova animação, escolha um osso/parte, avance o tempo e altere
   rotação ou deslocamento. O ajuste grava uma pose na linha do tempo.
   Os diamantes permitem voltar às poses; há comandos para gravar/apagar.
6. Em **Ambiente**, importe um panorama e ajuste rotação/intensidade.
   A rugosidade do material controla o espalhamento do reflexo.

As poses são offsets locais aditivos ao clipe importado (FK). A primeira
edição depois de 0 s cria uma pose neutra em 0 s. Não há criação automática
de esqueleto, pintura de pesos, IK, retargeting ou edição de topologia.
O modelo precisa trazer os pesos para deformação esquelética. Partes sem
skin podem ser animadas como objetos rígidos.

## Suporte implementado

| Formato/recurso | Estado |
| --- | --- |
| GLB/glTF 2.0 | Cena ativa, hierarquia, TRS/matrizes, múltiplas primitivas/materiais |
| Geometria glTF | Triângulos, strips/fans, UV, normais, buffers intercalados/sparse/normalizados |
| Rig glTF | Inverse bind matrices, até 256 ossos/skin e 8 influências/vértice |
| Animação glTF | Translação, quaternion, escala e morph weights; STEP, LINEAR/SLERP, CUBICSPLINE |
| Materiais glTF | Cor/textura base embutida/externa, UV transform, repetição/espelhamento, fatores metal/rugosidade, unlit |
| OBJ + MTL | Grupos, índices separados/negativos, UV, normais, material difuso e textura; triangulação de faces côncavas |
| FBX 7.x ASCII/binário | **Parcial:** malhas poligonais, hierarquia XYZ, cores difusas, UV/normais e clusters de skin linear |
| Persistência | Geometria, imagens embutidas, rig, clipes e poses dentro do projeto; não dependem do caminho temporário original |
| Panorama | Fonte até 1024×512, cubo 256 px/face, bilinear com bordas compartilhadas e níveis filtrados GGX de 32 amostras |

## Limitações que não devem ser ocultadas

Não é um Blender mobile completo. O renderizador continua baseado em Canvas,
sem depth buffer de GPU, ray tracing ou PBR por pixel. Interseções e transparências
complexas, perspectiva das texturas e reflexos da própria cena permanecem
aproximações. Normais importadas alimentam iluminação interpolada por vértice.
O panorama suporta imagens convencionais; não foi adicionado HDR/EXR linear.

Mapas normal/oclusão/metal-rugosidade/emissivo e cores por vértice não são
aplicados. São emitidos avisos. Draco/Meshopt e extensões obrigatórias não
implementadas são recusadas, com orientação para exportar GLB sem compressão.
Alpha MASK usa a aproximação existente do compositor, não recorte PBR por pixel.

FBX com texturas, pivôs especiais, ordem Euler diferente de XYZ e sistemas de
eixos não cobertos é recusado, em vez de importar silenciosamente distorcido.
Clipes FBX, constraints e blend shapes FBX não são importados; converter para
GLB com animação baked preserva mais recursos. Os rigs FBX suportados podem
receber novas poses/keyframes no app. Herança de escala não uniforme em rigs
FBX complexos ainda requer validação adicional; prefira GLB.

Limites defensivos: 64 MB por arquivo/96 MB agregados, 150 mil triângulos,
4096 nós, profundidade limitada. Modelos acima de 30 mil triângulos recebem
aviso de desempenho. Esses limites não garantem fluidez em todo iPhone.
O cache de panoramas mantém no máximo três cubos; pré-filtragem roda fora
da thread da interface. Texturas são limitadas a 1024 px no maior eixo.

## Verificação

- `model3d_engine_test.dart`: skinning, clipes, poses, interpolação, geometria,
  materiais/UV, OBJ e round-trip do projeto.
- `model_animation_screen_test.dart`: painel real em 375×812 e 430×932,
  scrub e gravação de poses.
- `fbx_import3d_test.dart`: ASCII, cluster/pose, rejeição segura e, quando
  disponível em `build/model3d-fixtures`, FBX binário público Assimp.
- `panorama_filter_test.dart`: continuidade de faces e efeito da rugosidade.
- `model3d_visual_test.dart`: quando disponível localmente, Fox oficial glTF
  com textura, três clipes, importação em isolate e capturas do compositor.
  `AUREA_3D_VISUAL=1` grava PNGs em `build/model3d-fixtures/render`.

Os modelos públicos de verificação não são empacotados no app. Testes de
widget/desktop não substituem validação de desempenho e memória em iPhone.
Esta alteração de código não gera um novo IPA por si só.

Referências de implementação: [skinning glTF — Khronos](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_020_Skins.html),
[interpolação glTF — Khronos](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_007_Animations.html),
[eixos FBX — Autodesk](https://help.autodesk.com/cloudhelp/2020/ENU/FBX-API-Reference/cpp_ref/class_fbx_axis_system.html).
