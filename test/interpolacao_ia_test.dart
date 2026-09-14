// O MODO "IA" da interpolacao de quadros: escolha explicita do RIFE, com
// queda honesta para o fluxo optico do ffmpeg quando o motor nao existe.
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/plano_de_interpolacao.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/temporal_interpolation.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

VideoLayer _lento(InterpolacaoDeQuadros modo) => VideoLayer(
  name: 'v',
  startTime: Duration.zero,
  duration: const Duration(seconds: 4),
  sourceDuration: const Duration(seconds: 4),
  sourcePath: 'x.mp4',
  speed: .5,
  interpolacao: modo,
);

void main() {
  test('IA usa o RIFE quando ha motor e cai no mci quando nao ha', () {
    final l = _lento(InterpolacaoDeQuadros.ia);
    final com = estrategiaDeInterpolacao(
      l,
      fps: 30,
      fpsDaFonte: 30,
      rifeDisponivel: true,
    );
    expect(com.como, ComoInterpolar.rife);
    final sem = estrategiaDeInterpolacao(
      l,
      fps: 30,
      fpsDaFonte: 30,
      rifeDisponivel: false,
    );
    expect(sem.como, ComoInterpolar.ffmpeg);
    expect(filtroDeInterpolacao(l, fps: 30), contains('mi_mode=mci'));
    expect(
      filtroDeInterpolacao(_lento(InterpolacaoDeQuadros.mesclar), fps: 30),
      contains('mi_mode=blend'),
    );
  });

  test('o modo IA sobrevive a salvar e abrir o projeto', () {
    final p = VideoProject(
      name: 'p',
      createdAt: DateTime(2026),
      layers: [_lento(InterpolacaoDeQuadros.ia)],
    );
    final volta = projectFromJson(projectToJson(p)).layers.single as VideoLayer;
    expect(volta.interpolacao, InterpolacaoDeQuadros.ia);
  });
}
