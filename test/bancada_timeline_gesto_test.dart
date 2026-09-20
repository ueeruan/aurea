// A BANCADA DO GESTO NA LINHA DO TEMPO.
//
// A bancada do editor media a linha do tempo com o RELOGIO andando. O
// relato do beta e outro: "lagando toda a timeline" — e o que se faz na
// timeline nao e assistir, e ARRASTAR. Arrastar um clipe, arrastar a
// borda para aparar, rolar o conteudo com o dedo.
//
// Cada uma dessas coisas MUTA O PROJETO a cada atualizacao do gesto, e a
// linha do tempo observa o projeto inteiro. Se cada passo do dedo
// reconstruir a timeline toda, o gesto engasga — e engasga mais quanto
// maior o projeto, que e exatamente o que o relato descreve.
//
// Esta bancada mede o que os dedos fazem, e conta as reconstrucoes.
//
// Rodar:  flutter test test/bancada_timeline_gesto_test.dart
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/perfil3d.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show FontLoader, rootBundle;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

VideoProject _projeto({required int camadas}) => VideoProject(
  name: 'bancada',
  createdAt: DateTime(2026, 9, 9),
  layers: [
    for (var i = 0; i < camadas; i++)
      ShapeLayer(
        id: 'c$i',
        name: 'Camada $i',
        startTime: Duration(milliseconds: 250 * i),
        duration: const Duration(seconds: 3),
        position: AnimatedOffset(const Offset(200, 200)),
        contents: [
          ShapePath(primitive: ShapePrimitive.rectangle),
          ShapeFill(color: const Color(0xFF3DDC97)),
        ],
      ),
  ],
);

class _Casca extends StatefulWidget {
  const _Casca({required this.aoCriar});
  final void Function(PlaybackController) aoCriar;

  @override
  State<_Casca> createState() => _CascaState();
}

class _CascaState extends State<_Casca> with SingleTickerProviderStateMixin {
  late final PlaybackController _playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 30),
  );

  @override
  void initState() {
    super.initState();
    widget.aoCriar(_playback);
  }

  @override
  void dispose() {
    _playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => SizedBox(
    height: 320,
    child: AmTimeline(playback: _playback, height: 320),
  );
}

/// Monta a timeline com [camadas] camadas e devolve o container.
Future<ProviderContainer> _montar(
  WidgetTester tester, {
  required int camadas,
}) async {
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container
      .read(editorControllerProvider.notifier)
      .openProject(_projeto(camadas: camadas));
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Scaffold(body: _Casca(aoCriar: (_) {})),
      ),
    ),
  );
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 100));
  return container;
}

/// Quantas vezes a linha do tempo se reconstruiu, e quanto custou cada
/// reconstrucao, enquanto [gesto] acontece.
///
/// [barras] conta as BARRAS reconstruidas. E a medida que mostra o efeito
/// da memorizacao por linha: a linha do tempo pode se reconstruir uma vez
/// por passo do dedo (ela observa o projeto) e ainda assim so UMA linha
/// descer ate as barras — as outras devolvem o mesmo widget.
Future<({int vezes, double msCada, double msTotal, int barras})> _durante(
  WidgetTester tester,
  Future<void> Function() gesto,
) async {
  Perfil3D.zerar();
  Perfil3D.ligado = true;
  await gesto();
  Perfil3D.ligado = false;
  final relatorio = Perfil3D.relatorio();
  final barras = relatorio.contas['build.barra'] ?? 0;
  final f = relatorio.fases['build.timeline'];
  if (f == null || f.chamadas == 0) {
    return (vezes: 0, msCada: 0.0, msTotal: 0.0, barras: barras);
  }
  return (
    vezes: f.chamadas,
    msCada: f.ms / f.chamadas,
    msTotal: f.ms,
    barras: barras,
  );
}

void main() {
  setUpAll(() async {
    for (final f in ['Aurea Motion Sans', 'Roboto', 'FlutterTest']) {
      await (FontLoader(f)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('bancada: quanto custa MEXER na linha do tempo', (tester) async {
    tester.view.physicalSize = const Size(1080, 1200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final linhas = <String>[
      '',
      'BANCADA DO GESTO NA LINHA DO TEMPO',
      '',
      'Cada linha e um gesto de verdade. "vezes" e quantas reconstrucoes',
      'inteiras da timeline o gesto provocou.',
      '',
      'camadas  gesto                    vezes   ms cada   ms total  barras',
      '----------------------------------------------------------------------',
    ];

    // UM TAMANHO SO, e de proposito. A pergunta desta bancada e "um
    // gesto reconstroi a linha do tempo inteira?", e quem responde isso
    // e a CONTAGEM de reconstrucoes, que nao depende de haver muitas
    // camadas. Com mais camadas o proprio harness deixa de acertar a
    // barra de forma confiavel (ela nasce fora da area visivel), e uma
    // bancada que erra o alvo mede zero e parece otima — foi o que
    // aconteceu nas primeiras versoes deste arquivo.
    for (final n in [4]) {
      final container = await _montar(tester, camadas: n);
      final alvo = find.byType(AmTimeline);
      final caixa = tester.getRect(alvo);
      Duration inicioDe(String id) => container
          .read(editorControllerProvider)
          .layers
          .firstWhere((l) => l.id == id)
          .startTime;

      // 1. ARRASTAR um clipe: cada passo muta o projeto.
      //
      // A barra e achada pela CHAVE, nao por um ponto chutado, e o
      // primeiro passo do dedo e grande de proposito: um passo pequeno
      // perde a arena de gestos para a rolagem horizontal que esta por
      // baixo, e ai o gesto vira uma rolagem. Um gesto que erra o alvo
      // mede zero e a bancada pareceria otima — por isso a conferencia
      // logo abaixo.
      // A PRIMEIRA BARRA QUE ESTA MESMO NA ARVORE. As faixas sao
      // preguicosas: com 12 ou 24 camadas a primeira pode nem existir
      // ainda, e procurar por uma chave fixa daria um alvo fantasma.
      var idDaBarra = '';
      for (var i = 0; i < n; i++) {
        if (find.byKey(ValueKey('clip-content-c$i')).evaluate().isNotEmpty) {
          idDaBarra = 'c$i';
          break;
        }
      }
      expect(idDaBarra, isNotEmpty, reason: 'nenhuma barra na tela com $n');
      final antesDoArrasto = inicioDe(idDaBarra);
      final barra = find.byKey(ValueKey('clip-content-$idDaBarra'));
      await tester.ensureVisible(barra);
      await tester.pump();
      // SELECIONAR PRIMEIRO: a barra so se move quando esta selecionada
      // (podeMover = selected && !locked), que e tambem o fluxo real —
      // toca para escolher, arrasta para mover.
      // O PONTO TEM DE ESTAR DENTRO DA JANELA, nao so dentro da barra:
      // com muitas camadas a barra comeca antes da borda esquerda, e o
      // centro dela cai numa regiao recortada, onde o toque nao acerta
      // nada. A intersecao entre a barra e a area visivel resolve.
      final visivel = tester.getRect(barra).intersect(caixa);
      expect(
        visivel.width > 8 && visivel.height > 4,
        isTrue,
        reason: 'a barra nao tem pedaco visivel com $n camadas',
      );
      final alvoDoDedo = visivel.center;
      await tester.tapAt(alvoDoDedo);
      await tester.pump(const Duration(milliseconds: 50));
      final arrasto = await _durante(tester, () async {
        final dedo = await tester.startGesture(alvoDoDedo);
        // 600 ms, e nao 350: `kLongPressTimeout` e 500. Com 350 o toque
        // longo ainda nao tinha disparado quando o dedo andou, o gesto
        // ia para a rolagem horizontal que esta por baixo, e a bancada
        // media uma ROLAGEM achando que media um arrasto de clipe.
        await tester.pump(const Duration(milliseconds: 600));
        await dedo.moveBy(const Offset(28, 0));
        await tester.pump(const Duration(milliseconds: 16));
        for (var i = 0; i < 11; i++) {
          await dedo.moveBy(const Offset(6, 0));
          await tester.pump(const Duration(milliseconds: 16));
        }
        await dedo.up();
        await tester.pump(const Duration(milliseconds: 16));
      });
      expect(
        inicioDe(idDaBarra),
        isNot(antesDoArrasto),
        reason:
            'o arrasto nao moveu o clipe: a bancada estaria medindo o '
            'gesto errado com $n camadas',
      );

      // 2. ROLAR o conteudo com o dedo, na horizontal. Vem DEPOIS do
      // arrasto de proposito: rolar leva a barra para fora da area
      // visivel, e o gesto seguinte erraria o alvo em silencio.
      final rolagem = await _durante(tester, () async {
        final ponto = Offset(caixa.center.dx, caixa.top + 40);
        final dedo = await tester.startGesture(ponto);
        for (var i = 0; i < 12; i++) {
          await dedo.moveBy(const Offset(-8, 0));
          await tester.pump(const Duration(milliseconds: 16));
        }
        await dedo.up();
        await tester.pump(const Duration(milliseconds: 16));
      });

      for (final (nome, m) in [
        ('rolar com o dedo', rolagem),
        ('arrastar um clipe', arrasto),
      ]) {
        linhas.add(
          '${n.toString().padLeft(5)}    ${nome.padRight(24)} '
          '${m.vezes.toString().padLeft(4)}  '
          '${m.msCada.toStringAsFixed(1).padLeft(7)}  '
          '${m.msTotal.toStringAsFixed(0).padLeft(8)}  '
          '${m.barras.toString().padLeft(6)}',
        );
      }
      await tester.pumpWidget(const SizedBox());
      await tester.pump(const Duration(milliseconds: 50));
    }

    linhas
      ..add('')
      ..add('Um gesto de 12 passos deveria custar POUCAS reconstrucoes.')
      ..add('Uma por passo significa a timeline inteira refeita a cada')
      ..add('pixel que o dedo anda.');
    // ignore: avoid_print
    print(linhas.join(String.fromCharCode(10)));
  });
}
