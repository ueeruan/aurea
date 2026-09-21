import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../../core/ds/ds.dart';
import '../../../../application/editor_controller.dart';
import '../../../../application/playback_controller.dart';
import '../../../../domain/layer.dart';
import '../../../am/estudio_do_tempo.dart' show rotuloDaInterpolacao;
import '../pecas_centrais.dart';

/// AS LINHAS DO TIME REMAP que a ficha do efeito nao descreve.
///
/// O efeito tem UM parametro guardado, `tempo` — a trilha do motor. As
/// outras linhas sao LEITURAS ou ESCRITAS dessa mesma trilha, e nao
/// numeros a parte: Speed e a derivada no cabecote, Frame e o instante em
/// quadros, Reverse e Freeze mexem no mapeamento. Guardar copias desses
/// fatos daria duas verdades para o mesmo instante (a licao do painel
/// antigo, `am/effects_panel.dart`).
///
/// SEM TELA PROPRIA: e o mesmo cartao, com as mesmas [AureaPropertyRow].
/// A curva abre o editor de curva geral (o mesmo de qualquer parametro).
abstract final class LinhasDoTimeRemap {
  /// "Speed", que vem ANTES do "Time" (a ordem que o dono pediu).
  static List<Widget> antes(
    WidgetRef ref, {
    required String layerId,
    required Duration t,
    required PlaybackController playback,
  }) {
    final c = ref.read(editorControllerProvider.notifier);
    return [
      AureaPropertyRow(
        rotulo: 'Speed',
        chave: 'time-remap-speed',
        valor: c.timeRemapSpeedAt(layerId, t),
        // Sem faixa: a velocidade de um remapeamento nao tem teto util, e
        // a regua de riscos mostra "quanto andou" em vez de "onde".
        sensibilidade: .01,
        casas: 2,
        unidade: '×',
        aoMudar: aCadaPasso(
          (v) => c.setTimeRemapSpeed(layerId, playback.time.value, v),
        ),
        aoComecarGesto: c.beginGesture,
        aoTerminarGesto: c.endGesture,
      ),
    ];
  }

  /// Frame, Reverse, Freeze, Interpolacao, Manter tom e a curva.
  static List<Widget> depois(
    BuildContext context,
    WidgetRef ref, {
    required String layerId,
    required VideoLayer video,
    required Duration t,
    required VoidCallback abrirCurva,
  }) {
    final c = ref.read(editorControllerProvider.notifier);
    return [
      AureaPropertyRow(
        rotulo: 'Frame',
        chave: 'time-remap-frame',
        valor: c.timeRemapFrameAt(layerId, t),
        sensibilidade: .5,
        casas: 0,
        aoMudar: aCadaPasso((v) => c.setTimeRemapFrame(layerId, t, v)),
        aoComecarGesto: c.beginGesture,
        aoTerminarGesto: c.endGesture,
      ),
      AureaPropertyRow.personalizada(
        rotulo: 'Reverse',
        chave: 'time-remap-reverse',
        filho: AureaToggle(
          valor: video.reverse,
          aoMudar: (v) => umPasso(ref, () => c.setClipReverse(layerId, v)),
        ),
      ),
      AureaPropertyRow.personalizada(
        rotulo: 'Freeze',
        chave: 'time-remap-freeze',
        filho: AureaToggle(
          valor: c.timeRemapCongeladoEm(layerId, t),
          // Ja e um passo so no controlador (runAsOneUndo).
          aoMudar: (v) => c.setTimeRemapFreeze(layerId, t, v),
        ),
      ),
      AureaPropertyRow.personalizada(
        rotulo: 'Interpolação',
        chave: 'time-remap-interpolacao',
        filho: AureaDropdown<InterpolacaoDeQuadros>(
          valor: video.interpolacao,
          opcoes: InterpolacaoDeQuadros.values,
          // A MESMA PALAVRA do painel Tempo (`rotuloDaInterpolacao`): as
          // superficies do tempo nao podem chamar o mesmo modo de dois
          // nomes.
          rotuloDe: rotuloDaInterpolacao,
          titulo: 'Interpolação',
          aoMudar: (i) =>
              umPasso(ref, () => c.setClipInterpolacao(layerId, i)),
        ),
      ),
      AureaPropertyRow.personalizada(
        rotulo: 'Manter tom',
        chave: 'time-remap-tom',
        filho: AureaToggle(
          valor: video.audio.preservePitch,
          aoMudar: (v) =>
              umPasso(ref, () => c.setClipPreservePitch(layerId, v)),
        ),
      ),
      AureaPropertyRow.personalizada(
        rotulo: 'Curva',
        chave: 'time-remap-curva',
        filho: AureaChip(
          key: const ValueKey('time-remap-abrir-curva'),
          rotulo: 'Abrir curva',
          icone: CupertinoIcons.graph_square,
          aoTocar: abrirCurva,
        ),
      ),
    ];
  }
}
