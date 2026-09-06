import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';

Duration _s(num v) => Duration(milliseconds: (v * 1000).round());

VideoLayer _clipe({double speed = 1.0, num dur = 10}) => VideoLayer(
      name: 'tomada',
      startTime: Duration.zero,
      duration: _s(dur),
      sourcePath: 'a.mp4',
      speed: speed,
    );

void main() {
  group('Velocidade', () {
    test('normal consome tanto de fonte quanto dura', () {
      expect(_clipe().sourceSpan, _s(10));
    });

    // Uma barra de 10 s a 2x consome 20 s de arquivo. Confundir isso
    // com "20 s de barra" e o erro que corta o fim do clipe.
    test('acelerado consome mais fonte que a barra', () {
      expect(_clipe(speed: 2).sourceSpan, _s(20));
    });

    test('camera lenta consome menos', () {
      expect(_clipe(speed: 0.5).sourceSpan, _s(5));
    });

    test('copiar preserva a velocidade', () {
      expect(_clipe(speed: 1.5).copyLayer(name: 'x').speed, 1.5);
      expect(_clipe(speed: 1.5).duplicated().speed, 1.5);
    });

    test('audio tem a mesma conta', () {
      final a = AudioLayer(
        name: 'fala',
        startTime: Duration.zero,
        duration: _s(8),
        sourcePath: 'f.wav',
        speed: 4,
      );
      expect(a.sourceSpan, _s(32));
      expect(a.copyLayer(name: 'y').speed, 4);
    });

    test('velocidade padrao e um', () {
      expect(_clipe().speed, 1.0);
      expect(
          AudioLayer(
                  name: 'f',
                  startTime: Duration.zero,
                  duration: _s(1),
                  sourcePath: 'f.wav')
              .speed,
          1.0);
    });
  });
}
