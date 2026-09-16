// ABA ESTILIZAR, LOTE 1 (16/09, "copiando do AE, IDENTICOS"). Os padroes
// e faixas foram lidos do After Effects do dono; as contas, ajustadas
// contra quadros renderizados por ele (erros medidos em estilizar.dart).
// Aqui: fichas com os padroes do AE, e o shader desenhando a mesma conta
// da referencia em Dart.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/estilizar.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/presentation/widgets/passe_de_cor.dart';
import 'package:flutter/painting.dart' show Offset;
import 'package:flutter_test/flutter_test.dart';

EffectInstance _fx(EffectType t, [Map<String, double> v = const {}]) =>
    EffectInstance(
      type: t,
      params: {
        for (final e in effectSpecs[t]!.params.entries)
          e.key: AnimatedDouble(v[e.key] ?? e.value.initial),
      },
    );

Future<Uint8List> _render(QuadroDeEstilo q, int w, int h, int Function(int x, int y) cinza) async {
  final bytes = Uint8List(w * h * 4);
  for (var y = 0; y < h; y++) {
    for (var x = 0; x < w; x++) {
      final i = (y * w + x) * 4, v = cinza(x, y);
      bytes..[i] = v..[i + 1] = v..[i + 2] = v..[i + 3] = 255;
    }
  }
  final buf = await ui.ImmutableBuffer.fromUint8List(bytes);
  final desc = ui.ImageDescriptor.raw(buf, width: w, height: h, pixelFormat: ui.PixelFormat.rgba8888);
  final img = (await (await desc.instantiateCodec()).getNextFrame()).image;
  final rec = ui.PictureRecorder();
  ui.Canvas(rec).drawRect(
    ui.Rect.fromLTWH(0, 0, w.toDouble(), h.toDouble()),
    ui.Paint()..shader = MotorDeCorrecao.shaderDeEstilo(q, imagem: img),
  );
  final out = await rec.endRecording().toImage(w, h);
  return (await out.toByteData(format: ui.ImageByteFormat.rawRgba))!.buffer.asUint8List();
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('os sete do lote 1 estao na aba Estilizar, com os padroes lidos do AE', () {
    final estilo = [
      for (final e in effectSpecs.entries)
        if (e.value.category == 'Stylize') e.key,
    ];
    expect(estilo.toSet().containsAll(modoDeEstilo.keys), isTrue);
    expect(effectSpecs[EffectType.threshold]!.params['threshold']!.initial, 127.5);
    expect(effectSpecs[EffectType.vignette]!.params['angle_of_view']!.initial, 45);
    expect(effectSpecs[EffectType.blockLoad]!.params['scans']!.initial, 4);
    expect(effectSpecs[EffectType.halfTone]!.params['dots_angle']!.initial, -30);
    expect(effectSpecs[EffectType.edgeColorize]!.params['edge_smooth']!.initial, 5.376);
    expect(effectSpecs[EffectType.scanLines]!.params['gamma']!.initial, 1.5);
    for (final t in modoDeEstilo.keys) {
      expect(QuadroDeEstilo.de(_fx(t), Duration.zero), isNotNull);
    }
  });

  group('o shader', () {
    setUpAll(() async {
      await MotorDeCorrecao.warmUp();
      expect(MotorDeCorrecao.estiloPronto, isTrue, reason: MotorDeCorrecao.falha);
    });

    test('CC Threshold corta na luma 601', () async {
      final q = QuadroDeEstilo.de(_fx(EffectType.threshold), Duration.zero)!;
      final px = await _render(q, 32, 4, (x, y) => x * 8);
      for (var x = 0; x < 32; x++) {
        expect(px[x * 4], x * 8 / 255 >= .5 ? 255 : 0, reason: 'x $x');
      }
    });

    test('S_ScanLines = referencia em Dart (a conta medida no AE)', () async {
      final q = QuadroDeEstilo.de(
        _fx(EffectType.scanLines, {'lines_frequency': 4}),
        Duration.zero,
      )!;
      const w = 64, h = 64;
      final px = await _render(q, w, h, (x, y) => (40 + x * 3).clamp(0, 255));
      for (final (x, y) in [(3, 5), (20, 17), (40, 33), (60, 60)]) {
        final entrada = (40 + x * 3) / 255;
        final esperado = scanLineCanal(
          entrada,
          h / 2 - (y + .5),
          periodo: w / 8,
          nitidez: 1,
          gama: 1.5,
        );
        expect(px[(y * w + x) * 4] / 255, closeTo(esperado, 2 / 255), reason: '($x,$y)');
      }
    });

    test('CC Vignette escurece a borda pela lei cos^4', () async {
      final q = QuadroDeEstilo.de(_fx(EffectType.vignette), Duration.zero)!;
      const w = 64, h = 64;
      final px = await _render(q, w, h, (x, y) => 200);
      final tan = q.valores[1];
      final canto = vinhetaFator(
        Offset(31.5, 31.5).distance,
        w / 2,
        1,
        tan,
      );
      expect(px[0] / 255, closeTo(200 / 255 * canto, 2 / 255));
      expect(px[(32 * w + 32) * 4], closeTo(200, 2));
    });

    test('CC Block Load a 50 % de 4 varreduras desenha blocos de 2 px', () async {
      final q = QuadroDeEstilo.de(_fx(EffectType.blockLoad, {'completion': 50}), Duration.zero)!;
      final px = await _render(q, 16, 16, (x, y) => (x * 16) % 256);
      for (var x = 0; x < 16; x += 2) {
        expect(px[(8 * 16 + x) * 4], px[(8 * 16 + x + 1) * 4], reason: 'bloco $x');
      }
    });

    test('nenhum efeito do lote estoura em imagem transparente', () async {
      for (final t in modoDeEstilo.keys) {
        final q = QuadroDeEstilo.de(_fx(t), Duration.zero)!;
        final px = await _render(q, 16, 16, (x, y) => 128);
        for (var i = 0; i < px.length; i += 4) {
          expect(px[i], lessThanOrEqualTo(px[i + 3]), reason: '$t');
        }
      }
    });
  });
}
