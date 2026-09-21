import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ui/pedir_nome.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../palco/aviso_de_bloqueio.dart';
import '../timeline/ima.dart' show magneticProvider;

// ===========================================================================
// AS ACOES ESTRUTURAIS SOBRE CAMADAS, num lugar so
// ===========================================================================
//
// O menu da camada, a barra do lote e a folha de adicionar chamam estas
// funcoes: dois botoes, uma regra so (ima, cadeado, limpar a selecao
// multipla). Vieram de `shell/layer_actions.dart` quando a UI antiga foi
// apagada — o comportamento e o mesmo.
/// Exclui as camadas de [targets], respeitando o magnetico.
///
/// SEM AVISO DE "EXCLUIDA". Os testadores do beta 1.0.5 pediram para tirar:
/// o aviso cobria a timeline logo depois de cada exclusao, e a camada
/// sumindo ja diz o que aconteceu. O Desfazer continua na barra de
/// reproducao.
void excluirCamadas(BuildContext context, WidgetRef ref, Set<String> targets) {
  if (targets.isEmpty) return;
  final controller = ref.read(editorControllerProvider.notifier);
  // A BLOQUEADA NAO VAI. Antes de qualquer coisa: quantas do alvo estao
  // travadas. Elas ficam, o resto some, e a tela diz quantas ficaram.
  final travadas = [
    for (final id in targets)
      if (controller.isLocked(id)) id,
  ];
  // MAGNETICO: excluir FECHA o buraco e puxa o que vinha depois.
  final magnetico = ref.read(magneticProvider);
  if (magnetico) {
    for (final id in targets) {
      if (controller.isLocked(id)) continue;
      controller.rippleDeleteLayer(id);
    }
    ref.read(multiSelectProvider.notifier).state = const {};
  } else {
    controller.removeLayers(targets);
  }
  if (travadas.isNotEmpty) {
    avisarCamadaBloqueada(
      context,
      ref,
      travadas.length == 1
          ? 'Camada bloqueada: desbloqueie para apagar'
          : '${travadas.length} camadas bloqueadas: desbloqueie para apagar',
      travadas.first,
    );
  }
}

/// Agrupa a selecao e sai da selecao multipla.
void agruparSelecao(WidgetRef ref, Set<String> targets) {
  ref.read(editorControllerProvider.notifier).groupLayers(targets.toList());
  ref.read(multiSelectProvider.notifier).state = const {};
}

/// RENOMEAR UMA CAMADA (toque no nome, no cabecalho da selecao).
Future<void> renomearCamada(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
) async {
  final nome = await pedirNome(
    context,
    titulo: 'Nome da camada',
    atual: layer.name,
  );
  if (nome == null) return;
  ref.read(editorControllerProvider.notifier).renameLayer(layer.id, nome);
}
