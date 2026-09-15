// OPCOES DE VISUALIZACAO (v1.1.1): o palco obedece, o arquivo nao.
//
// "Sem efeitos" e "vista livre" sao ajudas de montagem. A regra que estes
// testes seguram e a de sempre: o que a pessoa liga para trabalhar nunca
// sai no video — o projeto cru continua intacto e so a copia do palco
// perde efeito ou camera.
import 'dart:typed_data';

import 'package:aurea/src/features/editor/application/ui/opcoes_de_visualizacao.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

VideoProject _projeto(List<Layer> camadas) => VideoProject(
  name: 'p',
  createdAt: DateTime(2026, 9, 15),
  layers: camadas,
);

ShapeLayer _forma(String nome, {bool comEfeito = false}) => ShapeLayer(
  name: nome,
  startTime: Duration.zero,
  duration: const Duration(seconds: 4),
  effects: comEfeito
      ? [EffectInstance(type: EffectType.gaussianBlur)]
      : const [],
);

void main() {
  test('no padrao o palco desenha o MESMO projeto', () {
    final p = _projeto([_forma('a', comEfeito: true)]);
    expect(identical(projetoParaOPalco(p, const OpcoesDeVisualizacao()), p), isTrue);
  });

  test('sem efeitos tira os efeitos no palco, inclusive dentro de grupo', () {
    final grupo = GroupLayer(
      name: 'g',
      startTime: Duration.zero,
      duration: const Duration(seconds: 4),
      children: [_forma('filho', comEfeito: true)],
      effects: [EffectInstance(type: EffectType.vignette)],
    );
    final p = _projeto([_forma('a', comEfeito: true), grupo]);
    final palco = projetoParaOPalco(
      p,
      const OpcoesDeVisualizacao(modo: ModoDePrevia.semEfeitos),
    );
    expect(palco.layers.first.effects, isEmpty);
    final g = palco.layers[1] as GroupLayer;
    expect(g.effects, isEmpty);
    expect(g.children.single.effects, isEmpty);
    // O projeto de verdade — o que exporta — nao perdeu nada.
    expect(p.layers.first.effects, hasLength(1));
    expect((p.layers[1] as GroupLayer).children.single.effects, hasLength(1));
  });

  test('vista livre tira a camera do palco; a camera ativa some so ali', () {
    final camera = CameraLayer(
      name: 'Camera 1',
      startTime: Duration.zero,
      duration: const Duration(seconds: 4),
      is3D: true,
    );
    final p = _projeto([camera, _forma('a')]);
    expect(cameraAtivaEm(p, const Duration(seconds: 1)), isNotNull);
    final palco = projetoParaOPalco(
      p,
      const OpcoesDeVisualizacao(visaoDaCamera: false),
    );
    expect(palco.layers.whereType<CameraLayer>(), isEmpty);
    expect(cameraAtivaEm(palco, const Duration(seconds: 1)), isNull);
    expect(cameraAtivaEm(p, const Duration(seconds: 1)), isNotNull);
  });

  test('medidor: pico do audio no ar, no volume da camada', () {
    final envelope = Float32List(1000)..fillRange(0, 1000, .5);
    envelope[150] = .9; // 1,5 s de midia
    final audio = AudioLayer(
      name: 'musica',
      startTime: const Duration(seconds: 1),
      duration: const Duration(seconds: 5),
      sourcePath: 'musica.m4a',
      sourceOffset: const Duration(milliseconds: 500),
      volume: .5,
    );
    final p = _projeto([audio]);
    Float32List? picos(String c) => c == 'musica.m4a' ? envelope : null;
    // 2 s de projeto = 1 s de camada = 1,5 s de midia (com o desvio).
    expect(
      nivelDeAudioEm(p, const Duration(seconds: 2), picos: picos),
      closeTo(.45, 1e-6),
    );
    // Fora da camada, silencio.
    expect(nivelDeAudioEm(p, Duration.zero, picos: picos), 0);
  });

  test('tempo da barra de informacoes com sinal', () {
    expect(tempoDaInfobar(const Duration(milliseconds: 62350)), '1:02.35');
    expect(tempoDaInfobar(const Duration(milliseconds: -500)), '-0:00.50');
  });
}
