import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/effect.dart';
import '../../../domain/layer.dart';
import '../../am/color_picker_sheet.dart' show showColorPicker;
import '../../am/layer_menu.dart' show ColorFillPanel, showElement3DSheet;
import '../shell/contrato.dart';
import 'comum.dart';
import 'efeitos.dart';

/// COR — o que "cor" quer dizer depende do tipo:
///
///  * texto: a cor da letra (e o preenchimento completo pela porta);
///  * forma: o preenchimento (porta para o painel de cor que ja existe);
///  * video e imagem: a CORRECAO DE COR — os efeitos da categoria Cor,
///    com um toque para acrescentar cada um;
///  * elemento 3D: a ficha do elemento.
class PainelCor extends ConsumerWidget {
  const PainelCor({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Cor';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final c = ref.read(editorControllerProvider.notifier);
    final chave = 'painel-${PainelId.cor.name}';

    Future<void> preenchimentoCompleto() => mostrarAureaFolha<void>(
      context,
      titulo: 'Cor e preenchimento',
      altura: 320,
      construtor: (folha) => ColorFillPanel(
        playback: escopo.playback,
        onBack: () => Navigator.of(folha).maybePop(),
      ),
    );

    switch (camada) {
      case TextLayer():
        return AureaPanel(
          titulo: _titulo,
          chave: chave,
          aoFechar: escopo.fecharPainel,
          filhos: [
            AureaPropertyRow.cor(
              rotulo: 'Cor do texto',
              cor: camada.color,
              aoTocar: () async {
                final nova = await showColorPicker(
                  context,
                  initial: camada.color,
                  onChanged: (cor) => c.editTextLayer(layerId, color: cor),
                );
                if (nova != null) c.editTextLayer(layerId, color: nova);
              },
            ),
            LinhaDePorta(
              rotulo: 'Degradê e preenchimento',
              aoTocar: preenchimentoCompleto,
            ),
          ],
        );
      case ShapeLayer():
        return PainelDePortas(
          titulo: _titulo,
          chave: chave,
          portas: [
            LinhaDePorta(
              rotulo: 'Cor e preenchimento',
              aoTocar: preenchimentoCompleto,
            ),
          ],
        );
      case Element3DLayer():
        return PainelDePortas(
          titulo: _titulo,
          chave: chave,
          portas: [
            LinhaDePorta(
              rotulo: 'Cor e material do elemento',
              aoTocar: () => showElement3DSheet(context, ref, layerId),
            ),
          ],
        );
      default:
        final daCor = effectsInCategory('Color');
        return AureaPanel(
          titulo: _titulo,
          chave: chave,
          aoFechar: escopo.fecharPainel,
          corpo: PilhaDeEfeitos(
            layerId: layerId,
            filtro: (e) => e.conhecido && e.spec.category == 'Color',
            vazio: 'Nenhuma correção de cor. Escolha uma acima.',
            cabecalho: [
              Padding(
                padding: const EdgeInsets.only(bottom: AureaDims.e10),
                child: Wrap(
                  spacing: AureaDims.e6,
                  runSpacing: AureaDims.e6,
                  children: [
                    for (final tipo in daCor)
                      AureaChip(
                        key: ValueKey('cor-adicionar-${tipo.name}'),
                        rotulo: effectSpecs[tipo]?.name ?? tipo.name,
                        icone: CupertinoIcons.plus,
                        aoTocar: () => c.addEffect(layerId, tipo),
                      ),
                  ],
                ),
              ),
            ],
          ),
        );
    }
  }
}
