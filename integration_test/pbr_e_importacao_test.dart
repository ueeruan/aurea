// OS MATERIAIS PBR E A IMPORTACAO DE MODELO — MEDIDOS NA PLACA, NO APARELHO.
//
// ====================== POR QUE ESTE ARQUIVO EXISTE =====================
//
// "Implementado" nao e "funcionando". Um mapa de normal pode ser lido pelo
// analisador, empacotado pela porta, subido para a placa e AINDA ASSIM nao
// mudar um pixel — basta a tangente vir zerada, e nada no caminho acusa.
// Um `metallicFactor` de 1 pode chegar ao shader e produzir a mesma imagem
// de um plastico, se o ambiente que ele deveria refletir nao existir.
//
// ENTAO AQUI NAO SE CONFERE ESTADO: CONFERE-SE PIXEL. Cada teste desenha
// DUAS variantes pelo caminho de producao e exige que elas sejam
// DIFERENTES, do jeito certo. Um valor que nao muda a imagem e um valor que
// nao funciona, por mais bem transportado que ele esteja.
//
// ======================== O TABULEIRO DAS MEDIDAS =======================
//
// A superficie de prova e uma PLACA de frente para a camera, com a UV
// indo de 0 a 1 e a normal apontando para quem olha. Ela nao tem silhueta
// curva, nao tem autossombra e nao tem perspectiva: qualquer diferenca
// entre a metade esquerda e a direita da imagem so pode ter vindo do mapa.
//
// Os mapas de prova sao METADE/METADE de proposito — o teste compara a
// esquerda com a direita DA MESMA IMAGEM, e nao com um numero guardado.
// Assim ele nao depende de exposicao, de gama nem de qual placa desenhou.
//
// Rodar:
//   flutter test integration_test/pbr_e_importacao_test.dart -d emulator-5554
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/application/qualidade3d_controller.dart';
import 'package:aurea/src/features/editor/application/texture_cache.dart';
import 'package:aurea/src/features/editor/domain/orcamento_render.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/model_import3d.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart'
    show estado3DDoQuadro, nivelDeSombra3D;
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/painting.dart' show Color;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;
import 'package:integration_test/integration_test.dart';

// ======================================================= as imagens de prova

/// UM PNG DE DUAS METADES. `esquerda` vale no primeiro meio do U, `direita`
/// no segundo. E o unico formato de mapa que este arquivo usa: uma imagem
/// assim transforma "o mapa funciona?" na pergunta muito mais dura "as duas
/// metades da mesma foto sairam diferentes?".
Uint8List _pngMetades(
  (int, int, int, int) esquerda,
  (int, int, int, int) direita, {
  int lado = 64,
}) {
  final im = img.Image(width: lado, height: lado, numChannels: 4);
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      final c = x < lado ~/ 2 ? esquerda : direita;
      im.setPixelRgba(x, y, c.$1, c.$2, c.$3, c.$4);
    }
  }
  return Uint8List.fromList(img.encodePng(im));
}

// ============================================================== o GLB cru

Uint8List _glb(Map<String, dynamic> gltf, Uint8List bin) {
  Uint8List encher(Uint8List d, int fill) {
    final resto = (4 - (d.length % 4)) % 4;
    if (resto == 0) return d;
    return Uint8List.fromList([...d, ...List.filled(resto, fill)]);
  }

  final json = encher(Uint8List.fromList(utf8.encode(jsonEncode(gltf))), 0x20);
  final binPad = encher(bin, 0);
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

/// O QUE ENTRA NO BUFFER BINARIO DE UM GLB: as tres listas por vertice e os
/// indices, na ordem em que os `bufferView` abaixo os procuram.
class _Geometria {
  _Geometria(this.posicoes, this.normais, this.uvs, this.indices);
  final List<double> posicoes, normais, uvs;
  final List<int> indices;
  int get vertices => posicoes.length ~/ 3;
}

/// UMA PLACA DE FRENTE PARA A CAMERA. A UV vai de 0 a 1 da esquerda para a
/// direita, e e essa correspondencia — metade da UV, metade da tela — que
/// faz as medidas deste arquivo poderem comparar lado com lado.
_Geometria _placa({double raio = 1.0}) => _Geometria(
  [-raio, -raio, 0, raio, -raio, 0, raio, raio, 0, -raio, raio, 0],
  [0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1],
  // V INVERTIDO: no glTF a origem da textura e o canto de CIMA, e a placa
  // cresce para cima. Sem a inversao, a imagem sai de cabeca para baixo —
  // o que nao atrapalharia um mapa de metades verticais, mas atrapalharia
  // qualquer um que olhasse esta geometria depois.
  [0, 1, 1, 1, 1, 0, 0, 0],
  [0, 1, 2, 0, 2, 3],
);

/// UM CUBO FECHADO, com normal por face e UV por face.
_Geometria _cubo({double raio = 1.0}) {
  final pos = <double>[], nor = <double>[], uv = <double>[];
  final idx = <int>[];
  const faces = [
    [0, 1.0],
    [0, -1.0],
    [1, 1.0],
    [1, -1.0],
    [2, 1.0],
    [2, -1.0],
  ];
  for (final f in faces) {
    final eixo = f[0].toInt();
    final sinal = f[1].toDouble();
    final a = (eixo + 1) % 3, b = (eixo + 2) % 3;
    final n = [0.0, 0.0, 0.0];
    n[eixo] = sinal;
    // O ENROLAMENTO SEGUE O SINAL: sem isso metade das faces fica virada
    // para dentro e o cubo aparece oco no motor que descarta costas.
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
            [-1.0, 1.0 - 2.0],
          ];
    for (final c in cantos) {
      final v = [0.0, 0.0, 0.0];
      v[eixo] = sinal * raio;
      v[a] = c[0] * raio;
      v[b] = c[1] * raio;
      pos.addAll(v);
      nor.addAll(n);
      uv.addAll([(c[0] + 1) / 2, (c[1] + 1) / 2]);
    }
    final o = pos.length ~/ 3 - 4;
    idx.addAll([o, o + 1, o + 2, o, o + 2, o + 3]);
  }
  return _Geometria(pos, nor, uv, idx);
}

/// UMA ESFERA UV. E a sonda certa para a RUGOSIDADE e para o REFLEXO: numa
/// face plana a direcao do espelho e a mesma em todos os pixels, entao a
/// face inteira devolve UM ponto do ambiente e o desfoque por rugosidade nao
/// tem o que mostrar. Numa esfera a direcao varre o ambiente inteiro, que e
/// onde a diferenca entre um espelho e um metal fosco aparece.
_Geometria _esfera({int meridianos = 48, int paralelos = 24}) {
  final pos = <double>[], nor = <double>[], uv = <double>[];
  final idx = <int>[];
  for (var y = 0; y <= paralelos; y++) {
    final v = y / paralelos;
    final phi = v * math.pi;
    for (var x = 0; x <= meridianos; x++) {
      final u = x / meridianos;
      final theta = u * 2 * math.pi;
      final nx = math.sin(phi) * math.cos(theta);
      final ny = math.cos(phi);
      final nz = math.sin(phi) * math.sin(theta);
      pos.addAll([nx, ny, nz]);
      nor.addAll([nx, ny, nz]);
      uv.addAll([u, v]);
    }
  }
  for (var y = 0; y < paralelos; y++) {
    for (var x = 0; x < meridianos; x++) {
      final a = y * (meridianos + 1) + x;
      final b = a + meridianos + 1;
      idx.addAll([a, b, a + 1, a + 1, b, b + 1]);
    }
  }
  return _Geometria(pos, nor, uv, idx);
}

/// MONTA UM GLB com as geometrias e materiais dados. Cada geometria vira
/// uma `primitive` com o material de mesmo indice — e quando ha mais de uma,
/// o arquivo passa a ser o caso "varias malhas, varios materiais".
Uint8List _montarGlb({
  required List<_Geometria> pecas,
  required List<Map<String, dynamic>> materiais,
  /// Imagens como `data:` URI; o indice de `textures` casa com o de `images`.
  List<Uint8List> imagens = const [],
  String nome = 'Prova',
  List<List<double>>? deslocamentos,
}) {
  final bin = BytesBuilder();
  final bufferViews = <Map<String, dynamic>>[];
  final accessors = <Map<String, dynamic>>[];
  final primitivas = <Map<String, dynamic>>[];

  int vista(List<int> bytes) {
    // O ALINHAMENTO DE QUATRO NAO E ENFEITE: um `accessor` de float que
    // comeca fora do alinhamento e invalido no glTF, e um leitor estrito
    // recusa o arquivo inteiro por causa de um byte.
    while (bin.length % 4 != 0) {
      bin.addByte(0);
    }
    final inicio = bin.length;
    bin.add(bytes);
    bufferViews.add({
      'buffer': 0,
      'byteOffset': inicio,
      'byteLength': bytes.length,
    });
    return bufferViews.length - 1;
  }

  for (var p = 0; p < pecas.length; p++) {
    final g = pecas[p];
    final d = deslocamentos != null && p < deslocamentos.length
        ? deslocamentos[p]
        : const [0.0, 0.0, 0.0];
    final pos = Float32List(g.posicoes.length);
    for (var i = 0; i < g.posicoes.length; i++) {
      pos[i] = g.posicoes[i] + d[i % 3];
    }
    final vPos = vista(pos.buffer.asUint8List());
    final vNor = vista(Float32List.fromList(g.normais).buffer.asUint8List());
    final vUv = vista(Float32List.fromList(g.uvs).buffer.asUint8List());
    final vIdx = vista(Uint16List.fromList(g.indices).buffer.asUint8List());

    final lo = [0, 1, 2].map((k) {
      var v = double.infinity;
      for (var i = k; i < pos.length; i += 3) {
        v = math.min(v, pos[i]);
      }
      return v;
    }).toList();
    final hi = [0, 1, 2].map((k) {
      var v = double.negativeInfinity;
      for (var i = k; i < pos.length; i += 3) {
        v = math.max(v, pos[i]);
      }
      return v;
    }).toList();

    final aPos = accessors.length;
    accessors.add({
      'bufferView': vPos,
      'componentType': 5126,
      'count': g.vertices,
      'type': 'VEC3',
      'min': lo,
      'max': hi,
    });
    accessors.add({
      'bufferView': vNor,
      'componentType': 5126,
      'count': g.vertices,
      'type': 'VEC3',
    });
    accessors.add({
      'bufferView': vUv,
      'componentType': 5126,
      'count': g.vertices,
      'type': 'VEC2',
    });
    accessors.add({
      'bufferView': vIdx,
      'componentType': 5123,
      'count': g.indices.length,
      'type': 'SCALAR',
    });
    primitivas.add({
      'attributes': {
        'POSITION': aPos,
        'NORMAL': aPos + 1,
        'TEXCOORD_0': aPos + 2,
      },
      'indices': aPos + 3,
      if (p < materiais.length) 'material': p,
    });
  }

  final imagensJson = <Map<String, dynamic>>[];
  final texturasJson = <Map<String, dynamic>>[];
  for (final png in imagens) {
    final v = vista(png);
    imagensJson.add({'bufferView': v, 'mimeType': 'image/png'});
    texturasJson.add({'source': imagensJson.length - 1});
  }

  final bytes = bin.toBytes();
  final gltf = <String, dynamic>{
    'asset': {'version': '2.0'},
    'scene': 0,
    'scenes': [
      {'nodes': [0]},
    ],
    'nodes': [
      {'name': nome, 'mesh': 0},
    ],
    'meshes': [
      {'name': nome, 'primitives': primitivas},
    ],
    'materials': materiais,
    if (imagensJson.isNotEmpty) 'images': imagensJson,
    if (texturasJson.isNotEmpty) 'textures': texturasJson,
    'buffers': [
      {'byteLength': bytes.length},
    ],
    'bufferViews': bufferViews,
    'accessors': accessors,
  };
  return _glb(gltf, bytes);
}

// ================================================================ a medida

/// O QUE UMA IMAGEM DESENHADA TEM. Tudo aqui e contado sobre os pixels que
/// NAO sao fundo: um numero medio que incluisse o preto ao redor mudaria
/// conforme o objeto ficasse maior na tela, e nao conforme o material.
class Quadro {
  Quadro(this.bytes, this.largura, this.altura);

  final Uint8List bytes;
  final int largura;
  final int altura;

  bool _fundo(int i) => bytes[i + 3] < 8;

  /// Quantos pixels o objeto ocupa.
  int get pintados {
    var n = 0;
    for (var i = 0; i + 3 < bytes.length; i += 4) {
      if (!_fundo(i)) n++;
    }
    return n;
  }

  double get cobertura => pintados / (largura * altura);

  /// A COR MEDIA DE UMA FAIXA VERTICAL da imagem, em 0..1 de largura.
  /// `(0, 0.5)` e a metade esquerda.
  (double, double, double) mediaEmX(double de, double ate) {
    var r = 0.0, g = 0.0, b = 0.0;
    var n = 0;
    final x0 = (de * largura).round(), x1 = (ate * largura).round();
    for (var y = 0; y < altura; y++) {
      for (var x = x0; x < x1; x++) {
        final i = (y * largura + x) * 4;
        if (_fundo(i)) continue;
        r += bytes[i];
        g += bytes[i + 1];
        b += bytes[i + 2];
        n++;
      }
    }
    if (n == 0) return (0, 0, 0);
    return (r / n, g / n, b / n);
  }

  (double, double, double) get media => mediaEmX(0, 1);

  double get luminanciaMedia {
    final m = media;
    return (m.$1 * 0.299 + m.$2 * 0.587 + m.$3 * 0.114);
  }

  double luminanciaEmX(double de, double ate) {
    final m = mediaEmX(de, ate);
    return (m.$1 * 0.299 + m.$2 * 0.587 + m.$3 * 0.114);
  }

  /// Quantos pixels passam de [limiar] de luminancia. E a medida que separa
  /// um brilho CONCENTRADO (poucos, fortes) de um ESPALHADO (muitos, fracos).
  int acimaDe(double limiar) {
    var n = 0;
    for (var i = 0; i + 3 < bytes.length; i += 4) {
      if (_fundo(i)) continue;
      final l = bytes[i] * 0.299 + bytes[i + 1] * 0.587 + bytes[i + 2] * 0.114;
      if (l > limiar) n++;
    }
    return n;
  }

  double get maiorLuminancia {
    var v = 0.0;
    for (var i = 0; i + 3 < bytes.length; i += 4) {
      if (_fundo(i)) continue;
      final l = bytes[i] * 0.299 + bytes[i + 1] * 0.587 + bytes[i + 2] * 0.114;
      if (l > v) v = l;
    }
    return v;
  }

  /// OS LIMITES HORIZONTAIS DO QUE APARECEU, em pixels: (primeira coluna
  /// pintada, ultima coluna pintada).
  (int, int) get limitesX {
    var minX = largura, maxX = -1;
    for (var y = 0; y < altura; y++) {
      for (var x = 0; x < largura; x++) {
        if (_fundo((y * largura + x) * 4)) continue;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
      }
    }
    return maxX < 0 ? (0, 0) : (minX, maxX);
  }

  /// A COR MEDIA DE UMA FAIXA DA SILHUETA, em 0..1 da LARGURA DO OBJETO —
  /// e nao do quadro. E o que torna a medida independente de perspectiva e
  /// de tamanho: "o terco da esquerda do que apareceu" e o mesmo terco em
  /// qualquer distancia de camera.
  (double, double, double) mediaNaSilhueta(double de, double ate) {
    final (x0, x1) = limitesX;
    final w = x1 - x0 + 1;
    if (w <= 0) return (0, 0, 0);
    return mediaEmX((x0 + de * w) / largura, (x0 + ate * w) / largura);
  }

  /// A CAIXA DO QUE APARECEU — a silhueta. E por ela que se ve um objeto
  /// girar ou mudar de tamanho.
  (int, int) get caixa {
    var minX = largura, maxX = -1, minY = altura, maxY = -1;
    for (var y = 0; y < altura; y++) {
      for (var x = 0; x < largura; x++) {
        if (_fundo((y * largura + x) * 4)) continue;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
    if (maxX < 0) return (0, 0);
    return (maxX - minX + 1, maxY - minY + 1);
  }

  /// Quantos bytes diferem de outro quadro. Zero quer dizer "a mudanca nao
  /// chegou ao desenho" — e e essa a acusacao mais util deste arquivo.
  int diferencaDe(Quadro outro) {
    var n = 0;
    final menor = math.min(bytes.length, outro.bytes.length);
    for (var i = 0; i < menor; i++) {
      if (bytes[i] != outro.bytes[i]) n++;
    }
    return n;
  }

  @override
  String toString() =>
      'cobertura ${(cobertura * 100).toStringAsFixed(1)}% '
      'media rgb(${media.$1.round()}, ${media.$2.round()}, ${media.$3.round()}) '
      'lum ${luminanciaMedia.toStringAsFixed(1)} '
      'caixa ${caixa.$1}x${caixa.$2}';
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const lado = 384;

  setUpAll(() {
    expect(
      Motor3D.disponivel,
      isTrue,
      reason: 'esta versao nao tem a porta 3D compilada',
    );
    Motor3D.preparar();
    // ignore: avoid_print
    print(
      'MOTOR pronto=${Motor3D.pronto} backend="${Motor3D.backend}" '
      'motivo="${Motor3D.motivo}"',
    );
    expect(Motor3D.pronto, isTrue, reason: Motor3D.motivo);
  });

  /// DESENHA UMA CENA PELO CAMINHO DE PRODUCAO e devolve os pixels.
  Future<Quadro> desenhar(
    Scene3D cena,
    RenderCamera camera, {
    String rotulo = 'q',
  }) async {
    final motor = Motor3DNativo.instance;
    expect(motor.ligado, isTrue, reason: motor.motivo);
    motor.montar(
      cena: cena,
      camera: camera,
      local: Duration.zero,
      largura: lado,
      altura: lado,
      aspectoDaComposicao: 1,
      sombra: 0,
      amostras: 1,
    );
    // A CHAVE DO QUADRO MUDA A CADA CHAMADA de proposito: o motor guarda o
    // ultimo quadro por chave, e duas variantes com a mesma chave devolveriam
    // a MESMA imagem — o teste passaria comparando um quadro consigo mesmo.
    final imagem = await motor.quadroEsperando(
      '$rotulo-${DateTime.now().microsecondsSinceEpoch}',
    );
    expect(
      imagem,
      isNotNull,
      reason: 'o motor nao devolveu imagem ("${Motor3D.ultimoErro}")',
    );
    final dados = await imagem!.toByteData(format: ui.ImageByteFormat.rawRgba);
    return Quadro(dados!.buffer.asUint8List(), imagem.width, imagem.height);
  }

  /// A CENA PADRAO DE PROVA: um objeto, tres luzes e o estudio do metal.
  Scene3D cenaCom(
    SceneNode no, {
    EnvironmentKind ambiente = EnvironmentKind.estudioMetal,
    double reflexo = 0.9,
    double luzAmbiente = 0.28,
    List<Light3D>? luzes,
  }) => Scene3D(
    nodes: [no],
    lights: luzes ?? Scene3D.tresPontos,
    ambient: luzAmbiente,
    environment: ambiente,
    envReflect: reflexo,
  );

  const camaraDeFrente = RenderCamera(
    position: Vec3(0, 0, 340),
    target: Vec3(0, 0, 0),
  );

  /// Importa um GLB pelo analisador de producao e prepara os mapas dele no
  /// cache — o mesmo que o botao "Importar" faz antes de criar a camada.
  Future<ModelAsset3D> importar(Uint8List glb, {String de = 'prova'}) async {
    final asset = importGltf3D(glb);
    for (final m in (asset.data['materials'] as List? ?? const [])) {
      for (final chave in const [
        'image',
        'normalImage',
        'metalRoughImage',
        'emissiveImage',
        'occlusionImage',
      ]) {
        final caminho = (m as Map)[chave];
        if (caminho is! String || caminho.isEmpty) continue;
        final ok = await TextureCache.instance.prepareRgba(caminho);
        expect(ok, isTrue, reason: 'o mapa "$chave" de $de nao decodificou');
      }
    }
    return asset;
  }

  SceneNode noDe(ModelAsset3D asset, {double tamanho = 200}) =>
      SceneNode(name: asset.name, modelAsset: asset, size: tamanho);

  tearDown(() => Motor3DNativo.instance.limpar());

  // ==================================================================
  //  1. A COR BASE E A TEXTURA DE COR
  // ==================================================================

  test('1. base color: o FATOR do arquivo chega ao desenho', () async {
    Future<Quadro> comCor(List<double> cor) async {
      final glb = _montarGlb(
        pecas: [_placa()],
        materiais: [
          {
            'name': 'Liso',
            'pbrMetallicRoughness': {
              'baseColorFactor': cor,
              'metallicFactor': 0.0,
              'roughnessFactor': 0.6,
            },
          },
        ],
      );
      return desenhar(
        cenaCom(noDe(await importar(glb))),
        camaraDeFrente,
        rotulo: 'base${cor.join()}',
      );
    }

    final vermelho = await comCor([1.0, 0.05, 0.05, 1.0]);
    final azul = await comCor([0.05, 0.05, 1.0, 1.0]);
    // ignore: avoid_print
    print('1. VERMELHO $vermelho');
    // ignore: avoid_print
    print('1. AZUL     $azul');
    expect(vermelho.cobertura, greaterThan(0.1), reason: 'a placa nao desenhou');
    expect(azul.cobertura, greaterThan(0.1), reason: 'a placa nao desenhou');
    // A PROVA E A ORDEM DOS CANAIS, e nao um valor absoluto: um vermelho
    // tem de ter mais vermelho do que azul, e o azul o contrario.
    expect(
      vermelho.media.$1,
      greaterThan(vermelho.media.$3 * 2),
      reason: 'o baseColorFactor vermelho nao chegou (${vermelho.media})',
    );
    expect(
      azul.media.$3,
      greaterThan(azul.media.$1 * 2),
      reason: 'o baseColorFactor azul nao chegou (${azul.media})',
    );
  });

  test('1b. base color: a TEXTURA pinta lado a lado', () async {
    final glb = _montarGlb(
      pecas: [_placa()],
      imagens: [
        _pngMetades((255, 20, 20, 255), (20, 20, 255, 255)),
      ],
      materiais: [
        {
          'name': 'Texturado',
          'pbrMetallicRoughness': {
            'baseColorFactor': [1.0, 1.0, 1.0, 1.0],
            'baseColorTexture': {'index': 0},
            'metallicFactor': 0.0,
            'roughnessFactor': 0.6,
          },
        },
      ],
    );
    final asset = await importar(glb, de: 'cor texturizada');
    expect(
      (asset.data['materials'] as List).first['image'],
      isNotNull,
      reason: 'o analisador nao guardou a textura de cor',
    );
    final q = await desenhar(
      cenaCom(noDe(asset)),
      camaraDeFrente,
      rotulo: 'cortex',
    );
    final esq = q.mediaEmX(0.15, 0.45);
    final dir = q.mediaEmX(0.55, 0.85);
    // ignore: avoid_print
    print('1b. TEXTURA DE COR esquerda=$esq direita=$dir  $q');
    expect(q.cobertura, greaterThan(0.1));
    // O MAPA PINTOU CADA LADO DE UMA COR. Sem textura, os dois lados sairiam
    // do mesmo tom — e esta e exatamente a diferenca entre "carregou" e
    // "existe no arquivo".
    expect(
      esq.$1,
      greaterThan(esq.$3 * 2),
      reason: 'a metade esquerda do mapa de cor nao ficou vermelha ($esq)',
    );
    expect(
      dir.$3,
      greaterThan(dir.$1 * 2),
      reason: 'a metade direita do mapa de cor nao ficou azul ($dir)',
    );
  });

  // ==================================================================
  //  2. METALICO
  // ==================================================================

  test('2. metallic: um metal nao desenha como um plastico', () async {
    Future<Quadro> comMetal(double metalico) async {
      final glb = _montarGlb(
        pecas: [_cubo()],
        materiais: [
          {
            'name': 'M$metalico',
            'pbrMetallicRoughness': {
              // A MESMA COR BASE NOS DOIS: o que muda e so o metalico, entao
              // qualquer diferenca na imagem so pode ter vindo dele.
              'baseColorFactor': [0.9, 0.9, 0.92, 1.0],
              'metallicFactor': metalico,
              'roughnessFactor': 0.18,
            },
          },
        ],
      );
      return desenhar(
        cenaCom(noDe(await importar(glb))),
        camaraDeFrente,
        rotulo: 'met$metalico',
      );
    }

    final plastico = await comMetal(0.0);
    final metal = await comMetal(1.0);
    final diferenca = plastico.diferencaDe(metal);
    // ignore: avoid_print
    print('2. PLASTICO $plastico');
    // ignore: avoid_print
    print('2. METAL    $metal');
    // ignore: avoid_print
    print('2. BYTES DIFERENTES: $diferenca');
    expect(plastico.cobertura, greaterThan(0.05));
    expect(metal.cobertura, greaterThan(0.05));
    expect(
      diferenca,
      greaterThan(1000),
      reason: 'o metallicFactor nao mudou UM pixel — o material nao chega',
    );
    // UM METAL PERDE A DIFUSA. Com a mesma cor base e a mesma luz, o metal
    // fica MAIS ESCURO nas regioes que nao refletem nada, porque a luz que
    // no plastico voltava espalhada agora so volta no angulo do reflexo.
    // O sinal da diferenca e o que separa "metal" de "mais brilhante".
    expect(
      metal.luminanciaMedia,
      lessThan(plastico.luminanciaMedia),
      reason:
          'o metal ficou tao claro quanto o plastico — a difusa nao foi '
          'desligada (metal=${metal.luminanciaMedia.toStringAsFixed(1)} '
          'plastico=${plastico.luminanciaMedia.toStringAsFixed(1)})',
    );
  });

  test('2b. metallic: so o metal responde ao AMBIENTE', () async {
    Future<Quadro> com(double metalico, EnvironmentKind ambiente) async {
      final glb = _montarGlb(
        // ESFERA, E NAO CUBO: numa face plana o vetor do espelho e constante
        // e a face devolve UM texel do ambiente — trocar o estudio mudaria
        // seis cores e mais nada. Na esfera o reflexo varre o mapa inteiro.
        pecas: [_esfera()],
        materiais: [
          {
            'name': 'M',
            'pbrMetallicRoughness': {
              'baseColorFactor': [0.9, 0.9, 0.92, 1.0],
              'metallicFactor': metalico,
              'roughnessFactor': 0.12,
            },
          },
        ],
      );
      return desenhar(
        cenaCom(
          noDe(await importar(glb)),
          ambiente: ambiente,
          // SEM LUZ DIRETA: o que sobrar na imagem veio do ambiente, e so
          // dele. Com as tres luzes ligadas, a luz direta dominaria e a
          // troca de estudio ficaria embaixo dela.
          luzes: const [],
        ),
        camaraDeFrente,
        rotulo: 'amb$metalico${ambiente.index}',
      );
    }

    final metalA = await com(1.0, EnvironmentKind.estudioMetal);
    final metalB = await com(1.0, EnvironmentKind.branco);
    final foscoA = await com(0.0, EnvironmentKind.estudioMetal);
    final foscoB = await com(0.0, EnvironmentKind.branco);
    final mudouNoMetal = metalA.diferencaDe(metalB);
    final mudouNoFosco = foscoA.diferencaDe(foscoB);
    // ignore: avoid_print
    print('2b. METAL estudio=$metalA');
    // ignore: avoid_print
    print('2b. METAL branco =$metalB');
    // ignore: avoid_print
    print('2b. BYTES QUE MUDARAM: metal=$mudouNoMetal fosco=$mudouNoFosco');
    // TROCAR O ESTUDIO TEM DE MUDAR O METAL. Zero aqui quer dizer que o mapa
    // de ambiente nao chega ao shader — foi o defeito que a sonda do
    // `pbr.frag` estava atras de responder.
    expect(
      mudouNoMetal,
      greaterThan(1000),
      reason: 'trocar de ambiente nao mudou o metal: nao ha reflexo nenhum',
    );
    // E TEM DE MEXER MAIS NO METAL DO QUE NO FOSCO. Um ambiente que mudasse
    // os dois igualmente seria uma luz de preenchimento, e nao um reflexo.
    expect(
      mudouNoMetal,
      greaterThan(mudouNoFosco),
      reason:
          'o ambiente mexeu tanto no fosco quanto no metal — o que existe e '
          'luz ambiente chapada, e nao reflexo',
    );
  });

  // ==================================================================
  //  3. RUGOSIDADE
  // ==================================================================

  test('3. roughness: liso concentra o brilho, fosco espalha', () async {
    // A SONDA E UMA ESFERA E UMA LUZ SO.
    //
    // Numa esfera a normal varre todas as direcoes, entao existe SEMPRE um
    // ponto onde o espelho da luz aponta para a camera — o realce. Com
    // rugosidade baixa esse realce e um ponto pequeno e forte; com
    // rugosidade alta a mesma energia se espalha por uma calota inteira e
    // nenhum pixel chega perto daquele pico. E essa a definicao de
    // rugosidade, e e isso que se mede aqui.
    //
    // O MATERIAL E DIELETRICO (metalico 0) DE PROPOSITO: num metal dentro de
    // um estudio escuro quem manda na imagem e o reflexo do ambiente, e o
    // realce da luz fica por baixo dele. Com um dieletrico, a difusa e um
    // piso liso e o realce aparece sozinho por cima.
    Future<Quadro> comRugosidade(double r) async {
      final glb = _montarGlb(
        pecas: [_esfera()],
        materiais: [
          {
            'name': 'R$r',
            'pbrMetallicRoughness': {
              'baseColorFactor': [0.35, 0.35, 0.38, 1.0],
              'metallicFactor': 0.0,
              'roughnessFactor': r,
            },
          },
        ],
      );
      return desenhar(
        cenaCom(
          noDe(await importar(glb)),
          luzes: [
            Light3D(
              kind: Light3DKind.directional,
              direction: const Vec3(-0.3, -0.35, -1),
              intensity: AnimatedDouble(3.0),
            ),
          ],
          luzAmbiente: 0.05,
          reflexo: 0.0,
        ),
        camaraDeFrente,
        rotulo: 'rug$r',
      );
    }

    final liso = await comRugosidade(0.06);
    final fosco = await comRugosidade(0.95);
    // ignore: avoid_print
    print(
      '3. LISO  $liso pico=${liso.maiorLuminancia.toStringAsFixed(1)} '
      'acima200=${liso.acimaDe(200)}',
    );
    // ignore: avoid_print
    print(
      '3. FOSCO $fosco pico=${fosco.maiorLuminancia.toStringAsFixed(1)} '
      'acima200=${fosco.acimaDe(200)}',
    );
    // ignore: avoid_print
    print('3. BYTES DIFERENTES: ${liso.diferencaDe(fosco)}');
    expect(liso.cobertura, greaterThan(0.05), reason: 'a esfera nao desenhou');
    expect(
      liso.diferencaDe(fosco),
      greaterThan(1000),
      reason: 'o roughnessFactor nao mudou UM pixel',
    );
    // O PICO DE UM LISO E MAIOR: a mesma energia num angulo menor.
    expect(
      liso.maiorLuminancia,
      greaterThan(fosco.maiorLuminancia + 8),
      reason:
          'o realce do material liso nao ficou mais forte do que o do fosco '
          '(${liso.maiorLuminancia} vs ${fosco.maiorLuminancia}) — a '
          'rugosidade nao entra na distribuicao de microfaces',
    );
    // E O FOSCO E MAIS CLARO NA MEDIA: ele devolve luz numa area maior.
    expect(
      fosco.luminanciaMedia,
      greaterThan(liso.luminanciaMedia),
      reason:
          'o fosco nao espalhou o brilho '
          '(${fosco.luminanciaMedia} vs ${liso.luminanciaMedia})',
    );
  });

  test('3b. metallic-roughness MAP: o mapa manda, e nao o fator', () async {
    // O MAPA: esquerda METAL LISO (azul=metalico 255, verde=rugosidade 20),
    // direita FOSCO NAO-METAL (azul 0, verde 245). Os fatores ficam em 1
    // para que TUDO venha do mapa — o shader multiplica um pelo outro.
    final glb = _montarGlb(
      pecas: [_placa()],
      imagens: [
        _pngMetades((0, 20, 255, 255), (0, 245, 0, 255)),
      ],
      materiais: [
        {
          'name': 'MR',
          'pbrMetallicRoughness': {
            'baseColorFactor': [0.85, 0.85, 0.88, 1.0],
            'metallicFactor': 1.0,
            'roughnessFactor': 1.0,
            'metallicRoughnessTexture': {'index': 0},
          },
        },
      ],
    );
    final asset = await importar(glb, de: 'metal-rugosidade');
    expect(
      (asset.data['materials'] as List).first['metalRoughImage'],
      isNotNull,
      reason: 'o analisador nao guardou o mapa de metal/rugosidade',
    );
    final q = await desenhar(
      cenaCom(noDe(asset)),
      camaraDeFrente,
      rotulo: 'mrmap',
    );
    final esq = q.luminanciaEmX(0.15, 0.45);
    final dir = q.luminanciaEmX(0.55, 0.85);
    // ignore: avoid_print
    print(
      '3b. MAPA MR esquerda(metal liso)=${esq.toStringAsFixed(1)} '
      'direita(fosco)=${dir.toStringAsFixed(1)}  $q',
    );
    expect(q.cobertura, greaterThan(0.1));
    // AS DUAS METADES TEM DE SAIR DIFERENTES. Iguais quer dizer que o mapa
    // nao foi amostrado: o material inteiro ficou com o fator, e o fator e
    // o mesmo dos dois lados.
    expect(
      (esq - dir).abs(),
      greaterThan(6.0),
      reason:
          'as duas metades do mapa metal/rugosidade sairam iguais '
          '($esq vs $dir): o mapa nao esta sendo lido',
    );
  });

  // ==================================================================
  //  4. RELEVO (NORMAL MAP)
  // ==================================================================

  test('4. normal map: o relevo muda a luz na superficie', () async {
    // ESQUERDA PLANA (128,128,255 = a normal da propria face), DIREITA
    // INCLINADA para +X (255,128,255). Com a luz vindo do lado, as duas
    // metades de uma placa PLANA recebem luz diferente — e e so o mapa que
    // pode ter feito isso, porque a geometria e a mesma nos dois lados.
    Future<Quadro> com({required bool comMapa}) async {
      final glb = _montarGlb(
        pecas: [_placa()],
        imagens: comMapa
            ? [
                _pngMetades((128, 128, 255, 255), (250, 128, 160, 255)),
              ]
            : const [],
        materiais: [
          {
            'name': 'Relevo',
            'pbrMetallicRoughness': {
              'baseColorFactor': [0.8, 0.8, 0.8, 1.0],
              'metallicFactor': 0.0,
              'roughnessFactor': 0.35,
            },
            if (comMapa) 'normalTexture': {'index': 0},
          },
        ],
      );
      final asset = await importar(glb, de: 'relevo');
      if (comMapa) {
        expect(
          (asset.data['materials'] as List).first['normalImage'],
          isNotNull,
          reason: 'o analisador nao guardou o mapa de relevo',
        );
      }
      return desenhar(
        cenaCom(
          noDe(asset),
          // UMA LUZ SO, RASANTE E DE LADO: com luz de frente, inclinar a
          // normal quase nao muda o cosseno, e o teste nao veria a
          // diferenca que um olho ve.
          luzes: [
            Light3D(
              kind: Light3DKind.directional,
              direction: const Vec3(-1, -0.15, -0.55),
              intensity: AnimatedDouble(2.4),
            ),
          ],
          luzAmbiente: 0.05,
          reflexo: 0.0,
        ),
        camaraDeFrente,
        rotulo: comMapa ? 'relevo' : 'plano',
      );
    }

    final plano = await com(comMapa: false);
    final relevo = await com(comMapa: true);
    final esq = relevo.luminanciaEmX(0.15, 0.45);
    final dir = relevo.luminanciaEmX(0.55, 0.85);
    final planoEsq = plano.luminanciaEmX(0.15, 0.45);
    final planoDir = plano.luminanciaEmX(0.55, 0.85);
    // ignore: avoid_print
    print(
      '4. SEM MAPA  esq=${planoEsq.toStringAsFixed(1)} '
      'dir=${planoDir.toStringAsFixed(1)}  $plano',
    );
    // ignore: avoid_print
    print(
      '4. COM MAPA  esq=${esq.toStringAsFixed(1)} '
      'dir=${dir.toStringAsFixed(1)}  $relevo',
    );
    expect(relevo.cobertura, greaterThan(0.1));
    // SEM MAPA A PLACA E CHAPADA: os dois lados tem a mesma luz, porque a
    // normal e a mesma. Esta linha e a linha de base — sem ela, um teste que
    // so olhasse o lado com mapa poderia estar medindo um degrade da luz.
    expect(
      (planoEsq - planoDir).abs(),
      lessThan(4.0),
      reason: 'a placa sem mapa ja saiu desigual; a medida nao vale',
    );
    // COM MAPA OS DOIS LADOS DIVERGEM.
    expect(
      (esq - dir).abs(),
      greaterThan(8.0),
      reason:
          'o mapa de relevo nao mudou a luz entre as duas metades '
          '($esq vs $dir) — a normal do mapa nao chega ao shader, ou a '
          'tangente esta zerada',
    );
    // E O RESULTADO NAO E NaN. Uma tangente nula faz a normal virar NaN, e o
    // sintoma tipico e a placa inteira preta ou inteira branca.
    expect(
      relevo.luminanciaMedia,
      greaterThan(1.0),
      reason: 'a placa com relevo saiu preta — cheira a normal NaN',
    );
    expect(relevo.luminanciaMedia, lessThan(254.0));
  });

  // ==================================================================
  //  5. BRILHO PROPRIO (EMISSIVO)
  // ==================================================================

  test('5. emissive: acende sem luz nenhuma na cena', () async {
    // SEM LUZ E SEM AMBIENTE. O que aparecer na imagem so pode ter vindo do
    // emissivo: com o ambiente ligado, uma cor base clara ja daria pixels, e
    // o teste nao saberia distinguir uma coisa da outra.
    Future<Quadro> com({
      required bool comEmissivo,
      bool comMapa = false,
    }) async {
      final glb = _montarGlb(
        pecas: [_placa()],
        imagens: comMapa
            ? [
                _pngMetades((8, 8, 8, 255), (255, 255, 255, 255)),
              ]
            : const [],
        materiais: [
          {
            'name': 'Neon',
            'pbrMetallicRoughness': {
              'baseColorFactor': [0.5, 0.5, 0.5, 1.0],
              'metallicFactor': 0.0,
              'roughnessFactor': 0.5,
            },
            if (comEmissivo) 'emissiveFactor': [1.0, 0.25, 0.05],
            if (comMapa) 'emissiveTexture': {'index': 0},
          },
        ],
      );
      final asset = await importar(glb, de: 'emissivo');
      if (comMapa) {
        expect(
          (asset.data['materials'] as List).first['emissiveImage'],
          isNotNull,
          reason: 'o analisador nao guardou o mapa de brilho proprio',
        );
      }
      return desenhar(
        cenaCom(
          noDe(asset),
          luzes: const [],
          luzAmbiente: 0.0,
          reflexo: 0.0,
          ambiente: EnvironmentKind.branco,
        ),
        camaraDeFrente,
        rotulo: 'emi$comEmissivo$comMapa',
      );
    }

    final apagado = await com(comEmissivo: false);
    final aceso = await com(comEmissivo: true);
    // ignore: avoid_print
    print('5. SEM EMISSIVO $apagado');
    // ignore: avoid_print
    print('5. COM EMISSIVO $aceso');
    // NO ESCURO, SEM EMISSIVO, NAO HA NADA PARA VER.
    expect(
      apagado.luminanciaMedia,
      lessThan(12.0),
      reason:
          'sem luz e sem emissivo a placa apareceu acesa — ha uma luz '
          'escondida no caminho, e o teste do emissivo nao valeria',
    );
    expect(
      aceso.luminanciaMedia,
      greaterThan(apagado.luminanciaMedia + 20),
      reason: 'o emissiveFactor nao acendeu nada',
    );
    // A COR E A DO ARQUIVO (laranja), e nao a cor base (cinza). Este e o
    // defeito do `reduce(max)`: o emissivo saia com a cor base.
    expect(
      aceso.media.$1,
      greaterThan(aceso.media.$3 * 1.6),
      reason:
          'o emissivo acendeu, mas nao na cor do arquivo (${aceso.media}) — '
          'a cor do emissiveFactor esta sendo trocada pela cor base',
    );

    final comMapa = await com(comEmissivo: true, comMapa: true);
    final esq = comMapa.luminanciaEmX(0.15, 0.45);
    final dir = comMapa.luminanciaEmX(0.55, 0.85);
    // ignore: avoid_print
    print(
      '5b. MAPA EMISSIVO esq(preto)=${esq.toStringAsFixed(1)} '
      'dir(branco)=${dir.toStringAsFixed(1)}  $comMapa',
    );
    // O MAPA APAGA UM LADO E ACENDE O OUTRO.
    expect(
      dir,
      greaterThan(esq + 15),
      reason:
          'as duas metades do mapa emissivo sairam parecidas '
          '($esq vs $dir): o mapa nao foi amostrado',
    );
  });

  // ==================================================================
  //  6. VARIAS MALHAS E VARIOS MATERIAIS NO MESMO ARQUIVO
  // ==================================================================

  test('6. GLB com varias malhas e varios materiais', () async {
    final glb = _montarGlb(
      pecas: [_cubo(raio: 0.55), _cubo(raio: 0.55), _cubo(raio: 0.55)],
      deslocamentos: const [
        [-1.3, 0, 0],
        [0, 0, 0],
        [1.3, 0, 0],
      ],
      materiais: [
        {
          'name': 'Vermelho fosco',
          'pbrMetallicRoughness': {
            'baseColorFactor': [1.0, 0.08, 0.08, 1.0],
            'metallicFactor': 0.0,
            'roughnessFactor': 0.85,
          },
        },
        {
          'name': 'Verde metal',
          'pbrMetallicRoughness': {
            'baseColorFactor': [0.1, 1.0, 0.15, 1.0],
            'metallicFactor': 1.0,
            'roughnessFactor': 0.15,
          },
        },
        {
          'name': 'Azul emissivo',
          'pbrMetallicRoughness': {
            'baseColorFactor': [0.08, 0.1, 1.0, 1.0],
            'metallicFactor': 0.0,
            'roughnessFactor': 0.4,
          },
          'emissiveFactor': [0.0, 0.0, 0.9],
        },
      ],
      nome: 'TresPecas',
    );
    final asset = await importar(glb, de: 'tres pecas');
    // ignore: avoid_print
    print(
      '6. IMPORTADO prims=${asset.primitives.length} '
      'materiais=${(asset.data['materials'] as List).length} '
      'triangulos=${asset.triangleCount} avisos=${asset.warnings}',
    );
    expect(asset.primitives.length, 3, reason: 'as tres malhas nao entraram');
    expect((asset.data['materials'] as List).length, 3);

    final q = await desenhar(
      cenaCom(noDe(asset, tamanho: 260)),
      const RenderCamera(position: Vec3(0, 0, 420), target: Vec3(0, 0, 0)),
      rotulo: 'tres',
    );
    final esq = q.mediaEmX(0.06, 0.30);
    final meio = q.mediaEmX(0.40, 0.60);
    final dir = q.mediaEmX(0.70, 0.94);
    // ignore: avoid_print
    print('6. ESQ(vermelho)=$esq MEIO(verde)=$meio DIR(azul)=$dir  $q');
    expect(q.cobertura, greaterThan(0.08), reason: 'nada desenhou');
    // CADA PECA COM O SEU MATERIAL. Um unico material para as tres daria
    // tres regioes do mesmo tom — que e o defeito de quem junta as malhas.
    expect(
      esq.$1,
      greaterThan(esq.$2 + 10),
      reason: 'a peca da esquerda nao saiu vermelha ($esq)',
    );
    expect(
      meio.$2,
      greaterThan(meio.$1 + 10),
      reason: 'a peca do meio nao saiu verde ($meio)',
    );
    expect(
      dir.$3,
      greaterThan(dir.$1 + 10),
      reason: 'a peca da direita nao saiu azul ($dir)',
    );
  });

  // ==================================================================
  //  7. O TEXTO 3D ACEITA OS MESMOS MATERIAIS
  // ==================================================================

  test('7. texto 3D: metal e fosco sao materiais diferentes nele', () async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(VideoProject.empty('texto 3d pbr'));
    final id = await c.addTexto3D(Duration.zero, 'AUREA', EstiloDoTexto3D.ouro);
    expect(id, isNotNull, reason: 'o botao nao montou o texto 3D');
    final projeto = container.read(editorControllerProvider);
    final camada = projeto.layers.whereType<Scene3DLayer>().single;

    Future<Quadro> comMaterial(Material3D material, String rotulo) async {
      // `useModelMaterials: false` E O QUE FAZ O MATERIAL DO NO VALER.
      //
      // O texto 3D nasce como um MODELO — uma malha por letra, cada uma com
      // o metal do estilo escolhido no botao. Enquanto o no disser "use os
      // materiais do modelo", trocar o material do no nao muda nada, e o
      // teste estaria medindo a propria pergunta errada.
      final cena = camada.scene.copyWith(
        nodes: [
          for (final n in camada.scene.nodes)
            n.copyWith(material: material, useModelMaterials: false),
        ],
        environment: EnvironmentKind.estudioMetal,
        envReflect: 0.9,
      );
      final estado = estado3DDoQuadro(
        project: projeto,
        l: camada.copyScene(scene: cena),
        local: Duration.zero,
        global: Duration.zero,
        largura: lado,
        altura: lado,
      );
      final motor = Motor3DNativo.instance;
      motor.montar(
        cena: estado.cena,
        camera: estado.camera,
        local: Duration.zero,
        largura: lado,
        altura: lado,
        aspectoDaComposicao: 1,
        sombra: 0,
        amostras: 1,
      );
      final imagem = await motor.quadroEsperando(
        '$rotulo-${DateTime.now().microsecondsSinceEpoch}',
      );
      expect(imagem, isNotNull, reason: Motor3D.ultimoErro);
      final d = await imagem!.toByteData(format: ui.ImageByteFormat.rawRgba);
      return Quadro(d!.buffer.asUint8List(), imagem.width, imagem.height);
    }

    final cromo = await comMaterial(
      materialFromPreset(MaterialPreset3D.chrome),
      'cromo',
    );
    final borracha = await comMaterial(
      materialFromPreset(MaterialPreset3D.rubber),
      'borracha',
    );
    // ignore: avoid_print
    print('7. TEXTO CROMO    $cromo');
    // ignore: avoid_print
    print('7. TEXTO BORRACHA $borracha');
    // ignore: avoid_print
    print('7. BYTES DIFERENTES: ${cromo.diferencaDe(borracha)}');
    expect(cromo.cobertura, greaterThan(0.005), reason: 'o texto nao desenhou');
    expect(borracha.cobertura, greaterThan(0.005));
    expect(
      cromo.diferencaDe(borracha),
      greaterThan(500),
      reason: 'trocar o material do texto 3D nao mudou UM pixel',
    );
    // O CROMO BRILHA MAIS. E metal, liso e reflete o estudio; a borracha e
    // fosca e escura. Se o texto aceitasse o material "so no painel", os
    // dois picos seriam iguais.
    expect(
      cromo.maiorLuminancia,
      greaterThan(borracha.maiorLuminancia),
      reason:
          'o cromo nao brilhou mais do que a borracha no texto 3D '
          '(${cromo.maiorLuminancia} vs ${borracha.maiorLuminancia})',
    );
  });

  // ==================================================================
  //  9. OS DESENHOS NAO DIVIDEM ESTADO
  // ==================================================================

  test('9. isolamento: mexer no material do meio nao mexe nos vizinhos', () async {
    // ESTE E O TESTE QUE PROVA A ARQUITETURA, e nao um material.
    //
    // O defeito era estrutural: o renderizador tinha UM conjunto de recursos
    // por pipeline e o reescrevia entre as chamadas de desenho. Como os
    // desenhos de um quadro so rodam quando o lote inteiro e submetido, a
    // placa lia o ultimo estado do conjunto em TODOS eles — tres cubos de
    // cores diferentes saiam da mesma cor.
    //
    // A prova de que acabou nao e "os tres aparecem certos": e MEXER EM UM
    // e os outros dois NAO se mexerem. Um renderizador que ainda dividisse
    // estado passaria no primeiro e falharia neste.
    Future<Quadro> tres(List<double> corDoMeio) async {
      final glb = _montarGlb(
        pecas: [_cubo(raio: 0.5), _cubo(raio: 0.5), _cubo(raio: 0.5)],
        deslocamentos: const [
          [-1.35, 0, 0],
          [0, 0, 0],
          [1.35, 0, 0],
        ],
        materiais: [
          {
            'name': 'A',
            'pbrMetallicRoughness': {
              'baseColorFactor': [1.0, 0.05, 0.05, 1.0],
              'metallicFactor': 0.0,
              'roughnessFactor': 0.8,
            },
          },
          {
            'name': 'B',
            'pbrMetallicRoughness': {
              'baseColorFactor': corDoMeio,
              'metallicFactor': 0.0,
              'roughnessFactor': 0.8,
            },
          },
          {
            'name': 'C',
            'pbrMetallicRoughness': {
              'baseColorFactor': [0.05, 0.05, 1.0, 1.0],
              'metallicFactor': 0.0,
              'roughnessFactor': 0.8,
            },
          },
        ],
        nome: 'ABC',
      );
      return desenhar(
        cenaCom(
          // PEQUENO O BASTANTE PARA OS TRES CABEREM NO QUADRO. Em 300 o
          // modelo estourava a borda, o cubo da esquerda saia da tela e a
          // faixa "A" media o cubo do meio — o teste acusava vazamento de
          // estado onde havia so um enquadramento errado.
          noDe(await importar(glb), tamanho: 200),
          reflexo: 0.0,
        ),
        // A CAMERA LONGE: a 430 tres cubos enchiam a largura do quadro (a
        // silhueta media 0..383) e nao havia borda para provar que nada foi
        // cortado. A 1100 o modelo ocupa menos da metade da largura.
        const RenderCamera(position: Vec3(0, 0, 1100), target: Vec3(0, 0, 0)),
        rotulo: 'abc',
      );
    }

    final verde = await tres([0.05, 1.0, 0.05, 1.0]);
    final amarelo = await tres([1.0, 0.95, 0.05, 1.0]);

    // O MODELO INTEIRO ESTA NO QUADRO: se a silhueta encosta na borda, a
    // medida por faixas nao vale.
    final (lx0, lx1) = verde.limitesX;
    // ignore: avoid_print
    print('9. SILHUETA colunas $lx0..$lx1 de ${verde.largura}');
    expect(lx0, greaterThan(2), reason: 'o modelo saiu pela esquerda');
    expect(lx1, lessThan(verde.largura - 3), reason: 'o modelo saiu pela direita');

    // AS TRES FAIXAS, RELATIVAS A SILHUETA. Os cubos tem raio 0,5 a cada
    // 1,35: a silhueta vai de -1,85 a 1,85, e os centros ficam em 0,135,
    // 0,5 e 0,865 dela. Uma faixa de +-0,08 em volta de cada centro fica
    // dentro da face da frente do cubo, longe da quina e do vizinho.
    (double, double, double) faixa(Quadro q, int qual) {
      const centros = [0.135, 0.5, 0.865];
      return q.mediaNaSilhueta(centros[qual] - 0.08, centros[qual] + 0.08);
    }

    final a1 = faixa(verde, 0), b1 = faixa(verde, 1), c1 = faixa(verde, 2);
    final a2 = faixa(amarelo, 0), b2 = faixa(amarelo, 1), c2 = faixa(amarelo, 2);
    // ignore: avoid_print
    print('9. COM B VERDE   A=$a1 B=$b1 C=$c1');
    // ignore: avoid_print
    print('9. COM B AMARELO A=$a2 B=$b2 C=$c2');

    expect(verde.cobertura, greaterThan(0.05), reason: 'nada desenhou');
    // CADA UM COM A SUA COR, NO MESMO QUADRO.
    expect(a1.$1, greaterThan(a1.$2 + 15), reason: 'A nao saiu vermelho ($a1)');
    expect(b1.$2, greaterThan(b1.$1 + 15), reason: 'B nao saiu verde ($b1)');
    expect(c1.$3, greaterThan(c1.$1 + 15), reason: 'C nao saiu azul ($c1)');

    // MEXER NO DO MEIO MEXE SO NO DO MEIO.
    double dist((double, double, double) x, (double, double, double) y) =>
        (x.$1 - y.$1).abs() + (x.$2 - y.$2).abs() + (x.$3 - y.$3).abs();
    final mudouA = dist(a1, a2), mudouB = dist(b1, b2), mudouC = dist(c1, c2);
    // ignore: avoid_print
    print('9. MUDANCA A=$mudouA B=$mudouB C=$mudouC');
    expect(
      mudouB,
      greaterThan(30),
      reason: 'trocar o material do cubo do meio nao mudou o cubo do meio',
    );
    expect(
      mudouA,
      lessThan(6),
      reason:
          'o cubo da ESQUERDA mudou quando so o do meio foi mexido '
          '($mudouA): os desenhos ainda dividem estado',
    );
    expect(
      mudouC,
      lessThan(6),
      reason:
          'o cubo da DIREITA mudou quando so o do meio foi mexido '
          '($mudouC): os desenhos ainda dividem estado',
    );
  });

  // ==================================================================
  // 10. O TEXTO 3D COM CINCO MATERIAIS
  // ==================================================================

  test('10. texto 3D: cinco materiais, cinco imagens diferentes', () async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(VideoProject.empty('texto 3d materiais'));
    final id = await c.addTexto3D(Duration.zero, 'AUREA', EstiloDoTexto3D.ouro);
    expect(id, isNotNull);
    final projeto = container.read(editorControllerProvider);
    final camada = projeto.layers.whereType<Scene3DLayer>().single;

    Future<Quadro> com(Material3D material, String rotulo) async {
      final estado = estado3DDoQuadro(
        project: projeto,
        l: camada.copyScene(
          scene: camada.scene.copyWith(
            nodes: [
              for (final n in camada.scene.nodes)
                n.copyWith(material: material, useModelMaterials: false),
            ],
            environment: EnvironmentKind.estudioMetal,
            envReflect: 0.9,
          ),
        ),
        local: Duration.zero,
        global: Duration.zero,
        largura: lado,
        altura: lado,
      );
      final motor = Motor3DNativo.instance;
      motor.montar(
        cena: estado.cena,
        camera: estado.camera,
        local: Duration.zero,
        largura: lado,
        altura: lado,
        aspectoDaComposicao: 1,
        sombra: 0,
        amostras: 1,
      );
      final imagem = await motor.quadroEsperando(
        '$rotulo-${DateTime.now().microsecondsSinceEpoch}',
      );
      expect(imagem, isNotNull, reason: Motor3D.ultimoErro);
      final d = await imagem!.toByteData(format: ui.ImageByteFormat.rawRgba);
      return Quadro(d!.buffer.asUint8List(), imagem.width, imagem.height);
    }

    final receitas = <String, Material3D>{
      'plastico fosco': materialFromPreset(MaterialPreset3D.mattePaint),
      'metal escuro': const Material3D(
        name: 'Metal escuro',
        baseColor: Color(0xFF3A3F45),
        metallic: 1,
        roughness: 0.45,
        reflectivity: 0.9,
      ),
      'metal polido': materialFromPreset(MaterialPreset3D.polishedMetal),
      'dourado': const Material3D(
        name: 'Ouro',
        baseColor: Color(0xFFFFC34D),
        metallic: 1,
        roughness: 0.18,
        reflectivity: 1,
      ),
      'emissivo': materialFromPreset(MaterialPreset3D.emissiveNeon),
    };

    final quadros = <String, Quadro>{};
    for (final e in receitas.entries) {
      quadros[e.key] = await com(e.value, e.key);
      // ignore: avoid_print
      print('10. ${e.key.padRight(16)} ${quadros[e.key]}');
    }
    for (final q in quadros.values) {
      expect(q.cobertura, greaterThan(0.004), reason: 'o texto nao desenhou');
    }
    // CADA PAR TEM DE SER DIFERENTE. "Todos parecem iguais" e exatamente o
    // defeito que este teste existe para recusar.
    final nomes = receitas.keys.toList();
    for (var i = 0; i < nomes.length; i++) {
      for (var j = i + 1; j < nomes.length; j++) {
        final d = quadros[nomes[i]]!.diferencaDe(quadros[nomes[j]]!);
        // ignore: avoid_print
        print('10. ${nomes[i]} x ${nomes[j]}: $d bytes diferentes');
        expect(
          d,
          greaterThan(400),
          reason:
              'o texto 3D saiu igual com "${nomes[i]}" e "${nomes[j]}" — '
              'o material nao chega a geometria do texto',
        );
      }
    }
  });

  // ==================================================================
  // 11. ESTRESSE: MUITOS QUADROS, TROCANDO TUDO
  // ==================================================================

  test('11. estresse: criar, trocar e apagar por muitos quadros', () async {
    // O QUE ISTO CACA E TEMPO DE VIDA, e nao aparencia.
    //
    // Um conjunto de recursos que aponte para uma textura ja destruida nao
    // aparece no primeiro quadro: aparece quando o modelo e trocado, quando
    // o acervo recicla uma alca, quando o material muda pela decima vez.
    // Abrir e fechar uma cena uma vez nao encontra isso.
    final motor = Motor3DNativo.instance;
    var desenhados = 0;
    for (var volta = 0; volta < 12; volta++) {
      final pecas = 1 + (volta % 3);
      final glb = _montarGlb(
        pecas: [for (var k = 0; k < pecas; k++) _cubo(raio: 0.45)],
        deslocamentos: [
          for (var k = 0; k < pecas; k++) [(k - 1) * 1.2, 0.0, 0.0],
        ],
        imagens: volta.isEven
            ? [
                _pngMetades(
                  (255, (volta * 20) % 256, 40, 255),
                  (40, 80, (volta * 30) % 256, 255),
                ),
              ]
            : const [],
        materiais: [
          for (var k = 0; k < pecas; k++)
            {
              'name': 'M$volta$k',
              'pbrMetallicRoughness': {
                'baseColorFactor': [
                  ((volta + k) % 4) / 3.0,
                  ((volta + k * 2) % 5) / 4.0,
                  ((volta * 2 + k) % 3) / 2.0,
                  1.0,
                ],
                'metallicFactor': (volta % 2).toDouble(),
                'roughnessFactor': 0.1 + (volta % 7) / 10.0,
                if (volta.isEven) 'baseColorTexture': {'index': 0},
              },
              if (volta % 3 == 0) 'emissiveFactor': [0.0, 0.6, 0.2],
            },
        ],
        nome: 'Estresse$volta',
      );
      final asset = await importar(glb, de: 'estresse $volta');
      final q = await desenhar(
        cenaCom(
          noDe(asset, tamanho: 200 + (volta % 4) * 40),
          ambiente: volta.isEven
              ? EnvironmentKind.estudioMetal
              : EnvironmentKind.ceu,
          reflexo: (volta % 3) / 2.0,
        ),
        RenderCamera(
          position: Vec3(volta * 6.0, 20, 420),
          target: const Vec3(0, 0, 0),
        ),
        rotulo: 'estresse$volta',
      );
      if (q.cobertura > 0.01) desenhados++;
      // APAGA TUDO E RECOMECA: e aqui que uma alca reciclada encontra um
      // conjunto de recursos que ainda aponta para o modelo antigo.
      motor.limpar();
    }
    // ignore: avoid_print
    print('11. ESTRESSE: $desenhados de 12 voltas desenharam');
    expect(
      desenhados,
      12,
      reason: 'alguma volta do estresse nao desenhou nada',
    );
    // Chegar aqui ja quer dizer "nao caiu": um descritor solto derruba o
    // processo inteiro e o teste nem reporta.
    expect(Motor3D.pronto, isTrue, reason: 'o motor morreu no meio');
  });

  // ==================================================================
  // 12. O MODELO "MAIOR" DO RELATO: 7 malhas densas, sombra e MSAA
  // ==================================================================

  test('12. modelo de 7 malhas densas com sombra e MSAA nao derruba', () async {
    // O RELATO: importar um modelo maior FECHOU TUDO — no emulador, a VM
    // inteira — no primeiro quadro. O log parou logo depois dos sete draws
    // (um deles com 127 mil indices), com a sombra ligada (pso 8). Os testes
    // de cima desenham sem sombra, quadrados e pequenos; este imita o palco.
    final glb = _montarGlb(
      pecas: [
        for (var k = 0; k < 7; k++)
          _esfera(meridianos: k == 5 ? 210 : 60, paralelos: k == 5 ? 100 : 30),
      ],
      deslocamentos: [
        for (var k = 0; k < 7; k++) [(k - 3) * 0.9, (k % 2) * 0.6, 0.0],
      ],
      imagens: [_pngMetades((150, 108, 74, 255), (150, 108, 74, 255), lado: 16)],
      materiais: [
        {
          'name': 'ComMapa',
          'pbrMetallicRoughness': {
            'baseColorTexture': {'index': 0},
            'metallicFactor': 0.0,
            'roughnessFactor': 0.6,
          },
          'emissiveTexture': {'index': 0},
        },
        for (var k = 1; k < 7; k++)
          {
            'name': 'M$k',
            'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 0.6},
          },
      ],
      nome: 'Maior',
    );
    final asset = await importar(glb, de: 'maior');
    final motor = Motor3DNativo.instance;
    for (final (l, a, sombra, amostras) in const [
      (540, 960, 1, 1),
      (540, 960, 2, 4),
      (1080, 1920, 3, 4),
      (1080, 1920, 3, 4),
    ]) {
      motor.montar(
        cena: cenaCom(noDe(asset, tamanho: 220)),
        camera: const RenderCamera(position: Vec3(0, 0, 700), target: Vec3(0, 0, 0)),
        local: Duration.zero, largura: l, altura: a,
        aspectoDaComposicao: l / a, sombra: sombra, amostras: amostras,
      );
      final imagem = await motor.quadroEsperando(
        'maior-$l-$sombra-$amostras-${DateTime.now().microsecondsSinceEpoch}');
      // ignore: avoid_print
      print('12. ${l}x$a sombra=$sombra msaa=$amostras -> '
          '${imagem == null ? "SEM IMAGEM" : "${imagem.width}x${imagem.height}"} '
          'erro="${Motor3D.ultimoErro}"');
      expect(imagem, isNotNull, reason: Motor3D.ultimoErro);
    }
  });

  // ==================================================================
  // 13. O ARQUIVO REAL DO RELATO
  // ==================================================================

  test('13. GLB real do aparelho: importa e desenha com sombra', () async {
    for (final caminho in const ['/data/local/tmp/torch.glb', '/data/local/tmp/mc.glb']) {
      final arquivo = File(caminho);
      if (!arquivo.existsSync()) {
        // ignore: avoid_print
        print('13. $caminho nao existe neste aparelho — pulado');
        continue;
      }
      final asset = await importar(arquivo.readAsBytesSync(), de: caminho);
      // ignore: avoid_print
      print('13. $caminho prims=${asset.primitives.length} '
          'tri=${asset.triangleCount} avisos=${asset.warnings}');
      final motor = Motor3DNativo.instance;
      for (final (l, a, sombra, amostras) in const [(540, 960, 0, 1), (540, 960, 2, 4), (1080, 1920, 3, 4)]) {
        motor.montar(
          cena: cenaCom(noDe(asset, tamanho: 220)),
          camera: const RenderCamera(position: Vec3(0, 0, 700), target: Vec3(0, 0, 0)),
          local: Duration.zero, largura: l, altura: a,
          aspectoDaComposicao: l / a, sombra: sombra, amostras: amostras,
        );
        final imagem = await motor.quadroEsperando(
          'real-$l-$sombra-${DateTime.now().microsecondsSinceEpoch}');
        // ignore: avoid_print
        print('13.   ${l}x$a sombra=$sombra msaa=$amostras -> '
            '${imagem == null ? "SEM IMAGEM" : "ok"} erro="${Motor3D.ultimoErro}"');
        expect(imagem, isNotNull, reason: Motor3D.ultimoErro);
      }
      motor.limpar();
    }
  });

  // ==================================================================
  // 14. A SEQUENCIA DO APLICATIVO: cubo por muitos quadros, depois o GLB
  // ==================================================================

  test('14. sequencia do app: cubo 40 quadros, importa GLB real, 40 quadros', () async {
    final arquivo = File('/data/local/tmp/mc.glb');
    if (!arquivo.existsSync()) return;
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(VideoProject.empty('sequencia do app'));
    c.addScene3DLayer(Duration.zero);
    final cenaId = container.read(editorControllerProvider)
        .layers.whereType<Scene3DLayer>().single.id;
    c.updateScene3D(cenaId, (cena) => cena.copyWith(nodes: [
      ...cena.nodes,
      SceneNode(name: 'Cubo', kind: Element3DKind.cube, size: 120),
    ]));

    final receita = ControladorDeQualidade3D.instancia.receita;
    final projeto0 = container.read(editorControllerProvider);
    final alvo = alvoDoPreview(
      projeto0.outputWidth.toDouble(), projeto0.outputHeight.toDouble(), receita);
    final maior = math.max(alvo.largura, alvo.altura);
    final sombra = nivelDeSombra3D(receita, maior);
    final amostras = receita.msaa ? 4 : 1;
    // ignore: avoid_print
    print('14. RECEITA alvo=${alvo.largura}x${alvo.altura} sombra=$sombra '
        'msaa=$amostras comp=${projeto0.outputWidth}x${projeto0.outputHeight}');

    Future<void> quadros(int n, String rotulo) async {
      final motor = Motor3DNativo.instance;
      for (var i = 0; i < n; i++) {
        final projeto = container.read(editorControllerProvider);
        final camada = projeto.layers.whereType<Scene3DLayer>().single;
        final t = Duration(milliseconds: i * 33);
        final estado = estado3DDoQuadro(
          project: projeto, l: camada, local: t, global: t,
          largura: alvo.largura, altura: alvo.altura,
        );
        motor.montar(
          cena: estado.cena, camera: estado.camera, local: t,
          largura: alvo.largura, altura: alvo.altura,
          aspectoDaComposicao: alvo.largura / alvo.altura,
          sombra: sombra, amostras: amostras,
        );
        final imagem = await motor.quadroEsperando('$rotulo$i-${DateTime.now().microsecondsSinceEpoch}');
        expect(imagem, isNotNull, reason: '$rotulo quadro $i: ${Motor3D.ultimoErro}');
      }
      // ignore: avoid_print
      print('14. $rotulo: $n quadros ok');
    }

    await quadros(40, 'cubo');
    final asset = importGltf3D(arquivo.readAsBytesSync());
    // COMO O BOTAO: confere (sobe SEM textura), cria o no, e os mapas chegam
    // DEPOIS — a malha e refeita com textura um quadro adiante.
    expect(Motor3DNativo.instance.conferirModelo(asset), isNull);
    c.addModel3D(cenaId, asset);
    await quadros(3, 'semMapa');
    for (final m in (asset.data['materials'] as List)) {
      for (final k in const ['image', 'emissiveImage', 'normalImage', 'metalRoughImage', 'occlusionImage']) {
        final v = (m as Map)[k];
        if (v is String && v.isNotEmpty) await TextureCache.instance.prepareRgba(v);
      }
    }
    await quadros(40, 'comMapa');
  });

  // ==================================================================
  // 15. O MSAA TROCA NO MEIO DA SESSAO
  // ==================================================================

  test('15. trocar o MSAA entre quadros nao derruba a placa', () async {
    // O DEFEITO: o pipeline guardava a contagem de amostras com que nasceu e
    // o cache nao a tinha na chave. A qualidade adaptativa do palco troca o
    // MSAA sozinha (4x -> 1x quando a cena pesa), o pipeline antigo era
    // usado num alvo de outra contagem, e a placa acusava "3D WIDTH ZT
    // Violation" e reiniciava o driver — o emulador inteiro morria.
    final glb = _montarGlb(
      pecas: [_cubo(raio: 0.6), _esfera()],
      deslocamentos: const [[-0.9, 0, 0], [0.9, 0, 0]],
      materiais: [
        {'name': 'A', 'pbrMetallicRoughness': {'baseColorFactor': [1.0, 0.2, 0.2, 1.0], 'metallicFactor': 0.0}},
        {'name': 'B', 'pbrMetallicRoughness': {'baseColorFactor': [0.9, 0.9, 0.9, 1.0], 'metallicFactor': 1.0, 'roughnessFactor': 0.2}},
      ],
      nome: 'TrocaDeMsaa',
    );
    final asset = await importar(glb, de: 'msaa');
    final motor = Motor3DNativo.instance;
    var quadros = 0;
    for (final (amostras, sombra, l, a) in const [
      (4, 2, 540, 960), (1, 2, 540, 960), (4, 0, 540, 960), (1, 3, 720, 1280),
      (4, 3, 720, 1280), (1, 0, 384, 384), (4, 2, 384, 384), (1, 1, 540, 960),
    ]) {
      motor.montar(
        cena: cenaCom(noDe(asset, tamanho: 200)),
        camera: const RenderCamera(position: Vec3(0, 0, 600), target: Vec3(0, 0, 0)),
        local: Duration.zero, largura: l, altura: a,
        aspectoDaComposicao: l / a, sombra: sombra, amostras: amostras,
      );
      final imagem = await motor.quadroEsperando(
        'msaa$amostras-$sombra-$l-${DateTime.now().microsecondsSinceEpoch}');
      expect(imagem, isNotNull, reason: 'msaa=$amostras: ${Motor3D.ultimoErro}');
      quadros++;
    }
    // ignore: avoid_print
    print('15. TROCA DE MSAA: $quadros quadros, pipelines=${Motor3D.estatisticas}');
    expect(quadros, 8);
  });

  // ==================================================================
  //  8. O CICLO INTEIRO: IMPORTAR -> MEXER -> SALVAR -> REABRIR
  // ==================================================================

  test('8. ciclo: importar, mexer, salvar, reabrir e continuar', () async {
    final glb = _montarGlb(
      pecas: [_cubo(raio: 0.9)],
      imagens: [
        _pngMetades((255, 30, 30, 255), (30, 30, 255, 255)),
      ],
      materiais: [
        {
          'name': 'Ciclo',
          'pbrMetallicRoughness': {
            'baseColorFactor': [1.0, 1.0, 1.0, 1.0],
            'baseColorTexture': {'index': 0},
            'metallicFactor': 0.75,
            'roughnessFactor': 0.25,
          },
        },
      ],
      nome: 'ModeloDoCiclo',
    );

    // ---------------------------------------------------- selecionar/importar
    final arquivo = File(
      '${Directory.systemTemp.path}/aurea_ciclo_${DateTime.now().microsecondsSinceEpoch}.glb',
    )..writeAsBytesSync(glb);
    addTearDown(() {
      if (arquivo.existsSync()) arquivo.deleteSync();
    });
    // ignore: avoid_print
    print('8. ARQUIVO ${arquivo.path} (${arquivo.lengthSync()} bytes)');
    final asset = await importar(
      Uint8List.fromList(arquivo.readAsBytesSync()),
      de: 'ciclo',
    );

    // ------------------------------------------------------- criar a entidade
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(VideoProject.empty('ciclo do modelo'));
    c.addScene3DLayer(Duration.zero);
    final cenaId = container
        .read(editorControllerProvider)
        .layers
        .whereType<Scene3DLayer>()
        .single
        .id;
    final noId = c.addModel3D(cenaId, asset);
    expect(noId, isNotEmpty, reason: 'o modelo nao virou no na cena');
    // ignore: avoid_print
    print('8. CENA=$cenaId NO=$noId');

    // A CONFERENCIA DE PRODUCAO: o que impede o "sucesso falso" da folha de
    // importacao. Nulo quer dizer que o modelo entrou na placa de verdade.
    final defeito = Motor3DNativo.instance.conferirModelo(asset);
    // ignore: avoid_print
    print('8. CONFERIR MODELO: ${defeito ?? "entrou"}');
    expect(defeito, isNull, reason: 'o modelo nao entrou na placa: $defeito');

    Future<Quadro> desenharProjeto(String rotulo) async {
      final projeto = container.read(editorControllerProvider);
      final camada = projeto.layers.whereType<Scene3DLayer>().single;
      final estado = estado3DDoQuadro(
        project: projeto,
        l: camada,
        local: Duration.zero,
        global: Duration.zero,
        largura: lado,
        altura: lado,
      );
      final motor = Motor3DNativo.instance;
      motor.montar(
        cena: estado.cena,
        camera: estado.camera,
        local: Duration.zero,
        largura: lado,
        altura: lado,
        aspectoDaComposicao: 1,
        sombra: 0,
        amostras: 1,
      );
      final imagem = await motor.quadroEsperando(
        '$rotulo-${DateTime.now().microsecondsSinceEpoch}',
      );
      expect(imagem, isNotNull, reason: Motor3D.ultimoErro);
      final d = await imagem!.toByteData(format: ui.ImageByteFormat.rawRgba);
      return Quadro(d!.buffer.asUint8List(), imagem.width, imagem.height);
    }

    // ------------------------------------------------------ na viewport
    final inicial = await desenharProjeto('ciclo-inicial');
    // ignore: avoid_print
    print('8. NA VIEWPORT $inicial');
    expect(
      inicial.cobertura,
      greaterThan(0.03),
      reason: 'o modelo importado nao apareceu na viewport',
    );
    // A TEXTURA CHEGOU: o cubo tem um lado vermelho e outro azul.
    final somaR = inicial.media.$1, somaB = inicial.media.$3;
    // ignore: avoid_print
    print('8. COR MEDIA r=$somaR b=$somaB');
    expect(
      somaR + somaB,
      greaterThan(20),
      reason: 'o modelo apareceu, mas sem a textura do arquivo',
    );

    // ------------------------------------------------- mover, girar, escalar
    Scene3DLayer camadaAtual() => container
        .read(editorControllerProvider)
        .layers
        .whereType<Scene3DLayer>()
        .single;

    void mexer(SceneNode Function(SceneNode) f) {
      c.updateScene3D(
        cenaId,
        (cena) => cena.copyWith(
          nodes: [for (final n in cena.nodes) n.id == noId ? f(n) : n],
        ),
      );
    }

    mexer((n) => n.copyWith(x: AnimatedDouble(90)));
    final movido = await desenharProjeto('ciclo-movido');
    // ignore: avoid_print
    print('8. MOVIDO   $movido');
    expect(
      movido.diferencaDe(inicial),
      greaterThan(500),
      reason: 'mover o modelo nao mudou o desenho',
    );

    mexer((n) => n.copyWith(rotY: AnimatedDouble(38)));
    final girado = await desenharProjeto('ciclo-girado');
    // ignore: avoid_print
    print('8. GIRADO   $girado');
    expect(
      girado.diferencaDe(movido),
      greaterThan(500),
      reason: 'girar o modelo nao mudou o desenho',
    );

    mexer((n) => n.copyWith(size: 360));
    final grande = await desenharProjeto('ciclo-grande');
    // ignore: avoid_print
    print('8. ESCALADO $grande');
    expect(
      grande.cobertura,
      greaterThan(girado.cobertura * 1.2),
      reason:
          'escalar nao aumentou o objeto na tela '
          '(${girado.cobertura} -> ${grande.cobertura})',
    );

    // ------------------------------------------------------ salvar e reabrir
    final antesDeSalvar = await desenharProjeto('ciclo-antes');
    final json = projectToJson(container.read(editorControllerProvider));
    final texto = jsonEncode(json);
    // ignore: avoid_print
    print('8. SALVO: ${texto.length} bytes de JSON');
    expect(texto.length, greaterThan(200));

    // FECHAR DE VERDADE: o motor perde tudo o que estava na placa, e o
    // projeto e relido do texto — nao de um objeto que ficou na memoria.
    Motor3DNativo.instance.limpar();
    TextureCache.instance.clear();
    c.openProject(VideoProject.empty('outro projeto'));

    final relido = projectFromJson(
      (jsonDecode(texto) as Map).cast<String, dynamic>(),
    );
    c.openProject(relido);
    final camadaRelida = camadaAtual();
    final noRelido = camadaRelida.scene.nodes.firstWhere((n) => n.id == noId);
    // ignore: avoid_print
    print(
      '8. RELIDO no="${noRelido.name}" modelo=${noRelido.modelAsset != null} '
      'x=${noRelido.x.base} rotY=${noRelido.rotY.base} size=${noRelido.size}',
    );
    // O QUE FOI MEXIDO CONTINUA MEXIDO.
    expect(noRelido.modelAsset, isNotNull, reason: 'o modelo nao sobreviveu');
    expect(noRelido.x.base, closeTo(90, 0.01));
    expect(noRelido.rotY.base, closeTo(38, 0.01));
    expect(noRelido.size, closeTo(360, 0.01));
    // E A TEXTURA TAMBEM: o `data` do modelo guarda o `data:` URI do mapa.
    final materialRelido =
        (noRelido.modelAsset!.data['materials'] as List).first as Map;
    expect(
      materialRelido['image'],
      isNotNull,
      reason: 'a textura de cor nao sobreviveu ao salvar/reabrir',
    );

    // Reprepara os mapas como o aplicativo faz ao abrir um projeto.
    await TextureCache.instance.prepareRgba(materialRelido['image'] as String);
    final depois = await desenharProjeto('ciclo-depois');
    // ignore: avoid_print
    print('8. DEPOIS DE REABRIR $depois');
    expect(
      depois.cobertura,
      greaterThan(0.03),
      reason: 'depois de reabrir, o modelo nao desenha mais',
    );
    // O MESMO DESENHO DE ANTES. Nao byte a byte — a placa pode variar no
    // ultimo bit — mas a mesma silhueta e a mesma cor.
    expect(
      depois.cobertura,
      closeTo(antesDeSalvar.cobertura, 0.02),
      reason:
          'a silhueta mudou depois de reabrir '
          '(${antesDeSalvar.cobertura} -> ${depois.cobertura})',
    );
    expect(
      depois.luminanciaMedia,
      closeTo(antesDeSalvar.luminanciaMedia, 12),
      reason:
          'a cor mudou depois de reabrir '
          '(${antesDeSalvar.luminanciaMedia} -> ${depois.luminanciaMedia})',
    );
  });
}
