import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';

import 'package:uuid/uuid.dart';

import 'element3d.dart';
import 'keyframe.dart';
import 'model_asset3d.dart';
import 'panorama3d.dart';

/// CENA 3D (spec AUREA-cena-3d): um CONTEINER que, por fora, e UMA
/// camada do compositor e, por dentro, tem seu proprio renderizador.
///
/// A camada 3D atual (planos no espaco) continua existindo para 2.5D —
/// esta e a outra coisa: malhas, luzes, e ordenacao que resolve
/// INTERPENETRACAO, que o algoritmo do pintor por camada nao resolve.
///
/// Restricao honesta desta implementacao: o Canvas do Flutter nao expoe
/// buffer de profundidade nem instancing de GPU. O equivalente possivel
/// — e que passa o teste que a spec define como aprovacao — e ordenar
/// POR TRIANGULO em vez de por objeto, que resolve interpenetracao, e
/// bater todas as instancias numa unica chamada de desenho.

enum MaterialKind { pbr, unlit, transparent, cutout }

class Material3D {
  const Material3D({
    this.name = 'Material',
    this.baseColor = const Color(0xFFB8C4D0),
    this.metallic = 0.0,
    this.roughness = 0.6,
    this.emissive = 0.0,
    this.opacity = 1.0,
    this.kind = MaterialKind.pbr,
    this.textureLayerId,
    this.reflectivity = 0.0,
    this.imagePath,
    this.faceImagePaths = const {},
    this.normalStrength = 1,
    this.occlusionStrength = 1,
    this.alphaCutoff = 0.5,
    this.doubleSided = false,
    this.packedChannels = false,
    this.textureWrapX = TileMode.clamp,
    this.textureWrapY = TileMode.clamp,
  });

  final String name;
  final Color baseColor;
  final double metallic;
  final double roughness;
  final double emissive;
  final double opacity;
  final MaterialKind kind;

  /// REFLEXO DO AMBIENTE (0..1): quanto da cena ao redor a superficie
  /// devolve. Zero e fosco; um e espelho. Multiplica a forca global da
  /// cena, entao um material espelhado numa cena sem ambiente continua
  /// fosco.
  final double reflectivity;

  /// IMAGEM NA SUPERFICIE: um arquivo vestindo o objeto, projetado por
  /// caixa (cada face recebe a imagem pelo eixo que ela mais encara).
  final String? imagePath;

  /// Textura opcional por indice de face; a imagem geral continua sendo o
  /// fallback. Isso cobre embalagem/tela sem multiplicar objetos.
  final Map<int, String> faceImagePaths;

  final double normalStrength;
  final double occlusionStrength;
  final double alphaCutoff;
  final bool doubleSided;
  final bool packedChannels;
  final TileMode textureWrapX, textureWrapY;

  /// TEXTURA VINDA DE CAMADA DA CENA (§6): uma precomp animada vira a
  /// tela de um celular 3D ou o rotulo de uma embalagem. E o recurso
  /// que mais rende num app de motion.
  final String? textureLayerId;

  bool get isTransparent =>
      kind == MaterialKind.transparent ||
      kind == MaterialKind.cutout ||
      opacity < 0.999;

  Material3D copyWith({
    String? name,
    Color? baseColor,
    double? metallic,
    double? roughness,
    double? emissive,
    double? opacity,
    MaterialKind? kind,
    String? textureLayerId,
    double? reflectivity,
    String? imagePath,
    Map<int, String>? faceImagePaths,
    double? normalStrength,
    double? occlusionStrength,
    double? alphaCutoff,
    bool? doubleSided,
    bool? packedChannels,
    TileMode? textureWrapX,
    TileMode? textureWrapY,
    bool clearImage = false,
    bool clearTextureLayer = false,
  }) => Material3D(
    name: name ?? this.name,
    baseColor: baseColor ?? this.baseColor,
    metallic: metallic ?? this.metallic,
    roughness: roughness ?? this.roughness,
    emissive: emissive ?? this.emissive,
    opacity: opacity ?? this.opacity,
    kind: kind ?? this.kind,
    textureLayerId: clearTextureLayer
        ? null
        : (textureLayerId ?? this.textureLayerId),
    reflectivity: reflectivity ?? this.reflectivity,
    imagePath: clearImage ? null : (imagePath ?? this.imagePath),
    faceImagePaths: faceImagePaths ?? this.faceImagePaths,
    normalStrength: normalStrength ?? this.normalStrength,
    occlusionStrength: occlusionStrength ?? this.occlusionStrength,
    alphaCutoff: alphaCutoff ?? this.alphaCutoff,
    doubleSided: doubleSided ?? this.doubleSided,
    packedChannels: packedChannels ?? this.packedChannels,
    textureWrapX: textureWrapX ?? this.textureWrapX,
    textureWrapY: textureWrapY ?? this.textureWrapY,
  );
}

enum MaterialPreset3D {
  polishedMetal,
  brushedMetal,
  chrome,
  plastic,
  glass,
  frostedGlass,
  ceramic,
  rubber,
  wood,
  mattePaint,
  emissiveNeon,
  unlit,
}

String materialPresetLabel(MaterialPreset3D preset) => switch (preset) {
  MaterialPreset3D.polishedMetal => 'Metal polido',
  MaterialPreset3D.brushedMetal => 'Metal escovado',
  MaterialPreset3D.chrome => 'Cromo',
  MaterialPreset3D.plastic => 'Plastico',
  MaterialPreset3D.glass => 'Vidro',
  MaterialPreset3D.frostedGlass => 'Vidro fosco',
  MaterialPreset3D.ceramic => 'Ceramica',
  MaterialPreset3D.rubber => 'Borracha',
  MaterialPreset3D.wood => 'Madeira',
  MaterialPreset3D.mattePaint => 'Tinta fosca',
  MaterialPreset3D.emissiveNeon => 'Emissivo neon',
  MaterialPreset3D.unlit => 'Sem luz',
};

Material3D materialFromPreset(MaterialPreset3D preset) => switch (preset) {
  MaterialPreset3D.polishedMetal => const Material3D(
    name: 'Metal polido',
    baseColor: Color(0xFFCED5DC),
    metallic: 1,
    roughness: 0.05,
    reflectivity: 1,
  ),
  MaterialPreset3D.brushedMetal => const Material3D(
    name: 'Metal escovado',
    baseColor: Color(0xFFABB3BA),
    metallic: 0.95,
    roughness: 0.34,
    reflectivity: 0.8,
  ),
  MaterialPreset3D.chrome => const Material3D(
    name: 'Cromo',
    baseColor: Color(0xFFF2F4F5),
    metallic: 1,
    roughness: 0,
    reflectivity: 1,
  ),
  MaterialPreset3D.plastic => const Material3D(
    name: 'Plastico',
    baseColor: Color(0xFFEF4B55),
    roughness: 0.28,
    reflectivity: 0.28,
  ),
  MaterialPreset3D.glass => const Material3D(
    name: 'Vidro',
    baseColor: Color(0xFFDDF6FF),
    roughness: 0.04,
    opacity: 0.24,
    reflectivity: 0.9,
    kind: MaterialKind.transparent,
    doubleSided: true,
  ),
  MaterialPreset3D.frostedGlass => const Material3D(
    name: 'Vidro fosco',
    baseColor: Color(0xFFE8F7FA),
    roughness: 0.68,
    opacity: 0.52,
    reflectivity: 0.45,
    kind: MaterialKind.transparent,
    doubleSided: true,
  ),
  MaterialPreset3D.ceramic => const Material3D(
    name: 'Ceramica',
    baseColor: Color(0xFFF4EEE2),
    roughness: 0.18,
    reflectivity: 0.34,
  ),
  MaterialPreset3D.rubber => const Material3D(
    name: 'Borracha',
    baseColor: Color(0xFF25282B),
    roughness: 0.9,
    reflectivity: 0.04,
  ),
  MaterialPreset3D.wood => const Material3D(
    name: 'Madeira',
    baseColor: Color(0xFF9A6138),
    roughness: 0.65,
    reflectivity: 0.08,
  ),
  MaterialPreset3D.mattePaint => const Material3D(
    name: 'Tinta fosca',
    baseColor: Color(0xFF496BC8),
    roughness: 0.82,
    reflectivity: 0.08,
  ),
  MaterialPreset3D.emissiveNeon => const Material3D(
    name: 'Emissivo neon',
    baseColor: Color(0xFF35F4FF),
    roughness: 0.25,
    emissive: 1.4,
  ),
  MaterialPreset3D.unlit => const Material3D(
    name: 'Sem luz',
    baseColor: Color(0xFFFFFFFF),
    kind: MaterialKind.unlit,
  ),
};

enum Light3DKind { directional, point, ambient, spot }

class Light3D {
  Light3D({
    String? id,
    this.kind = Light3DKind.directional,
    this.color = const Color(0xFFFFFFFF),
    AnimatedDouble? intensity,
    this.direction = const Vec3(-0.4, -0.8, -0.45),
    this.position = const Vec3(0, 300, 300),
    this.range = 1200,
    this.castsShadow = false,
    this.coneDegrees = 45,
    this.softness = 0.2,
  }) : id = id ?? const Uuid().v4(),
       intensity = intensity ?? AnimatedDouble(1);

  final String id;
  final Light3DKind kind;
  final Color color;
  final AnimatedDouble intensity;
  final Vec3 direction;
  final Vec3 position;

  /// Alcance: usado no CULLING DE LUZ POR OBJETO — uma malha so recebe
  /// as luzes que a alcancam.
  final double range;
  final bool castsShadow;
  final double coneDegrees;
  final double softness;

  Light3D copyWith({
    Light3DKind? kind,
    Color? color,
    AnimatedDouble? intensity,
    Vec3? direction,
    Vec3? position,
    double? range,
    bool? castsShadow,
    double? coneDegrees,
    double? softness,
  }) => Light3D(
    id: id,
    kind: kind ?? this.kind,
    color: color ?? this.color,
    intensity: intensity ?? this.intensity,
    direction: direction ?? this.direction,
    position: position ?? this.position,
    range: range ?? this.range,
    castsShadow: castsShadow ?? this.castsShadow,
    coneDegrees: coneDegrees ?? this.coneDegrees,
    softness: softness ?? this.softness,
  );
}

/// Vetor 3D minimo (evita puxar vector_math para o modelo).
class Vec3 {
  const Vec3(this.x, this.y, this.z);

  final double x;
  final double y;
  final double z;

  static const zero = Vec3(0, 0, 0);

  Vec3 operator +(Vec3 o) => Vec3(x + o.x, y + o.y, z + o.z);
  Vec3 operator -(Vec3 o) => Vec3(x - o.x, y - o.y, z - o.z);
  Vec3 operator *(double s) => Vec3(x * s, y * s, z * s);

  double dot(Vec3 o) => x * o.x + y * o.y + z * o.z;

  Vec3 cross(Vec3 o) =>
      Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);

  double get length => math.sqrt(x * x + y * y + z * z);

  Vec3 get normalized {
    final l = length;
    return l < 1e-9 ? zero : Vec3(x / l, y / l, z / l);
  }

  @override
  String toString() => 'Vec3($x, $y, $z)';
}

/// Volume envolvente — base do culling hierarquico.
class Bounds3D {
  const Bounds3D(this.center, this.radius);

  final Vec3 center;
  final double radius;

  static const empty = Bounds3D(Vec3.zero, 0);
}

enum MeshLod3D { auto, high, medium, low }

class ModelCredit3D {
  const ModelCredit3D({this.author, this.license, this.url});

  final String? author;
  final String? license;
  final String? url;

  bool get isEmpty => author == null && license == null && url == null;
  String get badge => [
    author,
    license,
    if (author == null && license == null) url,
  ].whereType<String>().join(' · ');
}

/// Metadados suficientes para reabrir um modelo importado sem serializar
/// milhares de vertices no projeto. A persistencia pode reler [path] e
/// escolher os LODs novamente.
class ModelSource3D {
  const ModelSource3D({
    required this.path,
    required this.triangles,
    this.bytes = 0,
    this.meshes = 1,
    this.materials = 0,
    this.textures = 0,
    this.animations = 0,
    this.nodeNames = const [],
    this.animationNames = const [],
    this.overBudget = false,
    this.lodCount = 3,
    this.warning,
  });

  final String path;
  final int triangles;
  final int bytes;
  final int meshes;
  final int materials;
  final int textures;
  final int animations;
  final List<String> nodeNames;
  final List<String> animationNames;
  final bool overBudget;
  final int lodCount;
  final String? warning;
}

/// Um NO do grafo de cena.
class SceneNode {
  SceneNode({
    String? id,
    this.name = 'Objeto',
    this.kind = Element3DKind.cube,
    this.material = const Material3D(),
    AnimatedDouble? x,
    AnimatedDouble? y,
    AnimatedDouble? z,
    AnimatedDouble? rotX,
    AnimatedDouble? rotY,
    AnimatedDouble? rotZ,
    AnimatedDouble? scale,
    this.size = 100,
    this.visible = true,
    this.instances = const [],
    this.mesh,
    this.outline,
    this.extrudeDepth = 40,
    this.parentId,
    this.isNull = false,
    this.locked = false,
    this.colorTag = const Color(0xFF7C62FF),
    this.lod = MeshLod3D.auto,
    this.mediumMesh,
    this.lowMesh,
    this.subdivisions = 0,
    this.credit = const ModelCredit3D(),
    this.modelSource,
    this.animationClip,
    this.modelAsset,
    this.modelMotion = const ModelMotion3D(),
    this.useModelMaterials = true,
  }) : id = id ?? const Uuid().v4(),
       x = x ?? AnimatedDouble(0),
       y = y ?? AnimatedDouble(0),
       z = z ?? AnimatedDouble(0),
       rotX = rotX ?? AnimatedDouble(0),
       rotY = rotY ?? AnimatedDouble(0),
       rotZ = rotZ ?? AnimatedDouble(0),
       scale = scale ?? AnimatedDouble(1);

  final String id;
  final String name;
  final Element3DKind kind;
  final Material3D material;
  final AnimatedDouble x;
  final AnimatedDouble y;
  final AnimatedDouble z;
  final AnimatedDouble rotX;
  final AnimatedDouble rotY;
  final AnimatedDouble rotZ;
  final AnimatedDouble scale;
  final double size;
  final bool visible;

  /// INSTANCIACAO (§4.1): copias da MESMA malha desenhadas numa unica
  /// chamada. Vazio = so o proprio no.
  final List<Vec3> instances;

  /// MALHA PROPRIA (forma extrudada). Quando existe, manda no lugar da
  /// malha do [kind] — e como um logo vira volume sem virar um dos
  /// solidos prontos.
  final Element3DMesh? mesh;

  /// O contorno 2D que gerou a malha, guardado para poder mudar a
  /// espessura depois sem pedir a forma de novo.
  final List<Offset>? outline;

  final double extrudeDepth;

  /// PAI DENTRO DA CENA. Sem isto nao ha rigging la dentro: nao da para
  /// girar um conjunto de objetos junto, nem orbitar a camera interna.
  final String? parentId;

  /// NULO 3D: so transforma, nao desenha. E o pivo dos rigs.
  final bool isNull;
  final bool locked;
  final Color colorTag;
  final MeshLod3D lod;
  final Element3DMesh? mediumMesh;
  final Element3DMesh? lowMesh;
  final int subdivisions;
  final ModelCredit3D credit;
  final ModelSource3D? modelSource;
  final String? animationClip;
  final ModelAsset3D? modelAsset;
  final ModelMotion3D modelMotion;
  final bool useModelMaterials;

  Vec3 positionAt(Duration t) => Vec3(x.valueAt(t), y.valueAt(t), z.valueAt(t));

  SceneNode copyWith({
    String? name,
    Element3DKind? kind,
    Material3D? material,
    AnimatedDouble? x,
    AnimatedDouble? y,
    AnimatedDouble? z,
    AnimatedDouble? rotX,
    AnimatedDouble? rotY,
    AnimatedDouble? rotZ,
    AnimatedDouble? scale,
    double? size,
    bool? visible,
    List<Vec3>? instances,
    Element3DMesh? mesh,
    List<Offset>? outline,
    double? extrudeDepth,
    String? parentId,
    bool clearParent = false,
    bool? isNull,
    bool? locked,
    Color? colorTag,
    MeshLod3D? lod,
    Element3DMesh? mediumMesh,
    Element3DMesh? lowMesh,
    int? subdivisions,
    ModelCredit3D? credit,
    ModelSource3D? modelSource,
    String? animationClip,
    bool clearAnimationClip = false,
    ModelAsset3D? modelAsset,
    ModelMotion3D? modelMotion,
    bool? useModelMaterials,
  }) => SceneNode(
    id: id,
    name: name ?? this.name,
    kind: kind ?? this.kind,
    material: material ?? this.material,
    x: x ?? this.x,
    y: y ?? this.y,
    z: z ?? this.z,
    rotX: rotX ?? this.rotX,
    rotY: rotY ?? this.rotY,
    rotZ: rotZ ?? this.rotZ,
    scale: scale ?? this.scale,
    size: size ?? this.size,
    visible: visible ?? this.visible,
    instances: instances ?? this.instances,
    mesh: mesh ?? this.mesh,
    outline: outline ?? this.outline,
    extrudeDepth: extrudeDepth ?? this.extrudeDepth,
    parentId: clearParent ? null : (parentId ?? this.parentId),
    isNull: isNull ?? this.isNull,
    locked: locked ?? this.locked,
    colorTag: colorTag ?? this.colorTag,
    lod: lod ?? this.lod,
    mediumMesh: mediumMesh ?? this.mediumMesh,
    lowMesh: lowMesh ?? this.lowMesh,
    subdivisions: subdivisions ?? this.subdivisions,
    credit: credit ?? this.credit,
    modelSource: modelSource ?? this.modelSource,
    animationClip: clearAnimationClip
        ? null
        : (animationClip ?? this.animationClip),
    modelAsset: modelAsset ?? this.modelAsset,
    modelMotion: modelMotion ?? this.modelMotion,
    useModelMaterials: useModelMaterials ?? this.useModelMaterials,
  );

  SceneNode duplicate({String? name}) => SceneNode(
    name: name ?? '${this.name} copia',
    kind: kind,
    material: material,
    x: x,
    y: y,
    z: z,
    rotX: rotX,
    rotY: rotY,
    rotZ: rotZ,
    scale: scale,
    size: size,
    visible: visible,
    instances: instances,
    mesh: mesh,
    outline: outline,
    extrudeDepth: extrudeDepth,
    parentId: parentId,
    isNull: isNull,
    locked: locked,
    colorTag: colorTag,
    lod: lod,
    mediumMesh: mediumMesh,
    lowMesh: lowMesh,
    subdivisions: subdivisions,
    credit: credit,
    modelSource: modelSource,
    animationClip: animationClip,
    modelAsset: modelAsset,
    modelMotion: modelMotion,
    useModelMaterials: useModelMaterials,
  );
}

/// CAMERA SALVA (cena §9 / camera §6): guardar um enquadramento e
/// voltar nele com um toque.
class SavedView {
  const SavedView({
    required this.name,
    required this.position,
    required this.target,
  });

  final String name;
  final Vec3 position;
  final Vec3 target;
}

EnvironmentKind environmentForPanorama(PanoramaPreset preset) =>
    switch (preset) {
      PanoramaPreset.estudio => EnvironmentKind.estudio,
      PanoramaPreset.porDoSol => EnvironmentKind.porDoSol,
      PanoramaPreset.noite => EnvironmentKind.noite,
      PanoramaPreset.neon => EnvironmentKind.neon,
      PanoramaPreset.branco => EnvironmentKind.branco,
      PanoramaPreset.interior => EnvironmentKind.interior,
    };

/// A CENA: grafo de nos, luzes e orcamento.
class Scene3D {
  const Scene3D({
    this.nodes = const [],
    this.lights = const [],
    this.savedViews = const [],
    this.ambient = 0.28,
    this.skyColor = const Color(0xFF8FB7E8),
    this.groundColor = const Color(0xFF3A3128),
    this.tonemap = true,
    this.background,
    this.showFloorGrid = true,
    this.msaa = true,
    this.draftMode = false,
    this.cameraParentId,
    this.environment = EnvironmentKind.estudio,
    this.envReflect = 0.7,
    this.panorama = const Panorama3D(),
    this.reflectionProbe = const ReflectionProbe3D(),
    this.planarFloorReflection = false,
    this.planarFloorRoughness = 0.2,
    this.fogDensity = 0,
    this.fogStart = 0,
    this.fogColor = const Color(0xFF101E28),
  });

  final List<SceneNode> nodes;

  /// O AMBIENTE refletido pelos materiais (ver [EnvironmentKind]) e a
  /// forca global do reflexo, que multiplica a de cada material.
  final EnvironmentKind environment;
  final double envReflect;
  final Panorama3D panorama;
  final ReflectionProbe3D reflectionProbe;

  /// Reflexo planar simples do piso; separado da sonda para poder ser
  /// desligado primeiro na degradacao automatica.
  final bool planarFloorReflection;
  final double planarFloorRoughness;

  /// Exponential distance haze, disabled by default for old projects. This
  /// is atmospheric perspective, not volumetric ray-marched lighting.
  final double fogDensity, fogStart;
  final Color fogColor;
  double fogAt(double depth) => fogDensity <= 0
      ? 0
      : (1 - math.exp(-fogDensity * math.max(0, depth - fogStart))).clamp(
          0.0,
          1.0,
        );

  /// De qual NO da cena a camera interna e filha. Nulo = solta.
  ///
  /// E o que permite orbitar a camera de dentro: um nulo girando em Y
  /// com a camera deslocada em Z.
  final String? cameraParentId;

  /// O no de [id], ou null.
  SceneNode? nodeById(String id) {
    for (final n in nodes) {
      if (n.id == id) return n;
    }
    return null;
  }

  final List<Light3D> lights;
  final List<SavedView> savedViews;
  final double ambient;

  /// AMBIENTE POR HEMISFERIO: a cor que vem de cima e a que volta do
  /// chao.
  ///
  /// Ambiente como UM numero acende todas as faces igual, e e por isso
  /// que tudo parecia plastico: no mundo real a face virada para o ceu
  /// recebe luz de ceu e a virada para baixo recebe o que o chao
  /// devolveu. Duas cores e a interpolacao pela normal ja separam metal
  /// de plastico — e custam uma multiplicacao por face.
  final Color skyColor;
  final Color groundColor;

  /// Curva de saida ACES. Sem ela o realce estoura em branco chapado e
  /// a imagem inteira parece renderizada em 1998.
  final bool tonemap;

  final Color? background;
  final bool showFloorGrid;

  /// Em GPU de blocos MSAA e barato; aqui vira suavizacao de borda no
  /// desenho dos triangulos.
  final bool msaa;

  /// MODO RASCUNHO (camera §8): desliga sombra, DOF e ambiente por
  /// imagem SO no preview — a diferenca entre navegar e sofrer.
  final bool draftMode;

  Scene3D copyWith({
    List<SceneNode>? nodes,
    List<Light3D>? lights,
    List<SavedView>? savedViews,
    double? ambient,
    Color? skyColor,
    Color? groundColor,
    bool? tonemap,
    Color? background,
    bool? showFloorGrid,
    bool? msaa,
    bool? draftMode,
    String? cameraParentId,
    bool clearCameraParent = false,
    EnvironmentKind? environment,
    double? envReflect,
    Panorama3D? panorama,
    ReflectionProbe3D? reflectionProbe,
    bool? planarFloorReflection,
    double? planarFloorRoughness,
    double? fogDensity,
    double? fogStart,
    Color? fogColor,
  }) => Scene3D(
    environment: environment ?? this.environment,
    envReflect: envReflect ?? this.envReflect,
    panorama: panorama ?? this.panorama,
    reflectionProbe: reflectionProbe ?? this.reflectionProbe,
    planarFloorReflection: planarFloorReflection ?? this.planarFloorReflection,
    planarFloorRoughness: planarFloorRoughness ?? this.planarFloorRoughness,
    fogDensity: fogDensity ?? this.fogDensity,
    fogStart: fogStart ?? this.fogStart,
    fogColor: fogColor ?? this.fogColor,
    nodes: nodes ?? this.nodes,
    lights: lights ?? this.lights,
    savedViews: savedViews ?? this.savedViews,
    ambient: ambient ?? this.ambient,
    skyColor: skyColor ?? this.skyColor,
    groundColor: groundColor ?? this.groundColor,
    tonemap: tonemap ?? this.tonemap,
    background: background ?? this.background,
    showFloorGrid: showFloorGrid ?? this.showFloorGrid,
    msaa: msaa ?? this.msaa,
    draftMode: draftMode ?? this.draftMode,
    cameraParentId: clearCameraParent
        ? null
        : (cameraParentId ?? this.cameraParentId),
  );

  static Scene3D get demo => Scene3D(
    nodes: [
      SceneNode(
        name: 'Cubo',
        kind: Element3DKind.cube,
        size: 90,
        x: AnimatedDouble(-70),
        material: const Material3D(baseColor: Color(0xFF7C62FF)),
      ),
      SceneNode(
        name: 'Esfera',
        kind: Element3DKind.sphere,
        size: 80,
        x: AnimatedDouble(80),
        material: const Material3D(
          name: 'Metal polido',
          baseColor: Color(0xFFCED5DC),
          metallic: 1,
          roughness: 0.06,
          reflectivity: 1,
        ),
      ),
    ],
    lights: tresPontos,
  );

  /// TRES PONTOS: principal, preenchimento e contraluz.
  ///
  /// Uma luz so achata o objeto — a face iluminada estoura e a oposta
  /// morre no ambiente. Ninguem deveria precisar montar iluminacao para
  /// o primeiro cubo parecer decente, e este e o arranjo que qualquer
  /// estudio usa: a principal desenha a forma, o preenchimento abre a
  /// sombra sem apagar o volume, e a contraluz separa o objeto do fundo.
  static List<Light3D> get tresPontos => [
    // PRINCIPAL: alta, a 45 graus, levemente quente.
    Light3D(
      color: const Color(0xFFFFF4E6),
      direction: const Vec3(-0.5, -0.75, -0.45),
      intensity: AnimatedDouble(1),
      castsShadow: true,
    ),
    // PREENCHIMENTO: do outro lado, fria e fraca — abre a sombra
    // sem competir com a principal.
    Light3D(
      color: const Color(0xFFCFE0FF),
      direction: const Vec3(0.7, -0.25, -0.3),
      intensity: AnimatedDouble(0.35),
    ),
    // CONTRALUZ: de tras e de cima, para o objeto descolar do fundo.
    Light3D(
      color: const Color(0xFFFFFFFF),
      direction: const Vec3(0.15, -0.45, 0.85),
      intensity: AnimatedDouble(0.55),
    ),
  ];
}

// ---------------------------------------------------------- pipeline

/// Um triangulo pronto para desenhar, ja no espaco de tela, com a
/// profundidade que decide a ordem.
class RenderTri {
  const RenderTri({
    required this.a,
    required this.b,
    required this.c,
    required this.depth,
    required this.color,
    required this.transparent,
    this.nodeId = '',
    this.uvA,
    this.uvB,
    this.uvC,
    this.texture,
    this.colorA,
    this.colorB,
    this.colorC,
    this.wrapX = TileMode.clamp,
    this.wrapY = TileMode.clamp,
    this.fogA = 0,
    this.fogB = 0,
    this.fogC = 0,
  });

  final Offset a;
  final Offset b;
  final Offset c;

  /// Coordenadas de imagem (0..1) de cada canto, e o arquivo da imagem.
  /// Nulos = face de cor lisa.
  final Offset? uvA;
  final Offset? uvB;
  final Offset? uvC;
  final String? texture;

  /// Z medio em espaco de camera (maior = mais longe).
  final double depth;
  final Color color;
  final Color? colorA, colorB, colorC;
  final TileMode wrapX, wrapY;
  final double fogA, fogB, fogC;
  final bool transparent;

  /// De qual no da cena este triangulo saiu — e o que permite tocar no
  /// preview e selecionar o objeto certo.
  final String nodeId;
}

/// Resultado de um passe: os triangulos e as METRICAS do orcamento.
typedef SceneFrame = ({
  List<RenderTri> opaque,
  List<RenderTri> transparent,
  int drawCalls,
  int triangles,
  int culled,
});

typedef EnvironmentSample = ({double r, double g, double b});
typedef EnvironmentSampler = EnvironmentSample Function(
  Vec3 direction,
  double roughness,
);

/// Parametros de camera que o renderizador precisa.
class RenderCamera {
  const RenderCamera({
    this.position = const Vec3(0, 0, 800),
    this.target = Vec3.zero,
    this.up = const Vec3(0, 1, 0),
    this.focalLength = 50,
    this.filmWidth = 36,
    this.orthographic = false,
    this.orthoScale = 1,
    this.near = 1,
    this.far = 100000,
  });

  final Vec3 position;
  final Vec3 target;
  final Vec3 up;

  /// Distancia focal em mm e largura do filme em mm — a mesma grandeza
  /// do angulo de visao, so que vista de outro jeito.
  final double focalLength;
  final double filmWidth;

  final bool orthographic;
  final double orthoScale;
  final double near;
  final double far;

  /// ANGULO DE VISAO: 2*atan(filme / (2*focal)). E a mesma coisa que a
  /// distancia focal — mexer num muda o outro.
  double get fovRadians =>
      2 * math.atan(filmWidth / (2 * math.max(1e-6, focalLength)));

  double get fovDegrees => fovRadians * 180 / math.pi;
}

/// Distancia focal a partir do angulo de visao (a volta da conta).
double focalFromFov(double fovDegrees, {double filmWidth = 36}) {
  final rad = fovDegrees * math.pi / 180;
  return filmWidth / (2 * math.tan(rad / 2));
}

/// ZOOM (px) do AE: a distancia em que uma camada do tamanho da
/// composicao preenche o quadro.
double zoomFromFocal(
  double focalLength,
  double compWidth, {
  double filmWidth = 36,
}) => compWidth * focalLength / filmWidth;

double focalFromZoom(double zoom, double compWidth, {double filmWidth = 36}) =>
    zoom * filmWidth / math.max(1e-6, compWidth);

/// Base ortonormal da camera (olhar, direita, cima).
({Vec3 forward, Vec3 right, Vec3 up}) cameraBasis(RenderCamera cam) {
  final forward = (cam.target - cam.position).normalized;
  var right = forward.cross(cam.up).normalized;
  if (right.length < 1e-6) {
    right = forward.cross(const Vec3(0, 0, 1)).normalized;
  }
  final up = right.cross(forward).normalized;
  return (forward: forward, right: right, up: up);
}

/// Rotaciona um ponto local pelos angulos do no (X, depois Y, depois Z
/// — a mesma ordem do resto do app).
Vec3 _rotate(Vec3 v, double rx, double ry, double rz) {
  final cx = math.cos(rx), sx = math.sin(rx);
  final y1 = v.y * cx - v.z * sx;
  final z1 = v.y * sx + v.z * cx;
  final cy = math.cos(ry), sy = math.sin(ry);
  final x2 = v.x * cy + z1 * sy;
  final z2 = -v.x * sy + z1 * cy;
  final cz = math.cos(rz), sz = math.sin(rz);
  return Vec3(x2 * cz - y1 * sz, x2 * sz + y1 * cz, z2);
}

/// RENDERIZA a cena para triangulos de tela.
///
/// Pipeline (§3), na medida do que o Canvas permite:
///   1. cull pelo volume envolvente contra o frustum
///   2. transforma vertices para espaco de camera
///   3. separa OPACOS e TRANSPARENTES
///   4. ordena POR TRIANGULO (nao por objeto) — e o que resolve
///      interpenetracao, que o algoritmo do pintor por camada nao faz
///   5. transparentes depois, do mais distante ao mais proximo, e
///      nunca "escrevem profundidade" (nao entram na ordenacao opaca)
/// Transform EFETIVO de um no, com a cadeia de pais ja resolvida.
class NodeTransform {
  const NodeTransform({
    this.position = Vec3.zero,
    this.rotX = 0,
    this.rotY = 0,
    this.rotZ = 0,
    this.scale = 1,
  });

  /// Em GRAUS, como no resto do aplicativo.
  final Vec3 position;
  final double rotX;
  final double rotY;
  final double rotZ;
  final double scale;

  static const identity = NodeTransform();
}

final Map<(Element3DKind, int), Element3DMesh> _subdivisionCache = {};

Element3DMesh _subdividedPrimitive(Element3DKind kind, int levels) {
  final clamped = levels.clamp(0, 4);
  return _subdivisionCache.putIfAbsent((kind, clamped), () {
    var mesh = element3DMesh(kind);
    for (var level = 0; level < clamped; level++) {
      final verts = <List<double>>[
        for (final vertex in mesh.verts) [...vertex],
      ];
      final faces = <List<int>>[];
      for (final face in mesh.faces) {
        if (face.length < 3) continue;
        var x = 0.0, y = 0.0, z = 0.0;
        for (final index in face) {
          x += mesh.verts[index][0];
          y += mesh.verts[index][1];
          z += mesh.verts[index][2];
        }
        final center = verts.length;
        verts.add([x / face.length, y / face.length, z / face.length]);
        for (var i = 0; i < face.length; i++) {
          faces.add([face[i], face[(i + 1) % face.length], center]);
        }
      }
      mesh = Element3DMesh(verts, faces);
    }
    return mesh;
  });
}

Element3DMesh? _automaticLod(SceneNode node, bool draftMode) {
  final high = node.mesh;
  if (draftMode) return node.lowMesh ?? node.mediumMesh ?? high;
  final triangles = high?.faces.length ?? 0;
  if (triangles > 150000) return node.lowMesh ?? node.mediumMesh ?? high;
  if (triangles > 60000) return node.mediumMesh ?? node.lowMesh ?? high;
  return high;
}

/// Resolve a cadeia de pais de [node].
///
/// A posicao do filho e GIRADA pelo pai antes de somar — e isso que faz
/// o rig de orbita funcionar: um nulo girando em Y com o objeto deslocado
/// em Z faz o objeto dar a volta, em vez de girar no proprio eixo.
///
/// A profundidade e limitada: um ciclo de parentesco (A pai de B, B pai
/// de A) travaria o quadro em vez de desenhar errado.
NodeTransform resolveNodeTransform(
  Scene3D scene,
  SceneNode node,
  Duration t, {
  NodeTransform external = NodeTransform.identity,
  int depth = 0,
}) {
  final local = NodeTransform(
    position: node.positionAt(t),
    rotX: node.rotX.valueAt(t),
    rotY: node.rotY.valueAt(t),
    rotZ: node.rotZ.valueAt(t),
    scale: node.scale.valueAt(t),
  );

  final pid = node.parentId;
  NodeTransform pai;
  if (pid == null || depth >= 16) {
    pai = external;
  } else {
    final parent = scene.nodeById(pid);
    if (parent == null) {
      pai = external;
    } else {
      pai = resolveNodeTransform(
        scene,
        parent,
        t,
        external: external,
        depth: depth + 1,
      );
    }
  }

  return composeTransforms(pai, local);
}

/// Pai depois filho: a posicao do filho gira e escala com o pai.
Vec3 sceneLocalDelta(
  Scene3D scene,
  SceneNode node,
  Duration time,
  Vec3 worldDelta,
) {
  final parent = node.parentId == null ? null : scene.nodeById(node.parentId!);
  if (parent == null) return worldDelta;
  final xf = resolveNodeTransform(scene, parent, time);
  if (xf.scale.abs() < 1e-9) return Vec3.zero;
  final rad = math.pi / 180;
  var delta = _rotate(worldDelta, 0, 0, -xf.rotZ * rad);
  delta = _rotate(delta, 0, -xf.rotY * rad, 0);
  delta = _rotate(delta, -xf.rotX * rad, 0, 0);
  return delta * (1 / xf.scale);
}

/// Pai depois filho: a posicao do filho gira e escala com o pai.
NodeTransform composeTransforms(NodeTransform pai, NodeTransform filho) {
  final escalada = filho.position * pai.scale;
  final girada = _rotate(
    escalada,
    pai.rotX * math.pi / 180,
    pai.rotY * math.pi / 180,
    pai.rotZ * math.pi / 180,
  );
  return NodeTransform(
    position: pai.position + girada,
    rotX: pai.rotX + filho.rotX,
    rotY: pai.rotY + filho.rotY,
    rotZ: pai.rotZ + filho.rotZ,
    scale: pai.scale * filho.scale,
  );
}

/// Aplica um transform de pai a uma camera.
///
/// A camera herda posicao, rotacao e orientacao — mas NAO herda escala.
/// Camera nao tem escala, e herdar do pai e justamente o bug que faz o
/// enquadramento explodir quando alguem escala o nulo.
RenderCamera applyParentToCamera(RenderCamera cam, NodeTransform pai) {
  Vec3 mover(Vec3 p) =>
      pai.position +
      _rotate(
        p,
        pai.rotX * math.pi / 180,
        pai.rotY * math.pi / 180,
        pai.rotZ * math.pi / 180,
      );
  return RenderCamera(
    position: mover(cam.position),
    target: mover(cam.target),
    up: _rotate(
      cam.up,
      pai.rotX * math.pi / 180,
      pai.rotY * math.pi / 180,
      pai.rotZ * math.pi / 180,
    ),
    focalLength: cam.focalLength,
    filmWidth: cam.filmWidth,
    orthographic: cam.orthographic,
    orthoScale: cam.orthoScale,
    near: cam.near,
    far: cam.far,
  );
}

class _RasterVertex {
  const _RasterVertex(this.position, this.uv, this.color);
  final Vec3 position;
  final Offset? uv;
  final Color? color;
  _RasterVertex lerp(_RasterVertex b, double t) => _RasterVertex(
    position + (b.position - position) * t,
    uv == null || b.uv == null ? null : Offset.lerp(uv, b.uv, t),
    color == null || b.color == null ? null : Color.lerp(color, b.color, t),
  );
}

// Sutherland-Hodgman in camera space, before the perspective divide. Preserve
// UVs and vertex lighting on new intersections instead of dropping a wall
// when just one of its vertices passes behind the lens.
List<_RasterVertex> _clipDepth(List<_RasterVertex> input, double z, bool near) {
  double distance(_RasterVertex v) =>
      near ? v.position.z - z : z - v.position.z;
  if (input.every((v) => distance(v) >= 0)) return input;
  final output = <_RasterVertex>[];
  if (input.isEmpty) return output;
  var a = input.last, da = distance(input.last);
  for (final b in input) {
    final db = distance(b);
    if ((da >= 0) != (db >= 0)) output.add(a.lerp(b, da / (da - db)));
    if (db >= 0) output.add(b);
    a = b;
    da = db;
  }
  return output;
}

SceneFrame renderScene(
  Scene3D scene,
  RenderCamera cam,
  Size viewport,
  Duration t, {
  EnvironmentSampler? environmentSampler,
}) {
  final basis = cameraBasis(cam);
  final opaque = <RenderTri>[];
  final transparent = <RenderTri>[];
  var drawCalls = 0;
  var triangles = 0;
  var culled = 0;

  final halfW = viewport.width / 2;
  final halfH = viewport.height / 2;
  // Escala de projecao a partir do angulo de visao.
  final focalPx = halfW / math.tan(cam.fovRadians / 2);

  for (final node in scene.nodes) {
    if (!node.visible) continue;
    // NULO 3D so transforma os filhos; nao desenha nada.
    if (node.isNull) continue;
    final modelFrame = node.modelAsset?.evaluate(t, node.modelMotion);
    // Malha propria (forma extrudada) manda; sem ela, o solido do tipo.
    final selectedMesh =
        switch (node.lod) {
          MeshLod3D.low => node.lowMesh ?? node.mediumMesh ?? node.mesh,
          MeshLod3D.medium => node.mediumMesh ?? node.mesh,
          MeshLod3D.high => node.mesh,
          MeshLod3D.auto => _automaticLod(node, scene.draftMode),
        } ??
        element3DMesh(node.kind);
    final mesh =
        modelFrame?.mesh ??
        (node.mesh == null && node.subdivisions > 0
            ? _subdividedPrimitive(node.kind, node.subdivisions)
            : selectedMesh);
    if (mesh.verts.isEmpty) continue;

    final xf = resolveNodeTransform(scene, node, t);
    final s = node.size * xf.scale;
    final rx = xf.rotX * math.pi / 180;
    final ry = xf.rotY * math.pi / 180;
    final rz = xf.rotZ * math.pi / 180;
    final boundRadius = modelFrame == null
        ? 1.9
        : mesh.verts.fold<double>(
            0,
            (r, v) =>
                math.max(r, math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])),
          );

    // Uma "chamada de desenho" por NO — as instancias entram na mesma,
    // que e o equivalente possivel de instanciacao aqui.
    final offsets = node.instances.isEmpty ? const [Vec3.zero] : node.instances;
    var nodeEmitted = false;

    for (final inst in offsets) {
      final origin = composeTransforms(
        xf,
        NodeTransform(position: inst),
      ).position;

      // CULLING pelo volume envolvente: fora do frustum, descarta o
      // objeto inteiro com um teste so.
      final toCam = origin - cam.position;
      final zCam = toCam.dot(basis.forward);
      final radius = s.abs() * boundRadius;
      if (zCam + radius < cam.near || zCam - radius > cam.far) {
        culled++;
        continue;
      }
      if (zCam > 0) {
        final xCam = toCam.dot(basis.right).abs();
        final yCam = toCam.dot(basis.up).abs();
        final limitX = zCam * halfW / focalPx + radius;
        final limitY = zCam * halfH / focalPx + radius;
        if (xCam > limitX * 1.4 || yCam > limitY * 1.4) {
          culled++;
          continue;
        }
      }

      // Transforma os vertices uma vez por instancia.
      final n = mesh.verts.length;
      final cx = Float64List(n);
      final cy = Float64List(n);
      final cz = Float64List(n);
      final wx = Float64List(n);
      final wy = Float64List(n);
      final wz = Float64List(n);
      for (var i = 0; i < n; i++) {
        final v = mesh.verts[i];
        final r = _rotate(Vec3(v[0] * s, v[1] * s, v[2] * s), rx, ry, rz);
        final world = origin + r;
        wx[i] = world.x;
        wy[i] = world.y;
        wz[i] = world.z;
        final rel = world - cam.position;
        cx[i] = rel.dot(basis.right);
        cy[i] = rel.dot(basis.up);
        cz[i] = rel.dot(basis.forward);
      }

      // IMAGEM NA SUPERFICIE: a luz vira fator (base branca) e a imagem
      // entra multiplicada no desenho. As coordenadas vem por PROJECAO
      // DE CAIXA nas coordenadas locais da malha: cada face recebe a
      // imagem pelo eixo que ela mais encara.
      final temTextura =
          node.material.imagePath != null ||
          node.material.faceImagePaths.isNotEmpty;
      var lminX = double.infinity,
          lminY = double.infinity,
          lminZ = double.infinity;
      var lmaxX = -double.infinity,
          lmaxY = -double.infinity,
          lmaxZ = -double.infinity;
      if (temTextura) {
        for (final v in mesh.verts) {
          if (v[0] < lminX) lminX = v[0];
          if (v[0] > lmaxX) lmaxX = v[0];
          if (v[1] < lminY) lminY = v[1];
          if (v[1] > lmaxY) lmaxY = v[1];
          if (v[2] < lminZ) lminZ = v[2];
          if (v[2] > lmaxZ) lmaxZ = v[2];
        }
      }
      double faixa(double a, double lo, double hi) =>
          hi - lo < 1e-9 ? 0.5 : ((a - lo) / (hi - lo)).clamp(0.0, 1.0);

      for (var faceIndex = 0; faceIndex < mesh.faces.length; faceIndex++) {
        final face = mesh.faces[faceIndex];
        final material = modelFrame != null && node.useModelMaterials
            ? modelFrame.materials[faceIndex]
            : node.material;
        final isTransparent = material.isTransparent;
        final textura =
            material.faceImagePaths[faceIndex] ?? material.imagePath;
        final matLuz = textura == null || modelFrame != null
            ? material
            : material.copyWith(baseColor: const Color(0xFFFFFFFF));
        // Normal em espaco de MUNDO (Newell), para a iluminacao — e a
        // normal LOCAL, para escolher o eixo da projecao da imagem.
        var nx = 0.0, ny = 0.0, nz = 0.0;
        var lnx = 0.0, lny = 0.0, lnz = 0.0;
        var fcx = 0.0, fcy = 0.0, fcz = 0.0;
        for (var i = 0; i < face.length; i++) {
          final a = face[i];
          final b = face[(i + 1) % face.length];
          nx += (wy[a] - wy[b]) * (wz[a] + wz[b]);
          ny += (wz[a] - wz[b]) * (wx[a] + wx[b]);
          nz += (wx[a] - wx[b]) * (wy[a] + wy[b]);
          fcx += wx[a];
          fcy += wy[a];
          fcz += wz[a];
          if (textura != null) {
            final va = mesh.verts[a], vb = mesh.verts[b];
            lnx += (va[1] - vb[1]) * (va[2] + vb[2]);
            lny += (va[2] - vb[2]) * (va[0] + vb[0]);
            lnz += (va[0] - vb[0]) * (va[1] + vb[1]);
          }
        }
        Offset? uvDe(int i) {
          if (textura == null) return null;
          if (modelFrame != null) return modelFrame.uvs[i];
          final v = mesh.verts[i];
          final ax = lnx.abs(), ay = lny.abs(), az = lnz.abs();
          if (ax >= ay && ax >= az) {
            return Offset(faixa(v[2], lminZ, lmaxZ), faixa(v[1], lminY, lmaxY));
          }
          if (ay >= ax && ay >= az) {
            return Offset(faixa(v[0], lminX, lmaxX), faixa(v[2], lminZ, lmaxZ));
          }
          return Offset(faixa(v[0], lminX, lmaxX), faixa(v[1], lminY, lmaxY));
        }

        final inv = 1.0 / face.length;
        final faceCenter = Vec3(fcx * inv, fcy * inv, fcz * inv);

        // NORMAL PARA FORA, independente do sentido em que a face foi
        // escrita na malha. Sem isto, uma face com sentido invertido
        // recebe luz pelo lado errado — e escapa do descarte de costas,
        // que e onde mora metade do custo.
        var normal = Vec3(nx, ny, nz).normalized;
        final outward = faceCenter - origin;
        if (modelFrame == null && normal.dot(outward) < 0) {
          normal = Vec3(-normal.x, -normal.y, -normal.z);
        }

        // DESCARTE DE COSTAS: num solido fechado, a face virada para o
        // outro lado esta sempre escondida por outra. Deixar de emitir
        // corta perto da metade dos triangulos — some do emit, da
        // ordenacao e do desenho de uma vez.
        //
        // Material transparente NAO entra: ali se ve o fundo por dentro.
        if (!isTransparent && !material.doubleSided) {
          final toFace = faceCenter - cam.position;
          if (normal.dot(toFace) >= 0) continue;
        }

        final color = shadeFace(
          scene: scene,
          material: matLuz,
          normal: normal,
          point: faceCenter,
          t: t,
          viewDir: (cam.position - faceCenter).normalized,
          nodeId: node.id,
          environmentSampler: environmentSampler,
        );
        Color? vertexColor(int index) {
          final n = modelFrame?.normals[index];
          if (n == null) return null;
          var direction = _rotate(n, rx, ry, rz).normalized;
          final point = Vec3(wx[index], wy[index], wz[index]);
          final view = (cam.position - point).normalized;
          if (material.doubleSided && direction.dot(view) < 0) {
            direction = direction * -1;
          }
          return shadeFace(
            scene: scene,
            material: matLuz,
            normal: direction,
            point: point,
            t: t,
            viewDir: view,
            nodeId: node.id,
            environmentSampler: environmentSampler,
          );
        }

        // Leque de triangulos: o poligono vira triangulos, e cada um
        // entra na ordenacao com a SUA profundidade.
        for (var i = 1; i < face.length - 1; i++) {
          final ia = face[0], ib = face[i], ic = face[i + 1];
          final near = math.max(1e-4, cam.near);
          if ([ia, ib, ic].every((v) => cz[v] < near) ||
              [ia, ib, ic].every((v) => cz[v] > cam.far)) {
            continue;
          }
          var polygon = [
            for (final v in [ia, ib, ic])
              _RasterVertex(Vec3(cx[v], cy[v], cz[v]), uvDe(v), vertexColor(v)),
          ];
          polygon = _clipDepth(_clipDepth(polygon, near, true), cam.far, false);
          Offset project(_RasterVertex v) {
            final k = cam.orthographic
                ? cam.orthoScale
                : focalPx / v.position.z;
            return Offset(halfW + v.position.x * k, halfH - v.position.y * k);
          }

          Color? fogged(_RasterVertex v) {
            if (textura != null || scene.fogDensity <= 0) return v.color;
            final base = v.color ?? color;
            return Color.lerp(
              base,
              scene.fogColor,
              scene.fogAt(v.position.z),
            )!.withValues(alpha: base.a);
          }

          for (var p = 1; p < polygon.length - 1; p++) {
            final va = polygon[0], vb = polygon[p], vc = polygon[p + 1];
            final pa = project(va), pb = project(vb), pc = project(vc);

            // FORA DA TELA: um triangulo inteiramente para la da borda nao
            // pinta nada, mas pagaria ordenacao e chamada de desenho.
            final minX = pa.dx < pb.dx
                ? (pa.dx < pc.dx ? pa.dx : pc.dx)
                : (pb.dx < pc.dx ? pb.dx : pc.dx);
            if (minX > viewport.width) continue;
            final maxX = pa.dx > pb.dx
                ? (pa.dx > pc.dx ? pa.dx : pc.dx)
                : (pb.dx > pc.dx ? pb.dx : pc.dx);
            if (maxX < 0) continue;
            final minY = pa.dy < pb.dy
                ? (pa.dy < pc.dy ? pa.dy : pc.dy)
                : (pb.dy < pc.dy ? pb.dy : pc.dy);
            if (minY > viewport.height) continue;
            final maxY = pa.dy > pb.dy
                ? (pa.dy > pc.dy ? pa.dy : pc.dy)
                : (pb.dy > pc.dy ? pb.dy : pc.dy);
            if (maxY < 0) continue;

            final tri = RenderTri(
              a: pa,
              b: pb,
              c: pc,
              depth: (va.position.z + vb.position.z + vc.position.z) / 3,
              color: color,
              colorA: fogged(va),
              colorB: fogged(vb),
              colorC: fogged(vc),
              fogA: scene.fogAt(va.position.z),
              fogB: scene.fogAt(vb.position.z),
              fogC: scene.fogAt(vc.position.z),
              transparent: isTransparent,
              nodeId: node.id,
              uvA: va.uv,
              uvB: vb.uv,
              uvC: vc.uv,
              texture: textura,
              wrapX: material.textureWrapX,
              wrapY: material.textureWrapY,
            );
            if (isTransparent) {
              transparent.add(tri);
            } else {
              opaque.add(tri);
            }
            triangles++;
            nodeEmitted = true;
          }
        }
      }
    }
    if (nodeEmitted) drawCalls++;
  }

  // Sem Z-buffer, a ordem correta e a do pintor: do mais distante ao
  // mais proximo, POR TRIANGULO. E a ordenacao por triangulo (nao por
  // objeto) que resolve interpenetracao.
  //
  // Ordenar por comparacao custa n log n com uma chamada de funcao por
  // comparacao — com dezenas de milhares de triangulos vira o gargalo.
  // [depthSort] faz numa passada por balde.
  depthSort(opaque);
  depthSort(transparent);

  return (
    opaque: opaque,
    transparent: transparent,
    drawCalls: drawCalls,
    triangles: triangles,
    culled: culled,
  );
}

/// ORDENACAO POR BALDE, do mais distante ao mais proximo.
///
/// Ordenar por comparacao custa n log n e uma chamada de funcao por
/// comparacao — em Dart isso pesa. Aqui a profundidade e um numero num
/// intervalo conhecido, entao da para jogar cada triangulo direto no
/// balde dele em uma passada e ordenar apenas as colisoes dentro de cada um.
///
/// A resolucao acompanha a quantidade de triangulos, mas a extensao de
/// profundidade tambem importa. Mesmo um balde pequeno pode conter duas
/// superficies visivelmente diferentes: a ordenacao interna e exata.
void depthSort(List<RenderTri> tris) {
  final n = tris.length;
  if (n < 64) {
    tris.sort((a, b) => b.depth.compareTo(a.depth));
    return;
  }

  var lo = double.infinity, hi = -double.infinity;
  for (var i = 0; i < n; i++) {
    final d = tris[i].depth;
    if (d < lo) lo = d;
    if (d > hi) hi = d;
  }
  final span = hi - lo;
  if (!span.isFinite || span <= 1e-9) return;

  final buckets = (n * 4).clamp(256, 65536);
  final scale = (buckets - 1) / span;

  // Contagem por balde, deslocamento, distribuicao — o "counting sort",
  // que torna a distribuicao linear; colisoes recebem comparacao abaixo.
  final count = Int32List(buckets);
  final slot = Int32List(n);
  for (var i = 0; i < n; i++) {
    // Invertido: o balde 0 recebe o MAIS DISTANTE.
    final b = buckets - 1 - ((tris[i].depth - lo) * scale).floor();
    final bb = b < 0 ? 0 : (b >= buckets ? buckets - 1 : b);
    slot[i] = bb;
    count[bb]++;
  }
  var running = 0;
  for (var b = 0; b < buckets; b++) {
    final c = count[b];
    count[b] = running;
    running += c;
  }
  final out = List<RenderTri>.filled(n, tris[0]);
  for (var i = 0; i < n; i++) {
    out[count[slot[i]]++] = tris[i];
  }
  // Bucket width grows with the depth span. A deep environment must not
  // scramble nearby character surfaces that happen to share a bucket.
  var start = 0;
  for (final end in count) {
    if (end - start > 1) {
      final group = out.sublist(start, end)
        ..sort((a, b) => b.depth.compareTo(a.depth));
      out.setRange(start, end, group);
    }
    start = end;
  }
  tris.setAll(0, out);
}

/// ILUMINACAO DIRETA com poucas luzes (§5) — nada de diferida, que
/// consome banda, e banda e o gargalo. Inclui CULLING DE LUZ POR
/// OBJETO: a luz so entra na conta se alcanca o ponto.
Color shadeFace({
  required Scene3D scene,
  required Material3D material,
  required Vec3 normal,
  required Vec3 point,
  required Duration t,
  Vec3? viewDir,
  String? nodeId,
  EnvironmentSampler? environmentSampler,
}) {
  if (material.kind == MaterialKind.unlit) {
    return material.baseColor.withValues(
      alpha: material.baseColor.a * material.opacity.clamp(0.0, 1.0),
    );
  }

  var r = 0.0, g = 0.0, b = 0.0;
  final baseR = material.baseColor.r;
  final baseG = material.baseColor.g;
  final baseB = material.baseColor.b;

  // AMBIENTE POR HEMISFERIO, no lugar de um numero so.
  //
  // A face virada para cima pega a cor do ceu; a virada para baixo, o
  // que o chao devolveu. E a versao barata do ambiente por imagem, e e
  // ela que faz uma esfera lisa deixar de parecer um adesivo: o topo e
  // frio, a base e quente, e o olho le isso como volume antes de
  // qualquer luz direta chegar.
  final paraCima = ((normal.y + 1) / 2).clamp(0.0, 1.0);
  var ambR =
      scene.groundColor.r + (scene.skyColor.r - scene.groundColor.r) * paraCima;
  var ambG =
      scene.groundColor.g + (scene.skyColor.g - scene.groundColor.g) * paraCima;
  var ambB =
      scene.groundColor.b + (scene.skyColor.b - scene.groundColor.b) * paraCima;
  final envRotation = scene.panorama.rotationDegrees * math.pi / 180;
  final litNormal = Vec3(
    normal.x * math.cos(envRotation) - normal.z * math.sin(envRotation),
    normal.y,
    normal.x * math.sin(envRotation) + normal.z * math.cos(envRotation),
  );
  double envR, envG, envB;
  if (environmentSampler != null && scene.panorama.hasImage) {
    final sample = environmentSampler(litNormal, 1);
    envR = sample.r;
    envG = sample.g;
    envB = sample.b;
  } else {
    (envR, envG, envB) = environmentColor(
      scene.environment,
      litNormal.x,
      litNormal.y,
      litNormal.z,
    );
  }
  final directionalEnvironment =
      scene.panorama.rotationDegrees != 0 ||
      scene.panorama.source != PanoramaSource.preset ||
      scene.panorama.preset != PanoramaPreset.estudio;
  if (directionalEnvironment) {
    ambR = (ambR + envR) * 0.5;
    ambG = (ambG + envG) * 0.5;
    ambB = (ambB + envB) * 0.5;
  }
  final panoramaStrength = scene.panorama.intensity.clamp(0.0, 4.0).toDouble();
  final occlusion = material.occlusionStrength.clamp(0.0, 1.0).toDouble();
  r += baseR * ambR * scene.ambient * panoramaStrength * occlusion;
  g += baseG * ambG * scene.ambient * panoramaStrength * occlusion;
  b += baseB * ambB * scene.ambient * panoramaStrength * occlusion;

  for (final light in scene.lights) {
    final intensity = light.intensity.valueAt(t);
    if (intensity <= 0) continue;
    Vec3 dir;
    var atten = 1.0;
    switch (light.kind) {
      case Light3DKind.ambient:
        r += baseR * light.color.r * intensity;
        g += baseG * light.color.g * intensity;
        b += baseB * light.color.b * intensity;
        continue;
      case Light3DKind.directional:
        dir = (light.direction * -1).normalized;
      case Light3DKind.point:
        final delta = light.position - point;
        final d = delta.length;
        // CULLING DE LUZ POR OBJETO: fora do alcance, nem entra.
        if (d > light.range) continue;
        atten = 1 - (d / light.range);
        atten *= atten;
        dir = delta.normalized;
      case Light3DKind.spot:
        final spotDelta = light.position - point;
        final spotDistance = spotDelta.length;
        if (spotDistance > light.range) continue;
        final toPoint = (point - light.position).normalized;
        final axis = light.direction.normalized;
        final angle = math.acos(axis.dot(toPoint).clamp(-1.0, 1.0));
        final outer =
            light.coneDegrees.clamp(1.0, 179.0).toDouble() * math.pi / 360;
        if (angle >= outer) continue;
        final soft = light.softness.clamp(0.0, 1.0).toDouble();
        final inner = outer * (1 - soft * 0.9);
        final cone = angle <= inner
            ? 1.0
            : (1 - (angle - inner) / math.max(1e-6, outer - inner)).clamp(
                0.0,
                1.0,
              );
        atten = 1 - (spotDistance / light.range);
        atten = atten * atten * cone;
        dir = spotDelta.normalized;
    }
    final lambert = math.max(0.0, normal.dot(dir));
    if (lambert <= 0) continue;
    final k = lambert * intensity * atten;
    r += baseR * light.color.r * k;
    g += baseG * light.color.g * k;
    b += baseB * light.color.b * k;

    // Especular simples (rugosidade menor = realce mais concentrado).
    final shininess = (1 - material.roughness).clamp(0.0, 1.0);
    if (shininess > 0.05) {
      final spec =
          math.pow(lambert, 8 + shininess * 60).toDouble() *
          shininess *
          intensity *
          atten;
      final metalTint = material.metallic;
      r += (baseR * metalTint + (1 - metalTint)) * spec;
      g += (baseG * metalTint + (1 - metalTint)) * spec;
      b += (baseB * metalTint + (1 - metalTint)) * spec;
    }
  }

  // REFLEXO DO AMBIENTE. A superficie devolve o que ha ao redor na
  // direcao espelhada da vista; Fresnel faz a borda refletir mais que o
  // centro (e o que se ve numa bola de metal: o meio mostra a cor, a
  // borda mostra a sala). Rugosidade embaca o reflexo puxando-o para a
  // media do hemisferio, e metal tinge o reflexo com a propria cor.
  final refl = material.reflectivity * scene.envReflect;
  if (viewDir != null && refl > 0.001) {
    final nv = normal.dot(viewDir).clamp(0.0, 1.0);
    final rv = normal * (2 * nv) - viewDir;
    Vec3? sol;
    var solR = 1.0, solG = 1.0, solB = 1.0, solI = 0.0;
    for (final light in scene.lights) {
      if (light.kind != Light3DKind.directional) continue;
      final i = light.intensity.valueAt(t);
      if (i <= solI) continue;
      solI = i;
      sol = (light.direction * -1).normalized;
      solR = light.color.r;
      solG = light.color.g;
      solB = light.color.b;
    }
    final rough = material.roughness.clamp(0.0, 1.0).toDouble();
    final rot = envRotation;
    final rotated = Vec3(
      rv.x * math.cos(rot) - rv.z * math.sin(rot),
      rv.y,
      rv.x * math.sin(rot) + rv.z * math.cos(rot),
    );
    double er, eg, eb;
    if (environmentSampler != null && scene.panorama.hasImage) {
      final sample = environmentSampler(rotated, rough);
      er = sample.r;
      eg = sample.g;
      eb = sample.b;
    } else {
      (er, eg, eb) = environmentColor(
        scene.environment,
        rotated.x,
        rotated.y,
        rotated.z,
        sunX: sol?.x ?? 0,
        sunY: sol?.y ?? 0,
        sunZ: sol?.z ?? 0,
        sunR: solR,
        sunG: solG,
        sunB: solB,
        sunSharp: 8 + (1 - rough) * (1 - rough) * 300,
        sunGain: sol == null ? 0 : solI * (1 - rough * 0.7),
      );
    }
    // Embacar: mistura com a media do hemisferio na altura do reflexo.
    if (rough > 0.01 &&
        !(environmentSampler != null && scene.panorama.hasImage)) {
      final (mr, mg, mb) = environmentColor(
        scene.environment,
        math.sin(rot),
        rv.y,
        math.cos(rot),
      );
      final k = rough * 0.85;
      er += (mr - er) * k;
      eg += (mg - eg) * k;
      eb += (mb - eb) * k;
    }
    final boost = scene.panorama.highlightBoost.clamp(0.0, 2.0).toDouble();
    if (boost > 0) {
      final peak = math.max(er, math.max(eg, eb));
      final lift = math.max(0.0, peak - 0.58) * boost;
      er += er * lift;
      eg += eg * lift;
      eb += eb * lift;
    }
    er *= panoramaStrength;
    eg *= panoramaStrength;
    eb *= panoramaStrength;

    // No backend Canvas nao existe cubemap GPU. A sonda abaixo e a
    // aproximacao analitica explicita: amostra os outros volumes na direcao
    // refletida, nunca o proprio no, sem sombra/reflexo/pos. O contrato de
    // captura em seis faces continua em [ReflectionProbeScheduler] para a
    // futura migracao ao Flutter GPU.
    final probe = _sampleSceneProbe(
      scene,
      reflectiveNodeId: nodeId,
      direction: rv,
      t: t,
      roughness: rough,
    );
    if (probe != null) {
      er += (probe.r - er) * probe.weight;
      eg += (probe.g - eg) * probe.weight;
      eb += (probe.b - eb) * probe.weight;
    }
    final fresnel = 0.04 + 0.96 * math.pow(1 - nv, 5).toDouble();
    final metal = material.metallic.clamp(0.0, 1.0).toDouble();
    final amount =
        (refl * (metal * 0.9 + (1 - metal) * (0.25 + 0.75 * fresnel))).clamp(
          0.0,
          1.0,
        );
    final tintR = 1 - metal + metal * baseR;
    final tintG = 1 - metal + metal * baseG;
    final tintB = 1 - metal + metal * baseB;
    r = r * (1 - amount) + er * tintR * amount;
    g = g * (1 - amount) + eg * tintG * amount;
    b = b * (1 - amount) + eb * tintB * amount;
  }

  if (material.emissive > 0) {
    r += baseR * material.emissive;
    g += baseG * material.emissive;
    b += baseB * material.emissive;
  }

  if (scene.tonemap) {
    r = acesFilmic(r);
    g = acesFilmic(g);
    b = acesFilmic(b);
  }

  return Color.from(
    alpha: material.opacity.clamp(0.0, 1.0),
    red: r.clamp(0.0, 1.0),
    green: g.clamp(0.0, 1.0),
    blue: b.clamp(0.0, 1.0),
  );
}

({double r, double g, double b, double weight})? _sampleSceneProbe(
  Scene3D scene, {
  required String? reflectiveNodeId,
  required Vec3 direction,
  required Duration t,
  required double roughness,
}) {
  final probe = scene.reflectionProbe;
  if (!probe.enabled || scene.draftMode) return null;

  Vec3 origin = Vec3(probe.position.x, probe.position.y, probe.position.z);
  if (probe.perObject && reflectiveNodeId != null) {
    final own = scene.nodeById(reflectiveNodeId);
    if (own != null) origin = resolveNodeTransform(scene, own, t).position;
  }

  var sum = 0.0, r = 0.0, g = 0.0, b = 0.0;
  final mip = roughnessMip(roughness);
  final sharpness = 42 / (1 + mip * 0.9);
  for (final node in scene.nodes) {
    if (!node.visible ||
        node.isNull ||
        !probe.includes(node.id, reflectiveNodeId: reflectiveNodeId)) {
      continue;
    }
    final xf = resolveNodeTransform(scene, node, t);
    final delta = xf.position - origin;
    final distance = delta.length;
    if (distance < 1e-6) continue;
    final alignment = direction.normalized.dot(delta * (1 / distance));
    if (alignment <= 0) continue;
    final angular = math.atan2(node.size * xf.scale, distance);
    final footprint = math.sin(angular.clamp(0.02, math.pi / 2));
    final directional = math.pow(alignment, sharpness).toDouble();
    final weight = (directional * (0.25 + footprint * 2.2))
        .clamp(0.0, 1.0)
        .toDouble();
    if (weight <= 0.001) continue;
    r += node.material.baseColor.r * weight;
    g += node.material.baseColor.g * weight;
    b += node.material.baseColor.b * weight;
    sum += weight;
  }
  if (sum <= 0) return null;
  final blend = (sum / (1 + sum) * (1 - roughness * 0.35))
      .clamp(0.0, 0.92)
      .toDouble();
  return (r: r / sum, g: g / sum, b: b / sum, weight: blend);
}

/// CURVA DE SAIDA ACES (aproximacao de Narkowicz).
///
/// Sem curva, tudo acima de 1 vira o mesmo branco: dois realces de
/// brilhos muito diferentes saem identicos e chapados, e e isso que da
/// o aspecto de render antigo. A curva comprime o alto em vez de
/// cortar, entao a diferenca continua visivel.
double acesFilmic(double x) {
  if (x <= 0) return 0;
  const a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return ((x * (a * x + b)) / (x * (c * x + d) + e)).clamp(0.0, 1.0);
}

/// PROFUNDIDADE EXPORTADA (§8): a profundidade de cada triangulo,
/// normalizada em 0..1 (0 = perto). E o que permite ao compositor pôr
/// uma camada 2D ENTRE dois objetos 3D, com oclusao correta.
typedef DepthSample = ({double near, double far});

DepthSample sceneDepthRange(SceneFrame frame) {
  var near = double.infinity;
  var far = 0.0;
  for (final tri in [...frame.opaque, ...frame.transparent]) {
    if (tri.depth < near) near = tri.depth;
    if (tri.depth > far) far = tri.depth;
  }
  if (near == double.infinity) return (near: 0, far: 1);
  return (near: near, far: far <= near ? near + 1 : far);
}

/// Profundidade da cena no ponto de tela [p] (a do triangulo mais
/// PROXIMO que cobre o ponto), ou null se nada cobre. E a consulta que
/// decide se uma camada 2D fica na frente ou atras.
double? depthAtPoint(SceneFrame frame, Offset p) {
  double? best;
  for (final tri in [...frame.opaque, ...frame.transparent]) {
    if (_pointInTriangle(p, tri.a, tri.b, tri.c)) {
      if (best == null || tri.depth < best) best = tri.depth;
    }
  }
  return best;
}

/// Qual OBJETO esta sob o dedo: o no do triangulo mais proximo que
/// cobre o ponto. Sem isso, tocar na cena selecionaria a camada inteira
/// em vez do solido tocado.
String? pickNodeAt(SceneFrame frame, Offset p) {
  String? best;
  var bestDepth = double.infinity;
  for (final tri in [...frame.opaque, ...frame.transparent]) {
    if (tri.depth < bestDepth && _pointInTriangle(p, tri.a, tri.b, tri.c)) {
      bestDepth = tri.depth;
      best = tri.nodeId;
    }
  }
  return best;
}

bool _pointInTriangle(Offset p, Offset a, Offset b, Offset c) {
  double sign(Offset p1, Offset p2, Offset p3) =>
      (p1.dx - p3.dx) * (p2.dy - p3.dy) - (p2.dx - p3.dx) * (p1.dy - p3.dy);
  final d1 = sign(p, a, b);
  final d2 = sign(p, b, c);
  final d3 = sign(p, c, a);
  final hasNeg = d1 < 0 || d2 < 0 || d3 < 0;
  final hasPos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(hasNeg && hasPos);
}

/// ORCAMENTO (§10) e DEGRADACAO automatica.
typedef SceneBudget = ({
  int maxDrawCalls,
  int maxTriangles,
  int maxLights,
  int maxShadowLights,
  double maxMemoryMb,
});

const lowProfileBudget = (
  maxDrawCalls: 80,
  maxTriangles: 150000,
  maxLights: 4,
  maxShadowLights: 1,
  maxMemoryMb: 192,
);

double estimateSceneMemoryMb(Scene3D scene) {
  var bytes = 0.0;
  final textures = <String>{};
  for (final node in scene.nodes) {
    final meshes = <Element3DMesh?>[node.mesh, node.mediumMesh, node.lowMesh];
    bytes += node.modelAsset?.estimatedBytes ?? 0;
    for (final material in node.modelAsset?.data['materials'] as List? ?? []) {
      if (material['image'] != null) textures.add(material['image'] as String);
    }
    for (final mesh in meshes) {
      if (mesh == null) continue;
      bytes += mesh.verts.length * 3 * 8;
      bytes += mesh.faces.fold<int>(0, (sum, face) => sum + face.length) * 4;
    }
    if (node.material.imagePath != null) {
      textures.add(node.material.imagePath!);
    }
    textures.addAll(node.material.faceImagePaths.values);
  }
  // Texturas sao limitadas a 1024 e guardam mipmaps.
  bytes += textures.length * 1024 * 1024 * 4 * 4 / 3;
  if (scene.panorama.hasImage) {
    bytes += 1024 * 512 * 4;
    bytes += 256 * 256 * 3 * 4 * 6 * 4 / 3;
  }
  if (scene.reflectionProbe.enabled) {
    final side = scene.reflectionProbe.quality.faceResolution;
    // Seis faces RGBA e cadeia completa de mipmaps (~4/3).
    bytes += side * side * 4 * 6 * 4 / 3;
  }
  return bytes / (1024 * 1024);
}

/// Passos de degradacao, na ordem da spec. Devolve a cena rebaixada.
Scene3D degradeScene(Scene3D scene, int step) {
  var out = scene;
  if (step >= 1) {
    out = out.copyWith(
      planarFloorReflection: false,
      reflectionProbe: out.reflectionProbe.copyWith(
        quality: ProbeQuality.low,
        updateMode: ProbeUpdateMode.onMove,
      ),
    );
  }
  if (step >= 2) {
    // Desliga sombra da segunda luz em diante.
    var shadowed = 0;
    out = out.copyWith(
      lights: [
        for (final l in out.lights)
          if (l.castsShadow && shadowed++ >= 1)
            l.copyWith(castsShadow: false)
          else
            l,
      ],
    );
  }
  if (step >= 4) out = out.copyWith(msaa: false);
  if (step >= 5) out = out.copyWith(ambient: out.ambient * 0.8);
  return out;
}
