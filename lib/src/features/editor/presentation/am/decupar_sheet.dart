import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// DECUPAR — o clipe inteiro cortado onde a cena muda, sozinho.
///
/// O detector e o mesmo scdet que o Premiere usa por baixo do "Scene
/// Edit Detection" (FFmpeg, numa versao encolhida do video). A folha
/// oferece a SENSIBILIDADE em palavras, e duas saidas: cortar de
/// verdade (um desfazer so) ou virar marcas na regua para revisar
/// antes.
Future<void> showDecuparSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) => showParamSheet(
  context,
  title: 'Decupar',
  heightFactor: 0.45,
  builder: (sheetContext) => _Decupar(ref: ref, layerId: layerId),
);

class _Decupar extends StatefulWidget {
  const _Decupar({required this.ref, required this.layerId});

  final WidgetRef ref;
  final String layerId;

  @override
  State<_Decupar> createState() => _DecuparState();
}

enum _Sensibilidade {
  sensivel,
  normal,
  secos;

  String get emPalavras => switch (this) {
    _Sensibilidade.sensivel => 'Sensível',
    _Sensibilidade.normal => 'Normal',
    _Sensibilidade.secos => 'Só cortes secos',
  };

  String get explicacao => switch (this) {
    _Sensibilidade.sensivel =>
      'Pega até transições suaves. Pode cortar demais.',
    _Sensibilidade.normal => 'O padrão: cortes de câmera comuns.',
    _Sensibilidade.secos => 'Só mudanças bruscas de cena.',
  };

  double get limiar => switch (this) {
    _Sensibilidade.sensivel => 0.22,
    _Sensibilidade.normal => 0.35,
    _Sensibilidade.secos => 0.55,
  };
}

class _DecuparState extends State<_Decupar> {
  _Sensibilidade _sens = _Sensibilidade.normal;
  bool _rodando = false;

  EditorController get _c => widget.ref.read(editorControllerProvider.notifier);

  Future<void> _rodar({required bool cortar}) async {
    setState(() => _rodando = true);
    final n = cortar
        ? await _c.decuparCamada(widget.layerId, sensibilidade: _sens.limiar)
        : await _c.cortesDeCenaViramMarcas(
            widget.layerId,
            sensibilidade: _sens.limiar,
          );
    if (!mounted) return;
    setState(() => _rodando = false);
    if (n == null) {
      AureaSnack.show(
        context,
        'Não consegui ler esse vídeo para decupar.',
      );
      return;
    }
    Navigator.of(context).maybePop();
    AureaSnack.show(
      context,
      n == 0
          ? 'Nenhuma mudança de cena nesse trecho. Tente Sensível.'
          : cortar
              ? '$n corte${n == 1 ? '' : 's'} de cena feitos'
              : '$n marca${n == 1 ? '' : 's'} de cena na régua',
      actionLabel: n == 0 || !cortar ? null : 'Desfazer',
      onAction: _c.undo,
      duration: const Duration(seconds: 5),
    );
  }

  @override
  Widget build(BuildContext context) => SafeArea(
    child: Padding(
      padding: const EdgeInsets.fromLTRB(18, 12, 18, 18),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const AppText(
            'Acha onde a cena muda e corta o clipe em todos os pontos — '
            'o mesmo detector do Scene Edit Detection.',
            style: TextStyle(
              fontSize: 11.5,
              height: 1.35,
              color: AmColors.muted,
            ),
          ),
          const SizedBox(height: 12),
          Row(
            children: [
              for (final s in _Sensibilidade.values) ...[
                Tocavel(
                  key: ValueKey('decupar-${s.name}'),
                  onTap: () => setState(() => _sens = s),
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 12,
                      vertical: 9,
                    ),
                    decoration: BoxDecoration(
                      color: s == _sens ? AmColors.accentDim : AmColors.chip,
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: AppText(
                      s.emPalavras,
                      style: TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                        color: s == _sens ? AmColors.accent : AmColors.text,
                      ),
                    ),
                  ),
                ),
                const SizedBox(width: 6),
              ],
            ],
          ),
          const SizedBox(height: 6),
          AppText(
            _sens.explicacao,
            style: const TextStyle(fontSize: 11.5, color: AmColors.muted),
          ),
          const SizedBox(height: 14),
          if (_rodando)
            Container(
              key: const ValueKey('decupar-rodando'),
              width: double.infinity,
              constraints: const BoxConstraints(minHeight: 44),
              alignment: Alignment.center,
              decoration: BoxDecoration(
                color: AmColors.chip,
                borderRadius: BorderRadius.circular(12),
              ),
              child: const Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  CupertinoActivityIndicator(radius: 8),
                  SizedBox(width: 8),
                  AppText(
                    'Lendo o vídeo...',
                    style: TextStyle(fontSize: 13, color: AmColors.text),
                  ),
                ],
              ),
            )
          else ...[
            Tocavel(
              key: const ValueKey('decupar-cortar'),
              haptico: true,
              onTap: () => _rodar(cortar: true),
              child: Container(
                width: double.infinity,
                constraints: const BoxConstraints(minHeight: 44),
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: AmColors.accentDim,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: const AppText(
                  'Decupar agora',
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: AmColors.accent,
                  ),
                ),
              ),
            ),
            const SizedBox(height: 8),
            Tocavel(
              key: const ValueKey('decupar-marcas'),
              onTap: () => _rodar(cortar: false),
              child: Container(
                width: double.infinity,
                constraints: const BoxConstraints(minHeight: 44),
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: AmColors.chip,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: const AppText(
                  'Só marcar na régua (revisar antes)',
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: AmColors.text,
                  ),
                ),
              ),
            ),
          ],
        ],
      ),
    ),
  );
}
