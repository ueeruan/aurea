// O PREENCHIMENTO — medido contra render do After Effects.
//
// A REFERENCIA SAIU DO AE (build/qa/ae-novos/Fill.png): comp de 128x128 com
// um solido branco de 48x48 em (44,44), o `ADBE Fill` em azul e opacidade
// 1. O que o render mostrou:
//
//   * o quadrado inteiro saiu `(0,0,255,255)` — a cor PEDIDA, e nao uma
//     mistura com o branco de origem;
//   * o pixel (100,100), FORA do quadrado, saiu `(0,0,0,0)` — o alfa de
//     origem sobreviveu, e o preenchimento nao pintou o quadro inteiro.
//
// ESSAS DUAS LINHAS SAO A ESPECIFICACAO INTEIRA DO EFEITO, e sao elas que
// o teste cobra. Um "preenchimento" que pintasse o quadro todo passaria
// num teste de "ficou azul"; este nao.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:aurea/src/features/editor/presentation/widgets/pixel_effect_engine.dart';
import 'package:flutter_test/flutter_test.dart';

/// UMA IMAGEM 16x16: um quadrado 8x8 OPACO BRANCO no canto, e o resto
/// TRANSPARENTE. E a fonte certa para este efeito — uma imagem sem alfa
/// nao distingue "troca a cor" de "pinta por cima".
Future<ui.Image> _quadrado() async {
  const lado = 16;
  final pixels = Uint8List(lado * lado * 4);
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      final i = (y * lado + x) * 4;
      final dentro = x < 8 && y < 8;
      pixels[i] = 255;
      pixels[i + 1] = 255;
      pixels[i + 2] = 255;
      pixels[i + 3] = dentro ? 255 : 0;
    }
  }
  final buffer = await ui.ImmutableBuffer.fromUint8List(pixels);
  final descritor = ui.ImageDescriptor.raw(
    buffer,
    width: lado,
    height: lado,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descritor.instantiateCodec();
  return (await codec.getNextFrame()).image;
}

/// O EFEITO MONTADO como o palco monta.
EffectInstance _efeito({
  double difusaoH = 0,
  double difusaoV = 0,
  double opacidade = 100,
  int cor = 0xFF0000FF,
}) {
  var e = EffectInstance(
    type: EffectType.preenchimento,
    color: ui.Color(cor),
  );
  for (final (k, v) in [
    ('difusao_h', difusaoH),
    ('difusao_v', difusaoV),
    ('opacidade', opacidade),
  ]) {
    e = e.withParamEdited(k, Duration.zero, v);
  }
  return e;
}

/// DESENHA O EFEITO E DEVOLVE OS PIXELS.
Future<Uint8List> _desenhar(ui.Image fonte, EffectInstance efeito) async {
  final frame = PixelEffectFrame.of(efeito, Duration.zero);
  final shader = PixelEffectEngine.createShader(
    frame,
    width: fonte.width.toDouble(),
    height: fonte.height.toDouble(),
    image: fonte,
  );
  try {
    final gravador = ui.PictureRecorder();
    ui.Canvas(gravador).drawRect(
      ui.Rect.fromLTWH(0, 0, 16, 16),
      ui.Paint()..shader = shader,
    );
    final imagem = await gravador.endRecording().toImage(16, 16);
    final dados = await imagem.toByteData(format: ui.ImageByteFormat.rawRgba);
    imagem.dispose();
    return dados!.buffer.asUint8List();
  } finally {
    shader.dispose();
  }
}

List<int> _px(Uint8List p, int x, int y) {
  final i = (y * 16 + x) * 4;
  return [p[i], p[i + 1], p[i + 2], p[i + 3]];
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late ui.Image fonte;
  setUpAll(() async {
    await PixelEffectEngine.warmUp();
    expect(
      PixelEffectEngine.ready,
      isTrue,
      reason: 'o shader dos efeitos de pixel nao carregou: '
          '${PixelEffectEngine.failure}',
    );
    fonte = await _quadrado();
  });
  tearDownAll(() => fonte.dispose());

  test('o efeito esta no catalogo, com a categoria e o id certos', () {
    final spec = effectSpecs[EffectType.preenchimento];
    expect(spec, isNotNull);
    // O ID E O CONTRATO COM O ARQUIVO, e nao muda nunca.
    expect(spec!.id, 'adbe_fill');
    expect(spec.name, 'Preenchimento');
    expect(spec.category, 'Generate');
    expect(spec.hasColor, isTrue);
    // E a busca acha pelo nome que a pessoa procura.
    expect(spec.synonyms, contains('fill'));
    expect(spec.synonyms, contains('silhueta'));
  });

  test('o kernel esta ligado ao motor de pixel', () {
    // SEM ISTO O EFEITO NAO DESENHA: o motor so pega o que esta em
    // `pixelKernels`, e o resto cai no switch e fica neutro.
    final kernel = pixelKernels[EffectType.preenchimento];
    expect(kernel, isNotNull);
    expect(kernel!.keys, ['difusao_h', 'difusao_v', 'opacidade']);
  });

  group('a conta, contra o render do AE', () {
    test('a cor sai EXATAMENTE a pedida, e nao uma mistura com a origem',
        () async {
      // A PRIMEIRA LINHA DA REFERENCIA. O solido era branco e saiu
      // `(0,0,255)` — se o efeito misturasse com o branco de origem, sairia
      // um azul claro, e a diferenca aparece aqui.
      final p = await _desenhar(fonte, _efeito());
      final dentro = _px(p, 2, 2);
      expect(dentro[0], lessThan(12), reason: 'sobrou vermelho da origem');
      expect(dentro[1], lessThan(12), reason: 'sobrou verde da origem');
      expect(dentro[2], greaterThan(243), reason: 'nao chegou ao azul');
      expect(dentro[3], 255);
    });

    test('o que era TRANSPARENTE continua transparente', () async {
      // A SEGUNDA LINHA, e a que separa "preenchimento" de "pintar por
      // cima". Um retangulo pintado deixaria (12,12) azul e opaco.
      final p = await _desenhar(fonte, _efeito());
      final fora = _px(p, 12, 12);
      expect(
        fora,
        [0, 0, 0, 0],
        reason: 'o preenchimento pintou fora da camada',
      );
    });

    test('a opacidade mistura com o original', () async {
      final cheio = _px(await _desenhar(fonte, _efeito()), 2, 2);
      final meio = _px(await _desenhar(fonte, _efeito(opacidade: 50)), 2, 2);
      // O CANAL VERMELHO E QUE CONTA: o branco de origem tem 255 nos tres
      // canais e o azul do preenchimento tambem tem 255 no azul — entao o
      // azul fica 255 em qualquer opacidade, e so o VERMELHO mostra a
      // mistura. Comparar no canal errado daria "igual" para sempre.
      expect(cheio[0], lessThan(12), reason: 'a opacidade cheia devia zerar o vermelho');
      expect(
        meio[0],
        greaterThan(60),
        reason: 'a opacidade nao misturou com o branco de origem',
      );
      expect(meio[0], lessThan(200));
    });

    test('opacidade zero deixa a camada intacta', () async {
      final p = await _desenhar(fonte, _efeito(opacidade: 0));
      expect(_px(p, 2, 2), [255, 255, 255, 255]);
      expect(_px(p, 12, 12), [0, 0, 0, 0]);
    });

    test('a difusao espalha a mascara para FORA da beirada', () async {
      // O UNICO LUGAR onde a cor nao entra chapada. Sem difusao, (9,2) e
      // transparente; com difusao horizontal, o preenchimento chega la com
      // o alfa decaindo.
      final sem = await _desenhar(fonte, _efeito());
      expect(_px(sem, 9, 2)[3], 0);
      final com = await _desenhar(fonte, _efeito(difusaoH: 8));
      expect(
        _px(com, 9, 2)[3],
        greaterThan(0),
        reason: 'a difusao horizontal nao espalhou',
      );
      // E ela respeita o EIXO: em Y o pixel vizinho continua vazio.
      expect(_px(com, 2, 9)[3], 0, reason: 'espalhou no eixo errado');
    });

    test('as difusoes sao independentes por eixo', () async {
      final soV = await _desenhar(fonte, _efeito(difusaoV: 8));
      expect(_px(soV, 2, 9)[3], greaterThan(0));
      expect(_px(soV, 9, 2)[3], 0);
    });
  });
}
