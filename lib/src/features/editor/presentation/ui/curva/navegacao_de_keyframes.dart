import 'package:flutter/widgets.dart' show VoidCallback;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/aurea_keyframe_button.dart' show marcasVizinhas;
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/layer.dart';
import 'trilha_da_curva.dart';

// ===========================================================================
// IR A MARCA ANTERIOR / PROXIMA
// ===========================================================================
//
// O controlador nao tem "keyframe anterior" — e nao precisa: as marcas ja
// existem, e pular e escolher uma e fazer `seek`. A conta de QUAL marca e
// a `marcasVizinhas` do design system, a mesma das setas de todo painel:
// as ‹ › do editor de curva, do losango e da timeline tem de concordar.

/// A MARCA VIZINHA de [agoraLocal] em [marcasLocais]: a ultima antes
/// ([direcao] < 0) ou a primeira depois ([direcao] > 0), ignorando a que
/// esta em cima. Nula quando nao ha.
Duration? marcaVizinha({
  required Iterable<Duration> marcasLocais,
  required Duration agoraLocal,
  required int direcao,
}) {
  final viz = marcasVizinhas(
    marcasLocais.map((d) => d.inMicroseconds),
    agoraLocal.inMicroseconds,
  );
  final us = direcao < 0 ? viz.anterior : viz.proxima;
  return us == null ? null : Duration(microseconds: us);
}

/// VAI A MARCA ANTERIOR ([direcao] < 0) OU PROXIMA ([direcao] > 0): pausa
/// e move o cabecote (`PlaybackController.seek`).
///
/// [marcasLocais] estao no relogio da camada; [relogio] diz qual (o do Time
/// Remap e cru). Devolve o instante GLOBAL para onde o cabecote foi, ou nulo
/// quando nao havia marca naquela direcao (e ai nada se mexe).
///
/// Pausa antes: trocar de marca e escolha da pessoa, e o relogio andando
/// passaria por cima do `seek` no quadro seguinte.
Duration? irParaMarcaVizinha({
  required PlaybackController playback,
  required Layer camada,
  required Iterable<Duration> marcasLocais,
  required int direcao,
  RelogioDaTrilha relogio = relogioDaCamada,
}) {
  final alvo = marcaVizinha(
    marcasLocais: marcasLocais,
    agoraLocal: relogio(camada, playback.time.value),
    direcao: direcao,
  );
  if (alvo == null) return null;
  final global = camada.startTime + alvo;
  playback.pause();
  playback.seek(global);
  return global;
}

/// O MESMO, para uma [TrilhaDaCurva] da camada [layerId] — o que o losango
/// de qualquer painel e a timeline chamam.
Duration? irParaMarcaDaTrilha(
  WidgetRef ref,
  PlaybackController playback, {
  required String layerId,
  required TrilhaDaCurva trilha,
  required int direcao,
}) {
  final camada = ref.read(editorControllerProvider).layerById(layerId);
  if (camada == null) return null;
  final controller = ref.read(editorControllerProvider.notifier);
  return irParaMarcaVizinha(
    playback: playback,
    camada: camada,
    marcasLocais: trilha.marcasDe(controller, camada),
    direcao: direcao,
    relogio: trilha.relogio,
  );
}

/// AS SETAS PRONTAS para o `AureaKeyframeButton` (`aoAnterior`/`aoProximo`):
/// nula a seta que nao tem para onde ir, como o botao espera.
({VoidCallback? anterior, VoidCallback? proximo}) setasDaTrilha(
  WidgetRef ref,
  PlaybackController playback, {
  required Layer camada,
  required TrilhaDaCurva trilha,
}) {
  final controller = ref.read(editorControllerProvider.notifier);
  final marcas = trilha.marcasDe(controller, camada);
  final agora = trilha.localEm(camada, playback.time.value);
  VoidCallback? seta(int direcao) =>
      marcaVizinha(marcasLocais: marcas, agoraLocal: agora, direcao: direcao) ==
          null
      ? null
      : () => irParaMarcaDaTrilha(
          ref,
          playback,
          layerId: camada.id,
          trilha: trilha,
          direcao: direcao,
        );
  return (anterior: seta(-1), proximo: seta(1));
}
