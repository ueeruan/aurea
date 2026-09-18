import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/export/application/export_engine.dart';
import 'package:flutter_test/flutter_test.dart';

/// INTERPOLACAO DE QUADROS: a conta que decide quantos quadros inventar.
///
/// A camera lenta pede quadros que a fonte nao tem. O pedido foi
/// "funcional e otimizado": funcional e o ffmpeg inventando os quadros
/// do meio (minterpolate, que roda igual no Android e no iOS); otimizado
/// e so pagar por isso quando o clipe anda mais devagar que a fonte —
/// e nunca mais que quatro vezes, que e onde o ganho acaba e o custo
/// continua subindo.
VideoLayer _clipe({
  double speed = 1,
  InterpolacaoDeQuadros modo = InterpolacaoDeQuadros.movimento,
}) => VideoLayer(
  name: 'v',
  startTime: Duration.zero,
  duration: const Duration(seconds: 4),
  sourcePath: '/x.mp4',
  speed: speed,
  interpolacao: modo,
  position: AnimatedOffset(const Offset(960, 540)),
);

void main() {
  test('velocidade normal ou rapida: nada a inventar', () {
    expect(fatorDeInterpolacao(_clipe()), 1);
    expect(fatorDeInterpolacao(_clipe(speed: 2)), 1);
    expect(filtroDeInterpolacao(_clipe(speed: 2), fps: 30), '');
  });

  test('camera lenta pede o inverso da velocidade, ate quatro', () {
    expect(fatorDeInterpolacao(_clipe(speed: .5)), 2);
    expect(fatorDeInterpolacao(_clipe(speed: .25)), 4);
    expect(fatorDeInterpolacao(_clipe(speed: .1)), 4, reason: 'teto');
    expect(
      fatorDeInterpolacao(_clipe(speed: .4)),
      3,
      reason: 'arredonda para cima',
    );
  });

  test('sem pedir interpolacao, a camera lenta continua repetindo quadros', () {
    final c = _clipe(speed: .25, modo: InterpolacaoDeQuadros.nenhuma);
    expect(fatorDeInterpolacao(c), 1);
    expect(filtroDeInterpolacao(c, fps: 30), '');
  });

  test('o filtro e o do ffmpeg, na taxa certa, com a virgula no fim', () {
    expect(
      filtroDeInterpolacao(_clipe(speed: .5), fps: 30),
      'minterpolate=fps=60:mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1:scd=fdiff:scd_threshold=10,',
    );
    expect(
      filtroDeInterpolacao(
        _clipe(speed: .5, modo: InterpolacaoDeQuadros.mesclar),
        fps: 24,
      ),
      'minterpolate=fps=48:mi_mode=blend,',
    );
  });

  test(
    'no time remap, vale o trecho mais lento — e o quadro segurado nao conta',
    () {
      final base = _clipe();
      // Um trecho a 25% e um trecho segurado (mesmo valor nos dois lados).
      final track = AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(seconds: 2), 0.5)
          .withKeyframe(const Duration(seconds: 3), 0.5)
          .withKeyframe(const Duration(seconds: 4), 1.5);
      final remap = base.copyLayer(timeRemap: track);
      expect(hasTimeRemap(remap), isTrue);
      expect(velocidadeMaisLenta(remap), closeTo(0.25, 1e-9));
      expect(fatorDeInterpolacao(remap), 4);
    },
  );

  test('o modo sobrevive a gravacao do projeto', () {
    final p = VideoProject(
      name: 'p',
      createdAt: DateTime(2026, 9, 8),
      layers: [_clipe(speed: .5, modo: InterpolacaoDeQuadros.mesclar)],
    );
    final volta = projectFromJson(projectToJson(p));
    final v = volta.layers.single as VideoLayer;
    expect(v.interpolacao, InterpolacaoDeQuadros.mesclar);
    // O padrao nao entra no arquivo: projeto antigo continua igual.
    final semNada = projectToJson(
      VideoProject(
        name: 'p',
        createdAt: DateTime(2026),
        layers: [_clipe(modo: InterpolacaoDeQuadros.nenhuma)],
      ),
    );
    expect((semNada['layers'] as List).single['interpolacao'], isNull);
  });
}
