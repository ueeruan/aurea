import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../am/layer_menu.dart'
    show BlendingPanel, ModoDeMescla, categoriasDeMescla, showMasksSheet;
import '../shell/contrato.dart';
import 'comum.dart';
import 'pontos.dart';

/// MASCARA — mistura, opacidade e mascaras da camada (o "Blending &
/// Opacity" da referencia): opacidade com losango, o modo de mistura (os
/// nativos e os proprios do motor, todos) e a porta das mascaras.
class PainelMascara extends ConsumerWidget {
  const PainelMascara({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Mistura e máscara';

  /// TODOS os modos que o motor sabe fazer, na ordem das categorias.
  static final List<ModoDeMescla> _modos = [
    for (final cat in categoriasDeMescla) ...cat.modos,
  ];

  static ModoDeMescla _modoDe(Layer l) {
    final proprio = l.customBlend;
    for (final m in _modos) {
      if (proprio != null ? m.aurea == proprio : m.nativo == l.blendMode) {
        return m;
      }
    }
    return _modos.first;
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final visivel = camadaVisivel(ref, layerId);
    final gravada = camadaGravada(ref, layerId);
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: _titulo);
    }
    final c = ref.read(editorControllerProvider.notifier);
    void editarPontos(String maskId) =>
        abrirEditarPontosDaMascara(context, ref, escopo.playback, maskId);
    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.mascara.name}',
      aoFechar: escopo.fecharPainel,
      corpo: NoCabecote(
        construir: (context, t) {
          final kf = losangoDaPropriedade(
            ref,
            gravada: gravada,
            prop: LayerProp.opacity,
            t: t,
            playback: escopo.playback,
          );
          return ListView(
            padding: const EdgeInsets.fromLTRB(
              AureaDims.margemDoPainel,
              AureaDims.e4,
              AureaDims.margemDoPainel,
              AureaDims.topoDoPainel,
            ),
            children: [
              AureaPropertyRow(
                rotulo: 'Opacidade',
                valor: visivel.opacity.valueAt(visivel.localTime(t)) * 100,
                aoMudar: (v) => c.editOpacity(layerId, t, v / 100),
                min: 0,
                max: 100,
                unidade: '%',
                casas: 0,
                keyframe: kf.estado,
                aoAnterior: kf.anterior,
                aoProximo: kf.proximo,
                aoResetar: () => c.resetProp(layerId, LayerProp.opacity),
                aoComecarGesto: c.beginGesture,
                aoTerminarGesto: c.endGesture,
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Mistura',
                filho: AureaDropdown<ModoDeMescla>(
                  valor: _modoDe(visivel),
                  opcoes: _modos,
                  rotuloDe: (m) => m.rotulo,
                  titulo: 'Modo de mistura',
                  aoMudar: (m) {
                    final proprio = m.aurea;
                    final nativo = m.nativo;
                    if (proprio != null) {
                      c.setCustomBlend(layerId, proprio);
                    } else if (nativo != null) {
                      c.setBlendMode(layerId, nativo);
                    }
                  },
                ),
              ),
              LinhaDePorta(
                rotulo: visivel.masks.isEmpty
                    ? 'Adicionar máscara'
                    : 'Máscaras (${visivel.masks.length})',
                icone: CupertinoIcons.circle_lefthalf_fill,
                aoTocar: () {
                  escopo.playback.pause();
                  showMasksSheet(
                    context,
                    ref,
                    layerId,
                    escopo.playback,
                    onEditMaskPoints: editarPontos,
                  );
                },
              ),
              LinhaDePorta(
                rotulo: 'Recorte e mistura avançada',
                icone: CupertinoIcons.square_stack_3d_down_right,
                aoTocar: () => mostrarAureaFolha<void>(
                  context,
                  titulo: _titulo,
                  altura: 360,
                  construtor: (folha) => BlendingPanel(
                    playback: escopo.playback,
                    onBack: () => Navigator.of(folha).maybePop(),
                    onOpenCurve: (_) {},
                    onEditMaskPoints: (maskId) {
                      Navigator.of(folha).maybePop();
                      editarPontos(maskId);
                    },
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}
