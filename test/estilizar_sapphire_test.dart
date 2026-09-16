// ABA ESTILIZAR, LOTE 2 (Sapphire). Todo efeito com receita roda o seu
// shader de verdade sobre uma imagem com detalhe, com os padroes lidos do
// After Effects: tem de carregar, mudar a imagem e devolver cor
// pre-multiplicada valida.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/estilizar_lote2.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:flutter_test/flutter_test.dart';

const _lado = 48;

Future<ui.Image> _fixture() async {
  final bytes = Uint8List(_lado * _lado * 4);
  for (var y = 0; y < _lado; y++) {
    for (var x = 0; x < _lado; x++) {
      final i = (y * _lado + x) * 4;
      bytes[i] = (x * 5) % 256;
      bytes[i + 1] = (y * 5) % 256;
      bytes[i + 2] = ((x + y) % 12 < 6) ? 220 : 40;
      bytes[i + 3] = 255;
    }
  }
  final buf = await ui.ImmutableBuffer.fromUint8List(bytes);
  final desc = ui.ImageDescriptor.raw(
    buf,
    width: _lado,
    height: _lado,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  return (await (await desc.instantiateCodec()).getNextFrame()).image;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  for (final entrada in receitasSapphire.entries) {
    final tipo = entrada.key;
    final receita = entrada.value;
    test('${effectSpecs[tipo]!.name} roda com os parametros do AE', () async {
      final spec = effectSpecs[tipo]!;
      expect(['Stylize', 'Distort'], contains(spec.category));
      final fx = EffectInstance(
        type: tipo,
        params: {
          // O primeiro preset por cima do padrao: Optics Compensation nasce
          // com FOV 0, que no AE nao muda nada.
          for (final e in spec.params.entries)
            e.key: AnimatedDouble(
              spec.presets.first.valores[e.key] ?? e.value.initial,
            ),
        },
      );
      // Efeito que dispara em rajadas (Cross Glitch) pode estar quieto num
      // instante: vale o primeiro instante em que ele aparece.
      var mudouEmAlgum = 0;
      for (final ms in [500, 1100, 1700, 2300, 3100]) {
      final valores = receita.valores(fx, Duration(milliseconds: ms));
      expect(valores.every((v) => v.isFinite), isTrue);
      final prog = await ui.FragmentProgram.fromAsset(receita.asset);
      var img = await _fixture();
      final original = (await img.toByteData(
        format: ui.ImageByteFormat.rawRgba,
      ))!.buffer.asUint8List();
      for (var modo = 0; modo < receita.passadas; modo++) {
        final s = prog.fragmentShader()
          ..setFloat(0, _lado.toDouble())
          ..setFloat(1, _lado.toDouble())
          ..setFloat(2, 0)
          ..setFloat(3, _lado.toDouble())
          ..setFloat(4, _lado.toDouble())
          ..setFloat(5, 1)
          ..setFloat(6, ms / 1000)
          ..setFloat(7, modo.toDouble());
        for (var i = 0; i < valores.length; i++) {
          s.setFloat(8 + i, valores[i]);
        }
        final cores = receita.coresDe(fx);
        for (var k = 0; k < cores.length; k++) {
          s
            ..setFloat(72 + 4 * k, cores[k].r)
            ..setFloat(73 + 4 * k, cores[k].g)
            ..setFloat(74 + 4 * k, cores[k].b)
            ..setFloat(75 + 4 * k, cores[k].a);
        }
        s.setImageSampler(0, img);
        final rec = ui.PictureRecorder();
        ui.Canvas(rec).drawRect(
          const ui.Rect.fromLTWH(0, 0, _lado * 1.0, _lado * 1.0),
          ui.Paint()
            ..blendMode = ui.BlendMode.src
            ..shader = s,
        );
        img = await rec.endRecording().toImage(_lado, _lado);
      }
      final px = (await img.toByteData(
        format: ui.ImageByteFormat.rawRgba,
      ))!.buffer.asUint8List();
      var mudou = 0;
      for (var i = 0; i < px.length; i += 4) {
        expect(px[i], lessThanOrEqualTo(px[i + 3]), reason: 'pre-multiplicado');
        if ((px[i] - original[i]).abs() > 4 ||
            (px[i + 1] - original[i + 1]).abs() > 4) {
          mudou++;
        }
      }
      mudouEmAlgum = mudou > mudouEmAlgum ? mudou : mudouEmAlgum;
      if (mudouEmAlgum > 20) break;
      }
      expect(mudouEmAlgum, greaterThan(20), reason: 'o efeito tem de aparecer');
    });
  }
}
