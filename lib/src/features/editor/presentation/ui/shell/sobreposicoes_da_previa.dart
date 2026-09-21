import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/l10n/app_language.dart';
import '../../../application/desempenho/aurea_performance_manager.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../application/preview_stats.dart';
import '../../../application/qualidade3d_controller.dart';
import '../../../domain/effect.dart';
import '../../../domain/estilizar_lote2.dart';
import '../../../domain/gear.dart';
import '../../../domain/layer.dart';
import '../../../domain/orcamento_render.dart';
import '../../../../../core/ui/am_colors.dart';

// AS SOBREPOSICOES DA PREVIA que o `EditorScreen` antigo desenhava por
// cima do palco: o diagnostico (modo dev) e o aviso de rascunho enquanto
// toca. Vieram de la sem mudar a conta — so ganharam nome publico.

/// Overlay de diagnostico: MARCHA com o motivo, composicoes por segundo,
/// variancia entre ticks e % de tempo em marcha baixa.
class DiagnosticoDaPrevia extends ConsumerWidget {
  const DiagnosticoDaPrevia({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final fps = ref.watch(editorControllerProvider.select((p) => p.fps));
    final total = ref.watch(
      editorControllerProvider.select((p) => p.layers.length),
    );
    PreviewStats.hookTimings();
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        ValueListenableBuilder<GearDecision?>(
          valueListenable: PreviewStats.gear,
          builder: (context, gear, _) => ValueListenableBuilder<int>(
            valueListenable: PreviewStats.compsPerSec,
            builder: (context, comps, _) => ValueListenableBuilder<double>(
              valueListenable: PreviewStats.tickVarianceMs,
              builder: (context, variance, _) => ValueListenableBuilder<int>(
                valueListenable: PreviewStats.layersInFrame,
                builder: (context, inFrame, _) => ValueListenableBuilder<int>(
                  valueListenable: PreviewStats.lowGearPercent,
                  builder: (context, lowPct, _) {
                    return Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 10,
                        vertical: 6,
                      ),
                      decoration: BoxDecoration(
                        color: AmColors.panel.withValues(alpha: 0.8),
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(color: AmColors.hairline),
                      ),
                      child: ValueListenableBuilder<int>(
                        valueListenable: PreviewStats.jankFrames,
                        builder: (context, jank, _) => ValueListenableBuilder<double>(
                          valueListenable: PreviewStats.worstFrameMs,
                          builder: (context, pior, _) =>
                              ValueListenableBuilder<FrameReport?>(
                                valueListenable: FrameLog.report,
                                builder: (context, r, _) => AppText(
                                  'UI: $jank travadas · pior ${pior.toStringAsFixed(0)} ms\n'
                                  'MARCHA: ${gear == null ? '—' : gearLabel(gear.gear)}\n'
                                  'motivo: ${gear?.reason ?? '—'}\n'
                                  'compoe $comps/s · projeto ${fps}fps\n'
                                  'variancia entre ticks: $variance ms\n'
                                  'camadas no frame: $inFrame / $total · '
                                  'M1+M2: $lowPct%\n'
                                  '── registrador (${r?.seconds ?? 0}s) ──\n'
                                  'mediana ${r?.medianMs ?? 0} ms · '
                                  'pico ${r?.peakMs ?? 0} ms\n'
                                  'travadas ${r?.stutters ?? 0} · '
                                  'intervalo ${r?.gapS ?? 0}s (±${r?.gapSdS ?? 0})\n'
                                  'deriva video-audio ${r?.driftMs ?? 0} ms',
                                  style: TextStyle(
                                    fontSize: 11,
                                    color: AmColors.accent,
                                    height: 1.4,
                                    fontFeatures: [
                                      FontFeature.tabularFigures(),
                                    ],
                                  ),
                                ),
                              ),
                        ),
                      ),
                    );
                  },
                ),
              ),
            ),
          ),
        ),
        const SizedBox(height: 4),
        const _DiagDesempenho(),
        const SizedBox(height: 4),
        const _Diag3D(),
      ],
    );
  }
}

/// A POLITICA DE DESEMPENHO EM VIGOR, no overlay de diagnostico.
///
/// Sem isto, "por que a previa ficou borrada?" so se responde adivinhando:
/// a escada desce por temperatura, memoria ou tempo de quadro, e as tres
/// causas terminam no mesmo pixel maior. A linha diz o perfil, o motivo e
/// os numeros que estao valendo AGORA.
class _DiagDesempenho extends StatelessWidget {
  const _DiagDesempenho();

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<PoliticaDeDesempenho>(
      valueListenable: AureaPerformanceManager.instancia.politica,
      builder: (context, politica, _) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
        decoration: BoxDecoration(
          color: AmColors.panel.withValues(alpha: 0.8),
          borderRadius: BorderRadius.circular(8),
        ),
        child: AppText(
          '── desempenho ──\n$politica',
          style: TextStyle(
            fontSize: 11,
            color: AmColors.accent,
            height: 1.4,
            fontFeatures: [FontFeature.tabularFigures()],
          ),
        ),
      ),
    );
  }
}

/// O MOTOR 3D NO OVERLAY: nivel, pressao, a estimativa de GPU contra o
/// orcamento, memoria do processo e o que o ultimo quadro desenhou.
class _Diag3D extends StatelessWidget {
  const _Diag3D();

  @override
  Widget build(BuildContext context) {
    final c = ControladorDeQualidade3D.instancia;
    return ValueListenableBuilder<Estatisticas3D?>(
      valueListenable: PreviewStats.cena3d,
      builder: (context, e, _) => ValueListenableBuilder<Qualidade3D>(
        valueListenable: c.nivel,
        builder: (context, nivel, _) => ValueListenableBuilder<NivelDePressao>(
          valueListenable: c.pressao,
          builder: (context, pressao, _) => ValueListenableBuilder<int>(
            valueListenable: PreviewStats.rssMb,
            builder: (context, rss, _) {
              if (e == null && c.cenasNaTela == 0) {
                return const SizedBox.shrink();
              }
              final est = c.estimativa.value;
              return Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: 10,
                  vertical: 6,
                ),
                decoration: BoxDecoration(
                  color: AmColors.panel.withValues(alpha: 0.8),
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: AmColors.hairline),
                ),
                child: AppText(
                  '── 3D ──\n'
                  'nivel ${qualidade3dRotulo(nivel)} · pressao '
                  '${nivelDePressaoRotulo(pressao)} · ${c.motivo.value}\n'
                  'GPU estimada ${bytesLegiveis(est.total)} de '
                  '${bytesLegiveis(c.orcamentoBytes)} '
                  '(alvos ${bytesLegiveis(est.alvosDeRender)} · sombras '
                  '${bytesLegiveis(est.sombras)} · tex ${bytesLegiveis(est.texturas)} · '
                  'geo ${bytesLegiveis(est.geometria)})\n'
                  'RSS $rss MB · disponivel '
                  '${c.disponivelBytes >= 0 ? bytesLegiveis(c.disponivelBytes) : '?'} · '
                  'termico ${c.termico}\n'
                  '${e ?? 'sem quadro em GPU'}',
                  style: TextStyle(
                    fontSize: 11,
                    color: AmColors.accent,
                    height: 1.4,
                    fontFeatures: [FontFeature.tabularFigures()],
                  ),
                ),
              );
            },
          ),
        ),
      ),
    );
  }
}

/// "Rascunho" sobre o preview enquanto toca — so quando ha algo que o
/// rascunho simplifica (cena 3D, brilho, nitidez), para nao virar ruido.
///
/// A LISTA SAI DO PROPRIO MOTOR. Ela era escrita a mao e listava
/// `lightGlow` e `glowVol` — dois efeitos que sairam do catalogo em 16/09.
/// O aviso passou a NUNCA aparecer, inclusive nos efeitos de brilho que
/// HOJE sao simplificados ao tocar: quem dava play via um halo mais cru e
/// nao tinha como saber que a qualidade final era outra. Derivar de
/// `receitasSapphire` e `passadasDeNitidez` faz o aviso acompanhar o
/// motor sozinho.
class AvisoDeRascunho extends ConsumerWidget {
  const AvisoDeRascunho({super.key});

  /// A VARREDURA LEMBRADA PELA PILHA.
  ///
  /// Este `select` roda a cada mutacao do projeto — inclusive a cada
  /// passo de um slider — e percorre todas as camadas e todos os efeitos
  /// de cada uma. A resposta so pode mudar quando a pilha muda de
  /// identidade (a lista e imutavel), entao um par lembrado basta.
  static List<Layer>? _pilhaDoAviso;
  static bool _respostaDoAviso = false;

  static bool _simplificaAlgo(List<Layer> camadas) {
    if (identical(camadas, _pilhaDoAviso)) return _respostaDoAviso;
    _pilhaDoAviso = camadas;
    return _respostaDoAviso = camadas.any(
      (l) =>
          l is Scene3DLayer ||
          l.effects.any(
            (e) =>
                e.enabled &&
                ((receitasSapphire[e.type]?.usaOrcamentoDeAmostras ?? false) ||
                    e.type == EffectType.unsharpMask),
          ),
    );
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final simplifica = ref.watch(
      editorControllerProvider.select((p) => _simplificaAlgo(p.layers)),
    );
    if (!simplifica) return const SizedBox.shrink();
    return ValueListenableBuilder<bool>(
      valueListenable: PlaybackController.tocandoAgora,
      builder: (context, tocando, _) {
        if (!tocando) return const SizedBox.shrink();
        return IgnorePointer(
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
            decoration: BoxDecoration(
              color: AmColors.panel.withValues(alpha: 0.8),
              borderRadius: BorderRadius.circular(999),
            ),
            child: const AppText(
              'Rascunho · pause para ver a qualidade final',
              style: TextStyle(fontSize: 10.5, color: AmColors.muted),
            ),
          ),
        );
      },
    );
  }
}
