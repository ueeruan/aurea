// O VIDEO TERMINA ONDE A ULTIMA CAMADA TERMINA.
//
// Relato do dono: um projeto curto saia com cauda preta. A culpa era do piso
// de cinco segundos de `VideoProject.duration`, que existe para a linha do
// tempo NASCER utilizavel (largura para arrastar um clipe num projeto vazio).
// Esse piso e bom para editar e errado para gravar: quem exporta usa
// `duracaoDoConteudo`, que nao tem piso nenhum.
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

VideoProject _com(List<Layer> camadas) =>
    VideoProject.empty('duracao').copyWith(layers: camadas);

ShapeLayer _forma(Duration inicio, Duration duracao) => ShapeLayer(
  name: 'forma',
  startTime: inicio,
  duration: duracao,
);

void main() {
  test('projeto curto exporta so o que existe, e a linha do tempo mantem o piso',
      () {
    final p = _com([_forma(Duration.zero, const Duration(seconds: 2))]);
    expect(p.duracaoDoConteudo, const Duration(seconds: 2));
    // A linha do tempo continua com os cinco segundos de folga.
    expect(p.duration, const Duration(seconds: 5));
  });

  test('vale o fim da camada que termina por ultimo, nao a mais longa', () {
    final p = _com([
      _forma(Duration.zero, const Duration(seconds: 9)),
      _forma(const Duration(seconds: 8), const Duration(seconds: 3)),
    ]);
    expect(p.duracaoDoConteudo, const Duration(seconds: 11));
    expect(p.duration, const Duration(seconds: 11));
  });

  test('projeto longo: exportacao e linha do tempo dizem o mesmo', () {
    final p = _com([_forma(Duration.zero, const Duration(seconds: 30))]);
    expect(p.duracaoDoConteudo, p.duration);
  });

  test('projeto vazio nao tem o que gravar', () {
    final p = _com(const []);
    expect(p.duracaoDoConteudo, Duration.zero);
    expect(p.duration, const Duration(seconds: 5));
  });
}
