import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../am/texto3d_sheet.dart' show showTexto3DSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// Rotulos do alinhamento, usados por Texto e Estilo.
const alinhamentosDoTexto = <(TextAlign, String, IconData)>[
  (TextAlign.left, 'Esquerda', CupertinoIcons.text_alignleft),
  (TextAlign.center, 'Centro', CupertinoIcons.text_aligncenter),
  (TextAlign.right, 'Direita', CupertinoIcons.text_alignright),
];

/// A fileira de alinhamento: tres icones (rotulo so no leitor de tela) —
/// tres pilulas com texto nao cabem na linha de 51 de um celular de 360.
Widget fileiraDeAlinhamento(
  TextAlign atual,
  ValueChanged<TextAlign> aoMudar,
) => Row(
  mainAxisSize: MainAxisSize.min,
  children: [
    for (final (valor, rotulo, icone) in alinhamentosDoTexto)
      Padding(
        padding: const EdgeInsets.only(right: AureaDims.e6),
        child: Semantics(
          label: rotulo,
          button: true,
          child: Tocavel(
            key: ValueKey('alinhar-${valor.name}'),
            onTap: () => aoMudar(valor),
            child: Container(
              width: 40,
              height: AureaDims.alturaDaCaixaDeValor,
              decoration: BoxDecoration(
                color: atual == valor
                    ? AureaCores.destaqueApagado
                    : AureaCores.campo,
                borderRadius: BorderRadius.circular(AureaDims.raioMd),
              ),
              child: Icon(
                icone,
                size: AureaDims.iconeSm + 2,
                color: atual == valor
                    ? AureaCores.destaque
                    : AureaCores.texto,
              ),
            ),
          ),
        ),
      ),
  ],
);

/// TEXTO — o que esta escrito, o tamanho e o alinhamento. Na camada de
/// Texto 3D, a letra e editada na folha do Texto 3D (a fonte precisa ser
/// validada pelo extrusor antes de trocar).
class PainelTexto extends ConsumerStatefulWidget {
  const PainelTexto({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelTexto> createState() => _PainelTextoState();
}

class _PainelTextoState extends ConsumerState<PainelTexto> {
  final _campo = TextEditingController();
  final _foco = FocusNode();

  static const _titulo = 'Texto';

  @override
  void dispose() {
    _campo.dispose();
    _foco.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, widget.layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.texto.name}';
    if (camada is Scene3DLayer) {
      final no = camada.scene.nodes
          .where((n) => n.texto3d != null)
          .firstOrNull;
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: no == null ? 'Esta camada não tem texto 3D.' : null,
        portas: [
          if (no != null)
            LinhaDePorta(
              rotulo: 'Editar a palavra do Texto 3D',
              icone: CupertinoIcons.textformat,
              aoTocar: () => showTexto3DSheet(
                context,
                ref,
                sceneId: camada.id,
                nodeId: no.id,
                playhead: escopo.playback.time.value,
                playback: escopo.playback,
              ),
            ),
        ],
      );
    }
    if (camada is! TextLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de texto.',
        portas: const [],
      );
    }
    final c = ref.read(editorControllerProvider.notifier);
    // O CAMPO SEGUE A CAMADA enquanto nao esta sendo digitado: desfazer,
    // colar estilo ou trocar de camada atualizam o texto; digitando, quem
    // manda e o dedo (senao o cursor pularia a cada letra).
    if (!_foco.hasFocus && _campo.text != camada.text) {
      _campo.text = camada.text;
    }
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      filhos: [
        Padding(
          padding: const EdgeInsets.only(bottom: AureaDims.e6),
          child: CupertinoTextField(
            key: const ValueKey('texto-campo'),
            controller: _campo,
            focusNode: _foco,
            minLines: 1,
            maxLines: 3,
            placeholder: translate(context, 'Digite o texto'),
            style: AureaEstilos.corpo,
            padding: const EdgeInsets.symmetric(
              horizontal: AureaDims.e10,
              vertical: AureaDims.e8,
            ),
            decoration: BoxDecoration(
              color: AureaCores.campo,
              borderRadius: BorderRadius.circular(AureaDims.raioMd),
            ),
            onChanged: (v) => c.editTextLayer(widget.layerId, text: v),
          ),
        ),
        AureaPropertyRow(
          rotulo: 'Tamanho',
          valor: camada.fontSize,
          aoMudar: (v) => c.editTextLayer(widget.layerId, fontSize: v),
          min: 4,
          max: 400,
          casas: 0,
          aoComecarGesto: c.beginGesture,
          aoTerminarGesto: c.endGesture,
        ),
        AureaPropertyRow.personalizada(
          rotulo: 'Alinhamento',
          filho: fileiraDeAlinhamento(
            camada.alinhamento,
            (a) => c.editTextLayer(widget.layerId, alinhamento: a),
          ),
        ),
      ],
    );
  }
}
