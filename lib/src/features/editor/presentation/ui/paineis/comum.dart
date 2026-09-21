import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';

// AS PECAS QUE TODO PAINEL USA. Quem reescreve um painel continua usando
// estas — sao a garantia de que "o que se ve" e "o que esta gravado" sao
// lidos do mesmo jeito em todos.

/// A CAMADA QUE SE VE (com a edicao pendente, quando ha): e dela que o
/// painel tira os NUMEROS. Observa so a camada, nunca o projeto — o painel
/// fica aberto enquanto o dedo arrasta, e observar o projeto o refaria a
/// cada mutacao de qualquer camada.
Layer? camadaVisivel(WidgetRef ref, String id) =>
    ref.watch(projetoVisivelProvider.select((p) => p.layerById(id)));

/// A CAMADA GRAVADA: a verdade dos LOSANGOS. Uma edicao pendente mexe no
/// numero mas nao cria marca, e o losango nao pode mentir sobre isso.
Layer? camadaGravada(WidgetRef ref, String id) =>
    ref.watch(editorControllerProvider.select((p) => p.layerById(id)));

/// Ir a um instante LOCAL da camada: pausa e posiciona o cabecote.
void irParaMarca(PlaybackController playback, Layer camada, int localUs) {
  playback.pause();
  playback.seek(camada.startTime + Duration(microseconds: localUs));
}

/// O losango e as setas de um conjunto de marcas (tempos LOCAIS, µs).
({KeyframeState estado, VoidCallback? anterior, VoidCallback? proximo})
losangoDasMarcas({
  required Iterable<int> marcasUs,
  required Layer camada,
  required Duration t,
  required PlaybackController playback,
  required VoidCallback aoAlternar,
}) {
  final marcas = marcasUs.toList(growable: false);
  final agora = camada.localTime(t).inMicroseconds;
  final viz = marcasVizinhas(marcas, agora);
  final ant = viz.anterior;
  final prox = viz.proxima;
  return (
    estado: KeyframeState(
      animated: marcas.isNotEmpty,
      here: temMarcaEm(marcas, agora),
      onToggle: aoAlternar,
    ),
    anterior: ant == null ? null : () => irParaMarca(playback, camada, ant),
    proximo: prox == null ? null : () => irParaMarca(playback, camada, prox),
  );
}

/// O losango de uma propriedade de TRANSFORMACAO ([LayerProp]).
({KeyframeState estado, VoidCallback? anterior, VoidCallback? proximo})
losangoDaPropriedade(
  WidgetRef ref, {
  required Layer gravada,
  required LayerProp prop,
  required Duration t,
  required PlaybackController playback,
}) {
  final controller = ref.read(editorControllerProvider.notifier);
  return losangoDasMarcas(
    marcasUs: [
      for (final d in controller.propKeyframeTimes(gravada, prop))
        d.inMicroseconds,
    ],
    camada: gravada,
    t: t,
    playback: playback,
    aoAlternar: () => controller.toggleKeyframe(gravada.id, t, prop),
  );
}

/// O RELOGIO DO PAINEL: reconstroi [construir] quando o cabecote anda.
///
/// Sem isto o `t` capturado no build fica velho depois de um scrub e o
/// losango crava a marca no instante em que o painel abriu (o defeito do
/// "keyframe fora do playhead", do editor antigo).
class NoCabecote extends StatelessWidget {
  const NoCabecote({super.key, required this.construir});

  final Widget Function(BuildContext context, Duration t) construir;

  @override
  Widget build(BuildContext context) {
    final playback = EscopoDoEditor.of(context).playback;
    return ValueListenableBuilder<Duration>(
      valueListenable: playback.time,
      builder: (context, t, _) => construir(context, t),
    );
  }
}

/// A PORTA para um editor que ja existe e que o painel ainda nao
/// reimplementou. Um item de lista (37) com a seta: nenhuma funcao do app
/// fica sem caminho enquanto os paineis sao reescritos.
class LinhaDePorta extends StatelessWidget {
  const LinhaDePorta({
    super.key,
    required this.rotulo,
    required this.aoTocar,
    this.icone = CupertinoIcons.square_arrow_up_on_square,
  });

  final String rotulo;
  final VoidCallback aoTocar;
  final IconData icone;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: aoTocar,
    encolhe: 1,
    child: SizedBox(
      height: AureaDims.itemDeLista + AureaDims.e6,
      child: Row(
        children: [
          Icon(icone, size: AureaDims.iconeSm + 2, color: AureaCores.destaque),
          const SizedBox(width: AureaDims.e10),
          Expanded(
            child: AppText(
              rotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: AureaEstilos.corpo,
            ),
          ),
          Icon(
            CupertinoIcons.chevron_right,
            size: 13,
            color: AureaCores.textoSecundario,
          ),
        ],
      ),
    ),
  );
}

/// O PAINEL DE UMA CAMADA QUE SUMIU (apagada com o painel aberto, desfazer
/// que a levou): titulo e aviso, nunca uma tela vazia quebrada.
class PainelSemCamada extends StatelessWidget {
  const PainelSemCamada({super.key, required this.titulo});

  final String titulo;

  @override
  Widget build(BuildContext context) => AureaPanel(
    titulo: titulo,
    aoFechar: EscopoDoEditor.of(context).fecharPainel,
    filhos: const [
      AureaAvisoDoPainel(texto: 'Esta camada não existe mais.'),
    ],
  );
}

/// O PAINEL QUE SO TEM PORTAS: titulo, uma frase e os caminhos para os
/// editores que ja fazem aquilo. E o esqueleto honesto dos paineis que as
/// proximas frentes vao reescrever.
class PainelDePortas extends StatelessWidget {
  const PainelDePortas({
    super.key,
    required this.titulo,
    required this.portas,
    this.aviso,
    this.chave = 'painel',
  });

  final String titulo;
  final String? aviso;
  final List<Widget> portas;
  final String chave;

  @override
  Widget build(BuildContext context) => AureaPanel(
    titulo: titulo,
    chave: chave,
    aoFechar: EscopoDoEditor.of(context).fecharPainel,
    filhos: [
      if (aviso != null) AureaAvisoDoPainel(texto: aviso!),
      ...portas,
    ],
  );
}
