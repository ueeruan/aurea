import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../am/layer_menu.dart' show showParentSheet;
import '../../am/oficio_sheets.dart' show showLoopSheet, showOrganizeSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// A FICHA DA CAMADA — nome, olho, cadeado, solo, timida, o vinculo
/// (parent) e os atalhos de tempo que moravam na doca antiga ("inicio no
/// cabecote", "fim no cabecote").
class PainelPropriedades extends ConsumerStatefulWidget {
  const PainelPropriedades({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelPropriedades> createState() =>
      _PainelPropriedadesState();
}

class _PainelPropriedadesState extends ConsumerState<PainelPropriedades> {
  final _nome = TextEditingController();
  final _foco = FocusNode();

  static const _titulo = 'Camada';

  @override
  void dispose() {
    _nome.dispose();
    _foco.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final id = widget.layerId;
    final camada = camadaVisivel(ref, id);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    // SO A FICHA desta camada: o painel nao acorda por mutacao alheia.
    final meta = ref.watch(editorControllerProvider.select((p) => p.metaOf(id)));
    final temPai = ref.watch(
      editorControllerProvider.select(
        (p) => p.linkFor(id, LayerProp.parent) != null,
      ),
    );
    final c = ref.read(editorControllerProvider.notifier);
    if (!_foco.hasFocus && _nome.text != camada.name) _nome.text = camada.name;

    AureaPropertyRow chave(String rotulo, bool valor, VoidCallback alternar) =>
        AureaPropertyRow.personalizada(
          rotulo: rotulo,
          filho: AureaToggle(valor: valor, aoMudar: (_) => alternar()),
        );

    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.propriedades.name}',
      aoFechar: escopo.fecharPainel,
      filhos: [
        Padding(
          padding: const EdgeInsets.only(bottom: AureaDims.e6),
          child: CupertinoTextField(
            key: const ValueKey('camada-nome'),
            controller: _nome,
            focusNode: _foco,
            placeholder: translate(context, 'Nome da camada'),
            style: AureaEstilos.corpo,
            padding: const EdgeInsets.symmetric(
              horizontal: AureaDims.e10,
              vertical: AureaDims.e8,
            ),
            decoration: BoxDecoration(
              color: AureaCores.campo,
              borderRadius: BorderRadius.circular(AureaDims.raioMd),
            ),
            onSubmitted: (v) {
              if (v.trim().isNotEmpty) c.renameLayer(id, v.trim());
            },
          ),
        ),
        chave('Visível', !meta.hidden, () => c.toggleHidden(id)),
        chave('Bloqueada', meta.locked, () => c.toggleLocked(id)),
        chave('Solo', meta.solo, () => c.toggleSolo(id)),
        chave('Tímida', meta.shy, () => c.toggleShy(id)),
        LinhaDePorta(
          rotulo: temPai ? 'Vínculo (tem pai)' : 'Vincular a outra camada',
          icone: temPai ? CupertinoIcons.link_circle_fill : CupertinoIcons.link,
          aoTocar: () {
            escopo.playback.pause();
            showParentSheet(context, ref, camada, escopo.playback.time.value);
          },
        ),
        LinhaDePorta(
          rotulo: 'Início no cabeçote',
          icone: CupertinoIcons.arrow_right_to_line,
          aoTocar: () {
            escopo.playback.pause();
            c.moveLayer(id, escopo.playback.time.value);
          },
        ),
        LinhaDePorta(
          rotulo: 'Fim no cabeçote',
          icone: CupertinoIcons.arrow_left_to_line,
          aoTocar: () {
            escopo.playback.pause();
            final inicio = escopo.playback.time.value - camada.duration;
            c.moveLayer(id, inicio < Duration.zero ? Duration.zero : inicio);
          },
        ),
        LinhaDePorta(
          rotulo: 'Rótulo e pasta',
          icone: CupertinoIcons.tag,
          aoTocar: () => showOrganizeSheet(context, ref, id),
        ),
        LinhaDePorta(
          rotulo: 'Repetir movimento (loop)',
          icone: CupertinoIcons.repeat,
          aoTocar: () => showLoopSheet(context, ref, id),
        ),
      ],
    );
  }
}
