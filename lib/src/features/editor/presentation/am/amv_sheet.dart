import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/cut.dart';
import '../../domain/impacto_amv.dart';
import '../../domain/keyframe.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'beats_sheet.dart';
import 'estudio_do_tempo.dart';

/// A FOLHA AMV — as batidas viradas em edicao, num lugar so.
///
/// Cada acao e uma PORTA FINA para os sistemas normais do app: o
/// impacto anexa efeitos e keyframes de verdade, o whip e a transicao
/// de sempre, a rampa abre o estudio do tempo, os cortes usam a grade
/// de batidas ja analisada. Nada aqui e caixa preta.
Future<void> showAmvSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  await showParamSheet(
    context,
    title: 'AMV',
    heightFactor: 0.62,
    builder: (sheetContext) =>
        _Amv(layerId: layerId, playback: playback),
  );
}

class _Amv extends ConsumerWidget {
  const _Amv({required this.layerId, required this.playback});

  final String layerId;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controller = ref.read(editorControllerProvider.notifier);
    final projeto = ref.watch(editorControllerProvider);
    final layer = projeto.layerById(layerId);
    if (layer == null) return const SizedBox.shrink();
    final agora = playback.time.value;

    void feito(String texto) {
      AureaSnack.show(
        context,
        texto,
        actionLabel: 'Desfazer',
        onAction: controller.undo,
      );
    }

    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(18, 12, 18, 20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const AppText(
              'Tudo aqui vira efeitos e keyframes normais, editáveis na '
              'pilha e nas curvas — nada fechado.',
              style: TextStyle(
                fontSize: 11.5,
                height: 1.35,
                color: AmColors.muted,
              ),
            ),
            const SizedBox(height: 12),
            for (final nivel in ImpactoAmv.values)
              _Acao(
                chave: 'amv-impacto-${nivel.name}',
                titulo: nivel.emPalavras,
                detalhe: nivel.explicacao,
                onTap: () {
                  controller.aplicarImpactoAmv(layerId, agora, nivel);
                  feito('${nivel.emPalavras} no cabeçote.');
                },
              ),
            const SizedBox(height: 6),
            _Acao(
              chave: 'amv-rampa',
              titulo: 'Rampa de tempo',
              detalhe: 'Abre o estúdio do tempo: grafo de valor e de '
                  'velocidade, congelar e reverso.',
              onTap: () =>
                  showEstudioDoTempo(context, ref, layerId, playback),
            ),
            _Acao(
              chave: 'amv-whip',
              titulo: 'Whip para o próximo corte',
              detalhe: 'Transição de chicote na junção com o clipe '
                  'seguinte, editável na folha de transição.',
              onTap: () {
                final ok = controller.applyTransition(
                  layerId,
                  ClipTransitionType.whip,
                  duration: const Duration(milliseconds: 260),
                  curve: Easing.easeInOut,
                );
                feito(
                  ok
                      ? 'Whip aplicado na junção.'
                      : 'Não achei um clipe encostado depois deste.',
                );
              },
            ),
            _Acao(
              chave: 'amv-flash',
              titulo: 'Flash na batida',
              detalhe: 'Só o clarão de exposição, curto, no cabeçote.',
              onTap: () {
                controller.aplicarImpactoAmv(
                  layerId,
                  agora,
                  ImpactoAmv.suave,
                );
                feito('Flash cravado (impacto suave).');
              },
            ),
            const SizedBox(height: 6),
            _Acao(
              chave: 'amv-cortar-batidas',
              titulo: 'Cortar nas batidas',
              detalhe: projeto.beats.isEmpty
                  ? 'Analise a música primeiro (botão Batidas abaixo).'
                  : 'Divide este clipe em cada batida da grade '
                        '(${projeto.beats.length} no projeto).',
              onTap: () {
                final n = controller.cortarNasBatidas(layerId);
                feito(
                  n == 0
                      ? 'Nenhuma batida dentro deste clipe — analise a '
                            'música na folha Batidas.'
                      : 'Clipe dividido em $n batida(s).',
                );
              },
            ),
            _Acao(
              chave: 'amv-batidas',
              titulo: 'Batidas…',
              detalhe: 'Analisar a música (BPM e grade) e ajustar a '
                  'densidade.',
              onTap: () => showBeatsSheet(context, ref, layerId),
            ),
          ],
        ),
      ),
    );
  }
}

class _Acao extends StatelessWidget {
  const _Acao({
    required this.chave,
    required this.titulo,
    required this.detalhe,
    required this.onTap,
  });

  final String chave;
  final String titulo;
  final String detalhe;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: Tocavel(
      key: ValueKey(chave),
      onTap: onTap,
      child: Container(
        width: double.infinity,
        padding: const EdgeInsets.symmetric(horizontal: 13, vertical: 11),
        decoration: BoxDecoration(
          color: AmColors.chip,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            AppText(
              titulo,
              style: const TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
            const SizedBox(height: 2),
            AppText(
              detalhe,
              style: const TextStyle(fontSize: 11, color: AmColors.muted),
            ),
          ],
        ),
      ),
    ),
  );
}
