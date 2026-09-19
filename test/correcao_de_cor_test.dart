// "ADICIONE OS PRIMEIROS EFEITOS DE CORRECAO DE COR PODEROSOS E
// OTIMIZADOS... 5 EFEITOS, E O PRINCIPAL SENDO O UNSHARP MASK" (dono,
// 16/09/2026).
//
// Tres camadas de prova:
//   1. as fichas tem os nomes, faixas e padroes do After Effects;
//   2. a conta em Dart faz o que o AE faz (um stop e o dobro de LUZ, nao
//      do numero; saturacao -100 e cinza; nivel com gama clareia o meio);
//   3. os SHADERS de verdade desenham o mesmo que a conta em Dart, pixel a
//      pixel — inclusive quatro efeitos fundidos numa passada so.
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/correcao_de_cor.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/presentation/widgets/passe_de_cor.dart';
import 'package:flutter_test/flutter_test.dart';

const _w = 64, _h = 32;

/// Rampa opaca: vermelho cresce em x, verde em y, azul alterna em listras.
CorRgb _corDoFixture(int x, int y) => (
  r: (x * 255 ~/ (_w - 1)) / 255,
  g: (y * 255 ~/ (_h - 1)) / 255,
  b: (x % 8 < 4 ? 40 : 210) / 255,
);

Future<ui.Image> _imagem(Uint8List rgba, int w, int h) async {
  final buffer = await ui.ImmutableBuffer.fromUint8List(rgba);
  final descriptor = ui.ImageDescriptor.raw(
    buffer,
    width: w,
    height: h,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descriptor.instantiateCodec();
  final image = (await codec.getNextFrame()).image;
  codec.dispose();
  descriptor.dispose();
  buffer.dispose();
  return image;
}

Future<ui.Image> _fixtureDeCor() {
  final bytes = Uint8List(_w * _h * 4);
  for (var y = 0; y < _h; y++) {
    for (var x = 0; x < _w; x++) {
      final i = (y * _w + x) * 4;
      final c = _corDoFixture(x, y);
      bytes[i] = (c.r * 255).round();
      bytes[i + 1] = (c.g * 255).round();
      bytes[i + 2] = (c.b * 255).round();
      bytes[i + 3] = 255;
    }
  }
  return _imagem(bytes, _w, _h);
}

Future<Uint8List> _desenhar(ui.Image entrada, ui.FragmentShader shader) async {
  final w = entrada.width, h = entrada.height;
  final recorder = ui.PictureRecorder();
  ui.Canvas(recorder).drawRect(
    ui.Rect.fromLTWH(0, 0, w.toDouble(), h.toDouble()),
    ui.Paint()..shader = shader,
  );
  final picture = recorder.endRecording();
  final saida = await picture.toImage(w, h);
  picture.dispose();
  shader.dispose();
  final data = await saida.toByteData(format: ui.ImageByteFormat.rawRgba);
  saida.dispose();
  return data!.buffer.asUint8List();
}

EffectInstance _efeito(EffectType tipo, Map<String, double> valores) =>
    EffectInstance(
      type: tipo,
      params: {
        for (final e in effectSpecs[tipo]!.params.entries)
          e.key: AnimatedDouble(valores[e.key] ?? e.value.initial),
      },
    );

CorRgb _cadeia(CorRgb c, List<OperacaoDeCor> ops) {
  for (final op in ops) {
    c = aplicarOperacaoDeCor(c, op);
  }
  return c;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('as fichas', () {
    test('seis efeitos, todos em Cor, com os padroes do After Effects', () {
      final cor = [
        for (final e in effectSpecs.entries)
          if (e.value.category == 'Color') e.key,
      ];
      expect(cor.toSet(), {
        EffectType.unsharpMask,
        EffectType.levels,
        EffectType.brightnessContrast,
        EffectType.hueSaturation,
        EffectType.exposure,
        // O SEXTO (19/09): o Black & White e de cor e nao passa por aqui
        // — ele tem shader proprio, com doze numeros, e nao cabe nos dois
        // vec4 de uma operacao da passada fundida.
        EffectType.pretoEBranco,
      });
      for (final t in cor) {
        final spec = effectSpecs[t]!;
        expect(effectTypeFromId(spec.id), isNotNull, reason: spec.id);
        // TRES PRESETS E O PADRAO — menos o Black & White, que tem
        // quatro porque as faixas de cor pedem um ajuste a mais.
        expect(spec.presets.length, greaterThanOrEqualTo(3), reason: spec.id);
        for (final k in spec.montar) {
          expect(spec.params, contains(k), reason: '${spec.id}.$k');
        }
        for (final p in spec.presets) {
          for (final k in p.valores.keys) {
            expect(spec.params, contains(k), reason: '${spec.id} ${p.nome}');
          }
        }
      }
      final usm = effectSpecs[EffectType.unsharpMask]!.params;
      expect(usm['amount']!.initial, 50);
      expect(usm['amount']!.max, 500);
      expect(usm['radius']!.initial, 1);
      expect(usm['threshold']!.max, 255);
      final niveis = effectSpecs[EffectType.levels]!.params;
      expect(niveis['input_white']!.initial, 255);
      expect(niveis['gamma']!.initial, 1);
      final bc = effectSpecs[EffectType.brightnessContrast]!.params;
      expect((bc['brightness']!.min, bc['brightness']!.max), (-150, 150));
      expect(
        effectSpecs[EffectType.hueSaturation]!
            .params['colorize_saturation']!
            .initial,
        25,
      );
    });

    test('no padrao, os quatro de cor nao pagam passada nenhuma', () {
      for (final tipo in efeitosDeCorPorPixel) {
        expect(
          OperacaoDeCor.de(EffectInstance(type: tipo), Duration.zero),
          isNull,
          reason: effectSpecs[tipo]!.id,
        );
      }
      // O Unsharp Mask nasce ligado (50 %), como no AE; zero desliga.
      expect(
        ParametrosDeNitidez.de(
          EffectInstance(type: EffectType.unsharpMask),
          Duration.zero,
        ),
        isNotNull,
      );
      expect(
        ParametrosDeNitidez.de(
          _efeito(EffectType.unsharpMask, {'amount': 0}),
          Duration.zero,
        ),
        isNull,
      );
    });

    test('efeito desligado sai da cadeia; os ligados ficam na ordem', () {
      final a = _efeito(EffectType.exposure, {'exposure': 1});
      final b = _efeito(EffectType.levels, {'gamma': 2}).copyWith(
        enabled: false,
      );
      final c = _efeito(EffectType.hueSaturation, {'master_hue': 30});
      final ops = operacoesDeCor([a, b, c], Duration.zero);
      expect(ops.map((o) => o.modo), [
        ModoDeCor.exposicao,
        ModoDeCor.matizSaturacao,
      ]);
    });
  });

  group('a conta do After Effects', () {
    CorRgb cinza(double v) => (r: v, g: v, b: v);

    test('Levels: entrada, gama e saida em 0..255', () {
      final op = OperacaoDeCor.niveis(
        entradaPreto: 16,
        entradaBranco: 235,
        gama: 1.35,
        saidaPreto: 10,
        saidaBranco: 245,
      )!;
      final v = aplicarOperacaoDeCor(cinza(128 / 255), op).r;
      final esperado =
          (10 + 235 * math.pow((128 - 16) / (235 - 16), 1 / 1.35)) / 255;
      expect(v, closeTo(esperado, 1e-9));
      // Gama acima de 1 clareia o meio; preto e branco de entrada cortam.
      expect(aplicarOperacaoDeCor(cinza(10 / 255), op).r, closeTo(10 / 255, 1e-9));
      expect(aplicarOperacaoDeCor(cinza(1), op).r, closeTo(245 / 255, 1e-9));
      // Entrada invertida inverte a imagem.
      final inv = OperacaoDeCor.niveis(
        entradaPreto: 255,
        entradaBranco: 0,
        gama: 1,
        saidaPreto: 0,
        saidaBranco: 255,
      )!;
      expect(aplicarOperacaoDeCor(cinza(.2), inv).r, closeTo(.8, 1e-9));
    });

    test('Brightness & Contrast normal nunca tira o preto nem o branco', () {
      for (final (b, c) in [(150.0, 0.0), (-150.0, 0.0), (0.0, 100.0), (60.0, -100.0)]) {
        final op = OperacaoDeCor.brilhoContraste(
          brilho: b,
          contraste: c,
          legado: false,
        )!;
        expect(aplicarOperacaoDeCor(cinza(0), op).r, closeTo(0, 1e-9));
        expect(aplicarOperacaoDeCor(cinza(1), op).r, closeTo(1, 1e-9));
        var anterior = -1.0;
        for (var i = 0; i <= 50; i++) {
          final v = aplicarOperacaoDeCor(cinza(i / 50), op).r;
          expect(v, greaterThanOrEqualTo(anterior), reason: 'monotono $b $c');
          anterior = v;
        }
      }
      final claro = OperacaoDeCor.brilhoContraste(
        brilho: 150,
        contraste: 0,
        legado: false,
      )!;
      expect(aplicarOperacaoDeCor(cinza(.5), claro).r, greaterThan(.7));
    });

    test('Brightness & Contrast legado soma e estoura, como o AE antigo', () {
      final brilho = OperacaoDeCor.brilhoContraste(
        brilho: 51,
        contraste: 0,
        legado: true,
      )!;
      expect(aplicarOperacaoDeCor(cinza(.5), brilho).r, closeTo(.7, 1e-9));
      expect(aplicarOperacaoDeCor(cinza(.9), brilho).r, 1);
      final corte = OperacaoDeCor.brilhoContraste(
        brilho: 0,
        contraste: 100,
        legado: true,
      )!;
      expect(aplicarOperacaoDeCor(cinza(.49), corte).r, 0);
      expect(aplicarOperacaoDeCor(cinza(.51), corte).r, 1);
    });

    test('Hue/Saturation: -100 e cinza, 120 graus leva vermelho a verde', () {
      final pb = OperacaoDeCor.matizSaturacao(
        matiz: 0,
        saturacao: -100,
        luminosidade: 0,
        colorir: false,
        matizColorir: 0,
        saturacaoColorir: 25,
        luminosidadeColorir: 0,
      )!;
      final c = aplicarOperacaoDeCor((r: .9, g: .3, b: .1), pb);
      expect(c.r, closeTo(c.g, 1e-9));
      expect(c.g, closeTo(c.b, 1e-9));
      expect(c.r, closeTo(.5, 1e-9), reason: 'o cinza de mesma luminosidade HSL');

      final giro = OperacaoDeCor.matizSaturacao(
        matiz: 120,
        saturacao: 0,
        luminosidade: 0,
        colorir: false,
        matizColorir: 0,
        saturacaoColorir: 25,
        luminosidadeColorir: 0,
      )!;
      final verde = aplicarOperacaoDeCor((r: 1, g: 0, b: 0), giro);
      expect([verde.r, verde.g, verde.b], [closeTo(0, 1e-9), closeTo(1, 1e-9), closeTo(0, 1e-9)]);

      // Saturacao positiva para na saturacao cheia: nunca inverte.
      final vivo = OperacaoDeCor.matizSaturacao(
        matiz: 0,
        saturacao: 100,
        luminosidade: 0,
        colorir: false,
        matizColorir: 0,
        saturacaoColorir: 25,
        luminosidadeColorir: 0,
      )!;
      final s = aplicarOperacaoDeCor((r: .6, g: .5, b: .4), vivo);
      expect(s.r, closeTo(1, 1e-6));
      expect(s.b, closeTo(0, 1e-6));
    });

    test('Exposure: um stop e o dobro de LUZ, nao o dobro do numero', () {
      final op = OperacaoDeCor.exposicao(
        stops: 1,
        deslocamento: 0,
        gama: 1,
        ignorarLinear: false,
      )!;
      final v = aplicarOperacaoDeCor(cinza(.5), op).r;
      expect(v, closeTo(linearParaSrgb(srgbParaLinear(.5) * 2), 1e-9));
      expect(v, closeTo(.686, .002));
      // O jeito ingenuo (e o "Ignorar luz linear") dobra o numero.
      final cru = OperacaoDeCor.exposicao(
        stops: 1,
        deslocamento: 0,
        gama: 1,
        ignorarLinear: true,
      )!;
      expect(aplicarOperacaoDeCor(cinza(.5), cru).r, closeTo(1, 1e-9));
    });
  });

  group('os shaders desenham a mesma conta', () {
    setUpAll(() async {
      await MotorDeCorrecao.warmUp();
      expect(MotorDeCorrecao.falha, isNull);
      expect(MotorDeCorrecao.corPronta, isTrue);
      expect(MotorDeCorrecao.nitidezPronta, isTrue);
    });

    const amostras = [(0, 0), (3, 2), (12, 9), (20, 30), (31, 16), (40, 5), (47, 24), (58, 12), (63, 31)];

    Future<void> comparar(List<OperacaoDeCor> ops, String nome) async {
      final entrada = await _fixtureDeCor();
      final px = await _desenhar(
        entrada,
        MotorDeCorrecao.shaderDeCor(ops, imagem: entrada),
      );
      entrada.dispose();
      for (final (x, y) in amostras) {
        final i = (y * _w + x) * 4;
        final esperado = _cadeia(_corDoFixture(x, y), ops);
        final canais = [esperado.r, esperado.g, esperado.b];
        for (var c = 0; c < 3; c++) {
          expect(
            px[i + c].toDouble(),
            closeTo(canais[c] * 255, 1.6),
            reason: '$nome ($x,$y) canal $c',
          );
        }
        expect(px[i + 3], 255, reason: '$nome alfa');
      }
    }

    test('Levels', () async {
      await comparar([
        OperacaoDeCor.niveis(
          entradaPreto: 20,
          entradaBranco: 220,
          gama: .7,
          saidaPreto: 12,
          saidaBranco: 240,
        )!,
      ], 'levels');
    });

    test('Brightness & Contrast, normal e legado', () async {
      await comparar([
        OperacaoDeCor.brilhoContraste(brilho: 40, contraste: 45, legado: false)!,
      ], 'b&c');
      await comparar([
        OperacaoDeCor.brilhoContraste(brilho: -30, contraste: 60, legado: true)!,
      ], 'b&c legado');
    });

    test('Hue/Saturation, girando e colorindo', () async {
      await comparar([
        OperacaoDeCor.matizSaturacao(
          matiz: 75,
          saturacao: 40,
          luminosidade: -15,
          colorir: false,
          matizColorir: 0,
          saturacaoColorir: 25,
          luminosidadeColorir: 0,
        )!,
      ], 'h/s');
      await comparar([
        OperacaoDeCor.matizSaturacao(
          matiz: 0,
          saturacao: 0,
          luminosidade: 0,
          colorir: true,
          matizColorir: 35,
          saturacaoColorir: 30,
          luminosidadeColorir: 10,
        )!,
      ], 'colorir');
    });

    test('Exposure, em luz linear e crua', () async {
      await comparar([
        OperacaoDeCor.exposicao(stops: .8, deslocamento: .01, gama: 1.2, ignorarLinear: false)!,
      ], 'exposure');
      await comparar([
        OperacaoDeCor.exposicao(stops: -.5, deslocamento: 0, gama: .9, ignorarLinear: true)!,
      ], 'exposure crua');
    });

    test('QUATRO efeitos fundidos numa passada = os quatro em sequencia', () async {
      await comparar([
        OperacaoDeCor.exposicao(stops: .5, deslocamento: 0, gama: 1, ignorarLinear: false)!,
        OperacaoDeCor.niveis(entradaPreto: 10, entradaBranco: 245, gama: 1.2, saidaPreto: 0, saidaBranco: 255)!,
        OperacaoDeCor.matizSaturacao(matiz: -20, saturacao: 25, luminosidade: 0, colorir: false, matizColorir: 0, saturacaoColorir: 25, luminosidadeColorir: 0)!,
        OperacaoDeCor.brilhoContraste(brilho: 0, contraste: 30, legado: false)!,
      ], 'cadeia');
    });

    // ------------------------------------------------------ UNSHARP MASK

    /// Detalhe de verdade: listras finas, um degrau e ruido fixo.
    (Uint8List, List<double>) texturaOpaca() {
      final bytes = Uint8List(_w * _h * 4);
      final premul = List<double>.filled(_w * _h * 4, 0);
      final rnd = math.Random(7);
      for (var y = 0; y < _h; y++) {
        for (var x = 0; x < _w; x++) {
          final i = (y * _w + x) * 4;
          final base = x < 30 ? 60 : 190;
          final listra = (x + y) % 5 == 0 ? 40 : 0;
          final v = [
            (base + listra + rnd.nextInt(20)).clamp(0, 255),
            (base - listra + rnd.nextInt(20)).clamp(0, 255),
            (base + rnd.nextInt(30)).clamp(0, 255),
          ];
          for (var c = 0; c < 3; c++) {
            bytes[i + c] = v[c];
            premul[i + c] = v[c] / 255;
          }
          bytes[i + 3] = 255;
          premul[i + 3] = 1;
        }
      }
      return (bytes, premul);
    }

    Future<Uint8List> nitidez(
      Uint8List bytes, {
      required double raio,
      required double quantidade,
      double limiar = 0,
      bool luma = false,
      int w = _w,
      int h = _h,
    }) async {
      final entrada = await _imagem(bytes, w, h);
      final px = await _desenhar(
        entrada,
        MotorDeCorrecao.shaderDeNitidez(
          ParametrosDeNitidez(
            quantidade: quantidade,
            raio: raio,
            limiar: limiar / 255,
            soLuminancia: luma,
          ),
          imagem: entrada,
        ),
      );
      entrada.dispose();
      return px;
    }

    for (final (sigma, luma, limiar) in [
      (.5, false, 0.0),
      (1.0, false, 0.0),
      (1.5, false, 0.0),
      (1.0, true, 0.0),
      (1.0, false, 12.0),
    ]) {
      test('Unsharp Mask no nucleo exato: sigma $sigma, luma $luma, limiar $limiar', () async {
        final (bytes, premul) = texturaOpaca();
        final px = await nitidez(bytes, raio: sigma, quantidade: 1.5, limiar: limiar, luma: luma);
        final ref = unsharpMaskReferencia(
          premul,
          _w,
          _h,
          sigma: sigma,
          quantidade: 1.5,
          limiar: limiar / 255,
          soLuminancia: luma,
        );
        var mudou = 0;
        for (var y = 0; y < _h; y++) {
          for (var x = 0; x < _w; x++) {
            final i = (y * _w + x) * 4;
            for (var c = 0; c < 4; c++) {
              expect(
                px[i + c].toDouble(),
                closeTo(ref[i + c] * 255, 1.6),
                reason: '($x,$y) canal $c',
              );
              if ((px[i + c] - bytes[i + c]).abs() > 2) mudou++;
            }
          }
        }
        expect(mudou, greaterThan(100), reason: 'a nitidez tem de aparecer');
      });
    }

    test('imagem lisa continua lisa em qualquer raio (sem grao inventado)', () async {
      final bytes = Uint8List(_w * _h * 4);
      for (var i = 0; i < bytes.length; i += 4) {
        bytes[i] = 120;
        bytes[i + 1] = 90;
        bytes[i + 2] = 200;
        bytes[i + 3] = 255;
      }
      for (final raio in [.3, 1.0, 4.0, 20.0]) {
        final px = await nitidez(bytes, raio: raio, quantidade: 5);
        for (var i = 0; i < bytes.length; i++) {
          expect(px[i], bytes[i], reason: 'raio $raio byte $i');
        }
      }
    });

    test('raio grande (amostragem por importancia) afia o degrau sem inverter', () async {
      // Degrau vertical: escuro a esquerda, claro a direita.
      final bytes = Uint8List(_w * _h * 4);
      for (var y = 0; y < _h; y++) {
        for (var x = 0; x < _w; x++) {
          final i = (y * _w + x) * 4;
          final v = x < _w ~/ 2 ? 70 : 170;
          bytes[i] = v;
          bytes[i + 1] = v;
          bytes[i + 2] = v;
          bytes[i + 3] = 255;
        }
      }
      final px = await nitidez(bytes, raio: 4, quantidade: 1);
      final meio = _w ~/ 2;
      for (var y = 4; y < _h - 4; y++) {
        final escuroPerto = px[(y * _w + meio - 1) * 4];
        final claroPerto = px[(y * _w + meio) * 4];
        final escuroLonge = px[(y * _w + 2) * 4];
        final claroLonge = px[(y * _w + _w - 3) * 4];
        expect(escuroPerto, lessThan(70), reason: 'o escuro afunda na borda');
        expect(claroPerto, greaterThan(170), reason: 'o claro sobe na borda');
        expect(escuroLonge, closeTo(70, 3), reason: 'longe da borda nada muda');
        expect(claroLonge, closeTo(170, 3));
      }
    });

    test('limiar 255 protege tudo; quantidade zero nao existe', () async {
      final (bytes, _) = texturaOpaca();
      final px = await nitidez(bytes, raio: 1, quantidade: 3, limiar: 255);
      for (var i = 0; i < bytes.length; i++) {
        expect(px[i], bytes[i]);
      }
    });

    test('camada com transparencia: fora continua vazio, a cor nunca passa do alfa', () async {
      // Um quadrado branco opaco no meio de um fundo transparente.
      final bytes = Uint8List(_w * _h * 4);
      for (var y = 8; y < 24; y++) {
        for (var x = 16; x < 48; x++) {
          final i = (y * _w + x) * 4;
          bytes[i] = 255;
          bytes[i + 1] = 255;
          bytes[i + 2] = 255;
          bytes[i + 3] = 255;
        }
      }
      for (final raio in [1.0, 6.0]) {
        final px = await nitidez(bytes, raio: raio, quantidade: 2);
        for (var i = 0; i < px.length; i += 4) {
          expect(px[i], lessThanOrEqualTo(px[i + 3]), reason: 'premultiplicado');
        }
        // Bem longe do quadrado: nada.
        expect(px[(1 * _w + 1) * 4 + 3], 0);
        expect(px[(30 * _w + 62) * 4 + 3], 0);
        // No miolo: branco opaco.
        expect(px[(16 * _w + 32) * 4], 255);
        expect(px[(16 * _w + 32) * 4 + 3], 255);
      }
    });
  });
}
