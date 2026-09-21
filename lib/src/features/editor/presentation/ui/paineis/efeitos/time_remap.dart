import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../../core/ds/ds.dart';
import '../../../../application/editor_controller.dart';
import '../../../../application/playback_controller.dart';
import '../../../../domain/cut_ops.dart'
    show hasTimeRemap, timeRemapTrackOf, videoSourceSpan;
import '../../../../domain/layer.dart';
import '../../../../domain/remapear_tempo.dart'
    show curvaIdentidade, reversoAPartirDe;
import '../tempo.dart' show rotuloDaInterpolacao;
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
    PlaybackController? playback,
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
          aoMudar: (i) => umPasso(ref, () => c.setClipInterpolacao(layerId, i)),
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
      // AS ACOES DO ESTUDIO DO TEMPO (a tela propria saiu a pedido do dono;
      // as acoes dela nao podiam sair junto): espelhar a curva, reverso a
      // partir do cabecote e voltar a velocidade constante.
      FileiraDeAcoes(
        acoes: [
          AureaChip(
            key: const ValueKey('time-remap-inverter'),
            rotulo: 'Inverter a curva',
            icone: CupertinoIcons.arrow_left_right,
            aoTocar: () => inverterACurva(ref, layerId),
          ),
          AureaChip(
            key: const ValueKey('time-remap-reverso-daqui'),
            rotulo: 'Reverso a partir daqui',
            icone: CupertinoIcons.arrow_uturn_left,
            aoTocar: () =>
                reversoAPartirDoCabecote(ref, layerId, playback?.time.value ?? t),
          ),
          if (hasTimeRemap(video))
            AureaChip(
              key: const ValueKey('time-remap-constante'),
              rotulo: 'Velocidade constante',
              icone: CupertinoIcons.minus,
              // Tira a curva e fica a velocidade media do clipe: a duracao
              // na timeline nao muda.
              aoTocar: () =>
                  umPasso(ref, () => c.ligarCurvaDeTempo(layerId, false)),
            ),
        ],
      ),
    ];
  }

  /// O CLIPE PASSA A CORRER AO CONTRARIO pela curva. Com o interruptor
  /// Reverse ligado, primeiro grava na curva o que a previa ja toca (e
  /// desliga o interruptor); depois espelha. Um passo de desfazer.
  static void inverterACurva(WidgetRef ref, String layerId) {
    final c = ref.read(editorControllerProvider.notifier);
    final l = ref.read(editorControllerProvider).layerById(layerId);
    if (l is! VideoLayer) return;
    c.runAsOneUndo(() {
      if (l.reverse) c.assarReversoNaCurva(layerId);
      c.assarReversoNaCurva(layerId);
    });
  }

  /// REVERSO A PARTIR DO CABECOTE: o que ja passou fica; dali em diante a
  /// curva espelha em torno do quadro atual. Um passo de desfazer.
  static void reversoAPartirDoCabecote(
    WidgetRef ref,
    String layerId,
    Duration global,
  ) {
    final c = ref.read(editorControllerProvider.notifier);
    final inicial = ref.read(editorControllerProvider).layerById(layerId);
    if (inicial is! VideoLayer) return;
    c.runAsOneUndo(() {
      if (inicial.reverse) c.assarReversoNaCurva(layerId);
      final l = ref.read(editorControllerProvider).layerById(layerId);
      if (l is! VideoLayer) return;
      final span = videoSourceSpan(l).inMicroseconds / 1e6;
      final trilha =
          timeRemapTrackOf(l) ?? curvaIdentidade(l.duration, span);
      // A trilha do Time Remap vive no tempo CRU do clipe.
      var local = global - l.startTime;
      if (local < Duration.zero) local = Duration.zero;
      if (local > l.duration) local = l.duration;
      c.definirTrilhaDeTempo(layerId, reversoAPartirDe(trilha, local, span));
    });
  }
}
