import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../domain/camera3d.dart';
import '../../application/panorama_cache.dart';
import '../../application/texture_cache.dart';
import '../../domain/element3d.dart';
import '../../domain/scene3d.dart';

/// Pintor do CONTEINER CENA 3D. Faz os dois passes da spec §3:
///
///   passe opaco       — ordenado POR TRIANGULO, sem blend
///   passe transparente — depois, testando contra o opaco mas sem
///                        "escrever profundidade" (nao reordena o opaco)
///
/// A ordenacao por triangulo (e nao por objeto) e o que permite dois
/// cubos cruzados renderizarem a intersecao correta — exatamente o
/// teste que a spec define como aprovacao.
class Scene3DPainter extends CustomPainter {
  Scene3DPainter({
    required this.scene,
    required this.camera,
    this.resolvedCamera,
    required this.view,
    required this.time,
    this.showHelpers = false,
    this.helpersOnly = false,
    this.showModelRig = false,
    this.overrideCamera,
    this.selectedNodeId,
    this.onMetrics,
  }) : super(
         repaint: Listenable.merge([
           TextureCache.instance.revision,
           PanoramaCache.instance.revision,
         ]),
       );

  final Scene3D scene;
  final Camera3D camera;
  final SceneView view;
  final Duration time;

  /// Ajudas de cena: grade do chao, frustum, eixos. NUNCA na exportacao.
  final bool showHelpers;

  /// Editor overlay above a native texture; never evaluates scene triangles.
  final bool helpersOnly;
  final bool showModelRig;

  /// Vista livre navegada no estudio: quando presente, substitui a
  /// camera derivada de [view].
  final RenderCamera? overrideCamera;

  /// Volume envolvente desenhado so para o objeto selecionado.
  final String? selectedNodeId;

  final void Function(SceneFrame frame)? onMetrics;

  /// Camera ja resolvida por quem monta a cena (tomadas e transicoes).
  /// Nula = usa [camera] direto.
  final RenderCamera? resolvedCamera;

  RenderCamera _renderCamera() =>
      overrideCamera ??
      (view == SceneView.camera
          // A camera ATIVA no instante: com tomadas, e a da tomada no
          // ar (ou a mistura, se ainda esta na transicao).
          ? (resolvedCamera ?? camera.renderAt(time))
          : orthoViewCamera(view));

  @override
  void paint(Canvas canvas, Size size) {
    final cam = _renderCamera();
    canvas.save();
    canvas.clipRect(Offset.zero & size);

    if (helpersOnly) {
      if (showHelpers && scene.showFloorGrid) {
        _paintFloorGrid(canvas, size, cam);
      }
      _paintEditorHelpers(canvas, size, cam);
      canvas.restore();
      return;
    }

    if (scene.background != null) {
      canvas.drawRect(Offset.zero & size, Paint()..color = scene.background!);
    } else if (scene.panorama.showBackground) {
      _paintPanorama(canvas, size, cam);
    }

    if (showHelpers && scene.showFloorGrid) {
      _paintFloorGrid(canvas, size, cam);
    }

    final frame = renderScene(
      scene,
      cam,
      size,
      time,
      environmentSampler: PanoramaCache.instance.samplerFor(scene.panorama),
    );
    onMetrics?.call(frame);

    if (scene.planarFloorReflection && !scene.draftMode) {
      _paintPlanarFloorReflection(canvas, size, cam, frame);
    }

    // SOMBRA DE CONTATO antes da geometria: ela vive no chao, e tudo
    // que e objeto passa por cima dela.
    final hasShadowLight = scene.lights.any(
      (light) => light.castsShadow && light.intensity.valueAt(time) > 0,
    );
    if (!scene.draftMode && hasShadowLight) {
      _paintContactShadows(canvas, frame);
    }

    // Canvas has no depth test: an unconditional transparent second pass
    // incorrectly paints glass BEHIND an opaque object over its front.
    // Interleave both sorted lists; this remains a painter approximation,
    // not a replacement for a GPU depth buffer on intersecting triangles.
    final visible = [...frame.opaque, ...frame.transparent];
    depthSort(visible);
    _paintTriangles(canvas, visible);

    // PROFUNDIDADE DE CAMPO. No modo rascunho ela sai do caminho — e a
    // diferenca entre navegar a cena e sofrer num aparelho de entrada.
    if (camera.dof.enabled && !scene.draftMode && view == SceneView.camera) {
      _paintBokeh(canvas, frame);
    }

    _paintEditorHelpers(canvas, size, cam);
    canvas.restore();
  }

  void _paintEditorHelpers(Canvas canvas, Size size, RenderCamera cam) {
    if (showHelpers && (view != SceneView.camera || overrideCamera != null)) {
      // Fora da vista da camera ativa: frustum e plano de foco.
      _paintFrustum(canvas, size, cam);
      _paintDepthLines(canvas, size, cam);
    }
    if (showHelpers && selectedNodeId != null) {
      _paintSelectionBox(canvas, size, cam);
    }
    if (showModelRig && selectedNodeId != null) {
      _paintModelRig(canvas, size, cam);
    }
  }

  void _paintModelRig(Canvas canvas, Size size, RenderCamera cam) {
    final node = scene.nodeById(selectedNodeId!);
    final asset = node?.modelAsset;
    if (node == null || asset == null) return;
    final frame = asset.evaluate(time, node.modelMotion);
    final xf = resolveNodeTransform(scene, node, time);
    final m = Matrix4.identity()
      ..rotateZ(xf.rotZ * math.pi / 180)
      ..rotateY(xf.rotY * math.pi / 180)
      ..rotateX(xf.rotX * math.pi / 180);
    final points = <int, Offset>{};
    for (final entry in frame.joints.entries) {
      final rotated = m.transformed3(entry.value * (node.size * xf.scale));
      final point = _project(
        xf.position + Vec3(rotated.x, rotated.y, rotated.z),
        size,
        cam,
      );
      if (point != null) points[entry.key] = point;
    }
    final pen = Paint()
      ..color = const Color(0xFF61FFE0)
      ..strokeWidth = 2;
    canvas.save();
    canvas.clipRect(Offset.zero & size);
    for (final entry in points.entries) {
      var parent = asset.nodes[entry.key]['parent'] as int?;
      while (parent != null && !points.containsKey(parent)) {
        parent = asset.nodes[parent]['parent'] as int?;
      }
      if (parent != null) canvas.drawLine(points[parent]!, entry.value, pen);
      canvas.drawCircle(entry.value, 3.5, pen);
    }
    canvas.restore();
  }

  void _paintPanorama(Canvas canvas, Size size, RenderCamera cam) {
    final panorama = scene.panorama;
    final path = panorama.sourcePath;
    final image = path == null ? null : TextureCache.instance.imageFor(path);
    if (image != null) {
      _paintPanoramaImage(canvas, size, cam, image);
      return;
    }

    // Presets procedurais: poucos blocos, suficientes para conservar a
    // direcao do horizonte, softbox e neon quando o ambiente gira.
    const cols = 28, rows = 14;
    final basis = cameraBasis(cam);
    final tanX = math.tan(cam.fovRadians / 2);
    final tanY = tanX * size.height / math.max(1, size.width);
    final rotation = panorama.rotationDegrees * math.pi / 180;
    final cr = math.cos(rotation), sr = math.sin(rotation);
    final blur = panorama.backgroundBlur.clamp(0.0, 30.0).toDouble();
    if (blur > 0) {
      canvas.saveLayer(
        Offset.zero & size,
        Paint()..imageFilter = ui.ImageFilter.blur(sigmaX: blur, sigmaY: blur),
      );
    }
    for (var y = 0; y < rows; y++) {
      for (var x = 0; x < cols; x++) {
        final sx = ((x + 0.5) / cols * 2 - 1) * tanX;
        final sy = (1 - (y + 0.5) / rows * 2) * tanY;
        final ray =
            (basis.forward + basis.right * sx + basis.up * sy).normalized;
        final rotated = Vec3(
          ray.x * cr - ray.z * sr,
          ray.y,
          ray.x * sr + ray.z * cr,
        );
        var (r, g, b) = environmentColor(
          scene.environment,
          rotated.x,
          rotated.y,
          rotated.z,
        );
        final boost = panorama.highlightBoost.clamp(0.0, 2.0).toDouble();
        final peak = math.max(r, math.max(g, b));
        final lift = math.max(0.0, peak - 0.58) * boost;
        final gain = panorama.intensity.clamp(0.0, 4.0).toDouble();
        r = acesFilmic(r * (1 + lift) * gain);
        g = acesFilmic(g * (1 + lift) * gain);
        b = acesFilmic(b * (1 + lift) * gain);
        canvas.drawRect(
          Rect.fromLTWH(
            size.width * x / cols,
            size.height * y / rows,
            size.width / cols + 1,
            size.height / rows + 1,
          ),
          Paint()..color = Color.from(alpha: 1, red: r, green: g, blue: b),
        );
      }
    }
    if (blur > 0) canvas.restore();
  }

  void _paintPanoramaImage(
    Canvas canvas,
    Size size,
    RenderCamera cam,
    ui.Image image,
  ) {
    final panorama = scene.panorama;
    final forward = cameraBasis(cam).forward;
    final yaw =
        math.atan2(forward.x, forward.z) +
        panorama.rotationDegrees * math.pi / 180;
    var normalized = ((yaw / (2 * math.pi) + 0.5) % 1).toDouble();
    if (panorama.mirrorTo360) {
      final phase =
          ((normalized * 360 / panorama.coverageDegrees.clamp(1.0, 360.0)) % 2)
              .toDouble();
      normalized = phase <= 1 ? phase : 2 - phase;
    }
    final center = normalized * image.width;
    final sourceSpan = panorama.mirrorTo360
        ? panorama.coverageDegrees.clamp(1.0, 360.0).toDouble() * math.pi / 180
        : 2 * math.pi;
    final sourceWidth = (image.width * cam.fovRadians / sourceSpan)
        .clamp(1.0, image.width.toDouble())
        .toDouble();
    var sourceX = center - sourceWidth / 2;
    while (sourceX < 0) {
      sourceX += image.width;
    }
    while (sourceX >= image.width) {
      sourceX -= image.width;
    }

    final imageGain =
        panorama.intensity.clamp(0.0, 4.0).toDouble() *
        (1 + panorama.highlightBoost.clamp(0.0, 2.0).toDouble() * 0.28);
    final paint = Paint()
      ..isAntiAlias = true
      ..filterQuality = FilterQuality.medium
      ..color = Colors.white
      ..colorFilter = ColorFilter.matrix([
        imageGain,
        0,
        0,
        0,
        0,
        0,
        imageGain,
        0,
        0,
        0,
        0,
        0,
        imageGain,
        0,
        0,
        0,
        0,
        0,
        1,
        0,
      ]);
    if (panorama.backgroundBlur > 0) {
      final sigma = panorama.backgroundBlur.clamp(0.0, 30.0).toDouble();
      paint.imageFilter = ui.ImageFilter.blur(sigmaX: sigma, sigmaY: sigma);
    }

    var remaining = sourceWidth;
    var dx = 0.0;
    var sx = sourceX;
    while (remaining > 0.01) {
      final part = math.min(remaining, image.width - sx);
      final dw = size.width * part / sourceWidth;
      canvas.drawImageRect(
        image,
        Rect.fromLTWH(sx, 0, part, image.height.toDouble()),
        Rect.fromLTWH(dx, 0, dw + 0.5, size.height),
        paint,
      );
      remaining -= part;
      dx += dw;
      sx = 0;
    }
  }

  void _paintPlanarFloorReflection(
    Canvas canvas,
    Size size,
    RenderCamera cam,
    SceneFrame frame,
  ) {
    final floor = _project(Vec3.zero, size, cam)?.dy;
    if (floor == null || !floor.isFinite) return;
    final opacity = (0.28 * (1 - scene.planarFloorRoughness))
        .clamp(0.02, 0.28)
        .toDouble();
    final chao = Rect.fromLTRB(0, floor, size.width, size.height);
    canvas.save();
    canvas.clipRect(chao);
    final blur = scene.planarFloorRoughness.clamp(0.0, 1.0).toDouble() * 10;
    // UM DESFOQUE PARA O REFLEXO INTEIRO. Antes cada triangulo espelhado
    // saia como um caminho com MaskFilter.blur — e no Impeller cada um
    // desses e um passe de desfoque proprio na GPU: uma camada, dois
    // passes gaussianos, uma composicao. Milhares de faces eram milhares
    // de passes por quadro, e o aparelho reiniciava. Agora o reflexo e um
    // unico drawVertices dentro de UMA camada desfocada.
    if (blur > 0) {
      canvas.saveLayer(
        chao,
        Paint()..imageFilter = ui.ImageFilter.blur(sigmaX: blur, sigmaY: blur),
      );
    }
    final tris = [...frame.opaque, ...frame.transparent];
    if (tris.isNotEmpty) {
      final positions = Float32List(tris.length * 6);
      final colors = Int32List(tris.length * 3);
      for (var k = 0; k < tris.length; k++) {
        final t = tris[k];
        positions[k * 6] = t.a.dx;
        positions[k * 6 + 1] = floor * 2 - t.a.dy;
        positions[k * 6 + 2] = t.b.dx;
        positions[k * 6 + 3] = floor * 2 - t.b.dy;
        positions[k * 6 + 4] = t.c.dx;
        positions[k * 6 + 5] = floor * 2 - t.c.dy;
        final cor = t.color.withValues(alpha: t.color.a * opacity).toARGB32();
        colors[k * 3] = cor;
        colors[k * 3 + 1] = cor;
        colors[k * 3 + 2] = cor;
      }
      canvas.drawVertices(
        ui.Vertices.raw(ui.VertexMode.triangles, positions, colors: colors),
        BlendMode.modulate,
        Paint()..color = const Color(0xFFFFFFFF),
      );
    }
    if (blur > 0) canvas.restore();
    canvas.restore();
  }

  /// SOMBRA DE CONTATO: a mancha escura debaixo de cada objeto.
  ///
  /// Nao e sombra projetada — nao ha mapa de profundidade aqui, e nao
  /// vale ter. O que a percepcao cobra e uma coisa so: saber se o objeto
  /// esta APOIADO ou flutuando. Sem nada embaixo, todo objeto parece
  /// colado no fundo, e e o que mais denuncia render amador.
  ///
  /// A mancha e uma elipse desfocada sob o ponto mais baixo do objeto,
  /// com o tamanho vindo da largura dele em tela e a opacidade caindo
  /// com a altura — objeto longe do chao lanca sombra maior e mais fraca,
  /// que e o que a sombra de verdade faz.
  void _paintContactShadows(Canvas canvas, SceneFrame frame) {
    // Agrupa por objeto: a caixa de cada um em coordenadas de tela.
    final caixas = <String, Rect>{};
    for (final tri in frame.opaque) {
      if (tri.nodeId.isEmpty) continue;
      final r = Rect.fromLTRB(
        math.min(tri.a.dx, math.min(tri.b.dx, tri.c.dx)),
        math.min(tri.a.dy, math.min(tri.b.dy, tri.c.dy)),
        math.max(tri.a.dx, math.max(tri.b.dx, tri.c.dx)),
        math.max(tri.a.dy, math.max(tri.b.dy, tri.c.dy)),
      );
      final antiga = caixas[tri.nodeId];
      caixas[tri.nodeId] = antiga == null ? r : antiga.expandToInclude(r);
    }
    if (caixas.isEmpty) return;

    // AS MANCHAS DE TODOS OS OBJETOS numa camada so, com UM desfoque.
    // Cada MaskFilter e um passe de GPU proprio; uma cena com centenas de
    // nos (modelo importado) virava centenas de passes por quadro.
    final ovais = <Rect>[];
    for (final r in caixas.values) {
      if (r.width < 2 || r.height < 2) continue;
      final largura = r.width * 0.62;
      final altura = math.max(4.0, r.width * 0.16);
      ovais.add(
        Rect.fromCenter(
          center: Offset(r.center.dx, r.bottom - altura * 0.35),
          width: largura,
          height: altura,
        ),
      );
    }
    if (ovais.isEmpty) return;
    var caixa = ovais.first;
    for (final o in ovais.skip(1)) {
      caixa = caixa.expandToInclude(o);
    }
    const sigma = 9.0;
    canvas.saveLayer(
      caixa.inflate(sigma * 3),
      Paint()..imageFilter = ui.ImageFilter.blur(sigmaX: sigma, sigmaY: sigma),
    );
    final tinta = Paint()..color = const Color(0x66000000);
    for (final o in ovais) {
      canvas.drawOval(o, tinta);
    }
    canvas.restore();
  }

  /// LINHAS DE PROFUNDIDADE ligando cada objeto ao plano do chao — e o
  /// que revela a altura de cada um numa vista ortografica.
  void _paintDepthLines(Canvas canvas, Size size, RenderCamera cam) {
    final paint = Paint()
      ..color = const Color(0x449F8CFF)
      ..strokeWidth = 1;
    for (final n in scene.nodes) {
      if (!n.visible) continue;
      final p = n.positionAt(time);
      final a = _project(p, size, cam);
      final b = _project(Vec3(p.x, 0, p.z), size, cam);
      if (a != null && b != null) canvas.drawLine(a, b, paint);
    }
  }

  /// VOLUME ENVOLVENTE do objeto selecionado.
  void _paintSelectionBox(Canvas canvas, Size size, RenderCamera cam) {
    for (final n in scene.nodes) {
      if (n.id != selectedNodeId) continue;
      final p = n.positionAt(time);
      final c = _project(p, size, cam);
      if (c == null) continue;
      final r = n.size * n.scale.valueAt(time);
      final rel = p - cam.position;
      final z = rel.dot(cameraBasis(cam).forward);
      final k = cam.orthographic
          ? cam.orthoScale
          : (size.width / 2 / math.tan(cam.fovRadians / 2)) / math.max(1, z);
      canvas.drawRect(
        Rect.fromCenter(center: c, width: r * 2 * k, height: r * 2 * k),
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.4
          ..color = const Color(0xCCB8FF3D),
      );
    }
  }

  Offset? _project(Vec3 world, Size size, RenderCamera cam) {
    final basis = cameraBasis(cam);
    final rel = world - cam.position;
    final z = rel.dot(basis.forward);
    if (z <= cam.near) return null;
    final x = rel.dot(basis.right);
    final y = rel.dot(basis.up);
    final halfW = size.width / 2;
    final halfH = size.height / 2;
    if (cam.orthographic) {
      return Offset(halfW + x * cam.orthoScale, halfH - y * cam.orthoScale);
    }
    final k = (halfW / math.tan(cam.fovRadians / 2)) / z;
    return Offset(halfW + x * k, halfH - y * k);
  }

  void _paintTriangles(Canvas canvas, List<RenderTri> tris) {
    if (tris.isEmpty) return;
    // O contorno suavizado so ate um teto de faces: ver _tetoDoContorno.
    final contornar = scene.msaa && tris.length <= _tetoDoContorno;
    // Uma chamada de desenho por LOTE de mesma cor: drawVertices e o
    // mais proximo de instanciacao que o Canvas oferece.
    final paint = Paint()
      ..isAntiAlias = scene.msaa
      ..style = PaintingStyle.fill;
    final cache = TextureCache.instance;

    // Face de cor lisa — ou face com imagem que ainda nao chegou, que
    // sai lisa ate a imagem carregar.
    bool lisa(RenderTri t) =>
        t.texture == null || cache.imageFor(t.texture!) == null;

    var i = 0;
    while (i < tris.length) {
      final start = i;
      if (!lisa(tris[i])) {
        final tex = tris[i].texture;
        final wrapX = tris[i].wrapX, wrapY = tris[i].wrapY;
        while (i < tris.length &&
            tris[i].texture == tex &&
            tris[i].wrapX == wrapX &&
            tris[i].wrapY == wrapY) {
          i++;
        }
        final img = cache.imageFor(tex!)!;
        if (scene.fogDensity <= 0) {
          _paintTextured(canvas, tris, start, i, img, paint);
          continue;
        }
        // NEBLINA SOBRE A IMAGEM, em fatias do lote: ver _paintFog.
        for (var de = start; de < i; de += _fatiaDaNeblina) {
          final ate = math.min(i, de + _fatiaDaNeblina);
          _paintTextured(canvas, tris, de, ate, img, paint);
          _paintFog(canvas, tris, de, ate, paint);
        }
        continue;
      }
      final color = tris[i].color;
      while (i < tris.length && tris[i].color == color && lisa(tris[i])) {
        i++;
      }
      final count = i - start;
      final positions = Float32List(count * 6);
      final colors = Int32List(count * 3);
      var smooth = false;
      for (var k = 0; k < count; k++) {
        final t = tris[start + k];
        positions[k * 6] = t.a.dx;
        positions[k * 6 + 1] = t.a.dy;
        positions[k * 6 + 2] = t.b.dx;
        positions[k * 6 + 3] = t.b.dy;
        positions[k * 6 + 4] = t.c.dx;
        positions[k * 6 + 5] = t.c.dy;
        colors[k * 3] = (t.colorA ?? t.color).toARGB32();
        colors[k * 3 + 1] = (t.colorB ?? t.color).toARGB32();
        colors[k * 3 + 2] = (t.colorC ?? t.color).toARGB32();
        smooth = smooth || t.colorA != null;
      }
      paint.color = const Color(0xFFFFFFFF);
      canvas.drawVertices(
        ui.Vertices.raw(ui.VertexMode.triangles, positions, colors: colors),
        BlendMode.modulate,
        paint,
      );

      // BORDA SUAVIZADA. `drawVertices` NAO suaviza: os triangulos saem
      // com a escada de pixel na silhueta, e serrilhado e o que mais
      // denuncia um render. Contornar o mesmo lote com um traco fino da
      // MESMA cor, esse sim suavizado, cobre o degrau — e de quebra
      // fecha as costuras de meio pixel entre triangulos vizinhos.
      if (contornar && !smooth) {
        final contorno = Path();
        for (var k = 0; k < count; k++) {
          final t = tris[start + k];
          contorno
            ..moveTo(t.a.dx, t.a.dy)
            ..lineTo(t.b.dx, t.b.dy)
            ..lineTo(t.c.dx, t.c.dy)
            ..close();
        }
        canvas.drawPath(
          contorno,
          Paint()
            ..isAntiAlias = true
            ..style = PaintingStyle.stroke
            ..strokeWidth = 0.9
            ..strokeJoin = StrokeJoin.round
            ..color = color,
        );
      }
    }
  }

  /// QUANTAS FACES COM IMAGEM entram por fatia quando ha neblina.
  ///
  /// A neblina de uma face com imagem e um segundo desenho por cima dela
  /// (a cor do vertice multiplica a textura; nao da para misturar a
  /// neblina nela). Um segundo desenho do LOTE INTEIRO pintaria a
  /// neblina de uma face de tras por cima de uma face da frente — e a
  /// diferenca entre as duas pode ser grande num objeto fundo. Um
  /// segundo desenho POR FACE seria certo, mas sao duas chamadas por
  /// triangulo. A fatia e o meio: as faces vem ordenadas por
  /// profundidade, entao 64 vizinhas tem profundidade quase igual, e a
  /// neblina de uma sobre a outra e a mesma a olho.
  static const int _fatiaDaNeblina = 64;

  /// ATE QUANTAS FACES o contorno suavizado vale a pena.
  ///
  /// O contorno e um caminho com um subcaminho fechado por triangulo,
  /// tracado com junta redonda. Cada junta vira dezenas de vertices na
  /// tesselacao; num modelo importado de cem mil faces isso e milhoes de
  /// vertices POR QUADRO so para esconder o degrau da silhueta — que,
  /// nessa densidade, ninguem ve. Acima do teto o contorno sai; abaixo
  /// dele (todo elemento 3D primitivo, todo modelo leve) continua.
  static const int _tetoDoContorno = 4000;

  /// A NEBLINA das faces com imagem: o mesmo trecho do lote, pintado de
  /// novo com a cor da neblina e o alfa de cada vertice, por cima.
  ///
  /// O que NAO pode e o que havia aqui antes: um saveLayer sem limites
  /// POR TRIANGULO, para a neblina cair so nos pixels daquela face. Cada
  /// saveLayer e um alvo de render do tamanho do palco, mais um passe de
  /// GPU e uma composicao; com um modelo de alguns milhares de faces com
  /// imagem eram milhares de passes por quadro, e o iPhone reiniciava —
  /// o driver da GPU desiste e o sistema cai junto.
  ///
  /// Sem camada, a neblina entra com srcATop NO CANVAS: so pinta onde ja
  /// ha pixel e mantem o alfa que estava la. Sobre fundo transparente e
  /// exatamente o resultado da camada (o alfa da textura sobrevive). A
  /// unica diferenca e sobre fundo opaco, nos texels transparentes de um
  /// recorte: ali a neblina desta face cai no que esta atras — que esta
  /// mais longe e ja e mais enevoado, entao a olho e a mesma cor. E esse
  /// e o preco certo.
  void _paintFog(
    Canvas canvas,
    List<RenderTri> tris,
    int start,
    int end,
    Paint paint,
  ) {
    final count = end - start;
    if (count <= 0) return;
    final positions = Float32List(count * 6);
    final colors = Int32List(count * 3);
    final fog = scene.fogColor;
    for (var k = 0; k < count; k++) {
      final t = tris[start + k];
      positions[k * 6] = t.a.dx;
      positions[k * 6 + 1] = t.a.dy;
      positions[k * 6 + 2] = t.b.dx;
      positions[k * 6 + 3] = t.b.dy;
      positions[k * 6 + 4] = t.c.dx;
      positions[k * 6 + 5] = t.c.dy;
      colors[k * 3] = fog.withValues(alpha: t.fogA).toARGB32();
      colors[k * 3 + 1] = fog.withValues(alpha: t.fogB).toARGB32();
      colors[k * 3 + 2] = fog.withValues(alpha: t.fogC).toARGB32();
    }
    paint
      ..shader = null
      ..color = const Color(0xFFFFFFFF)
      ..blendMode = BlendMode.srcATop;
    canvas.drawVertices(
      ui.Vertices.raw(ui.VertexMode.triangles, positions, colors: colors),
      BlendMode.modulate,
      paint,
    );
    paint.blendMode = BlendMode.srcOver;
  }

  /// FACES COM IMAGEM: a imagem entra como shader e a luz como cor por
  /// vertice, multiplicadas. E o mesmo drawVertices — uma chamada por
  /// lote de mesma imagem — com coordenadas de textura em pixels.
  void _paintTextured(
    Canvas canvas,
    List<RenderTri> tris,
    int start,
    int end,
    ui.Image img,
    Paint paint,
  ) {
    final count = end - start;
    final positions = Float32List(count * 6);
    final coords = Float32List(count * 6);
    final colors = Int32List(count * 3);
    final w = img.width.toDouble(), h = img.height.toDouble();
    for (var k = 0; k < count; k++) {
      final t = tris[start + k];
      positions[k * 6] = t.a.dx;
      positions[k * 6 + 1] = t.a.dy;
      positions[k * 6 + 2] = t.b.dx;
      positions[k * 6 + 3] = t.b.dy;
      positions[k * 6 + 4] = t.c.dx;
      positions[k * 6 + 5] = t.c.dy;
      final ua = t.uvA ?? Offset.zero;
      final ub = t.uvB ?? Offset.zero;
      final uc = t.uvC ?? Offset.zero;
      coords[k * 6] = ua.dx * w;
      coords[k * 6 + 1] = ua.dy * h;
      coords[k * 6 + 2] = ub.dx * w;
      coords[k * 6 + 3] = ub.dy * h;
      coords[k * 6 + 4] = uc.dx * w;
      coords[k * 6 + 5] = uc.dy * h;
      colors[k * 3] = (t.colorA ?? t.color).toARGB32();
      colors[k * 3 + 1] = (t.colorB ?? t.color).toARGB32();
      colors[k * 3 + 2] = (t.colorC ?? t.color).toARGB32();
    }
    paint
      ..filterQuality = FilterQuality.medium
      ..color = const Color(0xFFFFFFFF)
      ..shader = ui.ImageShader(
        img,
        tris[start].wrapX,
        tris[start].wrapY,
        Matrix4.identity().storage,
      );
    canvas.drawVertices(
      ui.Vertices.raw(
        ui.VertexMode.triangles,
        positions,
        textureCoordinates: coords,
        colors: colors,
      ),
      BlendMode.modulate,
      paint,
    );
    paint.shader = null;
  }

  /// BOKEH: cada ponto de luz fora de foco vira o FORMATO DA IRIS.
  /// Sem ganho e limiar de realce isto seria um borrao cinza; com eles,
  /// vira a bola brilhante que a gente reconhece como fotografia.
  void _paintBokeh(Canvas canvas, SceneFrame frame) {
    final dof = camera.dof;
    final sprites = bokehSprites(frame, dof, time);
    if (sprites.isEmpty) return;

    final rot = dof.irisRotation.valueAt(time);
    final round = dof.irisRoundness.valueAt(time);
    final aspect = dof.irisAspect.valueAt(time);
    final fringe = dof.diffractionFringe.valueAt(time).clamp(0.0, 100.0);

    for (final s in sprites) {
      canvas.save();
      canvas.translate(s.center.dx, s.center.dy);
      final path = irisPath(
        dof.irisShape,
        s.radius,
        roundness: round,
        rotationDeg: rot,
        aspect: aspect,
      );
      canvas.drawPath(
        path,
        Paint()
          ..color = s.color.withValues(alpha: 0.55)
          ..blendMode = BlendMode.plus
          ..isAntiAlias = scene.msaa,
      );
      // FRANJA DE DIFRACAO: o anel brilhante na borda da bola.
      if (fringe > 0) {
        canvas.drawPath(
          path,
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = math.max(1, s.radius * 0.12)
            ..color = s.color.withValues(alpha: 0.25 + fringe / 200)
            ..blendMode = BlendMode.plus,
        );
      }
      canvas.restore();
    }
  }

  /// GRADE DO CHAO no plano Y=0, com desvanecimento pela distancia —
  /// e o que da nocao de escala.
  void _paintFloorGrid(Canvas canvas, Size size, RenderCamera cam) {
    final basis = cameraBasis(cam);
    final halfW = size.width / 2;
    final halfH = size.height / 2;
    final focalPx = halfW / math.tan(cam.fovRadians / 2);

    Offset? project(Vec3 world) {
      final rel = world - cam.position;
      final z = rel.dot(basis.forward);
      if (z <= cam.near) return null;
      final x = rel.dot(basis.right);
      final y = rel.dot(basis.up);
      if (cam.orthographic) {
        return Offset(halfW + x * cam.orthoScale, halfH - y * cam.orthoScale);
      }
      final k = focalPx / z;
      return Offset(halfW + x * k, halfH - y * k);
    }

    const step = 120.0;
    const lines = 10;
    for (var i = -lines; i <= lines; i++) {
      final d = i * step;
      final fade = (1 - (i.abs() / lines)).clamp(0.0, 1.0) * 0.25;
      final paint = Paint()
        ..color = Colors.white.withValues(alpha: fade)
        ..strokeWidth = 1;
      final a1 = project(Vec3(d, 0, -lines * step));
      final b1 = project(Vec3(d, 0, lines * step));
      if (a1 != null && b1 != null) canvas.drawLine(a1, b1, paint);
      final a2 = project(Vec3(-lines * step, 0, d));
      final b2 = project(Vec3(lines * step, 0, d));
      if (a2 != null && b2 != null) canvas.drawLine(a2, b2, paint);
    }
  }

  /// FRUSTUM da camera desenhado nas vistas ortograficas, com o plano
  /// de foco marcado quando a profundidade de campo esta ligada.
  void _paintFrustum(Canvas canvas, Size size, RenderCamera view) {
    final basis = cameraBasis(view);
    final halfW = size.width / 2;
    final halfH = size.height / 2;

    Offset? project(Vec3 world) {
      final rel = world - view.position;
      final z = rel.dot(basis.forward);
      if (z <= view.near) return null;
      final x = rel.dot(basis.right);
      final y = rel.dot(basis.up);
      return Offset(halfW + x * view.orthoScale, halfH - y * view.orthoScale);
    }

    final camPos = camera.positionAt(time);
    final camFwd = camera.forwardAt(time);
    final fov = camera.fovAt(time) * math.pi / 180;
    final spread = math.tan(fov / 2);
    final rightAxis = camFwd.cross(const Vec3(0, 1, 0)).normalized;
    final upAxis = rightAxis.cross(camFwd).normalized;

    final paint = Paint()
      ..color = const Color(0xAAB8FF3D)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5;

    const far = 900.0;
    final center = camPos + camFwd * far;
    final corners = <Vec3>[
      center + rightAxis * (far * spread) + upAxis * (far * spread * 0.6),
      center - rightAxis * (far * spread) + upAxis * (far * spread * 0.6),
      center - rightAxis * (far * spread) - upAxis * (far * spread * 0.6),
      center + rightAxis * (far * spread) - upAxis * (far * spread * 0.6),
    ];
    final apex = project(camPos);
    final projected = [for (final c in corners) project(c)];
    if (apex != null) {
      for (final p in projected) {
        if (p != null) canvas.drawLine(apex, p, paint);
      }
      canvas.drawCircle(apex, 5, Paint()..color = const Color(0xFFB8FF3D));
    }
    for (var i = 0; i < 4; i++) {
      final a = projected[i];
      final b = projected[(i + 1) % 4];
      if (a != null && b != null) canvas.drawLine(a, b, paint);
    }

    // PLANO DE FOCO: ajustar foco olhando para ele e muito mais facil
    // do que olhar numero.
    if (camera.dof.enabled) {
      final fd = camera.dof.focusDistance.valueAt(time);
      final fc = camPos + camFwd * fd;
      final w = fd * spread;
      final p1 = project(fc + rightAxis * w);
      final p2 = project(fc - rightAxis * w);
      if (p1 != null && p2 != null) {
        canvas.drawLine(
          p1,
          p2,
          Paint()
            ..color = const Color(0xCCFFB020)
            ..strokeWidth = 2.5,
        );
      }
    }
  }

  @override
  bool shouldRepaint(Scene3DPainter old) =>
      helpersOnly != old.helpersOnly ||
      old.showModelRig != showModelRig ||
      old.scene != scene ||
      old.camera != camera ||
      old.resolvedCamera != resolvedCamera ||
      old.view != view ||
      old.time != time ||
      old.showHelpers != showHelpers ||
      old.overrideCamera != overrideCamera ||
      old.selectedNodeId != selectedNodeId;
}

/// MINI-VISTA (camera §3.3): numa tela de seis polegadas, quatro
/// janelas sao inuteis. Uma janelinha com a vista de TOPO — mostrando
/// onde a camera esta, para onde aponta e onde os objetos estao em
/// profundidade — resolve 90% do que as quatro vistas resolvem, em 25%
/// do espaco.
class MiniViewPainter extends CustomPainter {
  const MiniViewPainter({
    required this.scene,
    required this.camera,
    required this.time,
    this.view = SceneView.top,
  });

  final Scene3D scene;
  final Camera3D camera;
  final Duration time;
  final SceneView view;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRRect(
      RRect.fromRectAndRadius(Offset.zero & size, const Radius.circular(8)),
      Paint()..color = const Color(0xCC0B0E12),
    );

    // Enquadra a cena e a camera juntas, olhando de cima.
    final bounds = sceneBounds(scene, time);
    final camPos = camera.positionAt(time);
    var extent = math.max(bounds.radius * 1.4, 300.0);
    final camDist = Vec3(
      camPos.x - bounds.center.x,
      0,
      camPos.z - bounds.center.z,
    ).length;
    extent = math.max(extent, camDist * 1.2);
    final k = math.min(size.width, size.height) / (extent * 2);

    Offset toScreen(double x, double z) => Offset(
      size.width / 2 + (x - bounds.center.x) * k,
      size.height / 2 + (z - bounds.center.z) * k,
    );

    // Objetos como pontos.
    for (final n in scene.nodes) {
      final p = n.positionAt(time);
      canvas.drawCircle(
        toScreen(p.x, p.z),
        math.max(2, n.size * n.scale.valueAt(time) * k * 0.5),
        Paint()..color = n.material.baseColor.withValues(alpha: 0.9),
      );
    }

    // Camera e seu frustum, vistos de cima.
    final fwd = camera.forwardAt(time);
    final camPt = toScreen(camPos.x, camPos.z);
    final fov = camera.fovAt(time) * math.pi / 180;
    final dir = math.atan2(fwd.x, fwd.z);
    final len = math.max(size.width, size.height) * 0.5;
    final left = dir - fov / 2;
    final right = dir + fov / 2;
    final cone = Path()
      ..moveTo(camPt.dx, camPt.dy)
      ..lineTo(camPt.dx + math.sin(left) * len, camPt.dy + math.cos(left) * len)
      ..lineTo(
        camPt.dx + math.sin(right) * len,
        camPt.dy + math.cos(right) * len,
      )
      ..close();
    canvas.drawPath(cone, Paint()..color = const Color(0x33B8FF3D));
    canvas.drawCircle(camPt, 4, Paint()..color = const Color(0xFFB8FF3D));
  }

  @override
  bool shouldRepaint(MiniViewPainter old) =>
      old.scene != scene ||
      old.camera != camera ||
      old.time != time ||
      old.view != view;
}

/// EIXOS DE REFERENCIA TOCAVEIS (camera §3.4): mostram a orientacao E
/// navegam — tocar no X vai para a vista lateral, no Y para o topo.
class AxisGizmo extends StatelessWidget {
  const AxisGizmo({
    super.key,
    required this.camera,
    required this.time,
    required this.onView,
    this.size = 62,
  });

  final Camera3D camera;
  final Duration time;
  final ValueChanged<SceneView> onView;
  final double size;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: size,
      height: size,
      child: Stack(
        children: [
          CustomPaint(
            size: Size(size, size),
            painter: _AxisPainter(camera: camera, time: time),
          ),
          // Areas tocaveis por eixo.
          Positioned(
            left: 0,
            top: size / 2 - 11,
            child: _AxisTap(
              label: 'X',
              color: const Color(0xFFE85B81),
              onTap: () => onView(SceneView.right),
            ),
          ),
          Positioned(
            left: size / 2 - 11,
            top: 0,
            child: _AxisTap(
              label: 'Y',
              color: const Color(0xFF2BE3A0),
              onTap: () => onView(SceneView.top),
            ),
          ),
          Positioned(
            right: 0,
            bottom: 0,
            child: _AxisTap(
              label: 'Z',
              color: const Color(0xFF35C4E7),
              onTap: () => onView(SceneView.front),
            ),
          ),
        ],
      ),
    );
  }
}

class _AxisTap extends StatelessWidget {
  const _AxisTap({
    required this.label,
    required this.color,
    required this.onTap,
  });

  final String label;
  final Color color;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 22,
        height: 22,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: color.withValues(alpha: 0.22),
          shape: BoxShape.circle,
          border: Border.all(color: color, width: 1.2),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontSize: 10,
            fontWeight: FontWeight.w700,
            color: color,
          ),
        ),
      ),
    );
  }
}

class _AxisPainter extends CustomPainter {
  const _AxisPainter({required this.camera, required this.time});

  final Camera3D camera;
  final Duration time;

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final rc = camera.renderAt(time);
    final basis = cameraBasis(rc);
    final r = size.width / 2 - 12;

    void axis(Vec3 dir, Color color) {
      final x = dir.dot(basis.right);
      final y = dir.dot(basis.up);
      canvas.drawLine(
        center,
        center + Offset(x * r, -y * r),
        Paint()
          ..color = color.withValues(alpha: 0.85)
          ..strokeWidth = 2,
      );
    }

    axis(const Vec3(1, 0, 0), const Color(0xFFE85B81));
    axis(const Vec3(0, 1, 0), const Color(0xFF2BE3A0));
    axis(const Vec3(0, 0, 1), const Color(0xFF35C4E7));
  }

  @override
  bool shouldRepaint(_AxisPainter old) =>
      old.camera != camera || old.time != time;
}
