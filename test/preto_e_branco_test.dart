// BLACK & WHITE, MEDIDO NO PIXEL.
//
// A PERGUNTA DESTE ARQUIVO: as seis faixas REALMENTE mandam no cinza? O
// que se prova aqui, uma por uma:
//
//   1. uma cor pura sai com o valor da PROPRIA faixa (vermelho com
//      Vermelhos em 40 da 0,40 — nao a luminancia de 0,21);
//   2. a faixa responde nos dois sentidos: negativa escurece ate o preto,
//      acima de 100 clareia ate o branco;
//   3. CINZA PURO NAO SE MEXE, com faixa nenhuma;
//   4. cada faixa manda na sua familia e nao na do vizinho;
//   5. mistura 0 devolve a imagem intacta; tingir multiplica o cinza pela
//      cor escolhida;
//   6. a ficha nao perde parametro.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/preto_e_branco.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// A IMAGEM DE ENTRADA: 4x4 de UMA cor so. A cor e a variavel do teste.
Future<ui.Image> _liso(Color cor, {int lado = 4}) async {
  final pixels = Uint8List(lado * lado * 4);
  for (var i = 0; i < pixels.length; i += 4) {
    pixels[i] = (cor.r * 255).round();
    pixels[i + 1] = (cor.g * 255).round();
    pixels[i + 2] = (cor.b * 255).round();
    pixels[i + 3] = 255;
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

/// Os doze floats, na ordem em que o `.frag` os declara.
List<double> _valores({
  double reds = 40,
  double yellows = 60,
  double greens = 40,
  double cyans = 60,
  double blues = 20,
  double magentas = 80,
  double mix = 100,
  double tint = 0,
  Color cor = const Color(0xFFE8C79A),
}) => [
  reds / 100,
  yellows / 100,
  greens / 100,
  cyans / 100,
  blues / 100,
  magentas / 100,
  mix / 100,
  tint / 100,
  cor.r,
  cor.g,
  cor.b,
  0,
];

/// Desenha o shader e devolve o pixel do canto.
///
/// A ORDEM DOS `setFloat` E A DA DECLARACAO (ver
/// `glsl-uniforme-por-declaracao`): 2 de uSize, uFilter, 2 de uLogico,
/// uEscalaRef, uTempo, uModo, e entao p0, p1 e c0 em 72.
Future<({double r, double g, double b, double a})> _passar(
  ui.Image fonte, {
  required List<double> valores,
  int lado = 4,
}) async {
  final programa = await ui.FragmentProgram.fromAsset(
    'shaders/preto_e_branco.frag',
  );
  final shader = programa.fragmentShader();
  try {
    final l = lado.toDouble();
    shader
      ..setFloat(0, l)
      ..setFloat(1, l)
      ..setFloat(2, 0) // uFilter
      ..setFloat(3, l)
      ..setFloat(4, l)
      ..setFloat(5, 1) // uEscalaRef
      ..setFloat(6, 0) // uTempo
      ..setFloat(7, 0) // uModo
      ..setImageSampler(0, fonte);
    // p0 e p1 em 8..15; as cores em 72..75, depois dos dezesseis p — a
    // mesma posicao que o `MotorSapphire` escreve.
    for (var i = 0; i < 8; i++) {
      shader.setFloat(8 + i, valores[i]);
    }
    shader.setFloat(72, valores[8]);
    shader.setFloat(73, valores[9]);
    shader.setFloat(74, valores[10]);

    final gravador = ui.PictureRecorder();
    ui.Canvas(gravador).drawRect(
      ui.Rect.fromLTWH(0, 0, l, l),
      ui.Paint()..shader = shader,
    );
    final imagem = await gravador.endRecording().toImage(lado, lado);
    final dados = await imagem.toByteData(format: ui.ImageByteFormat.rawRgba);
    imagem.dispose();
    final p = dados!.buffer.asUint8List();
    return (
      r: p[0] / 255,
      g: p[1] / 255,
      b: p[2] / 255,
      a: p[3] / 255,
    );
  } finally {
    shader.dispose();
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('as faixas mandam no cinza', () {
    test('VERMELHO PURO sai com o valor da faixa, e nao com a luminancia', () async {
      // A DIFERENCA QUE DEFINE O EFEITO. A luminancia do vermelho puro e
      // 0,2126; com Vermelhos em 40 o resultado tem de ser 0,40.
      final fonte = await _liso(const Color(0xFFFF0000));
      final q = await _passar(fonte, valores: _valores(reds: 40));
      expect(q.r, closeTo(.40, .01));
      expect(q.g, closeTo(.40, .01));
      expect(q.b, closeTo(.40, .01));
      fonte.dispose();
    });

    test('a faixa responde nos DOIS sentidos', () async {
      final fonte = await _liso(const Color(0xFFFF0000));
      final cem = await _passar(fonte, valores: _valores(reds: 100));
      expect(cem.r, closeTo(1, .01));

      final duzentos = await _passar(fonte, valores: _valores(reds: 200));
      expect(duzentos.r, closeTo(1, .01));

      final negativo = await _passar(fonte, valores: _valores(reds: -100));
      expect(negativo.r, closeTo(0, .01));

      final meio = await _passar(fonte, valores: _valores(reds: 60));
      expect(meio.r, closeTo(.60, .01));
      fonte.dispose();
    });

    test('CINZA PURO NAO SE MEXE, com faixa nenhuma', () async {
      final fonte = await _liso(const Color(0xFF808080));
      for (final v in [
        _valores(reds: -200),
        _valores(reds: 300),
        _valores(blues: 300, greens: -200, magentas: 300),
      ]) {
        final q = await _passar(fonte, valores: v);
        expect(q.r, closeTo(0x80 / 255, .01));
        expect(q.g, closeTo(0x80 / 255, .01));
        expect(q.b, closeTo(0x80 / 255, .01));
      }
      fonte.dispose();
    });

    test('cada faixa manda na familia dela, e nao na vizinha', () async {
      // AMARELO: quem manda e a faixa dos amarelos. Mexer nos azuis nao
      // pode mudar um amarelo.
      final amarelo = await _liso(const Color(0xFFFFE100));
      final a = await _passar(amarelo, valores: _valores(yellows: 30));
      expect(a.r, closeTo(.30, .02));
      final b = await _passar(amarelo, valores: _valores(yellows: 30, blues: 300));
      expect(b.r, closeTo(a.r, .01));
      amarelo.dispose();

      // AZUL: quem manda e a faixa dos azuis.
      final azul = await _liso(const Color(0xFF0028FF));
      final c = await _passar(azul, valores: _valores(blues: 50));
      expect(c.b, closeTo(.50, .02));
      final d = await _passar(azul, valores: _valores(blues: 150));
      expect(d.b, closeTo(1, .02));
      azul.dispose();
    });

    test('verde e ciano tambem sao familias proprias', () async {
      final verde = await _liso(const Color(0xFF00FF00));
      final v = await _passar(verde, valores: _valores(greens: 80));
      expect(v.g, closeTo(.80, .02));
      verde.dispose();

      // O CIANO NAO E SATURADO PURO: o valor dele e 0,898, e a faixa
      // multiplica O VALOR — 0,898 x 0,35.
      final ciano = await _liso(const Color(0xFF00E5E5));
      final c = await _passar(ciano, valores: _valores(cyans: 35));
      expect(c.g, closeTo(0xE5 / 255 * .35, .02));
      ciano.dispose();

      // MAGENTA: a matiz cai a 6 graus do centro, entao a familia leva
      // quase toda a faixa — e o resto vai para os vermelhos, que sao
      // vizinhos dela.
      final magenta = await _liso(const Color(0xFFFF00E5));
      final m = await _passar(magenta, valores: _valores(magentas: 70));
      expect(m.r, closeTo(.70, .06));
      magenta.dispose();
    });
  });

  group('mistura e tingimento', () {
    test('mistura 0 devolve a imagem intacta', () async {
      final fonte = await _liso(const Color(0xFFFF0000));
      final q = await _passar(fonte, valores: _valores(mix: 0, reds: 10));
      expect(q.r, closeTo(1, .01));
      expect(q.g, closeTo(0, .01));
      expect(q.b, closeTo(0, .01));
      fonte.dispose();
    });

    test('tingir multiplica o cinza pela cor escolhida', () async {
      // SEPIA: o cinza toma a cor, e o preto continua preto.
      final fonte = await _liso(const Color(0xFFFFFFFF));
      const sepia = Color(0xFFE8C79A);
      final q = await _passar(fonte, valores: _valores(tint: 100, cor: sepia));
      expect(q.r, closeTo(sepia.r, .02));
      expect(q.g, closeTo(sepia.g, .02));
      expect(q.b, closeTo(sepia.b, .02));
      fonte.dispose();
    });

    test('tingir 0 nao muda nada', () async {
      final fonte = await _liso(const Color(0xFFFFFFFF));
      final sem = await _passar(fonte, valores: _valores(tint: 0));
      final com = await _passar(
        fonte,
        valores: _valores(tint: 0, cor: const Color(0xFFFF0000)),
      );
      expect(com.r, closeTo(sem.r, .001));
      expect(com.g, closeTo(sem.g, .001));
      fonte.dispose();
    });
  });

  group('a ficha', () {
    final spec = effectSpecs[EffectType.pretoEBranco]!;

    test('e o Black & White, com as seis faixas', () {
      expect(spec.name, 'Black & White');
      expect(spec.id, 'black_and_white');
      expect(spec.hasColor, isTrue);
      for (final k in [
        'reds', 'yellows', 'greens', 'cyans', 'blues', 'magentas',
        'tint', 'mix',
      ]) {
        expect(spec.params.containsKey(k), isTrue, reason: 'falta $k');
      }
      // OS PADROES DO AFTER EFFECTS.
      expect(spec.params['reds']!.initial, 40);
      expect(spec.params['blues']!.initial, 20);
      expect(spec.params['magentas']!.initial, 80);
    });

    test('presets e montar so apontam para chave que existe', () {
      expect(spec.presets.length, 4);
      for (final p in spec.presets) {
        for (final k in p.valores.keys) {
          expect(spec.params.containsKey(k), isTrue,
              reason: '${p.nome} pede $k, que nao existe');
        }
      }
      expect(spec.montar.length, lessThanOrEqualTo(3));
      for (final k in spec.montar) {
        expect(spec.params.containsKey(k), isTrue);
      }
    });

    test('o avaliador entrega os doze floats, sem NaN', () {
      var e = EffectInstance(type: EffectType.pretoEBranco);
      final base = valoresPretoEBranco(e, Duration.zero);
      expect(base.length, 12);
      expect(base.every((x) => x.isFinite), isTrue);
      expect(base[0], closeTo(.40, 1e-9));
      expect(base[6], closeTo(1, 1e-9));

      // EXTREMOS: nenhum valor da ficha pode produzir NaN.
      for (final entry in spec.params.entries) {
        for (final valor in [entry.value.min, entry.value.max]) {
          e = EffectInstance(type: EffectType.pretoEBranco)
              .withParamEdited(entry.key, Duration.zero, valor);
          final v = valoresPretoEBranco(e, Duration.zero);
          expect(v.length, 12);
          expect(v.every((x) => x.isFinite), isTrue,
              reason: '${entry.key} = $valor');
        }
      }
    });

    test('todos os parametros sao animaveis', () {
      var e = EffectInstance(type: EffectType.pretoEBranco);
      e = e.withParamEdited('reds', Duration.zero, 40);
      e = e.withKeyframeToggled(Duration.zero);
      e = e.withParamEdited('reds', const Duration(seconds: 1), 200, forcar: true);
      expect(valoresPretoEBranco(e, Duration.zero)[0], closeTo(.40, 1e-9));
      expect(
        valoresPretoEBranco(e, const Duration(seconds: 1))[0],
        closeTo(2.0, 1e-9),
      );
      expect(
        valoresPretoEBranco(e, const Duration(milliseconds: 500))[0],
        closeTo(1.2, 1e-9),
      );
    });
  });
}
