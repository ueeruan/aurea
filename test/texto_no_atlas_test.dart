// O TEXTO DO PALCO NO CORPO DE DESENHO (bug de 16/09: atlas de glifos do
// Impeller corrompido por glifo gigante). O texto desenhado reduzido e
// ampliado tem de sair igual, pixel a pixel, ao desenhado no corpo cheio —
// e nenhum glifo pode ir ao atlas acima de 48 x corpoDeDesenho.
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/presentation/widgets/texto_no_atlas.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

const _l = 360, _a = 240;

Future<Uint8List> _pixels(void Function(Canvas c) pintar) async {
  final rec = ui.PictureRecorder();
  final c = Canvas(rec);
  c.drawColor(const Color(0xFF000000), BlendMode.src);
  pintar(c);
  final img = await rec.endRecording().toImage(_l, _a);
  final data = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
  img.dispose();
  return data!.buffer.asUint8List();
}

/// As faixas de linhas acesas (uma por linha de texto) com o centro de
/// cada uma em x e y.
List<(double, double)> _centros(Uint8List p) {
  bool acesa(int y) {
    for (var x = 0; x < _l; x++) {
      if (p[(y * _l + x) * 4] > 20) return true;
    }
    return false;
  }

  final out = <(double, double)>[];
  var y = 0;
  while (y < _a) {
    if (!acesa(y)) {
      y++;
      continue;
    }
    var sx = 0.0, sy = 0.0, w = 0.0;
    while (y < _a && acesa(y)) {
      for (var x = 0; x < _l; x++) {
        final v = p[(y * _l + x) * 4].toDouble();
        sx += v * x;
        sy += v * y;
        w += v;
      }
      y++;
    }
    out.add((sx / w, sy / w));
  }
  return out;
}

({double media, int acesos, double desvio}) _comparar(
  Uint8List a,
  Uint8List b,
) {
  var soma = 0, acesos = 0;
  for (var i = 0; i < a.length; i += 4) {
    if (a[i] > 40) acesos++;
    soma += (a[i] - b[i]).abs();
  }
  final ca = _centros(a), cb = _centros(b);
  var desvio = ca.length == cb.length ? 0.0 : double.infinity;
  for (var i = 0; i < ca.length && i < cb.length; i++) {
    desvio = [
      desvio,
      (ca[i].$1 - cb[i].$1).abs(),
      (ca[i].$2 - cb[i].$2).abs(),
    ].reduce((x, y) => x > y ? x : y);
  }
  return (media: soma / (a.length / 4), acesos: acesos, desvio: desvio);
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUpAll(() async {
    final loader = FontLoader('Aurea Motion Sans')
      ..addFont(rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'));
    await loader.load();
  });

  test('nenhum corpo manda ao atlas glifo acima de 48 x corpo de desenho', () {
    for (var corpo = .5; corpo < 4000; corpo *= 1.37) {
      final k = reducaoDoCorpo(corpo);
      expect(k, greaterThanOrEqualTo(1));
      expect(corpo / k * 48, lessThanOrEqualTo(48 * corpoDeDesenho + 1e-6));
    }
    expect(reducaoDoCorpo(double.infinity), 1);
    expect(reducaoDoCorpo(double.nan), 1);
  });

  final casos = <(String, String, double, TextAlign, double, double?)>[
    // nome, texto, corpo, alinhamento, escala do canvas, largura maxima
    ('uma linha, corpo 120', 'Teste', 120, TextAlign.left, .37, null),
    ('corpo quebrado 37,3', 'Aurea Beta', 37.3, TextAlign.left, 1.9, null),
    (
      'tres linhas centradas',
      'Um\nDois tres\nQuatro',
      61.7,
      TextAlign.center,
      .8,
      null,
    ),
    ('a direita', 'abc\nx', 88, TextAlign.right, .9, null),
    (
      'quebra pela largura',
      'palavras que quebram sozinhas',
      45,
      TextAlign.left,
      .7,
      400,
    ),
    (
      'corpo 13, quase sem reducao',
      'Legenda pequena',
      13,
      TextAlign.left,
      3.1,
      null,
    ),
  ];

  // Peso 400: nenhuma das duas fontes tem bold, e o negrito sintetico do
  // Skia de CPU (o dos testes) engrossa pelo corpo SEM a matriz — corpo 12
  // ampliado sairia mais grosso que corpo 45. O Impeller engrossa pelo
  // tamanho na tela (DrawGlyph e o contorno canonico), entao no aparelho
  // nao ha diferenca; aqui ela so esconderia o que o teste mede.
  for (final familia in <String?>[null, 'Aurea Motion Sans']) {
    for (final (nome, texto, corpo, alinhamento, escala, largura) in casos) {
      test(
        '$nome (${familia ?? 'fonte de teste'}) sai igual ao corpo cheio',
        () async {
          final estilo = TextStyle(
            color: const Color(0xFFFFFFFF),
            fontSize: corpo,
            fontFamily: familia,
            letterSpacing: -corpo * 0.02,
            height: 1.1,
          );
          final direto = TextPainter(
            text: TextSpan(text: texto, style: estilo),
            textAlign: alinhamento,
            textDirection: TextDirection.ltr,
          )..layout(maxWidth: largura ?? double.infinity);
          final seguro = TextoNoAtlas(
            texto: texto,
            estilo: estilo,
            alinhamento: alinhamento,
          )..layout(maxWidth: largura ?? double.infinity);
          expect(seguro.size, direto.size);
          if (corpo > corpoDeDesenho) expect(seguro.k, greaterThan(1));

          const origem = Offset(7.3, 11.6);
          void emEscala(Canvas c, void Function(Canvas) p) {
            c
              ..save()
              ..scale(escala);
            p(c);
            c.restore();
          }

          final a = await _pixels(
            (c) => emEscala(c, (c) => direto.paint(c, origem)),
          );
          final b = await _pixels(
            (c) => emEscala(c, (c) => seguro.paint(c, origem)),
          );
          final r = _comparar(a, b);
          expect(r.acesos, greaterThan(50), reason: 'o texto tem de aparecer');
          // A borda antialiasada varia um pouco (a diferenca e so o contorno);
          // cada linha tem de estar no lugar: centro a menos de 0,35 px.
          expect(r.desvio, lessThan(.35), reason: '$r');
          expect(r.media, lessThan(2.5), reason: '$r');
          direto.dispose();
          seguro.dispose();
        },
      );
    }
  }

  test('o criterio reprova texto um pixel fora do lugar', () async {
    final p = TextPainter(
      text: const TextSpan(
        text: 'Um\nDois tres',
        style: TextStyle(fontSize: 61.7, color: Color(0xFFFFFFFF)),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    final a = await _pixels((c) => p.paint(c, const Offset(7, 11)));
    final b = await _pixels((c) => p.paint(c, const Offset(7, 12)));
    final r = _comparar(a, b);
    expect(r.desvio, greaterThan(.35), reason: '$r');
    p.dispose();
  });

  test('paragrafo cru reduzido cai na mesma linha de base', () async {
    ui.Paragraph construir(double corpo) {
      final b =
          ui.ParagraphBuilder(ui.ParagraphStyle(fontSize: corpo, height: 1.15))
            ..pushStyle(
              ui.TextStyle(
                color: const Color(0xFFFFFFFF),
                fontSize: corpo,
                letterSpacing: 2.5 * corpo / 97,
              ),
            )
            ..addText('PALAVRA');
      return b.build()
        ..layout(const ui.ParagraphConstraints(width: double.infinity));
    }

    const corpo = 97.0;
    final k = reducaoDoCorpo(corpo);
    final cheio = construir(corpo);
    final reduzido = construir(corpo / k);
    final a = await _pixels((c) {
      c.scale(.43);
      c.drawParagraph(cheio, const Offset(5.5, 9.25));
    });
    final b = await _pixels((c) {
      c.scale(.43);
      desenharParagrafoNoAtlas(c, cheio, reduzido, k, const Offset(5.5, 9.25));
    });
    final r = _comparar(a, b);
    expect(r.acesos, greaterThan(50));
    expect(r.desvio, lessThan(.35), reason: '$r');
    expect(r.media, lessThan(2.5), reason: '$r');
  });

  testWidgets('TextoDoPalco tem a caixa do Text e desenha reduzido', (t) async {
    const estilo = TextStyle(fontSize: 120, height: 1.1, letterSpacing: -2.4);
    final a = GlobalKey(), b = GlobalKey();
    await t.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: Stack(
          children: [
            Positioned(
              left: 0,
              top: 0,
              child: Text('Teste\nlinha', key: a, style: estilo),
            ),
            Positioned(
              left: 0,
              top: 300,
              child: TextoDoPalco(
                'Teste\nlinha',
                key: b,
                estilo: estilo,
                alinhamento: TextAlign.center,
              ),
            ),
          ],
        ),
      ),
    );
    expect(t.getSize(find.byKey(b)), t.getSize(find.byKey(a)));
    final render = t.renderObject<RenderTextoDoPalco>(find.byKey(b));
    expect(render.reducao, 10);
  });
}
