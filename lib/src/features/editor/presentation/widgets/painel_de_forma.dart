import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/am_colors.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import '../../domain/shape.dart';
import 'linha_de_parametro.dart';
import 'rails_do_painel.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

final parametroDaFormaAbertoProvider = StateProvider<String?>((ref) => null);

/// O PAINEL DE EDITAR FORMA MODERNO.
///
/// Edita os parametros parametricos da forma (largura, altura, cantos, pontas, etc.)
/// com LinhaDeParametro e rails de keyframe/curva.
class PainelDeForma extends ConsumerWidget {
  const PainelDeForma({
    super.key,
    required this.camada,
    required this.playback,
    required this.aoVoltar,
    this.aoAbrirCurva,
  });

  final ShapeLayer camada;
  final PlaybackController playback;
  final VoidCallback aoVoltar;
  final void Function(String chave, Easing e, void Function(Easing) aoMudarCurva)? aoAbrirCurva;

  static const alturaMaxima = 300.0;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return ValueListenableBuilder<Duration>(
      valueListenable: playback.time,
      builder: (context, tempo, _) {
        final c = ref.read(editorControllerProvider.notifier);
        final real = ref.watch(projetoVisivelProvider).layerById(camada.id) as ShapeLayer? ?? camada;
        final local = real.localTime(tempo);
        final escolhida = ref.watch(parametroDaFormaAbertoProvider);

        final formas = real.contents.whereType<ShapeParametric>().toList();
        if (formas.isEmpty) {
          return Container(
            height: alturaMaxima,
            color: AmColors.panelHigh,
            alignment: Alignment.center,
            child: const AppText('Esta camada nao tem forma parametrica para ajustar.',
              style: TextStyle(fontSize: 12, color: AmColors.muted),
            ),
          );
        }

        final s = formas.first;
        final chaves = parametrosDaForma(s.kind);
        final chaveAtiva = (escolhida != null && chaves.contains(escolhida))
            ? escolhida
            : chaves.firstOrNull;

        final trilhaAtiva = chaveAtiva == null ? null : shapeParamTrackOf(s, chaveAtiva);
        final temKf = trilhaAtiva != null && trilhaAtiva.hasKeyframeAt(local);
        final animado = trilhaAtiva != null && trilhaAtiva.isAnimated;

        final alvo = AlvoDoRail(
          temKeyframeAqui: temKf,
          animado: animado,
          aoAlternarKeyframe: chaveAtiva == null
              ? null
              : () => c.toggleShapeParamKeyframe(real.id, chaveAtiva, tempo),
          aoAbrirCurva: (chaveAtiva != null && trilhaAtiva != null && trilhaAtiva.keyframes.length >= 2)
              ? () {
                  final kf = trilhaAtiva.keyframes.firstWhere(
                    (k) => (k.time - local).abs() < const Duration(milliseconds: 8),
                    orElse: () => trilhaAtiva.keyframes.first,
                  );
                  aoAbrirCurva?.call(
                    chaveAtiva,
                    kf.ease,
                    (novaCurva) => c.setShapeParamSegmentEase(real.id, chaveAtiva, local, novaCurva),
                  );
                }
              : null,
        );

        return Container(
          height: alturaMaxima,
          color: AmColors.panelHigh,
          child: Row(
            children: [
              RailEsquerdo(
                aoVoltar: aoVoltar,
                alvo: alvo,
              ),
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  child: ListView(
                    children: [
                      if (formas.length > 1)
                        const Padding(
                          padding: EdgeInsets.only(bottom: 8),
                          child: AppText('Ajustando o primeiro desenho desta camada.',
                            style: TextStyle(fontSize: 11, color: AmColors.muted),
                          ),
                        ),
                      for (final chave in chaves)
                        if (shapeParamTrackOf(s, chave) case final trilha?)
                          LinhaDeParametro(
                            rotulo: fichaDoParametroDaForma(chave).rotulo,
                            valor: trilha.valueAt(local),
                            casas: 0,
                            porPixel: fichaDoParametroDaForma(chave).teto / 300,
                            escolhida: chaveAtiva == chave,
                            aoEscolher: () =>
                                ref.read(parametroDaFormaAbertoProvider.notifier).state = chave,
                            aoComecar: c.beginGesture,
                            aoMudar: (v) => c.editShapeParam(real.id, chave, tempo, v),
                            aoTerminar: c.endGesture,
                            aoDigitar: (v) => c.editShapeParam(real.id, chave, tempo, v),
                          ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}
