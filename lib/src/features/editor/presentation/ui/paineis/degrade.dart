import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/layer.dart';
import '../../../domain/shape.dart';
import 'comum.dart' show camadaVisivel, irParaMarca;
import 'comum_de_objetos.dart' show linhaDeLigar, linhaNumerica;
import 'pecas_centrais.dart' show escolherCor, umPasso;

/// ABRE O EDITOR DO DEGRADE VETORIAL da forma [layerId].
///
/// Folha grande e SEM VEU ([modal] falso): o degrade so se julga olhando a
/// forma no palco, e um veu escuro por cima mentiria sobre as cores.
///
/// [playback] e opcional porque a folha tambem abre de lugares que nao tem
/// o relogio do editor (testes, telas avulsas): sem ele, tudo acontece no
/// instante zero da camada — o comportamento da folha antiga.
Future<void> showGradientFillSheet(
  BuildContext context,
  String layerId, {
  PlaybackController? playback,
}) {
  final altura = (MediaQuery.sizeOf(context).height * .55).clamp(
    280.0,
    560.0,
  );
  return mostrarAureaFolha<void>(
    context,
    titulo: 'Gradiente vetorial',
    grande: true,
    modal: false,
    altura: altura,
    construtor: (_) => playback == null
        ? _PainelDoDegrade(layerId: layerId)
        // O RELOGIO: sem reconstruir no cabecote, o losango crava a marca
        // no instante em que a folha abriu e nao onde o cabecote esta.
        : ValueListenableBuilder<Duration>(
            valueListenable: playback.time,
            builder: (_, t, _) => _PainelDoDegrade(
              layerId: layerId,
              t: t,
              playback: playback,
            ),
          ),
  );
}

class _PainelDoDegrade extends ConsumerWidget {
  const _PainelDoDegrade({
    required this.layerId,
    this.t = Duration.zero,
    this.playback,
  });

  final String layerId;
  final Duration t;
  final PlaybackController? playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final camada = camadaVisivel(ref, layerId);
    final List<Widget> filhos;
    if (camada is! ShapeLayer) {
      // A forma sumiu com a folha aberta (desfazer, apagar): aviso, nunca
      // uma folha vazia que parece quebrada.
      filhos = const [
        AureaAvisoDoPainel(texto: 'Esta camada não existe mais.'),
      ];
    } else {
      final degrades = camada.contents.whereType<ShapeGradientFill>().toList();
      filhos = degrades.isEmpty
          ? const [AureaAvisoDoPainel(texto: 'Esta forma não tem degradê.')]
          : [
              for (var i = 0; i < degrades.length; i++)
                _DegradeDaForma(
                  key: ValueKey('degrade-${degrades[i].id}'),
                  forma: camada,
                  g: degrades[i],
                  t: t,
                  playback: playback,
                ),
            ];
    }
    return ListView(
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        0,
        AureaDims.margemDoPainel,
        AureaDims.topoDoPainel,
      ),
      children: filhos,
    );
  }
}

/// UM DEGRADE DA FORMA: faixa de amostra, animar as cores (com o losango
/// das cores no cabecote), radial, cada parada (cor e posicao), angulo,
/// centro e alcance. As contas sao as da folha antiga, uma a uma.
class _DegradeDaForma extends ConsumerWidget {
  const _DegradeDaForma({
    super.key,
    required this.forma,
    required this.g,
    required this.t,
    required this.playback,
  });

  final ShapeLayer forma;
  final ShapeGradientFill g;
  final Duration t;
  final PlaybackController? playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final id = forma.id;
    final local = forma.localTime(t);
    final cores = g.colorsAt(local);
    final paradas = g.resolvedStops;
    final animado = g.colorFrames.isNotEmpty;

    void mudar(ShapeGradientFill Function(ShapeGradientFill) f) =>
        c.updateShapeGradient(id, g.id, f);

    // A MARCA NESTE INSTANTE: igualdade EXATA, a mesma que `withColorsAt`
    // usa para trocar a marca. Com a tolerancia de 8 ms dos outros
    // losangos, o losango diria "ha marca aqui" e o toque criaria outra
    // ao lado em vez de tirar a que ele mostra.
    final aqui = g.colorFrames.any((k) => k.time == local);

    // Tirar a marca daqui, ou gravar as cores de agora como marca nova.
    void alternarMarca() => umPasso(
      ref,
      () => mudar(
        (old) => old.colorFrames.any((k) => k.time == local)
            ? old.copyWith(
                colorFrames: old.colorFrames
                    .where((k) => k.time != local)
                    .toList(),
              )
            : old.withColorsAt(local, old.colorsAt(local)),
      ),
    );

    // AS SETAS so existem com o relogio do editor: pular de marca e mover
    // o cabecote, e sem ele nao ha cabecote para mover.
    final pb = playback;
    VoidCallback? anterior;
    VoidCallback? proximo;
    if (animado && pb != null) {
      final viz = marcasVizinhas([
        for (final k in g.colorFrames) k.time.inMicroseconds,
      ], local.inMicroseconds);
      final ant = viz.anterior;
      final prox = viz.proxima;
      if (ant != null) anterior = () => irParaMarca(pb, forma, ant);
      if (prox != null) proximo = () => irParaMarca(pb, forma, prox);
    }

    // Ligar ANIMA a partir das cores de agora; desligar congela as cores
    // do instante nas paradas fixas e joga fora as marcas.
    void animarCores(bool ligar) => umPasso(
      ref,
      () => mudar((old) {
        if (ligar) return old.withColorsAt(local, old.paradas);
        final agora = old.colorsAt(local);
        return old.copyWith(
          colorA: agora.first,
          colorB: agora.last,
          extras: agora.sublist(1, agora.length - 1),
          colorFrames: [],
        );
      }),
    );

    // A COR DE UMA PARADA: animado grava na marca do instante; parado
    // troca a parada fixa. O seletor e um gesto so (um passo de desfazer).
    void corDaParada(int indice, Color nova) => mudar((old) {
      final lista = [...old.colorsAt(local)];
      if (indice >= lista.length) return old;
      lista[indice] = nova;
      if (old.colorFrames.isNotEmpty) return old.withColorsAt(local, lista);
      return old.copyWith(
        colorA: lista.first,
        colorB: lista.last,
        extras: lista.sublist(1, lista.length - 1),
      );
    });

    // A POSICAO DE UMA PARADA nunca passa das vizinhas: a ordem das
    // paradas e a ordem das cores no pincel.
    void posicaoDaParada(int indice, double porcento) => mudar((old) {
      final stops = [...old.resolvedStops];
      if (indice >= stops.length) return old;
      stops[indice] = (porcento / 100).clamp(
        indice == 0 ? 0.0 : stops[indice - 1],
        indice == stops.length - 1 ? 1.0 : stops[indice + 1],
      );
      return old.copyWith(stops: stops);
    });

    return AureaSection(
      titulo: 'Cores e distribuicao',
      chave: 'degrade-cores-${g.id}',
      recolhivel: false,
      filhos: [
        const SizedBox(height: AureaDims.e4),
        // A AMOSTRA da distribuicao: e o que se ajusta nas linhas abaixo.
        ClipRRect(
          borderRadius: BorderRadius.circular(AureaDims.raioMd),
          child: SizedBox(
            key: const ValueKey('degrade-amostra'),
            height: 28,
            child: DecoratedBox(
              decoration: BoxDecoration(
                gradient: LinearGradient(colors: cores, stops: paradas),
              ),
            ),
          ),
        ),
        const SizedBox(height: AureaDims.e4),
        AureaPropertyRow.personalizada(
          rotulo: 'Animar cores',
          chave: 'degrade-animar',
          filho: AureaToggle(valor: animado, aoMudar: animarCores),
          keyframe: animado
              ? KeyframeState(
                  animated: true,
                  here: aqui,
                  onToggle: alternarMarca,
                )
              : null,
          aoAnterior: anterior,
          aoProximo: proximo,
        ),
        linhaDeLigar(
          rotulo: 'Radial',
          chave: 'degrade-radial',
          valor: g.radial,
          aoMudar: (v) =>
              umPasso(ref, () => mudar((old) => old.copyWith(radial: v))),
        ),
        for (var i = 0; i < g.paradas.length && i < cores.length; i++) ...[
          // O ROTULO JA VAI MONTADO: o molde com o numero passa pelo
          // catalogo aqui, e a linha so repete o que recebeu.
          AureaPropertyRow.cor(
            rotulo: moldar(context, 'Cor {0}', [i + 1]),
            chave: 'degrade-cor-$i',
            cor: cores[i],
            aoTocar: () => escolherCor(
              context,
              ref,
              inicial: cores[i],
              aplicar: (nova) => corDaParada(i, nova),
            ),
          ),
          linhaNumerica(
            ref,
            rotulo: moldar(context, 'Posição {0}', [i + 1]),
            chave: 'degrade-posicao-$i',
            valor: paradas[i] * 100,
            min: 0,
            max: 100,
            unidade: '%',
            aoMudar: (v) => posicaoDaParada(i, v),
          ),
        ],
        linhaNumerica(
          ref,
          rotulo: 'Ângulo',
          chave: 'degrade-angulo',
          valor: g.angleDeg.isFinite ? g.angleDeg : 0,
          min: -180,
          max: 180,
          unidade: '°',
          aoMudar: (v) => mudar((old) => old.copyWith(angleDeg: v)),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Centro X',
          chave: 'degrade-centro-x',
          valor: g.center.dx.isFinite ? g.center.dx : 0,
          min: -1,
          max: 1,
          casas: 2,
          aoMudar: (v) =>
              mudar((old) => old.copyWith(center: Offset(v, old.center.dy))),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Centro Y',
          chave: 'degrade-centro-y',
          valor: g.center.dy.isFinite ? g.center.dy : 0,
          min: -1,
          max: 1,
          casas: 2,
          aoMudar: (v) =>
              mudar((old) => old.copyWith(center: Offset(old.center.dx, v))),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Alcance',
          chave: 'degrade-alcance',
          valor: g.radiusScale.isFinite ? g.radiusScale : .05,
          min: .05,
          max: 3,
          casas: 2,
          aoMudar: (v) => mudar((old) => old.copyWith(radiusScale: v)),
        ),
      ],
    );
  }
}
