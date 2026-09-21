import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../am/audio_sheet.dart' show showAudioSheet;
import '../../am/beats_sheet.dart' show showBeatsSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// AUDIO — o volume aqui mesmo; fades, limpeza, voz e batidas pelas portas
/// dos editores que ja existem.
class PainelAudio extends ConsumerWidget {
  const PainelAudio({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Áudio';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final c = ref.read(editorControllerProvider.notifier);
    final volume = switch (camada) {
      AudioLayer l => l.volume,
      VideoLayer l => l.volume,
      _ => null,
    };
    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.audio.name}',
      aoFechar: escopo.fecharPainel,
      filhos: [
        if (volume != null)
          AureaPropertyRow(
            rotulo: 'Volume',
            valor: volume * 100,
            aoMudar: (v) => c.editVolume(layerId, v / 100),
            min: 0,
            max: 100,
            unidade: '%',
            casas: 0,
            aoResetar: () => c.editVolume(layerId, 1),
            aoComecarGesto: c.beginGesture,
            aoTerminarGesto: c.endGesture,
          )
        else
          const AureaAvisoDoPainel(texto: 'Esta camada não tem som.'),
        LinhaDePorta(
          rotulo: 'Fades, limpeza e voz',
          icone: CupertinoIcons.waveform,
          aoTocar: () =>
              showAudioSheet(context, ref, layerId, playback: escopo.playback),
        ),
        LinhaDePorta(
          rotulo: 'Batidas e BPM',
          icone: CupertinoIcons.metronome,
          aoTocar: () {
            escopo.playback.pause();
            showBeatsSheet(context, ref, layerId);
          },
        ),
      ],
    );
  }
}
