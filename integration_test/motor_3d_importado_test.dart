// O MOTOR 3D DESENHA UM MODELO IMPORTADO? MEDIDO NO APARELHO.
//
// A pergunta do dono era binaria — "a viewport fica preta e o objeto nao
// aparece" — e a resposta nao pode ser "deve funcionar". Este arquivo desenha
// de verdade, pelo caminho de producao (a ponte do `Motor3DNativo`), e CONTA
// OS PIXELS que sairam do fundo.
//
// DOIS CASOS, DE PROPOSITO:
//
//   * o CUBO de dentro do aplicativo, sem arquivo nenhum. Ele separa o
//     PROBLEMA DO RENDERIZADOR do problema do importador: se o cubo nao
//     aparecer, nada que venha de arquivo tem chance;
//   * o GLB lido pelo importador de producao, montado pelo mesmo caminho
//     que o botao "Importar modelo" usa.
//
// Rodar:
//   flutter test integration_test/motor_3d_importado_test.dart -d emulator-5554
//
// O EMULADOR NAO MEDE DESEMPENHO (SwiftShader desenha por software). O que
// se mede aqui e COBERTURA — quantos pixels de objeto existem, e de que cor —
// e isso nao depende de a placa ser rapida.
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/fonte_truetype.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d_animado.dart';
import 'package:aurea/src/features/editor/domain/model_import3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart'
    show estado3DDoQuadro;
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:integration_test/integration_test.dart';

// ------------------------------------------------------------- um GLB real

Uint8List _glb(Map<String, dynamic> gltf, Uint8List bin) {
  Uint8List pad(Uint8List d, int fill) {
    final resto = (4 - (d.length % 4)) % 4;
    if (resto == 0) return d;
    return Uint8List.fromList([...d, ...List.filled(resto, fill)]);
  }

  final json = pad(Uint8List.fromList(utf8.encode(jsonEncode(gltf))), 0x20);
  final binPad = pad(bin, 0);
  final total = 12 + 8 + json.length + 8 + binPad.length;
  final out = BytesBuilder();
  out.add(
    (ByteData(12)
          ..setUint32(0, 0x46546C67, Endian.little)
          ..setUint32(4, 2, Endian.little)
          ..setUint32(8, total, Endian.little))
        .buffer
        .asUint8List(),
  );
  out
    ..add(
      (ByteData(8)
            ..setUint32(0, json.length, Endian.little)
            ..setUint32(4, 0x4E4F534A, Endian.little))
          .buffer
          .asUint8List(),
    )
    ..add(json)
    ..add(
      (ByteData(8)
            ..setUint32(0, binPad.length, Endian.little)
            ..setUint32(4, 0x004E4942, Endian.little))
          .buffer
          .asUint8List(),
    )
    ..add(binPad);
  return out.toBytes();
}

/// Um cubo fechado com POSITION, NORMAL e TEXCOORD_0 e um material dourado.
/// Mesma convencao do glTF: triangulo anti-horario visto de FORA.
({Map<String, dynamic> gltf, Uint8List bin}) _cuboTex() {
  final pos = <double>[];
  final nor = <double>[];
  final uv = <double>[];
  final idx = <int>[];
  // Seis faces, uma por eixo/sinal. O eixo e o sinal saem do indice.
  const eixos = [
    [1, 0.0, 0.0, 1.0], // +X
    [1, 0.0, 0.0, -1.0],
    [0, 1.0, 0.0, 1.0], // +Y
    [0, 1.0, 0.0, -1.0],
    [2, 0.0, 1.0, 1.0], // +Z
    [2, 0.0, 1.0, -1.0],
  ];
  for (final e in eixos) {
    final eixo = e[0].toInt();
    final sinal = e[3].toDouble();
    final a = (eixo + 1) % 3;
    final b = (eixo + 2) % 3;
    final n = <double>[0.0, 0.0, 0.0];
    n[eixo] = sinal;
    // O enrolamento depende do sinal: sem isso, metade das faces fica
    // virada para dentro e o cubo aparece oco.
    final cantos = sinal > 0
        ? [
            [-1.0, -1.0],
            [1.0, -1.0],
            [1.0, 1.0],
            [-1.0, 1.0],
          ]
        : [
            [-1.0, 1.0],
            [1.0, 1.0],
            [1.0, -1.0],
            [-1.0, -1.0],
          ];
    for (final c in cantos) {
      final v = <double>[0.0, 0.0, 0.0];
      v[eixo] = sinal;
      v[a] = c[0];
      v[b] = c[1];
      pos.addAll(v);
      nor.addAll(n);
      uv.addAll([(c[0] + 1) / 2, (c[1] + 1) / 2]);
    }
    final o = idx.length ~/ 6 * 4;
    idx.addAll([o, o + 1, o + 2, o, o + 2, o + 3]);
  }
  final bin = Uint8List.fromList([
    ...Float32List.fromList(pos).buffer.asUint8List(),
    ...Float32List.fromList(nor).buffer.asUint8List(),
    ...Float32List.fromList(uv).buffer.asUint8List(),
    ...Uint16List.fromList(idx).buffer.asUint8List(),
  ]);
  final nv = pos.length ~/ 3;
  final gltf = <String, dynamic>{
    'asset': {'version': '2.0'},
    'scene': 0,
    'scenes': [
      {'nodes': [0]},
    ],
    'nodes': [
      {'name': 'CuboImportado', 'mesh': 0},
    ],
    'meshes': [
      {
        'name': 'CuboImportado',
        'primitives': [
          {
            'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
            'indices': 3,
            'material': 0,
          },
        ],
      },
    ],
    'materials': [
      {
        'name': 'Dourado',
        'pbrMetallicRoughness': {
          'baseColorFactor': [1.0, 0.766, 0.336, 1.0],
          'metallicFactor': 1.0,
          'roughnessFactor': 0.25,
        },
      },
    ],
    'buffers': [
      {'byteLength': bin.length},
    ],
    'bufferViews': [
      {'buffer': 0, 'byteOffset': 0, 'byteLength': nv * 12},
      {'buffer': 0, 'byteOffset': nv * 12, 'byteLength': nv * 12},
      {
        'buffer': 0,
        'byteOffset': nv * 24,
        'byteLength': nv * 8,
      },
      {
        'buffer': 0,
        'byteOffset': nv * 32,
        'byteLength': idx.length * 2,
      },
    ],
    'accessors': [
      {
        'bufferView': 0,
        'componentType': 5126,
        'count': nv,
        'type': 'VEC3',
        'min': [-1, -1, -1],
        'max': [1, 1, 1],
      },
      {
        'bufferView': 1,
        'componentType': 5126,
        'count': nv,
        'type': 'VEC3',
      },
      {
        'bufferView': 2,
        'componentType': 5126,
        'count': nv,
        'type': 'VEC2',
      },
      {
        'bufferView': 3,
        'componentType': 5123,
        'count': idx.length,
        'type': 'SCALAR',
      },
    ],
  };
  return (gltf: gltf, bin: bin);
}

// ------------------------------------------------------------- a medida

/// O QUE A IMAGEM TEM: quantos pixels sairam do fundo, o quanto o objeto
/// cobre do quadro, a cor media do que apareceu e a CAIXA do que apareceu
/// (a silhueta — e por ela que se ve um objeto girar de lado).
class Medida {
  Medida(this.diferentes, this.total, this.media, {this.largura = 0, this.altura = 0});

  final int diferentes;
  final int total;
  final (int, int, int) media;
  final int largura;
  final int altura;

  double get cobertura => total == 0 ? 0 : diferentes / total;

  @override
  String toString() =>
      '$diferentes/$total pixels (${(cobertura * 100).toStringAsFixed(1)}%) '
      'caixa ${largura}x$altura cor media rgb$media';
}

/// GUARDA O QUADRO NO APARELHO, para o dono poder OLHAR o antes e o depois
/// (`adb pull /sdcard/Download/aurea_giro_*.png`). Nao poder salvar nao
/// derruba a medida.
Future<void> _salvar(ui.Image imagem, String nome) async {
  try {
    final png = await imagem.toByteData(format: ui.ImageByteFormat.png);
    if (png == null) return;
    File('/sdcard/Download/$nome').writeAsBytesSync(png.buffer.asUint8List());
    // ignore: avoid_print
    print('  PNG salvo em /sdcard/Download/$nome');
  } catch (e) {
    // ignore: avoid_print
    print('  nao deu para salvar o PNG: $e');
  }
}

Future<Medida> _medir(ui.Image imagem, (int, int, int) fundo) async {
  final dados = await imagem.toByteData(format: ui.ImageByteFormat.rawRgba);
  final bytes = dados!.buffer.asUint8List();
  final largura = imagem.width;
  var diferentes = 0;
  var sr = 0, sg = 0, sb = 0;
  var minX = largura, maxX = -1, minY = imagem.height, maxY = -1;
  for (var i = 0; i + 3 < bytes.length; i += 4) {
    final r = bytes[i], g = bytes[i + 1], b = bytes[i + 2];
    final dr = (r - fundo.$1).abs();
    final dg = (g - fundo.$2).abs();
    final db = (b - fundo.$3).abs();
    if (dr + dg + db <= 6) continue;
    diferentes++;
    sr += r;
    sg += g;
    sb += b;
    final p = i ~/ 4;
    final x = p % largura, y = p ~/ largura;
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
  }
  if (diferentes == 0) return Medida(0, bytes.length ~/ 4, (0, 0, 0));
  return Medida(
    diferentes,
    bytes.length ~/ 4,
    (sr ~/ diferentes, sg ~/ diferentes, sb ~/ diferentes),
    largura: maxX - minX + 1,
    altura: maxY - minY + 1,
  );
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  setUpAll(() {
    expect(
      Motor3D.disponivel,
      isTrue,
      reason: 'esta versao nao tem a porta 3D compilada',
    );
    final subiu = Motor3D.preparar();
    // ignore: avoid_print
    print('MOTOR preparar=$subiu pronto=${Motor3D.pronto} '
        'backend="${Motor3D.backend}" motivo="${Motor3D.motivo}"');
    expect(Motor3D.pronto, isTrue, reason: Motor3D.motivo);
  });

  /// Desenha uma cena de um objeto so e devolve a imagem. [camera] e a
  /// MESMA que o aplicativo usa numa camada 3D nova.
  Future<ui.Image> desenhar(Scene3D cena, RenderCamera camera) async {
    final motor = Motor3DNativo.instance;
    expect(motor.ligado, isTrue, reason: motor.motivo);
    motor.montar(
      cena: cena,
      camera: camera,
      local: Duration.zero,
      largura: 512,
      altura: 512,
      aspectoDaComposicao: 1,
      sombra: 0,
      amostras: 1,
    );
    final imagem = await motor.quadroEsperando('diag-${cena.nodes.first.id}');
    expect(imagem, isNotNull, reason: 'o motor nao devolveu imagem nenhuma');
    final numeros = motor.numeros;
    // ignore: avoid_print
    print('  NUMEROS camadas=${numeros.camadas} desenhadas='
        '${numeros.desenhadas} fora=${numeros.foraDoCampo} '
        'semModelo=${numeros.semModelo} triangulos=${numeros.triangulos}');
    // ignore: avoid_print
    print('  ERRO="${Motor3D.ultimoErro}" stats=${Motor3D.estatisticas}');
    final c = motor.ultimaCena.camadas.isEmpty
        ? null
        : motor.ultimaCena.camadas.first;
    if (c != null) {
      // ignore: avoid_print
      print('  CAMADA modelo=${c.modelo} escala=${c.escalaX} '
          'pos=(${c.posicaoX},${c.posicaoY},${c.posicaoZ}) '
          'visivel=${c.visivel}');
    }
    // ignore: avoid_print
    print('  CAMERA pos=(${motor.ultimaCena.camera.posicaoX},'
        '${motor.ultimaCena.camera.posicaoY},'
        '${motor.ultimaCena.camera.posicaoZ}) '
        'alvo=(${motor.ultimaCena.camera.alvoX},'
        '${motor.ultimaCena.camera.alvoY},'
        '${motor.ultimaCena.camera.alvoZ}) '
        'fov=${motor.ultimaCena.camera.fovGraus} '
        'perto=${motor.ultimaCena.camera.perto} '
        'longe=${motor.ultimaCena.camera.longe}');
    final d = await imagem!.toByteData(format: ui.ImageByteFormat.rawRgba);
    final b = d!.buffer.asUint8List();
    var minA = 255, maxA = 0, maxC = 0;
    for (var i = 0; i + 3 < b.length; i += 4) {
      if (b[i + 3] < minA) minA = b[i + 3];
      if (b[i + 3] > maxA) maxA = b[i + 3];
      for (var k = 0; k < 3; k++) {
        if (b[i + k] > maxC) maxC = b[i + k];
      }
    }
    // ignore: avoid_print
    print('  PIXELS alfa=$minA..$maxA maiorCanal=$maxC '
        'centro=${b[(256 * 512 + 256) * 4]}');
    final linhas = StringBuffer();
    for (var gy = 0; gy < 9; gy++) {
      final linha = StringBuffer();
      for (var gx = 0; gx < 9; gx++) {
        final x = (gx * 511) ~/ 8, y = (gy * 511) ~/ 8;
        final i = (y * 512 + x) * 4;
        final v = (b[i] + b[i + 1] + b[i + 2]) ~/ 3;
        linha.write(v >= 200
            ? '#'
            : v >= 120
            ? '+'
            : v >= 40
            ? '.'
            : b[i + 3] < 8
            ? ' '
            : ':');
      }
      linhas.writeln('  |$linha|');
    }
    // ignore: avoid_print
    print(linhas.toString().trimRight());
    return imagem;
  }

  test('o cubo de dentro do aplicativo desenha na placa', () async {
    final motor = Motor3DNativo.instance;
    final cena = Scene3D(
      nodes: [SceneNode(name: 'Cubo', kind: Element3DKind.cube, size: 120)],
      lights: Scene3D.tresPontos,
      ambient: 0.28,
    );
    final imagem = await desenhar(
      cena,
      const RenderCamera(position: Vec3(0, 0, 420), target: Vec3(0, 0, 0)),
    );
    final m = await _medir(imagem, (0, 0, 0));
    // ignore: avoid_print
    print('CUBO NATIVO: $m');
    // O CUBO A 420 UNIDADES COM 240 DE LADO ENCHE O QUADRO: a face da frente
    // fica a 300 do olho e e maior do que o cone nessa distancia.
    expect(m.cobertura, greaterThan(0.5), reason: 'o cubo do catalogo nao desenhou');
    motor.limpar();
  });

  test('o GLB importado desenha, com o material do arquivo', () async {
    final motor = Motor3DNativo.instance;
    final f = _cuboTex();
    final asset = importGltf3D(_glb(f.gltf, f.bin));
    // ignore: avoid_print
    print('IMPORTADO nome="${asset.name}" prims=${asset.primitives.length} '
        'triangulos=${asset.triangleCount} avisos=${asset.warnings}');
    expect(asset.triangleCount, greaterThan(0));
    final cena = Scene3D(
      nodes: [
        SceneNode(name: asset.name, modelAsset: asset, size: 120),
      ],
      lights: Scene3D.tresPontos,
      ambient: 0.28,
      // O AMBIENTE METALICO E O QUE O APLICATIVO USA AO IMPORTAR.
      environment: EnvironmentKind.estudioMetal,
      envReflect: 0.9,
    );
    final imagem = await desenhar(
      cena,
      const RenderCamera(position: Vec3(0, 0, 420), target: Vec3(0, 0, 0)),
    );
    final m = await _medir(imagem, (0, 0, 0));
    // ignore: avoid_print
    print('GLB IMPORTADO: $m');
    expect(m.cobertura, greaterThan(0.5), reason: 'o modelo importado nao desenhou');
    // O MATERIAL DO ARQUIVO CHEGOU: o cubo e dourado (1.0, 0.766, 0.336), e
    // um objeto sem material sairia cinza — o vermelho tem de ganhar do azul.
    expect(
      m.media.$1,
      greaterThan(m.media.$3),
      reason: 'o dourado do arquivo nao chegou ao desenho (${m.media})',
    );
    motor.limpar();
  });

  test('o TEXTO 3D desenha com geometria e extrusao de verdade', () async {
    final motor = Motor3DNativo.instance;
    // A FONTE EMPACOTADA, pelo pacote de recursos: e a mesma que o botao
    // "Texto 3D" usa quando nao ha fonte importada.
    final bytes = await rootBundle.load(
      'assets/templates/dnyx/AureaMotionSans.ttf',
    );
    final fonte = FonteTrueType.ler(bytes.buffer.asUint8List());
    const texto = Texto3D(texto: 'AUREA');
    final asset = modeloDoTexto3DPorLetra(
      disporTexto3D(texto, fonte),
      texto,
      fonte.unidadesPorEm,
      'AUREA',
      EstiloDoTexto3D.ouro,
    );
    expect(asset.triangleCount, greaterThan(0));
    // ignore: avoid_print
    print('TEXTO 3D: triangulos=${asset.triangleCount} '
        'prims=${asset.primitives.length} avisos=${asset.warnings}');
    // A EXTRUSAO E O NUMERO DE PARTES: frente, chanfro e lateral viram
    // malhas separadas, cada uma com o metal dela.
    expect(asset.primitives.length, greaterThanOrEqualTo(2));

    final no = SceneNode(name: 'AUREA', modelAsset: asset, size: 120);
    final cena = Scene3D(
      nodes: [no],
      lights: Scene3D.tresPontos,
      ambient: 0.28,
      environment: EnvironmentKind.estudioMetal,
      envReflect: 0.9,
    );
    const camera = RenderCamera(position: Vec3(0, 0, 700), target: Vec3(0, 0, 0));
    final deFrente = await _medir(await desenhar(cena, camera), (0, 0, 0));
    // ignore: avoid_print
    print('TEXTO DE FRENTE: $deFrente');
    expect(
      deFrente.cobertura,
      greaterThan(0.02),
      reason: 'o texto 3D nao desenhou',
    );
    // O OURO DO ESTILO: vermelho acima de azul.
    expect(
      deFrente.media.$1,
      greaterThan(deFrente.media.$3),
      reason: 'o material do estilo nao chegou (${deFrente.media})',
    );

    // A CAMERA MANDA NO TEXTO COMO EM QUALQUER OBJETO: girar em Y mostra a
    // lateral extrudada, e a silhueta muda.
    motor.limpar();
    final girado = Scene3D(
      nodes: [no.copyWith(rotY: AnimatedDouble(55))],
      lights: Scene3D.tresPontos,
      ambient: 0.28,
      environment: EnvironmentKind.estudioMetal,
      envReflect: 0.9,
    );
    final deLado = await _medir(await desenhar(girado, camera), (0, 0, 0));
    // ignore: avoid_print
    print('TEXTO GIRADO 55 EM Y: $deLado');
    expect(deLado.cobertura, greaterThan(0.02));
    expect(
      deLado.cobertura,
      isNot(closeTo(deFrente.cobertura, 0.002)),
      reason: 'girar em Y nao mudou a silhueta — o texto nao tem profundidade',
    );
    motor.limpar();
  });

  test('dois objetos 3D desenham no mesmo quadro', () async {    final motor = Motor3DNativo.instance;
    final asset = importGltf3D(_glb(_cuboTex().gltf, _cuboTex().bin));
    final um = SceneNode(
      name: 'Cubo',
      kind: Element3DKind.cube,
      size: 60,
      x: AnimatedDouble(-80),
    );
    final dois = SceneNode(
      name: asset.name,
      modelAsset: asset,
      size: 60,
      x: AnimatedDouble(80),
    );
    final so = Scene3D(
      nodes: [um],
      lights: Scene3D.tresPontos,
      ambient: 0.28,
    );
    const camera = RenderCamera(position: Vec3(0, 0, 700), target: Vec3(0, 0, 0));
    final comUm = await _medir(
      await desenhar(so, camera),
      (0, 0, 0),
    );
    // ignore: avoid_print
    print('SO UM: $comUm');
    motor.limpar();
    final osDois = Scene3D(
      nodes: [um, dois],
      lights: Scene3D.tresPontos,
      ambient: 0.28,
    );
    final comDois = await _medir(
      await desenhar(
        osDois,
        const RenderCamera(position: Vec3(0, 0, 460), target: Vec3(0, 0, 0)),
      ),
      (0, 0, 0),
    );
    // ignore: avoid_print
    print('OS DOIS: $comDois');
    expect(comUm.cobertura, greaterThan(0));
    expect(comDois.cobertura, greaterThan(0));
    motor.limpar();
  });

  test('o DIAL de giro da camada gira o texto 3D de verdade', () async {
    // O RELATO DO DONO, com print: um "Texto 3D" girado em -64 graus em Y
    // continuava chapado — "um fake 3D". O giro da camada era aplicado
    // como perspectiva de CARTAO sobre a imagem pronta, e o motor recebia
    // sempre o mesmo estado: o dial nao mudava UM pixel do desenho.
    //
    // Aqui o caminho e o de producao inteiro — o botao "Texto 3D", o
    // estado que o palco monta e a ponte do motor — e a medida e a
    // SILHUETA: girando de lado, a letra encurta na horizontal.
    final motor = Motor3DNativo.instance;
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final controller = container.read(editorControllerProvider.notifier);
    controller.openProject(VideoProject.empty('giro do texto 3d'));
    final noId = await controller.addTexto3D(
      Duration.zero,
      'AUREA',
      EstiloDoTexto3D.ouro,
    );
    expect(noId, isNotNull, reason: 'o botao nao montou o texto');
    final projeto = container.read(editorControllerProvider);
    final cena = projeto.layers.whereType<Scene3DLayer>().single;

    Future<Medida> comGiro(double grausY) async {
      final camada = cena.copyLayer(rotationY: AnimatedDouble(grausY));
      final estado = estado3DDoQuadro(
        project: projeto,
        l: camada,
        local: Duration.zero,
        global: Duration.zero,
        largura: 512,
        altura: 512,
      );
      motor.montar(
        cena: estado.cena,
        camera: estado.camera,
        local: Duration.zero,
        largura: 512,
        altura: 512,
        aspectoDaComposicao: 1,
        sombra: 0,
        amostras: 1,
      );
      final imagem = await motor.quadroEsperando('giro$grausY');
      expect(imagem, isNotNull, reason: 'o motor nao devolveu imagem');
      final numeros = motor.numeros;
      // ignore: avoid_print
      print('  GIRO $grausY: triangulos=${numeros.triangulos} '
          'desenhadas=${numeros.desenhadas}');
      await _salvar(imagem!, 'aurea_giro_${grausY.toInt()}.png');
      return _medir(imagem, (0, 0, 0));
    }

    final deFrente = await comGiro(0);
    final deLado = await comGiro(-64.1);
    // ignore: avoid_print
    print('TEXTO 3D DE FRENTE: $deFrente');
    // ignore: avoid_print
    print('TEXTO 3D GIRADO -64 EM Y: $deLado');
    motor.limpar();

    expect(deFrente.cobertura, greaterThan(0.005), reason: 'nao desenhou');
    // A SILHUETA MUDOU — e esse o defeito que o dono viu: antes, os dois
    // numeros eram identicos, porque o motor nunca via o giro.
    expect(
      deLado.largura,
      lessThan(deFrente.largura * 0.85),
      reason: 'o dial girou a imagem, mas nao o objeto',
    );
    // E nao e so uma imagem esticada: a ALTURA fica — quem encurta na
    // horizontal e a letra vista de lado.
    expect(deLado.altura, greaterThan(deFrente.altura * 0.6));
  });
}
