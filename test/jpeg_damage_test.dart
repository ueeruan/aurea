// S_JPEGDAMAGE (16/09, "copiando do AE, IDENTICOS"). Compressao JPEG de
// verdade na GPU: DCT 8x8 quantizada com as tabelas do padrao (Anexo K) e
// a escala de qualidade da IJG, depois IDCT. Contra quadros renderizados
// pelo After Effects do dono o erro medio ficou em 1,2 a 1,9 em 255 (a
// imagem sem efeito fica a 2 a 6,6 do render).
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/jpeg_damage.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:flutter_test/flutter_test.dart';

EffectInstance _fx([Map<String, double> v = const {}]) => EffectInstance(
  type: EffectType.jpegDamage,
  params: {
    for (final e in effectSpecs[EffectType.jpegDamage]!.params.entries)
      e.key: AnimatedDouble(v[e.key] ?? e.value.initial),
  },
);

Future<Uint8List> _jpeg(
  EffectInstance fx,
  int w,
  int h,
  int Function(int x, int y, int c) cor,
) async {
  final prog = await ui.FragmentProgram.fromAsset('shaders/jpeg_damage.frag');
  final bytes = Uint8List(w * h * 4);
  for (var y = 0; y < h; y++) {
    for (var x = 0; x < w; x++) {
      final i = (y * w + x) * 4;
      for (var c = 0; c < 3; c++) {
        bytes[i + c] = cor(x, y, c);
      }
      bytes[i + 3] = 255;
    }
  }
  final buf = await ui.ImmutableBuffer.fromUint8List(bytes);
  final desc = ui.ImageDescriptor.raw(
    buf,
    width: w,
    height: h,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  var img = (await (await desc.instantiateCodec()).getNextFrame()).image;
  final vals = valoresJpegDamage(fx, Duration.zero);
  for (final modo in [0.0, 1.0]) {
    final s = prog.fragmentShader()
      ..setFloat(0, w.toDouble())
      ..setFloat(1, h.toDouble())
      ..setFloat(2, 0)
      ..setFloat(3, w.toDouble())
      ..setFloat(4, h.toDouble())
      ..setFloat(5, 1)
      ..setFloat(6, 0)
      ..setFloat(7, modo);
    for (var i = 0; i < vals.length; i++) {
      s.setFloat(8 + i, vals[i]);
    }
    s.setImageSampler(0, img);
    final rec = ui.PictureRecorder();
    ui.Canvas(rec).drawRect(
      ui.Rect.fromLTWH(0, 0, w.toDouble(), h.toDouble()),
      ui.Paint()
        ..blendMode = ui.BlendMode.src
        ..shader = s,
    );
    img = await rec.endRecording().toImage(w, h);
  }
  return (await img.toByteData(format: ui.ImageByteFormat.rawRgba))!
      .buffer
      .asUint8List();
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('padroes do AE e a escala de qualidade da IJG', () {
    final p = effectSpecs[EffectType.jpegDamage]!.params;
    expect(p['quality']!.initial, .1);
    expect(p['affect_chroma']!.initial, .5);
    expect(p['err_block_density']!.initial, .75);
    // Qualidade 10 %: escala 500 -> o DC de luma (16) vira 80.
    expect(passoDeQuantizacao(16, .1), 80);
    // Qualidade 50 %: tabela do padrao.
    expect(passoDeQuantizacao(16, .5), 16);
    expect(passoDeQuantizacao(99, 1), 1);
    expect(valoresJpegDamage(_fx(), Duration.zero), hasLength(104));
  });

  // Na qualidade 10 % o DC da croma quantiza com passo 85 e um tom liso ja
  // anda ate ~9 niveis — como num JPEG de verdade. Em 50 % o passo e 17.
  test('cor lisa atravessa a compressao sem mudar', () async {
    final px = await _jpeg(_fx({'quality': .5}), 32, 32, (x, y, c) => [180, 90, 60][c]);
    for (var i = 0; i < px.length; i += 4) {
      expect(px[i], closeTo(180, 3));
      expect(px[i + 1], closeTo(90, 3));
      expect(px[i + 2], closeTo(60, 3));
    }
  });

  test('qualidade baixa achata o bloco; qualidade alta preserva', () async {
    int degrade(int x, int y, int c) => (x * 7 + y * 3) % 256;
    final ruim = await _jpeg(_fx({'quality': .01}), 32, 32, degrade);
    final boa = await _jpeg(_fx({'quality': 1}), 32, 32, degrade);
    double erro(Uint8List px) {
      var soma = 0.0;
      for (var y = 0; y < 32; y++) {
        for (var x = 0; x < 32; x++) {
          soma += (px[(y * 32 + x) * 4] - degrade(x, y, 0)).abs();
        }
      }
      return soma / (32 * 32);
    }

    expect(erro(boa), lessThan(3));
    expect(erro(ruim), greaterThan(erro(boa) + 3));
  });
}
