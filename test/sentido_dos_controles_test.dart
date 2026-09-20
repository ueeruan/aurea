// O SENTIDO DOS CONTROLES: DIREITA AUMENTA, ESQUERDA DIMINUI.
//
// O relato do beta foi "todos os sliders estao invertidos", e a historia
// dele e a razao deste arquivo existir: em 12/09 a CONTA da regua trocou
// de sinal (direita passou a aumentar) e o DESENHO ficou para tras — os
// riscos andavam contra o dedo. A suite so cobrava a conta, e ficou verde
// por oito dias com o controle parecendo ao contrario. Depois de
// consertado o sinal do desenho, ainda sobrava um engano de olho: riscos
// todos iguais a cada 9 px parecem andar para tras num arrasto rapido
// (roda de carroca), e nada na regua dizia para que lado fica o "mais".
//
// Por isso cada controle de arrasto responde aqui a TRES perguntas:
//
//   1. a CONTA    — +N px sobe o valor, -N px desce;
//   2. os RISCOS  — andam para o MESMO lado que o dedo, e nao sao todos
//                   iguais (um forte a cada cinco);
//   3. a LEITURA  — com min e max finitos, o preenchimento cresce para a
//                   DIREITA.
//
// E um vigia de fonte fecha a porta por onde a copia invertida entrou.

import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:aurea/src/core/ui/am_tick_ruler.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart'
    as am;
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
import 'package:aurea/src/features/editor/presentation/widgets/dial_de_angulo.dart';
import 'package:aurea/src/features/editor/presentation/widgets/fita_de_ajuste.dart';
import 'package:aurea/src/features/editor/presentation/widgets/linha_de_parametro.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// Um canvas que anota os tracos verticais e os retangulos, para
/// perguntar ao pintor o que ele pintou sem depender de imagem nenhuma
/// (o molde e o de `linha_de_apoio_test.dart`).
class _Espiao implements ui.Canvas {
  /// (x, comprimento) de cada traco vertical, na ordem em que saiu.
  final tracos = <({double x, double comprimento})>[];
  final retangulos = <Rect>[];

  @override
  void drawLine(Offset a, Offset b, ui.Paint p) {
    if (a.dx == b.dx) tracos.add((x: a.dx, comprimento: (b.dy - a.dy).abs()));
  }

  @override
  void drawRRect(RRect r, ui.Paint p) => retangulos.add(r.outerRect);

  @override
  void drawRect(Rect r, ui.Paint p) => retangulos.add(r);

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

/// O que o pintor do controle desenhou agora.
///
/// O INDICADOR CENTRAL E O ULTIMO TRACO nos dois pintores (regua e fita)
/// e sai da lista de riscos — ele nao anda, e conta-lo junto faria o
/// "risco mais a esquerda" virar o centro quando a fita esta vazia.
({List<double> riscos, List<double> fortes, List<Rect> retangulos, Size size})
_pintado(WidgetTester t, Finder controle) {
  final alvo = find
      .descendant(of: controle, matching: find.byType(CustomPaint))
      .first;
  final size = t.getSize(alvo);
  final espiao = _Espiao();
  t.widget<CustomPaint>(alvo).painter!.paint(espiao, size);
  expect(espiao.tracos, isNotEmpty, reason: 'o pintor nao desenhou riscos');
  expect(
    espiao.tracos.last.x,
    closeTo(size.width / 2, .001),
    reason: 'o ultimo traco tem de ser o indicador central',
  );
  final riscos = espiao.tracos.sublist(0, espiao.tracos.length - 1);
  final maior = riscos.map((r) => r.comprimento).reduce(math.max);
  return (
    riscos: [for (final r in riscos) r.x],
    fortes: [
      for (final r in riscos)
        if (r.comprimento > maior - .001) r.x,
    ],
    retangulos: espiao.retangulos,
    size: size,
  );
}

/// QUANTO O DESENHO ANDOU entre dois quadros, em pixels, olhando so os
/// riscos FORTES — que se repetem a cada 45 px, e por isso um passo de
/// poucos pixels nao tem como ser confundido com o vizinho.
double _andou(List<double> antes, List<double> depois) {
  const periodo = passoDosRiscos * riscosPorForte;
  // O forte que esta mais perto do meio, para nenhum dos dois quadros
  // perde-lo pela borda.
  double doMeio(List<double> xs) {
    final ordenados = [...xs]..sort();
    return ordenados[ordenados.length ~/ 2];
  }

  var passo = (doMeio(depois) - doMeio(antes)) % periodo;
  if (passo > periodo / 2) passo -= periodo;
  return passo;
}

typedef _Caso = ({
  String nome,
  Type tipo,
  double porPixel,
  Widget Function(double valor, ValueChanged<double> aoMudar) monta,
});

/// TODO CONTROLE DE ARRASTO RETO DO APP. Os demais (audio, Texto 3D,
/// transporte das folhas, as vinte e tantas reguas soltas) sao um destes
/// por dentro: e a origem que se testa, e nao cada chamada.
final _casos = <_Caso>[
  (
    nome: 'AmTickRuler (pela porta de am_widgets)',
    tipo: am.AmTickRuler,
    porPixel: .5,
    monta: (v, set) =>
        am.AmTickRuler(value: v, unitsPerPixel: .5, onChanged: set),
  ),
  (
    nome: 'ParameterRow',
    tipo: ParameterRow,
    porPixel: .5,
    monta: (v, set) =>
        ParameterRow(label: 'Espessura', value: v, unitsPerPixel: .5, onChanged: set),
  ),
  (
    // O `_Slider` da ficha de audio e uma ParameterRow com faixa e com a
    // sensibilidade tirada dela — montado aqui com a mesma receita.
    nome: 'ParameterRow com faixa (o _Slider do audio)',
    tipo: ParameterRow,
    porPixel: 200 / 420,
    monta: (v, set) => ParameterRow(
      label: 'Volume',
      value: v.clamp(0, 200),
      min: 0,
      max: 200,
      unitsPerPixel: 200 / 420,
      onChanged: (n) => set(n.clamp(0, 200)),
    ),
  ),
  (
    nome: 'FitaDeAjuste',
    tipo: FitaDeAjuste,
    porPixel: .5,
    monta: (v, set) =>
        FitaDeAjuste(rotulo: 'Escala', valor: v, porPixel: .5, aoMudar: set),
  ),
  (
    nome: 'LinhaDeParametro',
    tipo: LinhaDeParametro,
    porPixel: .5,
    monta: (v, set) =>
        LinhaDeParametro(rotulo: 'Brilho', valor: v, porPixel: .5, aoMudar: set),
  ),
];

Widget _palco(Widget filho) => MaterialApp(
  home: Scaffold(
    body: Center(child: SizedBox(width: 360, child: filho)),
  ),
);

void main() {
  group('a conta e o desenho, controle por controle', () {
    for (final c in _casos) {
      testWidgets('${c.nome}: direita aumenta, esquerda diminui, e os riscos '
          'seguem o dedo', (t) async {
        var v = 100.0;
        await t.pumpWidget(
          _palco(
            StatefulBuilder(
              builder: (_, set) => c.monta(v, (n) => set(() => v = n)),
            ),
          ),
        );
        final alvo = find.byType(c.tipo);
        // O DEDO DESCE EM CIMA DOS RISCOS, e nao no centro da linha: numa
        // linha com rotulo e campo de valor o centro pode cair fora da
        // faixa, e o que se quer provar e o gesto sobre o desenho.
        final riscos = find
            .descendant(of: alvo, matching: find.byType(CustomPaint))
            .first;
        final g = await t.startGesture(t.getCenter(riscos));
        // Passa da folga de arrasto antes de medir qualquer coisa.
        await g.moveBy(const Offset(30, 0));
        // DOIS PUMPS: a superficie de arrasto segura o segundo evento do
        // quadro e o solta depois de pintar.
        await t.pump();
        await t.pump();
        final v0 = v;
        final d0 = _pintado(t, alvo);

        await g.moveBy(const Offset(4, 0));
        await t.pump();
        await t.pump();
        expect(v, greaterThan(v0), reason: 'DIREITA tem de AUMENTAR');
        final d1 = _pintado(t, alvo);
        expect(
          _andou(d0.fortes, d1.fortes),
          closeTo((v - v0) / c.porPixel, .01),
          reason: 'os riscos tem de andar PARA A DIREITA, junto com o dedo',
        );
        expect(_andou(d0.fortes, d1.fortes), greaterThan(0));
        final v1 = v;

        await g.moveBy(const Offset(-60, 0));
        await t.pump();
        await t.pump();
        expect(v, lessThan(v0), reason: 'ESQUERDA tem de DIMINUIR');
        final d2 = _pintado(t, alvo);
        // 60 px sao mais que um periodo: compara-se dentro dele.
        const periodo = passoDosRiscos * riscosPorForte;
        var esperado = ((v - v1) / c.porPixel) % periodo;
        if (esperado > periodo / 2) esperado -= periodo;
        expect(
          _andou(d1.fortes, d2.fortes),
          closeTo(esperado, .01),
          reason: 'na volta os riscos tambem acompanham o valor',
        );
        await g.up();
        await t.pump();
      });
    }

    testWidgets('os riscos NAO sao todos iguais: um forte a cada cinco', (
      t,
    ) async {
      for (final c in _casos) {
        await t.pumpWidget(_palco(c.monta(37, (_) {})));
        final d = _pintado(t, find.byType(c.tipo));
        expect(d.fortes, isNotEmpty, reason: c.nome);
        expect(
          d.fortes.length,
          lessThan(d.riscos.length / 3),
          reason: '${c.nome}: forte demais vira padrao periodico de novo',
        );
        final ordenados = [...d.fortes]..sort();
        for (var i = 1; i < ordenados.length; i++) {
          expect(
            ordenados[i] - ordenados[i - 1],
            closeTo(passoDosRiscos * riscosPorForte, .001),
            reason: c.nome,
          );
        }
      }
    });
  });

  group('a conta dos riscos (paraCadaRisco)', () {
    test('valor maior = riscos mais a DIREITA, pixel por pixel', () {
      final antes = riscosDaFita(valor: 10, porPixel: .5, largura: 300);
      final depois = riscosDaFita(valor: 11.5, porPixel: .5, largura: 300);
      // 1,5 de valor a 0,5 por pixel = 3 px de dedo para a direita.
      final fortesAntes = [for (final r in antes) if (r.forte) r.x];
      final fortesDepois = [for (final r in depois) if (r.forte) r.x];
      expect(fortesDepois.first - fortesAntes.first, closeTo(3, 1e-9));
    });

    test('valor menor = riscos mais a ESQUERDA', () {
      final antes = riscosDaFita(valor: 10, porPixel: .5, largura: 300);
      final depois = riscosDaFita(valor: 8, porPixel: .5, largura: 300);
      final fortesAntes = [for (final r in antes) if (r.forte) r.x];
      final fortesDepois = [for (final r in depois) if (r.forte) r.x];
      expect(fortesDepois.first - fortesAntes.first, closeTo(-4, 1e-9));
    });

    test('o forte e sempre o MESMO risco do papel (indice absoluto)', () {
      // Atravessar um passo inteiro (9 px) nao pode fazer o forte "pular"
      // de volta: ele anda 9 px, como todos os outros.
      for (final valor in [-1000.0, -3.0, 0.0, 4.4, 4.6, 900.0]) {
        final a = riscosDaFita(valor: valor, porPixel: 1, largura: 400);
        final b = riscosDaFita(valor: valor + 9, porPixel: 1, largura: 400);
        final fa = [for (final r in a) if (r.forte) r.x];
        final fb = [for (final r in b) if (r.forte) r.x];
        // Procura, para cada forte de `a` que continua na tela, o mesmo
        // forte 9 px a direita em `b`.
        for (final x in fa.where((x) => x + 9 <= 400)) {
          expect(
            fb.any((y) => (y - (x + 9)).abs() < 1e-6),
            isTrue,
            reason: 'valor $valor: o forte em $x nao andou 9 px',
          );
        }
      }
    });

    test('o desenho so se repete a cada 45 px de dedo, e nao a cada 9', () {
      List<(double, bool)> em(double valor) => [
        for (final r in riscosDaFita(valor: valor, porPixel: 1, largura: 400))
          (double.parse(r.x.toStringAsFixed(6)), r.forte),
      ];
      expect(em(45), em(0), reason: '45 px = um periodo inteiro');
      expect(em(9), isNot(em(0)), reason: '9 px NAO pode ser um periodo');
    });

    test('valor quebrado ou sensibilidade zero nao somem com a fita', () {
      for (final (valor, porPixel) in [
        (double.nan, 1.0),
        (double.infinity, 1.0),
        (10.0, 0.0),
      ]) {
        final r = riscosDaFita(valor: valor, porPixel: porPixel, largura: 200);
        expect(r, isNotEmpty);
        expect(r.every((e) => e.x.isFinite), isTrue);
      }
    });
  });

  group('a leitura de posicao', () {
    test('cresce para a DIREITA com o valor', () {
      final pouco = leituraDePosicao(valor: 20, min: 0, max: 100, largura: 300)!;
      final muito = leituraDePosicao(valor: 70, min: 0, max: 100, largura: 300)!;
      expect(pouco.de, 0);
      expect(muito.de, 0);
      expect(pouco.ate, closeTo(60, 1e-9));
      expect(muito.ate, closeTo(210, 1e-9));
      expect(muito.ate, greaterThan(pouco.ate));
    });

    test('faixa que cruza o zero cresce a partir do zero', () {
      final positivo = leituraDePosicao(
        valor: 50,
        min: -100,
        max: 100,
        largura: 200,
      )!;
      expect(positivo.de, closeTo(100, 1e-9));
      expect(positivo.ate, closeTo(150, 1e-9));
      final negativo = leituraDePosicao(
        valor: -50,
        min: -100,
        max: 100,
        largura: 200,
      )!;
      expect(negativo.de, closeTo(50, 1e-9));
      expect(negativo.ate, closeTo(100, 1e-9));
    });

    test('sem faixa nao ha posicao para mostrar', () {
      expect(
        leituraDePosicao(
          valor: 5,
          min: double.negativeInfinity,
          max: double.infinity,
          largura: 200,
        ),
        isNull,
      );
      expect(
        leituraDePosicao(valor: 5, min: 0, max: double.infinity, largura: 200),
        isNull,
      );
      expect(
        leituraDePosicao(valor: double.nan, min: 0, max: 1, largura: 200),
        isNull,
      );
      expect(leituraDePosicao(valor: 1, min: 3, max: 3, largura: 200), isNull);
    });

    testWidgets('a regua com faixa pinta o trilho, e ele enche para a '
        'direita quando o dedo vai para a direita', (t) async {
      var v = 20.0;
      await t.pumpWidget(
        _palco(
          StatefulBuilder(
            builder: (_, set) => AmTickRuler(
              value: v,
              min: 0,
              max: 100,
              unitsPerPixel: .25,
              onChanged: (n) => set(() => v = n),
            ),
          ),
        ),
      );
      final alvo = find.byType(AmTickRuler);
      final antes = _pintado(t, alvo);
      // Trilho inteiro + preenchimento.
      expect(antes.retangulos, hasLength(2));
      expect(antes.retangulos[0].width, closeTo(antes.size.width, .001));
      expect(antes.retangulos[1].left, 0);
      expect(
        antes.retangulos[1].right,
        closeTo(antes.size.width * .2, .001),
      );

      await t.drag(alvo, const Offset(120, 0));
      await t.pump();
      expect(v, greaterThan(20));
      final depois = _pintado(t, alvo);
      expect(depois.retangulos[1].left, 0);
      expect(
        depois.retangulos[1].right,
        greaterThan(antes.retangulos[1].right),
        reason: 'direita = mais: o preenchimento tem de CRESCER',
      );

      await t.drag(alvo, const Offset(-200, 0));
      await t.pump();
      final voltou = _pintado(t, alvo);
      expect(
        voltou.retangulos.length < 2 ||
            voltou.retangulos[1].right < depois.retangulos[1].right,
        isTrue,
        reason: 'esquerda = menos: o preenchimento tem de ENCOLHER',
      );
    });

    testWidgets('a fita com faixa pinta o mesmo trilho; sem faixa, nenhum', (
      t,
    ) async {
      await t.pumpWidget(
        _palco(
          FitaDeAjuste(
            rotulo: 'Opacidade',
            valor: 75,
            porPixel: .35,
            min: 0,
            max: 100,
            aoMudar: (_) {},
          ),
        ),
      );
      final com = _pintado(t, find.byType(FitaDeAjuste));
      expect(com.retangulos, hasLength(2));
      expect(com.retangulos[1].left, 0);
      expect(com.retangulos[1].right, closeTo(com.size.width * .75, .001));

      await t.pumpWidget(
        _palco(
          FitaDeAjuste(
            rotulo: 'Escala',
            valor: 75,
            porPixel: .35,
            aoMudar: (_) {},
          ),
        ),
      );
      expect(_pintado(t, find.byType(FitaDeAjuste)).retangulos, isEmpty);
    });

    testWidgets('a regua repinta quando so a sensibilidade ou a faixa muda', (
      t,
    ) async {
      CustomPainter pintor() => t
          .widget<CustomPaint>(
            find
                .descendant(
                  of: find.byType(AmTickRuler),
                  matching: find.byType(CustomPaint),
                )
                .first,
          )
          .painter!;
      await t.pumpWidget(
        _palco(AmTickRuler(value: 0, unitsPerPixel: .5, onChanged: (_) {})),
      );
      final velho = pintor();
      await t.pumpWidget(
        _palco(AmTickRuler(value: 0, unitsPerPixel: .1, onChanged: (_) {})),
      );
      expect(pintor().shouldRepaint(velho), isTrue);
      final semFaixa = pintor();
      await t.pumpWidget(
        _palco(
          AmTickRuler(
            value: 0,
            unitsPerPixel: .1,
            min: 0,
            max: 10,
            onChanged: (_) {},
          ),
        ),
      );
      expect(pintor().shouldRepaint(semFaixa), isTrue);
    });
  });

  group('o dial', () {
    Future<List<double>> girar(WidgetTester t, List<double> graus) async {
      final vistos = <double>[];
      await t.pumpWidget(
        Directionality(
          textDirection: TextDirection.ltr,
          child: Center(
            child: SizedBox(
              width: 200,
              height: 200,
              child: DialDeAngulo(angulo: 0, aoMudar: vistos.add),
            ),
          ),
        ),
      );
      final centro = t.getCenter(find.byType(DialDeAngulo));
      Offset em(double a) =>
          centro +
          Offset(math.cos(a * math.pi / 180), math.sin(a * math.pi / 180)) * 80;
      final g = await t.startGesture(em(graus.first));
      for (final a in graus.skip(1)) {
        await g.moveTo(em(a));
      }
      await g.up();
      await t.pump();
      return vistos;
    }

    testWidgets('horario aumenta, anti-horario diminui', (t) async {
      final horario = await girar(t, [0, 15, 30, 45, 60]);
      expect(horario.last, closeTo(60, 1));
      final anti = await girar(t, [0, -15, -30, -45, -60]);
      expect(anti.last, closeTo(-60, 1));
    });

    testWidgets('no alto do dial, dedo para a direita aumenta', (t) async {
      // 12 horas = -90 graus. Andar para a direita ali e andar no sentido
      // horario: e o mesmo "direita = mais" dos controles retos.
      final vistos = await girar(t, [-90, -80, -70, -60]);
      expect(vistos.last, greaterThan(0));
      expect(vistos.last, closeTo(30, 1));
    });

    testWidgets('chips de volta: MENOS a esquerda, MAIS a direita', (t) async {
      final vistos = <double>[];
      await t.pumpWidget(
        Directionality(
          textDirection: TextDirection.ltr,
          child: Center(
            child: SizedBox(
              width: 240,
              height: 200,
              child: DialDeAngulo(
                angulo: 10,
                aoMudar: vistos.add,
                passosDeVolta: (mais: 'volta-mais', menos: 'volta-menos'),
              ),
            ),
          ),
        ),
      );
      final mais = find.byKey(const ValueKey('volta-mais'));
      final menos = find.byKey(const ValueKey('volta-menos'));
      expect(
        t.getCenter(menos).dx,
        lessThan(t.getCenter(mais).dx),
        reason: 'menos|mais se le da esquerda para a direita',
      );
      // E cada chave continua fazendo o que o nome diz.
      await t.tap(mais);
      expect(vistos.last, closeTo(370, 1e-9));
      await t.tap(menos);
      expect(vistos.last, closeTo(-350, 1e-9));
    });
  });

  group('os deslizantes do sistema', () {
    testWidgets('CupertinoSlider (o scrub do rastreio): direita avanca', (
      t,
    ) async {
      // O estudio do rastreio so monta com quadros do rastreador, entao o
      // que se prova e o mesmo deslizante, com a mesma receita, sob a
      // mesma direcao que `app.dart` impoe ao app inteiro.
      var indice = 10.0;
      await t.pumpWidget(
        CupertinoApp(
          home: Directionality(
            textDirection: TextDirection.ltr,
            child: Center(
              child: SizedBox(
                width: 300,
                height: 34,
                child: StatefulBuilder(
                  builder: (_, set) => CupertinoSlider(
                    key: const ValueKey('estudio-rastreio-scrub'),
                    value: indice,
                    max: 40,
                    onChanged: (v) => set(() => indice = v),
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      final alvo = find.byType(CupertinoSlider);
      final box = t.getRect(alvo);
      // Pega o botao onde ele esta (10 de 40 = um quarto do trilho).
      final botao = Offset(box.left + box.width * .25, box.center.dy);
      await t.dragFrom(botao, const Offset(80, 0));
      await t.pump();
      expect(indice, greaterThan(10), reason: 'direita tem de AVANCAR');
      final noMeio = indice;
      await t.dragFrom(
        Offset(box.left + box.width * (noMeio / 40), box.center.dy),
        const Offset(-120, 0),
      );
      await t.pump();
      expect(indice, lessThan(noMeio), reason: 'esquerda tem de VOLTAR');
    });

    test('o app nao espelha: a direcao e LTR em qualquer idioma', () {
      // Em RTL o Flutter inverte todo deslizante do sistema. `app.dart`
      // fixa LTR de proposito; se alguem tirar, o scrub do rastreio e o
      // Slider do aprimoramento passam a andar ao contrario em arabe.
      final fonte = File('lib/src/app.dart').readAsStringSync();
      expect(fonte.contains('textDirection: TextDirection.ltr'), isTrue);
    });
  });

  group('vigia de fonte', () {
    test('nenhum arrasto subtrai o dedo, e nenhuma sensibilidade e negativa', () {
      // Foi assim que a copia invertida de `core/ui/am_tick_ruler.dart`
      // sobreviveu: ninguem a importava, entao nenhum teste de widget a
      // via. O vigia le o texto.
      final proibidos = <RegExp>[
        RegExp(r'-\s*_acumulado\s*\*'),
        RegExp(r'-\s*_andado\s*\*'),
        RegExp(r'unitsPerPixel:\s*-'),
        RegExp(r'porPixel:\s*-'),
      ];
      final achados = <String>[];
      for (final f in Directory('lib').listSync(recursive: true)) {
        if (f is! File || !f.path.endsWith('.dart')) continue;
        final linhas = f.readAsLinesSync();
        for (var i = 0; i < linhas.length; i++) {
          final linha = linhas[i];
          final codigo = linha.trimLeft();
          if (codigo.startsWith('//')) continue;
          for (final p in proibidos) {
            if (p.hasMatch(linha)) achados.add('${f.path}:${i + 1}: $codigo');
          }
        }
      }
      expect(achados, isEmpty, reason: achados.join('\n'));
    });

    test('a regua tem UMA origem: am_widgets so reexporta', () {
      final porta = File(
        'lib/src/features/editor/presentation/am/am_widgets.dart',
      ).readAsStringSync();
      expect(porta.contains('class AmTickRuler'), isFalse);
      expect(porta.contains('class AmArrastoDeValor'), isFalse);
      expect(porta.contains("core/ui/am_tick_ruler.dart'"), isTrue);
      final origem = File('lib/src/core/ui/am_tick_ruler.dart')
          .readAsStringSync();
      expect(origem.contains('_inicio + _acumulado * widget.unitsPerPixel'),
          isTrue);
    });
  });
}
