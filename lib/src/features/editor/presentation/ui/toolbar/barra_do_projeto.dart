import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show MaterialPageRoute;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../help/presentation/quick_guide_screen.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/layer.dart';
import '../../am/beats_sheet.dart' show showBeatsSheet;
import '../../am/layer_menu.dart' show showReasonToast;
import '../../shell/layer_actions.dart' show menuDasMarcas;
import '../../shell/project_settings_sheet.dart' show showProjectSettingsSheet;
import '../../widgets/add_layer_sheet.dart' show showCaptionCreationSheet;
import '../shell/contrato.dart';
import 'adicionar.dart';
import 'barra_contextual.dart';
import 'barra_do_lote.dart';

// ===========================================================================
// A BARRA DO PROJETO (nivel 0: nada escolhido)
// ===========================================================================
//
// Sem camada escolhida a base da timeline nao fica vazia: ela mostra o que
// se faz com o PROJETO — adicionar, as configuracoes, legendas automaticas
// (quando ha um som para ouvir), escolher varias, colar a camada copiada,
// marcas, batidas e o guia. O "+" continua flutuando no canto direito; a
// barra guarda o lugar dele.

/// Os ids das acoes do projeto (chave `ferramenta-<id>`).
abstract final class AcaoDoProjeto {
  static const adicionar = 'projeto-adicionar';
  static const configuracoes = 'projeto-configuracoes';
  static const legendas = 'projeto-legendas';
  static const selecionar = 'projeto-selecionar';
  static const colar = 'projeto-colar';
  static const marcas = 'projeto-marcas';
  static const batidas = 'projeto-batidas';
  static const guia = 'projeto-guia';
}

/// O QUE O PROJETO OFERECE AGORA. O que depende de conteudo so aparece
/// quando existe: legendas e batidas pedem um som; selecionar pede duas
/// camadas; colar pede uma camada copiada.
List<Ferramenta> ferramentasDoProjeto({
  required int camadas,
  required bool temSom,
  required bool temCamadaCopiada,
  void Function(String idDaAcao)? aoAcionar,
}) {
  Ferramenta f(String id, IconData icone, String rotulo) => Ferramenta(
    id: id,
    icone: icone,
    rotulo: rotulo,
    acao: aoAcionar == null ? null : () => aoAcionar(id),
  );
  return [
    f(AcaoDoProjeto.adicionar, CupertinoIcons.plus_square, 'Adicionar'),
    f(AcaoDoProjeto.configuracoes, CupertinoIcons.gear, 'Projeto'),
    if (temSom)
      f(AcaoDoProjeto.legendas, CupertinoIcons.captions_bubble, 'Legendas'),
    if (camadas >= 2)
      f(AcaoDoProjeto.selecionar, iconeDeSelecionar, 'Selecionar'),
    if (temCamadaCopiada)
      f(AcaoDoProjeto.colar, CupertinoIcons.doc_on_clipboard, 'Colar'),
    f(AcaoDoProjeto.marcas, CupertinoIcons.bookmark, 'Marcas'),
    if (temSom) f(AcaoDoProjeto.batidas, CupertinoIcons.metronome, 'Batidas'),
    f(AcaoDoProjeto.guia, CupertinoIcons.question_circle, 'Guia'),
  ];
}

/// A BARRA: observa so o que muda a LISTA (quantas camadas, se ha som) —
/// um passo de slider nao chega aqui. Quem executa e [aoAcionar] (a casca,
/// com o contexto e o `ref` que sobrevivem a barra: adicionar escolhe a
/// camada nova, e a barra sai da arvore no meio da acao).
///
/// Chave: `barra-do-projeto`.
class BarraDoProjeto extends ConsumerWidget {
  const BarraDoProjeto({super.key, required this.aoAcionar});

  final void Function(String idDaAcao) aoAcionar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final (camadas, temSom) = ref.watch(
      editorControllerProvider.select(
        (p) => (
          p.layers.length,
          p.layers.any((l) => l is VideoLayer || l is AudioLayer),
        ),
      ),
    );
    return BarraContextual(
      chave: 'barra-do-projeto',
      ferramentas: ferramentasDoProjeto(
        camadas: camadas,
        temSom: temSom,
        temCamadaCopiada: ref
            .read(editorControllerProvider.notifier)
            .temCamadaCopiada,
        aoAcionar: aoAcionar,
      ),
      aoTocar: (f) => f.acao?.call(),
      // O "+" (73, a 6 da borda) flutua sobre o canto direito da barra.
      recuoFinal:
          AureaDims.botaoAdicionar +
          AureaDims.margemDoAdicionar +
          AureaDims.e6,
    );
  }
}

/// EXECUTA uma acao do projeto ([AcaoDoProjeto]).
Future<void> acionarProjeto(
  BuildContext context,
  WidgetRef ref,
  String acao, {
  required PlaybackController playback,
  required void Function(PainelId id) abrirPainel,
}) async {
  final c = ref.read(editorControllerProvider.notifier);
  playback.pause();
  switch (acao) {
    case AcaoDoProjeto.adicionar:
      await abrirAdicionar(
        context,
        ref,
        playback: playback,
        abrirPainel: abrirPainel,
      );
    case AcaoDoProjeto.configuracoes:
      await showProjectSettingsSheet(context, ref);
    case AcaoDoProjeto.legendas:
      await showCaptionCreationSheet(context, ref);
    case AcaoDoProjeto.selecionar:
      ligarModoSelecionar(ref);
    case AcaoDoProjeto.colar:
      c.runAsOneUndo(() => c.colarCamada(playback.time.value));
    case AcaoDoProjeto.marcas:
      await menuDasMarcas(context, ref, playback);
    case AcaoDoProjeto.batidas:
      final som = ref
          .read(editorControllerProvider)
          .layers
          .where((l) => l is AudioLayer || l is VideoLayer)
          .firstOrNull;
      if (som == null) {
        showReasonToast(context, 'Adicione um áudio ou um vídeo primeiro');
        return;
      }
      await showBeatsSheet(context, ref, som.id);
    case AcaoDoProjeto.guia:
      await Navigator.of(context).push(
        MaterialPageRoute<void>(
          builder: (_) => const QuickGuideScreen(initialQuery: ''),
        ),
      );
  }
}
