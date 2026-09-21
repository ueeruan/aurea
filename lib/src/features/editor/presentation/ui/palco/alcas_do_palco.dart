import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../application/ui/editor_session.dart';
import '../../../domain/layer.dart';
import '../../../domain/selection_geometry.dart';
import '../../../domain/shape.dart';
import '../../../domain/video_project.dart';
import '../../widgets/mask_node_editor.dart'
    show editorDeNosAtivo, pathEditTargetProvider;

// AS ALCAS E A MOLDURA DA SELECAO, EM PIXELS DE TELA.
//
// A moldura ficava DENTRO da composicao: 4 px da composicao, que num palco
// de celular (1920 -> 390) viram 0,8 px — e ainda engordavam e afinavam com
// a escala da camada. As alcas eram marcadores de 0x0: invisiveis. Aqui as
// duas coisas sao desenhadas por cima do quadro, na medida do DS (traco de
// selecao 2, alca de raio 5/6, pegador de giro 35), por UM pintor so —
// nenhum saveLayer, filtro ou sombra por alca (ver a memoria
// aurea-gpu-por-primitiva: cada um desses e um passe de GPU inteiro).

/// A GEOMETRIA DA SELECAO NA TELA DO PALCO.
class GeometriaDaSelecao {
  const GeometriaDaSelecao({
    required this.contorno,
    required this.centro,
    required this.pivo,
    required this.escala,
    required this.giro,
    required this.supEsq,
    required this.infEsq,
  });

  /// Os quatro cantos da caixa projetados (cima-esq, cima-dir, baixo-dir,
  /// baixo-esq), sem prender: e a moldura que se desenha.
  final List<Offset> contorno;
  final Offset centro;

  /// O ponto fixo do giro e da escala (posicao + pivo), na tela.
  final Offset pivo;

  /// Canto de baixo e da direita: escala (chave `alca-escala`).
  final Offset escala;

  /// Canto de cima e da direita: o pegador de giro (chave `alca-giro`).
  final Offset giro;

  /// Os outros dois cantos: tambem escalam.
  final Offset supEsq;
  final Offset infEsq;

  List<Offset> get cantosDeEscala => [escala, supEsq, infEsq];
}

/// O raio do DEDO numa alca de canto (o desenho tem 5): 26 px de cada lado
/// da um alvo de 52, acima do minimo de toque do app.
const double raioDeToqueDaAlca = 26;

/// O pegador de giro: 35 dp de area (DS `alcaDeGiro`); o dedo pega em 44.
const double raioDeToqueDoGiro = AureaDims.toqueConfortavel / 2;

/// Onde ficam a moldura e as alcas de [camada], ou nulo quando ela nao tem
/// moldura agora (passou da camera, caixa vazia).
///
/// [caixa] e a caixa SEM escala da camada (`layerBoxRect(scaled: false)`);
/// [origem] e [escala] levam a composicao a tela do palco, e [palco] e o
/// tamanho da area onde as alcas tem de caber.
GeometriaDaSelecao? geometriaDaSelecao({
  required VideoProject projeto,
  required Layer camada,
  required Rect caixa,
  required Duration t,
  required Offset origem,
  required double escala,
  required Size palco,
}) {
  if (caixa.isEmpty) return null;
  final matriz = selectionTransform(projeto, camada, t);
  // Camada que passou da camera nao aparece: sem alcas soltas no canto.
  if (matriz.storage.every((v) => v == 0)) return null;
  Offset naTela(Offset p) =>
      origem + MatrixUtils.transformPoint(matriz, p) * escala;
  final contorno = [
    naTela(caixa.topLeft),
    naTela(caixa.topRight),
    naTela(caixa.bottomRight),
    naTela(caixa.bottomLeft),
  ];
  final centro = naTela(caixa.center);
  final pivo = naTela(camada.pivot.valueAt(camada.localTime(t)));

  // AS ALCAS NUNCA ENCOSTAM UMA NA OUTRA NEM NO MEIO DO OBJETO.
  //
  // Num objeto pequeno (ou num palco baixo) os cantos caem a poucos pixels
  // um do outro, e a alca testada primeiro ganhava as duas — era o "a
  // rotacao nao gira" do beta. Um afastamento minimo em pixels DE TELA
  // resolve e nao mexe em objeto grande, onde os cantos ja estao longe.
  const afastamentoMinimo = 30.0;

  // AS ALCAS FICAM DENTRO DO PALCO: um objeto maior que o quadro empurra o
  // canto para fora da area visivel, e ali a alca nao existe para o dedo.
  final area = Offset.zero & palco;
  Offset presa(Offset p) => area.isEmpty
      ? p
      : Offset(
          p.dx.clamp(area.left + 22, math.max(area.left + 22, area.right - 22)),
          p.dy.clamp(area.top + 22, math.max(area.top + 22, area.bottom - 22)),
        );
  Offset afastada(Offset canto) {
    var delta = canto - centro;
    if (delta.distance < afastamentoMinimo && delta.distance > 1e-6) {
      delta *= afastamentoMinimo / delta.distance;
    }
    return presa(centro + delta);
  }

  var baixoDir = afastada(contorno[2]);
  var cimaDir = afastada(contorno[1]);
  if ((baixoDir - cimaDir).distance < 60) {
    final meio = (baixoDir + cimaDir) / 2;
    baixoDir = presa(meio + const Offset(0, 30));
    cimaDir = presa(meio - const Offset(0, 30));
  }
  return GeometriaDaSelecao(
    contorno: contorno,
    centro: centro,
    pivo: pivo,
    escala: baixoDir,
    giro: cimaDir,
    supEsq: afastada(contorno[0]),
    infEsq: afastada(contorno[3]),
  );
}

/// AS ALCAS DA FORMA VIVA (Editar forma), na tela: so na forma parametrica.
List<({String chave, Offset ponto})> alcasDaFormaNaTela({
  required VideoProject projeto,
  required ShapeLayer camada,
  required Duration t,
  required Offset origem,
  required double escala,
}) {
  final forma = camada.contents.whereType<ShapeParametric>().firstOrNull;
  if (forma == null) return const [];
  final local = camada.localTime(t);
  final matriz = selectionTransform(projeto, camada, t);
  if (matriz.storage.every((v) => v == 0)) return const [];
  final centro = shapeBounds(evaluateShape(camada.contents, local)).center;
  return [
    for (final a in alcasDaForma(forma, local))
      (
        chave: a.chave,
        ponto:
            origem +
            MatrixUtils.transformPoint(matriz, a.ponto - centro) * escala,
      ),
  ];
}

/// O QUE O PINTOR DESENHA NUM QUADRO.
class DesenhoDaSelecao {
  const DesenhoDaSelecao({
    this.principal,
    this.bloqueada = false,
    this.semAlcas = false,
    this.outras = const [],
    this.formas = const [],
  });

  final GeometriaDaSelecao? principal;
  final bool bloqueada;

  /// So a moldura, sem alcas (bloqueada, ou o editor de nos com o dedo).
  final bool semAlcas;
  final List<List<Offset>> outras;
  final List<({String chave, Offset ponto})> formas;
}

/// UM PINTOR, UMA PASSADA: moldura, alcas e pegador. Nenhuma camada, filtro
/// ou sombra — o contorno escuro por baixo e so um traco mais largo.
class PintorDaSelecao extends CustomPainter {
  PintorDaSelecao({required this.desenho, required this.alcaAtiva})
    : super(repaint: alcaAtiva);

  final DesenhoDaSelecao desenho;

  /// A alca sob o dedo (`escala`, `giro`, `forma:<chave>`): desenhada com o
  /// raio de escolhida (6).
  final ValueListenable<String?> alcaAtiva;

  @override
  void paint(Canvas canvas, Size size) {
    final destaque = AureaCores.destaque;
    final fundo = AureaCores.palco.withValues(alpha: .55);
    final branco = AureaCores.texto;

    final tracoDeFundo = Paint()
      ..style = PaintingStyle.stroke
      ..color = fundo
      ..strokeJoin = StrokeJoin.round;
    final traco = Paint()
      ..style = PaintingStyle.stroke
      ..strokeJoin = StrokeJoin.round;

    Path fechado(List<Offset> pts) => Path()..addPolygon(pts, true);

    // SELECAO MULTIPLA: as outras escolhidas, mais finas.
    for (final c in desenho.outras) {
      final p = fechado(c);
      canvas.drawPath(p, tracoDeFundo..strokeWidth = 2.5);
      canvas.drawPath(
        p,
        traco
          ..strokeWidth = AureaDims.tracoDeMultisselecao
          ..color = destaque,
      );
    }

    final g = desenho.principal;
    if (g != null) {
      final p = fechado(g.contorno);
      canvas.drawPath(
        p,
        tracoDeFundo..strokeWidth = AureaDims.tracoDeSelecao + 1.5,
      );
      canvas.drawPath(
        p,
        traco
          ..strokeWidth = AureaDims.tracoDeSelecao
          ..color = desenho.bloqueada ? AureaCores.textoSecundario : destaque,
      );
      if (!desenho.bloqueada && !desenho.semAlcas) {
        final ativa = alcaAtiva.value;
        for (final canto in g.cantosDeEscala) {
          _alca(canvas, canto, ativa == 'escala', branco, destaque, fundo);
        }
        _pegadorDeGiro(
          canvas,
          g.giro,
          ativa == 'giro',
          branco,
          destaque,
          fundo,
        );
      }
    }

    final ativa = alcaAtiva.value;
    for (final a in desenho.formas) {
      _alca(
        canvas,
        a.ponto,
        ativa == 'forma:${a.chave}',
        branco,
        destaque,
        fundo,
      );
    }
  }

  static void _alca(
    Canvas canvas,
    Offset c,
    bool escolhida,
    Color miolo,
    Color borda,
    Color fundo,
  ) {
    final r = escolhida
        ? AureaDims.raioDaAlcaDoPalcoEscolhida
        : AureaDims.raioDaAlcaDoPalco;
    canvas.drawCircle(c, r + 1.5, Paint()..color = fundo);
    canvas.drawCircle(c, r, Paint()..color = miolo);
    canvas.drawCircle(
      c,
      r,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5
        ..color = borda,
    );
  }

  /// O PEGADOR DE GIRO: um disco com a seta circular, dentro dos 35 dp do
  /// DS. Desenhado com traco e caminho — sem glifo de fonte no palco (o
  /// atlas de glifos do Impeller ja estourou com texto ali).
  static void _pegadorDeGiro(
    Canvas canvas,
    Offset c,
    bool escolhido,
    Color seta,
    Color borda,
    Color fundo,
  ) {
    final r = AureaDims.alcaDeGiro / 2 * (escolhido ? .62 : .55);
    canvas.drawCircle(c, r + 1.5, Paint()..color = fundo);
    canvas.drawCircle(
      c,
      r,
      Paint()..color = escolhido ? borda : AureaCores.painel,
    );
    final raioDaSeta = r * .52;
    const inicio = -math.pi * .9;
    const varredura = math.pi * 1.5;
    final arco = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.6
      ..strokeCap = StrokeCap.round
      ..color = seta;
    canvas.drawArc(
      Rect.fromCircle(center: c, radius: raioDaSeta),
      inicio,
      varredura,
      false,
      arco,
    );
    // A PONTA DA SETA, no fim do arco, apontando no sentido do giro.
    const fim = inicio + varredura;
    final ponta = c + Offset.fromDirection(fim, raioDaSeta);
    final tangente = Offset.fromDirection(fim + math.pi / 2, 1);
    final normal = Offset.fromDirection(fim, 1);
    final tam = raioDaSeta * .75;
    canvas.drawPath(
      Path()
        ..moveTo(ponta.dx + tangente.dx * tam, ponta.dy + tangente.dy * tam)
        ..lineTo(
          ponta.dx + normal.dx * tam * .7,
          ponta.dy + normal.dy * tam * .7,
        )
        ..lineTo(
          ponta.dx - normal.dx * tam * .7,
          ponta.dy - normal.dy * tam * .7,
        )
        ..close(),
      Paint()..color = seta,
    );
    if (!escolhido) {
      canvas.drawCircle(
        c,
        r,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1
          ..color = borda,
      );
    }
  }

  @override
  bool shouldRepaint(PintorDaSelecao old) => !identical(old.desenho, desenho);
}

/// A CAMADA DAS ALCAS: moldura e alcas da selecao por cima do quadro.
///
/// Vive DENTRO do LayoutBuilder do palco, com a geometria que ele acabou de
/// medir — assim o zoom e o passeio da vista nunca deixam as alcas um
/// quadro atras. So quem acompanha a mutacao do projeto e este ramo, e so
/// enquanto ha selecao: sem ela nem o relogio e escutado.
///
/// Os marcadores 0x0 com chave (`alca-escala`, `alca-giro`,
/// `alca-forma-<chave>`) continuam no lugar de cada alca: e por eles que os
/// testes (e quem mais precisar) acham a alca na tela.
class CamadaDaSelecao extends ConsumerWidget {
  const CamadaDaSelecao({
    super.key,
    required this.tempo,
    required this.origem,
    required this.escala,
    required this.palco,
    required this.alcaAtiva,
  });

  final ValueListenable<Duration> tempo;
  final Offset origem;
  final double escala;
  final Size palco;
  final ValueListenable<String?> alcaAtiva;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final id = ref.watch(selectedLayerProvider);
    final multi = ref.watch(multiSelectProvider);
    if (id == null && multi.isEmpty) return const SizedBox.shrink();
    final (painel, itemDosPontos) = ref.watch(
      editorSessionProvider.select((s) => (s.panel, s.pointsItemId)),
    );
    final editandoForma = painel == EditorPanel.editShape;
    // EDITANDO NOS (mascara/caminho) o dedo e do editor de nos: as alcas da
    // camada sairiam por cima dele prometendo um gesto que nao acontece.
    final editandoNos = editorDeNosAtivo(
      painel: painel,
      itemDaSessao: itemDosPontos,
      alvo: ref.watch(pathEditTargetProvider),
    );
    return IgnorePointer(
      child: ValueListenableBuilder<Duration>(
        valueListenable: tempo,
        builder: (context, t, _) => Consumer(
          builder: (context, ref, _) {
            final projeto = ref.watch(editorControllerProvider);
            final controller = ref.read(editorControllerProvider.notifier);
            GeometriaDaSelecao? geo(Layer l) {
              if (l is AudioLayer || !l.activeAt(t)) return null;
              return geometriaDaSelecao(
                projeto: projeto,
                camada: l,
                caixa: controller.layerBoxRect(l, t, scaled: false),
                t: t,
                origem: origem,
                escala: escala,
                palco: palco,
              );
            }

            final camada = id == null ? null : projeto.layerById(id);
            final principal = camada == null ? null : geo(camada);
            final bloqueada = id != null && projeto.metaOf(id).locked;
            final outras = <List<Offset>>[];
            for (final o in multi) {
              if (o == id) continue;
              final l = projeto.layerById(o);
              final c = l == null ? null : geo(l)?.contorno;
              if (c != null) outras.add(c);
            }
            final formas =
                editandoForma &&
                    camada is ShapeLayer &&
                    !bloqueada &&
                    camada.activeAt(t)
                ? alcasDaFormaNaTela(
                    projeto: projeto,
                    camada: camada,
                    t: t,
                    origem: origem,
                    escala: escala,
                  )
                : const <({String chave, Offset ponto})>[];
            final semAlcas = bloqueada || editandoNos;
            final desenho = DesenhoDaSelecao(
              principal: principal,
              bloqueada: bloqueada,
              semAlcas: semAlcas,
              outras: outras,
              formas: formas,
            );
            Widget marca(Offset p, String chave) => Positioned(
              left: p.dx,
              top: p.dy,
              child: SizedBox(key: ValueKey(chave), width: 0, height: 0),
            );
            return Stack(
              clipBehavior: Clip.none,
              children: [
                Positioned.fill(
                  child: CustomPaint(
                    key: const ValueKey('palco-selecao'),
                    painter: PintorDaSelecao(
                      desenho: desenho,
                      alcaAtiva: alcaAtiva,
                    ),
                  ),
                ),
                if (principal != null && !semAlcas) ...[
                  marca(principal.escala, 'alca-escala'),
                  marca(principal.giro, 'alca-giro'),
                ],
                for (final a in formas) marca(a.ponto, 'alca-forma-${a.chave}'),
              ],
            );
          },
        ),
      ),
    );
  }
}
