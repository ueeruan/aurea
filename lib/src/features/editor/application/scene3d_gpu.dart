import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter_scene/scene.dart' as fs;
import 'package:vector_math/vector_math.dart' as vm;

import '../domain/camera3d.dart';
import '../domain/element3d.dart';
import '../domain/scene3d.dart';
import '../domain/preview_quality.dart';
import 'motor3d_modo.dart';
import 'texture_cache.dart';

/// O MOTOR 3D EM GPU.
///
/// A cena do dominio (`Scene3D`, `SceneNode`, `Material3D`, `Light3D`,
/// `Camera3D`) continua sendo a verdade — e o que o editor edita, o que o
/// projeto salva, o que o pintor em CPU desenha. Esta classe e a PONTE:
/// traduz essa cena para o `flutter_scene` (Flutter GPU / Impeller), que
/// renderiza com buffer de profundidade, materiais fisicos, luz por
/// imagem, sombras em cascata, neblina e profundidade de campo — o que o
/// pintor em CPU nao tem como fazer.
///
/// O que fica do lado de ca: a geometria dos nos vira `MeshGeometry`
/// (uma primitiva por material), a transformacao de cada no vira a
/// matriz local, as texturas do `TextureCache` sobem para a GPU uma vez,
/// as luzes viram componentes, o ambiente vira ceu procedural ou mapa
/// equiretangular. Tudo e cacheado por assinatura: um quadro novo so
/// mexe nas matrizes; geometria e material so sao reconstruidos quando
/// o que os descreve muda.
///
/// Quando o Flutter GPU nao esta disponivel (aparelho sem suporte, ou
/// os testes, que rodam em Skia) [Scene3DGpu.pronto] nunca vira true e
/// quem desenha e o pintor de sempre.
class Scene3DGpu {
  Scene3DGpu();

  static Future<void>? _preparo;
  static bool _prontoParaRender = false;
  static bool _falhou = false;
  static String _motivo = '';
  // Uploads include an RGBA readback and mip generation. Serializing them
  // across views prevents a textured import from allocating all copies at once.
  static Future<void> _uploads = Future<void>.value();

  /// Carrega os shaders e recursos estaticos do motor. Falha em silencio
  /// (com log) onde nao ha GPU: [pronto] fica false e o pintor em CPU
  /// continua sendo usado.
  static Future<void> preparar() => _preparo ??= _prepararDeVerdade();

  static Future<void> _prepararDeVerdade() async {
    // Nos testes (Skia) e fora de iOS/Android o Flutter GPU nao existe:
    // nem tentar, para nao vazar a excecao do motor no laco de teste.
    if (kIsWeb ||
        Platform.environment.containsKey('FLUTTER_TEST') ||
        !(Platform.isIOS || Platform.isAndroid)) {
      _falhou = true;
      return;
    }
    // A MIGALHA: se a sessao anterior nao voltou de um quadro em GPU,
    // esta desenha em CPU. Ver Motor3DPreferencia.
    final pref = Motor3DPreferencia.instancia;
    if (pref != null && !pref.permiteGpu) {
      _falhou = true;
      _motivo = pref.motivoDeNaoTentar;
      return;
    }
    try {
      // initializeStaticResources engole a falha (sem GPU) e devolve
      // normalmente; a verdade esta em isReadyToRender.
      await fs.Scene.initializeStaticResources();
      _prontoParaRender = fs.Scene.isReadyToRender;
      _falhou = !_prontoParaRender;
      if (_falhou) {
        _motivo = 'os recursos do motor nao carregaram';
        debugPrint('Motor 3D em GPU indisponivel; fica o pintor em CPU.');
      }
    } catch (e, st) {
      _falhou = true;
      _motivo = '$e';
      debugPrint('Motor 3D em GPU indisponivel; pintor em CPU: $e\n$st');
    }
  }

  /// COMO A CENA 3D ESTA SENDO DESENHADA, em duas letras.
  ///
  /// Isto aparece no app de proposito. Um motor que cai para o pintor em
  /// CPU nao pode ser segredo: a diferenca entre os dois e a diferenca
  /// entre uma cena que roda e uma que engasga, e quem esta com o
  /// aparelho na mao e a unica pessoa que consegue ver qual dos dois
  /// esta valendo.
  static String get comoDesenha => _prontoParaRender
      ? 'GPU'
      : _falhou
      ? 'CPU'
      : '...';

  /// Por que caiu para o pintor em CPU, quando caiu.
  static String get motivo => _motivo;

  /// O motor esta pronto para desenhar?
  static bool get pronto => _prontoParaRender;

  /// A preparacao ja foi tentada e falhou (sem GPU).
  static bool get indisponivel => _falhou;

  final fs.Scene cena = fs.Scene();

  final Map<String, _NoGpu> _nos = {};
  final Map<String, fs.Texture2D> _texturas = {};
  final Map<String, Future<void>> _texturasACaminho = {};
  final Map<String, List<void Function(fs.Texture2D)>> _esperandoTextura = {};
  final List<fs.Node> _luzes = [];
  String? _chaveAmbiente;
  int _epocaAmbiente = 0;
  bool _descartado = false;
  List<Light3D>? _ultimasLuzes;
  List<double> _ultimasIntensidades = const [];

  /// Sincroniza a cena da GPU com [scene] no instante [t].
  ///
  /// Devolve na hora com o que ja esta pronto. Texturas e o ambiente por
  /// imagem chegam depois, em segundo plano, e chamam [onMudou] para o
  /// quadro ser redesenhado.
  void sincronizar(
    Scene3D scene,
    Duration t, {
    VoidCallback? onMudou,
    bool rascunho = false,
  }) {
    if (_descartado) return;
    _sincronizarNos(scene, t, onMudou);
    _sincronizarLuzes(scene, t);
    _sincronizarAmbiente(scene, t, onMudou);
    _sincronizarNevoa(scene);
    _sincronizarPos(scene, rascunho);
  }

  /// Camera do motor a partir da camera resolvida do dominio, para uma
  /// area de [tamanho] pixels. O campo de visao do dominio e HORIZONTAL;
  /// o do motor, vertical.
  fs.PerspectiveCamera camera(RenderCamera cam, ui.Size tamanho) {
    final basis = cameraBasis(cam);
    final aspecto = tamanho.height <= 0
        ? 16 / 9
        : tamanho.width / tamanho.height;
    final fovX = cam.orthographic ? 0.6 : cam.fovRadians;
    final fovY = 2 * math.atan(math.tan(fovX / 2) / aspecto);
    return fs.PerspectiveCamera(
      position: _v(cam.position),
      target: _v(cam.target),
      up: _v(basis.up),
      fovRadiansY: fovY.clamp(0.05, 3.0),
      fovNear: math.max(1.0, cam.near),
      // Uma cena espacial poe estrelas a dezenas de milhares de
      // unidades; o plano distante do dominio e 100 mil.
      fovFar: math.min(cam.far, 120000),
    );
  }

  /// Profundidade de campo do motor a partir da da camera do dominio.
  void configurarProfundidadeDeCampo(
    Camera3D camera,
    Duration t, {
    bool rascunho = false,
  }) {
    final dof = camera.dof;
    final ligada = dof.enabled && !rascunho;
    cena.depthOfField.enabled = ligada;
    if (!ligada) return;
    final focal = camera.focalLength.valueAt(t);
    cena.depthOfField
      ..focusDistance = math.max(1.0, dof.focusDistance.valueAt(t))
      ..fStop = dof.fStopFor(focal, t).clamp(0.5, 32.0)
      ..blurScale = (dof.blurLevel.valueAt(t) / 100).clamp(0.0, 2.0)
      ..bladeCount = irisSides(dof.irisShape)
      ..bladeRotation = dof.irisRotation.valueAt(t) * math.pi / 180;
  }

  /// Desenha a cena em [canvas], dentro de [area].
  void desenhar(
    ui.Canvas canvas,
    ui.Rect area,
    fs.Camera camera, {
    bool rascunho = false,
    bool exporting = false,
  }) {
    if (!pronto) return;
    cena.renderScale = scenePreviewScale(
      area.width,
      area.height,
      interacting: rascunho,
      exporting: exporting,
    );
    cena.render(camera, canvas, viewport: area, pixelRatio: 1.0);
  }

  void descartar() {
    _descartado = true;
    _epocaAmbiente++;
    for (final n in _nos.values) {
      n.remover(cena);
    }
    _nos.clear();
    for (final l in _luzes) {
      cena.remove(l);
    }
    _luzes.clear();
    _texturas.clear();
    _esperandoTextura.clear();
    _texturasACaminho.clear();
    cena.environment = null;
    cena.skyEnvironment = null;
    cena.skybox = null;
    cena.directionalLight = null;
  }

  // ------------------------------------------------------------- nos

  void _sincronizarNos(Scene3D scene, Duration t, VoidCallback? onMudou) {
    final vivos = <String>{};
    for (final node in scene.nodes) {
      if (!node.visible || node.isNull) continue;
      final xf = resolveNodeTransform(scene, node, t);
      final malha = _malhaDe(node, t);
      if (malha == null) continue;
      vivos.add(node.id);
      var g = _nos[node.id];
      if (g == null || g.assinatura != malha.assinatura) {
        g?.remover(cena);
        g = _construir(node, malha, onMudou);
        _nos[node.id] = g;
      } else if (malha.dinamica && !identical(g.ultimaMalha, malha.malha)) {
        _construir(node, malha, onMudou, existente: g);
      }
      g.transformar(xf, node);
    }
    for (final id in _nos.keys.toList()) {
      if (!vivos.contains(id)) {
        _nos.remove(id)!.remover(cena);
      }
    }
    final usadas = {for (final n in _nos.values) ...n.texturas};
    _texturas.removeWhere((path, _) => !usadas.contains(path));
    _esperandoTextura.removeWhere((path, _) => !usadas.contains(path));
  }

  /// A malha do no neste instante: vertices, faces, normais e UVs por
  /// vertice quando existem (modelos), e o material de cada face.
  _MalhaFonte? _malhaDe(SceneNode node, Duration t) {
    final asset = node.modelAsset;
    if (asset != null) {
      final motion = node.modelMotion;
      final animado =
          motion.keys.isNotEmpty ||
          (motion.clip >= 0 && motion.clip < asset.clips.length);
      final frame = asset.evaluate(t, motion);
      final materiais = node.useModelMaterials
          ? frame.materials
          : List<Material3D>.filled(frame.mesh.faces.length, node.material);
      return _MalhaFonte(
        malha: frame.mesh,
        normais: frame.normals,
        uvs: frame.uvs,
        materiais: materiais,
        dinamica: animado,
        assinatura:
            'm${identityHashCode(asset)}:${identityHashCode(motion)}:${node.instances.isNotEmpty}:'
            '${node.useModelMaterials ? 'a' : _assinaturaMaterial(node.material)}',
      );
    }
    final mesh = node.mesh ?? element3DMesh(node.kind);
    return _MalhaFonte(
      malha: mesh,
      normais: null,
      uvs: null,
      materiais: List<Material3D>.filled(mesh.faces.length, node.material),
      assinatura:
          'p${node.kind.index}:${identityHashCode(mesh)}:${node.instances.isNotEmpty}:${_assinaturaMaterial(node.material)}',
    );
  }

  static String _assinaturaMaterial(Material3D m) =>
      '${m.baseColor.toARGB32()}:${m.metallic}:${m.roughness}:${m.emissive}:'
      '${m.opacity}:${m.kind.index}:${m.imagePath}:${m.doubleSided}:'
      '${m.alphaCutoff}';

  _NoGpu _construir(
    SceneNode node,
    _MalhaFonte fonte,
    VoidCallback? onMudou, {
    _NoGpu? existente,
  }) {
    // Faces agrupadas por material: uma primitiva (uma chamada) por grupo.
    final grupos = <Material3D, _Grupo>{};
    final materiaisDoGrupo = <Material3D, Material3D>{};
    final malha = fonte.malha;
    final lisa =
        fonte.normais != null &&
        fonte.normais!.length == malha.verts.length &&
        fonte.normais!.every((n) => n != null);
    final caixa = _Caixa.de(malha);

    for (var f = 0; f < malha.faces.length; f++) {
      final face = malha.faces[f];
      if (face.length < 3) continue;
      final material = fonte.materiais[f];
      final chave = material;
      final grupo = grupos[chave] ??= _Grupo();
      materiaisDoGrupo[chave] = material;

      // Normal da face (Newell): decide o lado e serve as faces planas.
      var nx = 0.0, ny = 0.0, nz = 0.0;
      for (var i = 0; i < face.length; i++) {
        final a = malha.verts[face[i]],
            b = malha.verts[face[(i + 1) % face.length]];
        nx += (a[1] - b[1]) * (a[2] + b[2]);
        ny += (a[2] - b[2]) * (a[0] + b[0]);
        nz += (a[0] - b[0]) * (a[1] + b[1]);
      }
      final len = math.sqrt(nx * nx + ny * ny + nz * nz);
      if (len < 1e-12) continue;
      nx /= len;
      ny /= len;
      nz /= len;

      for (var i = 1; i < face.length - 1; i++) {
        var ia = face[0], ib = face[i], ic = face[i + 1];
        // O motor descarta a face de costas pela ordem dos vertices; a
        // normal manda: se a ordem discorda dela, inverte.
        final a = malha.verts[ia], b = malha.verts[ib], c = malha.verts[ic];
        final ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        final vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        final wx = uy * vz - uz * vy,
            wy = uz * vx - ux * vz,
            wz = ux * vy - uy * vx;
        if (wx * nx + wy * ny + wz * nz < 0) {
          final tmp = ib;
          ib = ic;
          ic = tmp;
        }
        for (final v in [ia, ib, ic]) {
          final p = malha.verts[v];
          if (lisa) {
            final idx = grupo.mapa[v] ??= grupo.adicionar(
              p,
              _xyz(fonte.normais![v]!),
              _uvDe(fonte, v, p, nx, ny, nz, caixa),
            );
            grupo.indices.add(idx);
          } else {
            grupo.indices.add(
              grupo.adicionar(p, [
                nx,
                ny,
                nz,
              ], _uvDe(fonte, v, p, nx, ny, nz, caixa)),
            );
          }
        }
      }
    }

    final no = existente ?? _NoGpu(fonte.assinatura, fs.Node(name: node.name));
    no.ultimaMalha = fonte.malha;
    final instanciado = node.instances.isNotEmpty;
    for (final e in grupos.entries) {
      final g = e.value;
      final antiga = no.geometrias[e.key];
      if (g.indices.isEmpty && antiga == null) continue;
      if (antiga != null) {
        final positions = Float32List.fromList(g.positions);
        final normals = Float32List.fromList(g.normals);
        final uvs = Float32List.fromList(g.uvs);
        if (listEquals(no.indices[e.key], g.indices) &&
            no.tamanhos[e.key] == positions.length) {
          antiga.updatePositions(positions);
          antiga.updateNormals(normals);
          antiga.updateTexCoords(uvs);
        } else {
          antiga.rebuild(
            positions: positions,
            normals: normals,
            texCoords: uvs,
            indices: g.indices,
          );
          no.indices[e.key] = g.indices;
          no.tamanhos[e.key] = positions.length;
        }
        continue;
      }
      final material = _materialGpu(materiaisDoGrupo[e.key]!, onMudou);
      final path = materiaisDoGrupo[e.key]!.imagePath;
      if (path != null && path.isNotEmpty) no.texturas.add(path);
      final geometria = fs.MeshGeometry.fromArrays(
        storage: fonte.dinamica
            ? fs.GeometryStorage.updatable
            : fs.GeometryStorage.fixed,
        positions: Float32List.fromList(g.positions),
        normals: Float32List.fromList(g.normals),
        texCoords: Float32List.fromList(g.uvs),
        indices: g.indices,
      );
      no.geometrias[e.key] = geometria;
      no.indices[e.key] = g.indices;
      no.tamanhos[e.key] = g.positions.length;
      if (instanciado) {
        final im = fs.InstancedMesh(geometry: geometria, material: material);
        no.instancias.add(im);
        final filho = fs.Node()..addComponent(fs.InstancedMeshComponent(im));
        no.no.add(filho);
      } else {
        no.no.add(
          fs.Node()..addComponent(
            fs.MeshComponent(
              fs.Mesh.primitives(
                primitives: [fs.MeshPrimitive(geometria, material)],
              ),
            ),
          ),
        );
      }
    }
    if (existente == null) cena.add(no.no);
    return no;
  }

  static List<double> _xyz(Vec3 v) => [v.x, v.y, v.z];

  /// UV do vertice: a do modelo quando existe; senao a projecao planar
  /// pelo eixo dominante da face, normalizada pela caixa da malha — a
  /// mesma regra do pintor em CPU, para a imagem cair no mesmo lugar.
  static List<double> _uvDe(
    _MalhaFonte fonte,
    int v,
    List<double> p,
    double nx,
    double ny,
    double nz,
    _Caixa caixa,
  ) {
    final uv = fonte.uvs;
    if (uv != null && v < uv.length && uv[v] != null) {
      return [uv[v]!.dx, uv[v]!.dy];
    }
    final ax = nx.abs(), ay = ny.abs(), az = nz.abs();
    if (ax >= ay && ax >= az) {
      return [caixa.faixa(p[2], 2), caixa.faixa(p[1], 1)];
    }
    if (ay >= ax && ay >= az) {
      return [caixa.faixa(p[0], 0), caixa.faixa(p[2], 2)];
    }
    return [caixa.faixa(p[0], 0), caixa.faixa(p[1], 1)];
  }

  // ------------------------------------------------------- materiais

  fs.Material _materialGpu(Material3D m, VoidCallback? onMudou) {
    final cor = _linear(m.baseColor);
    final opacidade = (m.baseColor.a * m.opacity).clamp(0.0, 1.0);
    final fator = vm.Vector4(cor.x, cor.y, cor.z, opacidade);
    if (m.kind == MaterialKind.unlit) {
      final u = fs.UnlitMaterial();
      u.baseColorFactor = fator;
      u.doubleSided = m.doubleSided;
      if (opacidade < .999) u.alphaMode = fs.AlphaMode.blend;
      _ligarTextura(m.imagePath, onMudou, (tex) => u.baseColorTexture = tex);
      return u;
    }
    final p = fs.PhysicallyBasedMaterial();
    p.baseColorFactor = fator;
    p.metallicFactor = m.metallic.clamp(0.0, 1.0);
    p.roughnessFactor = m.roughness.clamp(0.04, 1.0);
    // EMISSIVO forte o bastante para o bloom pegar: o dominio guarda 0..1,
    // e um quadro de HDR acima de 1 e o que separa "claro" de "acende".
    if (m.emissive > 0) {
      final k = m.emissive * 6;
      p.emissiveFactor = vm.Vector4(cor.x * k, cor.y * k, cor.z * k, 1);
    }
    p.doubleSided = m.doubleSided;
    p.alphaMode = switch (m.kind) {
      MaterialKind.transparent => fs.AlphaMode.blend,
      MaterialKind.cutout => fs.AlphaMode.mask,
      _ => opacidade < .999 ? fs.AlphaMode.blend : fs.AlphaMode.opaque,
    };
    p.alphaCutoff = m.alphaCutoff;
    _ligarTextura(m.imagePath, onMudou, (tex) => p.baseColorTexture = tex);
    return p;
  }

  /// A textura de [path] sobe para a GPU uma vez; quem precisa dela
  /// recebe pelo [aplicar] — agora, se ja esta la, ou quando chegar.
  void _ligarTextura(
    String? path,
    VoidCallback? onMudou,
    void Function(fs.Texture2D) aplicar,
  ) {
    if (path == null || path.isEmpty) return;
    final pronta = _texturas[path];
    if (pronta != null) {
      aplicar(pronta);
      return;
    }
    (_esperandoTextura[path] ??= []).add(aplicar);
    if (_texturasACaminho.containsKey(path)) return;
    final upload = _uploads.then((_) async {
      try {
        if (_descartado || !_esperandoTextura.containsKey(path)) return;
        var imagem = TextureCache.instance.imageFor(path);
        if (imagem == null) {
          await TextureCache.instance.prepare(path);
          imagem = TextureCache.instance.imageFor(path);
        }
        if (imagem == null || _descartado) return;
        // A decode eviction must not invalidate an upload that is in flight.
        final owned = imagem.clone();
        late final fs.Texture2D tex;
        try {
          tex = await fs.Texture2D.fromImage(owned);
        } finally {
          owned.dispose();
        }
        if (_descartado || !_esperandoTextura.containsKey(path)) return;
        _texturas[path] = tex;
        final fila = _esperandoTextura.remove(path) ?? const [];
        for (final f in fila) {
          f(tex);
        }
        onMudou?.call();
      } catch (e) {
        debugPrint('Textura 3D nao subiu para a GPU ($path): $e');
      } finally {
        _esperandoTextura.remove(path);
        _texturasACaminho.remove(path);
      }
    });
    _uploads = upload;
    _texturasACaminho[path] = upload;
  }

  // ------------------------------------------------------------ luzes

  void _sincronizarLuzes(Scene3D scene, Duration t) {
    final intensidades = [for (final l in scene.lights) l.intensity.valueAt(t)];
    if (listEquals(_ultimasLuzes, scene.lights) &&
        listEquals(_ultimasIntensidades, intensidades)) {
      return;
    }
    _ultimasLuzes = scene.lights;
    _ultimasIntensidades = intensidades;
    for (final l in _luzes) {
      cena.remove(l);
    }
    _luzes.clear();
    fs.DirectionalLight? principal;
    var principalForca = 0.0;
    for (final l in scene.lights) {
      final i = l.intensity.valueAt(t);
      if (i <= 0) continue;
      final cor = _linear3(l.color);
      switch (l.kind) {
        case Light3DKind.directional:
          final d = fs.DirectionalLight(
            direction: _v(l.direction).normalized(),
            color: cor,
            intensity: i * 2.6,
            castsShadow: l.castsShadow,
            shadowSoftness: 3 + l.softness.clamp(0.0, 1.0) * 14,
            shadowMaxDistance: 5000,
            // Duas cascatas de 1024: quatro vezes menos memoria de GPU
            // que 3 x 2048, e num celular a diferenca nao se ve.
            shadowCascadeCount: 2,
            shadowMapResolution: 1024,
            shadowDepthBias: 0.8,
            shadowNormalBias: 0.8,
            shadowFadeRange: 400,
          );
          // Uma luz direcional e a principal (a que faz sombra); as
          // outras entram como componentes.
          if (principal == null || (l.castsShadow && i > principalForca)) {
            if (principal != null) {
              _luzes.add(_noDeLuz(fs.DirectionalLightComponent(principal)));
            }
            principal = d;
            principalForca = i;
          } else {
            _luzes.add(_noDeLuz(fs.DirectionalLightComponent(d)));
          }
        case Light3DKind.point:
          final alcance = l.range <= 0 ? 1200.0 : l.range;
          _luzes.add(
            _noDeLuz(
              fs.PointLightComponent(
                fs.PointLight(
                  color: cor,
                  // O dominio atenua (1 - d/alcance)^2; o motor, 1/d^2. Igualar
                  // no meio do alcance: I = i * alcance^2 / 16.
                  intensity: i * alcance * alcance / 16,
                  range: alcance,
                ),
              ),
              posicao: l.position,
            ),
          );
        case Light3DKind.spot:
          final alcance = l.range <= 0 ? 1200.0 : l.range;
          final externo = l.coneDegrees.clamp(1.0, 179.0) * math.pi / 360;
          _luzes.add(
            _noDeLuz(
              fs.SpotLightComponent(
                fs.SpotLight(
                  color: cor,
                  intensity: i * alcance * alcance / 16,
                  range: alcance,
                  direction: _v(l.direction).normalized(),
                  innerConeAngle:
                      externo * (1 - l.softness.clamp(0.0, 1.0) * .9),
                  outerConeAngle: externo,
                  castsShadow: l.castsShadow,
                  shadowMapResolution: 512,
                  shadowSoftness: 2 + l.softness * 6,
                ),
              ),
              posicao: l.position,
            ),
          );
        case Light3DKind.ambient:
          // Entra no ambiente (ver _sincronizarAmbiente).
          break;
      }
    }
    cena.directionalLight = principal;
    for (final n in _luzes) {
      cena.add(n);
    }
  }

  fs.Node _noDeLuz(fs.Component componente, {Vec3? posicao}) {
    final no = fs.Node(
      localTransform: posicao == null
          ? null
          : vm.Matrix4.translation(_v(posicao)),
    );
    no.addComponent(componente);
    return no;
  }

  // --------------------------------------------------------- ambiente

  void _sincronizarAmbiente(Scene3D scene, Duration t, VoidCallback? onMudou) {
    var extra = 0.0;
    for (final l in scene.lights) {
      if (l.kind == Light3DKind.ambient) extra += l.intensity.valueAt(t);
    }
    final pano = scene.panorama;
    cena.environmentIntensity =
        ((scene.ambient + extra) / .28) * pano.intensity.clamp(0.0, 4.0);
    final caminho = pano.hasImage ? pano.sourcePath : null;
    final chave = caminho != null
        ? 'img:$caminho:${pano.showBackground}:${pano.backgroundBlur}'
        : 'ceu:${scene.environment.index}:${scene.skyColor.toARGB32()}:'
              '${scene.groundColor.toARGB32()}:${pano.showBackground}';
    if (chave == _chaveAmbiente) return;
    _chaveAmbiente = chave;
    final epoca = ++_epocaAmbiente;

    if (caminho != null) {
      () async {
        try {
          final bytes = await File(caminho).readAsBytes();
          final mapa = await fs.EnvironmentMap.fromEquirectImageBytes(
            bytes: bytes,
            maxWidth: 2048,
          );
          if (_descartado || epoca != _epocaAmbiente) return;
          cena.skyEnvironment = null;
          cena.environment = mapa;
          cena.skybox = pano.showBackground
              ? fs.Skybox(
                  fs.EnvironmentSkySource(
                    blurriness: (pano.backgroundBlur / 30).clamp(0.0, 1.0),
                  ),
                )
              : null;
          onMudou?.call();
        } catch (e) {
          debugPrint('Panorama nao carregou na GPU ($caminho): $e');
        }
      }();
      return;
    }

    // CEU PROCEDURAL a partir das cores do dominio: zenite = ceu, chao =
    // chao, horizonte no meio, e o sol na direcao da luz principal.
    final ceu = _linear3(scene.skyColor);
    final chao = _linear3(scene.groundColor);
    final horizonte = (ceu + chao) * .5;
    vm.Vector3 sol = vm.Vector3(0.4, 0.5, 0.6);
    vm.Vector3 corDoSol = vm.Vector3(3.0, 2.7, 2.2);
    for (final l in scene.lights) {
      if (l.kind == Light3DKind.directional && l.intensity.valueAt(t) > 0) {
        sol = (_v(l.direction) * -1).normalized();
        corDoSol = _linear3(l.color) * 3.0;
        break;
      }
    }
    final fonte = fs.GradientSkySource(
      zenithColor: ceu,
      horizonColor: horizonte,
      groundColor: chao,
      sunDirection: sol,
      sunColor: corDoSol,
    );
    cena.environment = null;
    cena.skyEnvironment = fs.SkyEnvironment(fonte);
    cena.skybox = pano.showBackground ? fs.Skybox(fonte) : null;
  }

  // ----------------------------------------------------------- neblina

  void _sincronizarNevoa(Scene3D scene) {
    final f = cena.fog;
    f.enabled = scene.fogDensity > 0;
    if (!f.enabled) return;
    f
      ..mode = fs.FogMode.exponential
      ..density = scene.fogDensity
      ..start = scene.fogStart
      ..color = _linear3(scene.fogColor)
      ..maxOpacity = 1.0;
  }

  // --------------------------------------------------------------- pos

  void _sincronizarPos(Scene3D scene, bool rascunho) {
    var emissivo = false;
    for (final n in scene.nodes) {
      if (n.material.emissive > 0) {
        emissivo = true;
        break;
      }
      final mats = n.modelAsset?.data['materials'] as List? ?? const [];
      for (final m in mats) {
        if (((m as Map)['emissive'] as num? ?? 0) > 0) {
          emissivo = true;
          break;
        }
      }
      if (emissivo) break;
    }
    // O bloom monta uma cadeia de mips do tamanho da tela a cada
    // quadro. Enquanto toca, isso e memoria e banda de GPU trocadas por
    // um brilho que ninguem esta olhando parado.
    cena.postProcess.bloom
      ..enabled = emissivo && !rascunho
      ..threshold = 1.0
      ..intensity = .45
      ..scatter = .7;
    cena.toneMapping = fs.ToneMappingMode.pbrNeutral;
  }

  // ---------------------------------------------------------- utilidades

  static vm.Vector3 _v(Vec3 v) => vm.Vector3(v.x, v.y, v.z);

  static double _lin(double c) =>
      c <= 0.04045 ? c / 12.92 : math.pow((c + 0.055) / 1.055, 2.4).toDouble();

  static vm.Vector3 _linear3(ui.Color c) =>
      vm.Vector3(_lin(c.r), _lin(c.g), _lin(c.b));

  static vm.Vector3 _linear(ui.Color c) => _linear3(c);
}

/// Um no do dominio ja traduzido: o no do motor e a assinatura do que
/// ele contem. Quando a assinatura muda, o no e refeito.
class _NoGpu {
  _NoGpu(this.assinatura, this.no);

  final String assinatura;
  final fs.Node no;
  final List<fs.InstancedMesh> instancias = [];
  final Map<Material3D, fs.MeshGeometry> geometrias = {};
  final Map<Material3D, List<int>> indices = {};
  final Map<Material3D, int> tamanhos = {};
  final Set<String> texturas = {};
  Element3DMesh? ultimaMalha;
  List<Vec3>? _ultimasInstancias;
  double? _ultimoTamanho;

  void transformar(NodeTransform xf, SceneNode node) {
    final rad = math.pi / 180;
    final m = vm.Matrix4.translation(
      vm.Vector3(xf.position.x, xf.position.y, xf.position.z),
    );
    m.multiply(vm.Matrix4.rotationZ(xf.rotZ * rad));
    m.multiply(vm.Matrix4.rotationY(xf.rotY * rad));
    m.multiply(vm.Matrix4.rotationX(xf.rotX * rad));
    if (instancias.isEmpty) {
      final s = node.size * xf.scale;
      m.multiply(vm.Matrix4.diagonal3Values(s, s, s));
      no.localTransform = m;
      return;
    }
    // Instancias: o no leva posicao, rotacao e escala; cada instancia
    // leva o deslocamento e o tamanho da malha unitaria.
    m.multiply(vm.Matrix4.diagonal3Values(xf.scale, xf.scale, xf.scale));
    no.localTransform = m;
    if (!identical(_ultimasInstancias, node.instances) ||
        _ultimoTamanho != node.size) {
      _ultimasInstancias = node.instances;
      _ultimoTamanho = node.size;
      for (final im in instancias) {
        im.clearInstances();
        for (final p in node.instances) {
          im.addInstance(
            vm.Matrix4.translation(vm.Vector3(p.x, p.y, p.z))..multiply(
              vm.Matrix4.diagonal3Values(node.size, node.size, node.size),
            ),
          );
        }
      }
    }
  }

  void remover(fs.Scene cena) => cena.remove(no);
}

class _MalhaFonte {
  _MalhaFonte({
    required this.malha,
    required this.normais,
    required this.uvs,
    required this.materiais,
    required this.assinatura,
    this.dinamica = false,
  });

  final Element3DMesh malha;
  final List<Vec3?>? normais;
  final List<ui.Offset?>? uvs;
  final List<Material3D> materiais;
  final String assinatura;
  final bool dinamica;
}

class _Grupo {
  final positions = <double>[];
  final normals = <double>[];
  final uvs = <double>[];
  final indices = <int>[];
  final mapa = <int, int>{};

  int adicionar(List<double> p, List<double> n, List<double> uv) {
    positions.addAll([p[0], p[1], p[2]]);
    normals.addAll([n[0], n[1], n[2]]);
    uvs.addAll([uv[0], uv[1]]);
    return positions.length ~/ 3 - 1;
  }
}

class _Caixa {
  _Caixa(this.lo, this.hi);

  final List<double> lo;
  final List<double> hi;

  static _Caixa de(Element3DMesh m) {
    final lo = [double.infinity, double.infinity, double.infinity];
    final hi = [-double.infinity, -double.infinity, -double.infinity];
    for (final v in m.verts) {
      for (var i = 0; i < 3; i++) {
        if (v[i] < lo[i]) lo[i] = v[i];
        if (v[i] > hi[i]) hi[i] = v[i];
      }
    }
    return _Caixa(lo, hi);
  }

  double faixa(double v, int eixo) {
    final d = hi[eixo] - lo[eixo];
    return d <= 1e-9 ? 0.5 : (v - lo[eixo]) / d;
  }
}
