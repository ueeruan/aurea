import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/video_project.dart';

Duration _s(num v) => Duration(milliseconds: (v * 1000).round());

VideoProject _proj(List<Marker> m) => VideoProject(
      name: 'p',
      createdAt: DateTime(2026),
      markers: m,
    );

void main() {
  group('Marcadores', () {
    test('ficam sempre em ordem de tempo', () {
      final p = _proj([
        Marker(time: _s(9)),
        Marker(time: _s(2)),
        Marker(time: _s(5)),
      ]);
      expect(p.markers.map((m) => m.time).toList(),
          [_s(2), _s(5), _s(9)]);
    });

    // A tolerancia e o que faz "marcar de novo no mesmo lugar" apagar:
    // sem ela, o playhead num microssegundo diferente criaria uma
    // segunda marca em cima da primeira.
    test('acha a marca perto dentro da tolerancia', () {
      final p = _proj([Marker(time: _s(5))]);
      expect(p.markerNear(_s(5.05), _s(0.12)), isNotNull);
      expect(p.markerNear(_s(5.5), _s(0.12)), isNull);
    });

    test('entre duas, devolve a mais perto', () {
      final p = _proj([Marker(time: _s(5)), Marker(time: _s(5.2))]);
      expect(p.markerNear(_s(5.18), _s(0.5))!.time, _s(5.2));
    });

    test('sem marcas, nao acha nada', () {
      expect(_proj(const []).markerNear(Duration.zero, _s(1)), isNull);
    });

    test('copiar preserva o rotulo e a cor', () {
      const m = Marker(time: Duration(seconds: 3), label: 'refrao');
      expect(m.copyWith(time: const Duration(seconds: 4)).label, 'refrao');
      expect(m.copyWith(label: 'ponte').time, const Duration(seconds: 3));
    });

    test('copiar o projeto preserva as marcas', () {
      final p = _proj([Marker(time: _s(1))]);
      expect(p.copyWith(name: 'outro').markers, hasLength(1));
    });
  });
}
