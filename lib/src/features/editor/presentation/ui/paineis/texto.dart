import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/font_service.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';
import 'fonte.dart' show rotuloDaFontePadrao;

/// Rotulos do alinhamento, usados por Texto e Estilo.
const alinhamentosDoTexto = <(TextAlign, String, IconData)>[
  (TextAlign.left, 'Esquerda', CupertinoIcons.text_alignleft),
  (TextAlign.center, 'Centro', CupertinoIcons.text_aligncenter),
  (TextAlign.right, 'Direita', CupertinoIcons.text_alignright),
];

/// A fileira de alinhamento: tres icones (rotulo so no leitor de tela) —
/// tres pilulas com texto nao cabem na linha de 51 de um celular de 360.
Widget fileiraDeAlinhamento(TextAlign atual, ValueChanged<TextAlign> aoMudar) =>
    Row(
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

/// TEXTO — o que esta escrito e o basico dele: tamanho, cor, alinhamento
/// e a fonte (atalho para o painel Fonte).
///
/// O CAMPO E A PRIMEIRA COISA DO PAINEL, fixo no topo e grande: era a
/// queixa numero um ("onde eu escrevo?"). A lista de propriedades rola
/// embaixo dele; o campo nunca sai da vista.
///
/// Na camada de Texto 3D a palavra e editada na folha do Texto 3D (a
/// fonte precisa ser validada pelo extrusor antes de trocar).
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
  void initState() {
    super.initState();
    // Ao SAIR do campo ele volta a seguir a camada (um desfazer feito com
    // o teclado aberto nao fica escondido atras do que foi digitado).
    _foco.addListener(() {
      if (!_foco.hasFocus && mounted) setState(() {});
    });
  }

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
    if (camada is Scene3DLayer) return _texto3D(context, camada, chave);
    if (camada is! TextLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de texto.',
      );
    }
    final c = ref.read(editorControllerProvider.notifier);
    final id = widget.layerId;
    // O CAMPO SEGUE A CAMADA enquanto nao esta sendo digitado: desfazer,
    // colar estilo ou trocar de camada atualizam o texto; digitando, quem
    // manda e o dedo (senao o cursor pularia a cada letra).
    if (!_foco.hasFocus && _campo.text != camada.text) {
      _campo.value = TextEditingValue(
        text: camada.text,
        selection: TextSelection.collapsed(offset: camada.text.length),
      );
    }
    final familia = camada.fontFamily;
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: () {
        _foco.unfocus();
        escopo.fecharPainel();
      },
      corpo: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(
              AureaDims.margemDoPainel,
              AureaDims.e2,
              AureaDims.margemDoPainel,
              AureaDims.e4,
            ),
            child: CupertinoTextField(
              key: const ValueKey('texto-campo'),
              controller: _campo,
              focusNode: _foco,
              minLines: 1,
              maxLines: 3,
              keyboardType: TextInputType.multiline,
              textCapitalization: TextCapitalization.sentences,
              placeholder: translate(context, 'Digite o texto'),
              placeholderStyle: AureaEstilos.corpo.copyWith(
                fontSize: 16,
                color: AureaCores.textoSecundario,
              ),
              style: AureaEstilos.corpo.copyWith(
                fontSize: 16,
                color: AureaCores.texto,
              ),
              cursorColor: AureaCores.destaque,
              padding: const EdgeInsets.symmetric(
                horizontal: AureaDims.e10,
                vertical: AureaDims.e10,
              ),
              decoration: BoxDecoration(
                color: AureaCores.campo,
                borderRadius: BorderRadius.circular(AureaDims.raioXl),
              ),
              suffixMode: OverlayVisibilityMode.editing,
              suffix: Tocavel(
                key: const ValueKey('texto-fechar-teclado'),
                onTap: _foco.unfocus,
                child: SizedBox(
                  width: AureaDims.toqueConfortavel,
                  height: AureaDims.toqueConfortavel,
                  child: Icon(
                    CupertinoIcons.keyboard_chevron_compact_down,
                    size: AureaDims.iconeMd,
                    color: AureaCores.textoSecundario,
                  ),
                ),
              ),
              onTapOutside: (_) => _foco.unfocus(),
              onChanged: (v) => c.editTextLayer(id, text: v),
            ),
          ),
          Expanded(
            child: ListView(
              padding: const EdgeInsets.fromLTRB(
                AureaDims.margemDoPainel,
                0,
                AureaDims.margemDoPainel,
                AureaDims.topoDoPainel,
              ),
              children: [
                linhaNumerica(
                  ref,
                  rotulo: 'Tamanho',
                  valor: camada.fontSize,
                  min: 4,
                  max: 400,
                  aoMudar: (v) => c.editTextLayer(id, fontSize: v),
                  aoResetar: () => c.editTextLayer(id, fontSize: 120),
                ),
                AureaPropertyRow.cor(
                  rotulo: 'Cor',
                  cor: camada.color,
                  aoTocar: () => _escolherCor(camada),
                ),
                AureaPropertyRow.personalizada(
                  rotulo: 'Alinhamento',
                  filho: fileiraDeAlinhamento(
                    camada.alinhamento,
                    (a) => c.editTextLayer(id, alinhamento: a),
                  ),
                ),
                AureaPropertyRow.personalizada(
                  rotulo: 'Fonte',
                  filho: Tocavel(
                    key: const ValueKey('texto-fonte'),
                    onTap: () {
                      _foco.unfocus();
                      escopo.abrirPainel(PainelId.fonte);
                    },
                    child: SizedBox(
                      height: AureaDims.linhaDePropriedade,
                      child: Row(
                        children: [
                          Expanded(
                            // O NOME DA FONTE e conteudo: Text, na propria
                            // fonte. Sem fonte escolhida, o rotulo do app.
                            child: familia == null
                                ? AppText(
                                    rotuloDaFontePadrao,
                                    maxLines: 1,
                                    style: AureaEstilos.valor.copyWith(
                                      fontSize: 14,
                                    ),
                                  )
                                : Text(
                                    familia,
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                    style: AureaEstilos.valor.copyWith(
                                      fontFamily: resolveFontFamily(familia),
                                      fontSize: 14,
                                    ),
                                  ),
                          ),
                          Icon(
                            CupertinoIcons.chevron_right,
                            size: 13,
                            color: AureaCores.textoSecundario,
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Future<void> _escolherCor(TextLayer camada) async {
    final escopo = EscopoDoEditor.of(context);
    final c = ref.read(editorControllerProvider.notifier);
    escopo.playback.pause();
    _foco.unfocus();
    final nova = await showColorPicker(
      context,
      initial: camada.color,
      onChanged: (cor) => c.editTextLayer(widget.layerId, color: cor),
    );
    if (nova != null) c.editTextLayer(widget.layerId, color: nova);
  }

  Widget _texto3D(BuildContext context, Scene3DLayer camada, String chave) {
    final escopo = EscopoDoEditor.of(context);
    final no = camada.scene.nodes.where((n) => n.texto3d != null).firstOrNull;
    return PainelDePortas(
      titulo: _titulo,
      chave: chave,
      aviso: no == null ? 'Esta camada não tem texto 3D.' : null,
      portas: [
        if (no != null)
          LinhaDePorta(
            rotulo: 'Editar a palavra do Texto 3D',
            icone: CupertinoIcons.textformat,
            // O painel Texto 3D e o editor da palavra (fonte, volume,
            // metal, letras): trocar de painel, nao abrir folha.
            aoTocar: () => escopo.abrirPainel(PainelId.texto3d),
          ),
      ],
    );
  }
}
