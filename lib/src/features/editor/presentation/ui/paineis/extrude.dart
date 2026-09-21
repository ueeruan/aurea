import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import 'comum_3d.dart';
import 'pecas_centrais.dart';

/// A FOLHA DO EXTRUDE 3D: a espessura da camada.
///
/// A espessura so aparece com a camada girada em X ou Y — de frente, ela
/// fica escondida atras da propria camada. Por isso a folha diz, em cima,
/// se a camada ja esta inclinada: sem o aviso, "mexi e nada mudou" parece
/// defeito.
///
/// Folha NAO modal: o palco continua tocavel e mostra a espessura enquanto
/// o dedo arrasta.
Future<void> showExtrudeSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  if (!context.mounted) return;
  await mostrarAureaFolha<void>(
    context,
    modal: false,
    // Cabecalho, aviso, a linha da espessura e a fileira dos prontos: cabe
    // sem rolar num celular de 360, e o palco fica quase todo a vista.
    altura: AureaDims.painel + AureaDims.linhaDePropriedade - AureaDims.e10,
    construtor: (_) => _FolhaDoExtrude(layerId: layerId),
  );
}

/// Os prontos de um toque (px). Zero desliga.
const List<double> _prontos = [0, 20, 40, 80, 160];

class _FolhaDoExtrude extends ConsumerWidget {
  const _FolhaDoExtrude({required this.layerId});

  final String layerId;

  static const _titulo = 'Extrude 3D';
  static const _chave = 'folha-extrude';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    void fechar() => Navigator.of(context).maybePop();
    // So a camada e o numero dela: a folha nao se refaz a cada mutacao de
    // outra camada (e o desfazer ainda a redesenha).
    final camada = ref.watch(
      editorControllerProvider.select((p) => p.layerById(layerId)),
    );
    final atual = ref.watch(
      editorControllerProvider.select((p) => p.metaOf(layerId).extrude),
    );
    if (camada == null) {
      return AureaPanel(
        titulo: _titulo,
        chave: _chave,
        aoFechar: fechar,
        filhos: const [
          AureaAvisoDoPainel(texto: 'Esta camada não existe mais.'),
        ],
      );
    }
    final c = ref.read(editorControllerProvider.notifier);
    final inclinada =
        camada.rotationX.isAnimated ||
        camada.rotationY.isAnimated ||
        camada.rotationX.base != 0 ||
        camada.rotationY.base != 0;
    return AureaPanel(
      titulo: _titulo,
      chave: _chave,
      aoFechar: fechar,
      filhos: [
        AureaAvisoDoPainel(
          texto: inclinada
              ? 'Espessura da camada. Gire em X ou Y para ver a lateral.'
              : 'A espessura só aparece com a camada girada em X ou Y '
                    '(Transformar > Rotação, com a Camada 3D ligada).',
        ),
        // Um arrasto = um passo de desfazer; toque longo no rotulo zera.
        linhaSemLosango(
          c,
          rotulo: 'Espessura',
          chave: 'extrude-espessura',
          valor: atual,
          min: 0,
          max: 400,
          aoMudar: (v) => c.setLayerExtrude(layerId, v),
          aoResetar: () => c.setLayerExtrude(layerId, 0),
        ),
        FileiraDeAcoes(
          acoes: [
            for (final v in _prontos)
              AureaChip(
                key: ValueKey('extrude-pronto-${v.round()}'),
                rotulo: v == 0 ? 'Desligado' : '${v.round()}',
                ativo: (atual - v).abs() < 0.5,
                aoTocar: () => c.setLayerExtrude(layerId, v),
              ),
          ],
        ),
      ],
    );
  }
}
