import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer_meta.dart';
import '../../am/layer_menu.dart' show showParentSheet;
import '../../am/oficio_sheets.dart' show showLoopSheet, showOrganizeSheet;
import '../shell/contrato.dart';
import 'comum.dart';
import 'mascara.dart' show gravarModoDeMistura, modoDeMisturaDe, modosDeMistura;
import 'pecas_centrais.dart';

/// A FICHA DA CAMADA — nome, cor da etiqueta, mistura, opacidade (com
/// losango), visivel, cadeado, solo, timida; e as portas do vinculo
/// (parent), dos atalhos de tempo e do loop.
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
  void initState() {
    super.initState();
    // SAIR DO CAMPO GRAVA o nome: quem digita e toca noutro lugar espera
    // que o nome fique, sem precisar do "concluir" do teclado.
    _foco.addListener(() {
      if (!_foco.hasFocus) _gravarNome(_nome.text);
    });
  }

  void _gravarNome(String v) {
    if (!mounted) return;
    final nome = v.trim();
    final atual = ref
        .read(editorControllerProvider)
        .layerById(widget.layerId)
        ?.name;
    if (nome.isEmpty || nome == atual) return;
    umPasso(
      ref,
      () => ref
          .read(editorControllerProvider.notifier)
          .renameLayer(widget.layerId, nome),
    );
  }

  @override
  void dispose() {
    _nome.dispose();
    _foco.dispose();
    super.dispose();
  }

  Future<void> _escolherEtiqueta(LayerLabel? atual) async {
    final escolha = await mostrarAureaMenu<int>(
      context,
      titulo: 'Cor da etiqueta',
      itens: [
        AureaMenuItem(
          valor: -1,
          rotulo: 'Sem etiqueta',
          marcado: atual == null,
          chave: 'etiqueta-nenhuma',
        ),
        for (final (i, l) in LayerLabel.palette.indexed)
          AureaMenuItem(
            valor: i,
            rotulo: l.name,
            marcado: atual?.color == l.color,
            chave: 'etiqueta-$i',
          ),
      ],
    );
    if (escolha == null) return;
    umPasso(
      ref,
      () => ref
          .read(editorControllerProvider.notifier)
          .setLayerLabel(
            widget.layerId,
            escolha < 0 ? null : LayerLabel.palette[escolha],
          ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final id = widget.layerId;
    final camada = camadaVisivel(ref, id);
    final gravada = camadaGravada(ref, id);
    if (camada == null || gravada == null) {
      return const PainelSemCamada(titulo: _titulo);
    }
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
          filho: AureaToggle(
            valor: valor,
            aoMudar: (_) => umPasso(ref, alternar),
          ),
        );

    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.propriedades.name}',
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
            padding: respiroDoPainel,
            children: [
              AureaPropertyRow.personalizada(
                rotulo: 'Nome',
                chave: 'nome',
                filho: CupertinoTextField(
                  key: const ValueKey('camada-nome'),
                  controller: _nome,
                  focusNode: _foco,
                  placeholder: translate(context, 'Nome da camada'),
                  style: AureaEstilos.corpo,
                  padding: const EdgeInsets.symmetric(
                    horizontal: AureaDims.e10,
                    vertical: AureaDims.e6,
                  ),
                  decoration: BoxDecoration(
                    color: AureaCores.campo,
                    borderRadius: BorderRadius.circular(AureaDims.raioMd),
                  ),
                  onSubmitted: _gravarNome,
                ),
              ),
              AureaPropertyRow.cor(
                rotulo: 'Etiqueta',
                chave: 'etiqueta',
                cor: meta.label?.color ?? AureaCores.campoAlto,
                aoTocar: () => _escolherEtiqueta(meta.label),
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Mistura',
                chave: 'mistura',
                filho: AureaDropdown(
                  valor: modoDeMisturaDe(camada),
                  opcoes: modosDeMistura,
                  rotuloDe: (m) => m.rotulo,
                  titulo: 'Modo de mistura',
                  aoMudar: (m) =>
                      umPasso(ref, () => gravarModoDeMistura(c, id, m)),
                ),
              ),
              AureaPropertyRow(
                rotulo: 'Opacidade',
                valor: camada.opacity.valueAt(camada.localTime(t)) * 100,
                aoMudar: aCadaPasso(
                  (v) =>
                      c.editOpacity(id, escopo.playback.time.value, v / 100),
                ),
                min: 0,
                max: 100,
                unidade: '%',
                casas: 0,
                keyframe: kf.estado,
                aoAnterior: kf.anterior,
                aoProximo: kf.proximo,
                aoResetar: () =>
                    umPasso(ref, () => c.resetProp(id, LayerProp.opacity)),
                aoComecarGesto: c.beginGesture,
                aoTerminarGesto: c.endGesture,
              ),
              chave('Visível', !meta.hidden, () => c.toggleHidden(id)),
              chave('Bloqueada', meta.locked, () => c.toggleLocked(id)),
              chave('Solo', meta.solo, () => c.toggleSolo(id)),
              chave('Tímida', meta.shy, () => c.toggleShy(id)),
              LinhaDePorta(
                rotulo: temPai
                    ? 'Vínculo (tem pai)'
                    : 'Vincular a outra camada',
                icone: temPai
                    ? CupertinoIcons.link_circle_fill
                    : CupertinoIcons.link,
                aoTocar: () {
                  escopo.playback.pause();
                  showParentSheet(
                    context,
                    ref,
                    camada,
                    escopo.playback.time.value,
                  );
                },
              ),
              LinhaDePorta(
                rotulo: 'Início no cabeçote',
                icone: CupertinoIcons.arrow_right_to_line,
                aoTocar: () {
                  escopo.playback.pause();
                  umPasso(
                    ref,
                    () => c.moveLayer(id, escopo.playback.time.value),
                  );
                },
              ),
              LinhaDePorta(
                rotulo: 'Fim no cabeçote',
                icone: CupertinoIcons.arrow_left_to_line,
                aoTocar: () {
                  escopo.playback.pause();
                  final inicio = escopo.playback.time.value - camada.duration;
                  umPasso(
                    ref,
                    () => c.moveLayer(
                      id,
                      inicio < Duration.zero ? Duration.zero : inicio,
                    ),
                  );
                },
              ),
              LinhaDePorta(
                rotulo: 'Pasta',
                icone: CupertinoIcons.folder,
                aoTocar: () => showOrganizeSheet(context, ref, id),
              ),
              LinhaDePorta(
                rotulo: 'Repetir movimento (loop)',
                icone: CupertinoIcons.repeat,
                aoTocar: () => showLoopSheet(context, ref, id),
              ),
            ],
          );
        },
      ),
    );
  }
}
