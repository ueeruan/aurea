import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../../core/ds/ds.dart';
import '../../../../application/editor_controller.dart';
import '../../../../domain/layer.dart';
import '../../shell/contrato.dart';
import '../audio.dart' show SecaoDeEfeitosDeAudio;
import '../comum.dart';
import '../pecas_centrais.dart';
import 'catalogo_de_efeitos.dart';
import 'pilha_de_efeitos.dart';

/// EFEITOS — a pilha da camada e a porta do catalogo.
///
///   [Efeitos ................. +  ✓]
///   [ cartao · cartao · cartao ]     (arrasta a alca para reordenar)
///   [ + Adicionar efeito · Colar ]
///
/// Numa camada de AUDIO a pilha e a dos efeitos de som (eco, compressor,
/// EQ...): e o que "efeito" quer dizer ali.
class PainelEfeitos extends ConsumerWidget {
  const PainelEfeitos({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    // SO O TIPO da camada: quem acompanha os numeros e a pilha.
    final tipo = ref.watch(
      projetoVisivelProvider.select((p) => p.layerById(layerId)?.runtimeType),
    );
    if (tipo == null) return const PainelSemCamada(titulo: 'Efeitos');
    final chave = 'painel-${PainelId.efeitos.name}';
    if (tipo == AudioLayer) {
      return AureaPanel(
        titulo: 'Efeitos',
        chave: chave,
        aoFechar: escopo.fecharPainel,
        filhos: [SecaoDeEfeitosDeAudio(layerId: layerId, comTitulo: false)],
      );
    }
    final c = ref.read(editorControllerProvider.notifier);
    void catalogo() =>
        abrirCatalogoDeEfeitos(context, layerId: layerId, escopo: escopo);
    return AureaPanel(
      titulo: 'Efeitos',
      chave: chave,
      aoFechar: escopo.fecharPainel,
      acoes: [
        AcaoDoCabecalho(
          key: const ValueKey('efeitos-adicionar'),
          icone: CupertinoIcons.plus,
          ativo: true,
          aoTocar: catalogo,
        ),
      ],
      corpo: PilhaDeEfeitos(
        layerId: layerId,
        rodape: [
          FileiraDeAcoes(
            acoes: [
              AureaChip(
                key: const ValueKey('efeitos-adicionar-rodape'),
                rotulo: 'Adicionar efeito',
                icone: CupertinoIcons.plus,
                aoTocar: catalogo,
              ),
              if (c.temEfeitosCopiados)
                AureaChip(
                  key: const ValueKey('efeitos-colar'),
                  rotulo: 'Colar efeitos',
                  icone: CupertinoIcons.doc_on_clipboard,
                  aoTocar: () => umPasso(ref, () => c.pasteEffects(layerId)),
                ),
            ],
          ),
        ],
      ),
    );
  }
}
