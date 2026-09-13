import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../domain/camera3d.dart';
import '../../domain/camera_cuts.dart';
import '../../domain/element3d.dart';
import '../../domain/estudio_ux.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../../domain/scene_motion.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'color_picker_sheet.dart';
import 'model_import_button.dart';
import 'scene3d_sheet.dart';

/// AS FOLHAS DO ESTUDIO 3D — o que abre por cima da vista.
///
/// A regra da reestruturacao: simples por padrao, e cada folha simples
/// tem um "Avancado" que leva a ficha completa da cena (nada foi
/// tirado; mudou o caminho ate chegar). Cada folha aqui recebe o id da
/// camada e le o projeto na hora — nunca guarda um objeto velho.

// ------------------------------------------------------------ casca

/// FECHA AS FOLHAS abertas por cima da tela [raiz] — para abrir a
/// proxima a partir da tela, e nao empilhada por cima das outras.
void fecharFolhas(BuildContext ctx, BuildContext raiz) {
  final rota = ModalRoute.of(raiz);
  if (rota == null) {
    Navigator.pop(ctx);
    return;
  }
  Navigator.of(ctx).popUntil((r) => r == rota);
}

/// A folha padrao do Estudio: painel escuro, canto redondo, sem borda.
Future<T?> folhaDoEstudio<T>(
  BuildContext context, {
  required Widget Function(BuildContext, StateSetter) builder,
  String? titulo,
  double alturaFator = 0.55,
}) {
  return showModalBottomSheet<T>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    barrierColor: Colors.black38,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
    ),
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setSheet) => SafeArea(
        top: false,
        child: ConstrainedBox(
          constraints: BoxConstraints(
            maxHeight: MediaQuery.of(ctx).size.height * alturaFator,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SizedBox(height: 8),
              Center(
                child: Container(
                  width: 36,
                  height: 4,
                  decoration: BoxDecoration(
                    color: AmColors.muted.withValues(alpha: .5),
                    borderRadius: BorderRadius.circular(2),
                  ),
                ),
              ),
              if (titulo != null)
                Padding(
                  padding: const EdgeInsets.fromLTRB(18, 12, 18, 4),
                  child: AppText(titulo,
                    style: const TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                ),
              Flexible(child: builder(ctx, setSheet)),
            ],
          ),
        ),
      ),
    ),
  );
}

/// Uma linha de menu: icone, titulo, subtitulo opcional, e ou um
/// interruptor, ou um chevron, ou nada.
class LinhaDoEstudio extends StatelessWidget {
  const LinhaDoEstudio({
    super.key,
    required this.titulo,
    this.icone,
    this.subtitulo,
    this.ligado,
    this.ativo = false,
    this.perigo = false,
    this.chevron = false,
    this.onTap,
    this.trailing,
  });

  final String titulo;
  final IconData? icone;
  final String? subtitulo;

  /// Com valor, a linha vira um interruptor.
  final bool? ligado;
  final bool ativo;
  final bool perigo;
  final bool chevron;
  final VoidCallback? onTap;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final cor = perigo
        ? AmColors.pink
        : ativo
        ? AmColors.accent
        : onTap == null && ligado == null
        ? AmColors.muted
        : AmColors.text;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 11),
        child: Row(
          children: [
            if (icone != null) ...[
              Icon(icone, size: 20, color: ativo ? AmColors.accent : cor),
              const SizedBox(width: 12),
            ],
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  AppText(titulo,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 15,
                      color: cor,
                      fontWeight: ativo ? FontWeight.w600 : FontWeight.w400,
                    ),
                  ),
                  if (subtitulo != null)
                    AppText(
                      subtitulo!,
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 11.5,
                        height: 1.3,
                        color: AmColors.muted,
                      ),
                    ),
                ],
              ),
            ),
            ?trailing,
            if (ligado != null)
              CupertinoSwitch(
                value: ligado!,
                activeTrackColor: AmColors.accent,
                onChanged: onTap == null ? null : (_) => onTap!(),
              )
            else if (ativo)
              const Icon(
                CupertinoIcons.checkmark_alt,
                size: 18,
                color: AmColors.accent,
              )
            else if (chevron)
              const Icon(
                CupertinoIcons.chevron_right,
                size: 15,
                color: AmColors.muted,
              ),
          ],
        ),
      ),
    );
  }
}

class SecaoDoEstudio extends StatelessWidget {
  const SecaoDoEstudio(this.titulo, {super.key});
  final String titulo;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(18, 14, 18, 4),
    child: AppText(
      titulo.toUpperCase(),
      style: const TextStyle(
        fontSize: 11,
        letterSpacing: .6,
        fontWeight: FontWeight.w600,
        color: AmColors.muted,
      ),
    ),
  );
}

/// O chip do Estudio: pilula, sem borda, aceso em verde.
class ChipDoEstudio extends StatelessWidget {
  const ChipDoEstudio({
    super.key,
    required this.label,
    this.icone,
    this.aceso = false,
    this.onTap,
    this.compacto = false,
  });

  final String label;
  final IconData? icone;
  final bool aceso;
  final VoidCallback? onTap;
  final bool compacto;

  @override
  Widget build(BuildContext context) {
    final desligado = onTap == null;
    final cor = desligado
        ? AmColors.muted.withValues(alpha: .55)
        : aceso
        ? AmColors.accent
        : AmColors.text;
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: EdgeInsets.symmetric(
          horizontal: compacto ? 10 : 13,
          vertical: compacto ? 6 : 9,
        ),
        decoration: BoxDecoration(
          color: aceso ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(11),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (icone != null) ...[
              Icon(icone, size: compacto ? 13 : 15, color: cor),
              const SizedBox(width: 5),
            ],
            AppText(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: compacto ? 11.5 : 12.5,
                fontWeight: aceso ? FontWeight.w600 : FontWeight.w500,
                color: cor,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Um numero que se ajusta pela SUPERFICIE DE ARRASTO (a regua do app,
/// correcao 10.1.1: nenhum slider pequeno), com nome e valor.
class ReguaDoEstudio extends StatelessWidget {
  const ReguaDoEstudio({
    super.key,
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    this.formato,
    this.porPixel,
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;
  final String Function(double)? formato;

  /// Sensibilidade do arrasto; por padrao, a faixa inteira em 240 px.
  final double? porPixel;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(18, 4, 18, 0),
    child: Row(
      children: [
        SizedBox(
          width: 92,
          child: AppText(
            label,
            style: const TextStyle(fontSize: 13, color: AmColors.text),
          ),
        ),
        Expanded(
          child: AmTickRuler(
            height: 40,
            value: value.clamp(min, max),
            min: min,
            max: max,
            unitsPerPixel: porPixel ?? (max - min) / 240,
            onChanged: onChanged,
          ),
        ),
        SizedBox(
          width: 52,
          child: AppText(
            formato?.call(value) ?? value.toStringAsFixed(2),
            textAlign: TextAlign.right,
            style: const TextStyle(fontSize: 12, color: AmColors.muted),
          ),
        ),
      ],
    ),
  );
}

// ------------------------------------------------------------ nome

/// Pede um nome. Devolve null se a pessoa desistiu.
Future<String?> pedirNome(
  BuildContext context, {
  required String titulo,
  required String atual,
}) async {
  final ctrl = TextEditingController(text: atual);
  final r = await showCupertinoDialog<String>(
    context: context,
    builder: (ctx) => CupertinoAlertDialog(
      title: AppText(titulo),
      content: Padding(
        padding: const EdgeInsets.only(top: 12),
        child: CupertinoTextField(
          key: const ValueKey('estudio-nome'),
          controller: ctrl,
          autofocus: true,
          onSubmitted: (v) => Navigator.pop(ctx, v),
        ),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.pop(ctx),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.pop(ctx, ctrl.text),
          child: const AppText('OK'),
        ),
      ],
    ),
  );
  ctrl.dispose();
  final nome = r?.trim();
  return nome == null || nome.isEmpty ? null : nome;
}

// ------------------------------------------------------------ adicionar

/// O MENU "+": dois toques para qualquer coisa nova.
Future<void> showAdicionar(
  BuildContext context,
  WidgetRef ref,
  String layerId, {
  required ValueChanged<String> aoCriarNo,
  required ValueChanged<String> aoCriarLuz,
  required VoidCallback aoNovaCamera,
  required VoidCallback aoAmbiente,
}) {
  var nivel = 0; // 0 raiz, 1 objetos, 2 luzes
  return folhaDoEstudio<void>(
    context,
    alturaFator: 0.7,
    builder: (ctx, setSheet) {
      final controller = ref.read(editorControllerProvider.notifier);
      Scene3DLayer? camada() {
        final l = ref.read(editorControllerProvider).layerById(layerId);
        return l is Scene3DLayer ? l : null;
      }

      Widget cabecalho(String titulo) => Row(
        children: [
          CupertinoButton(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            onPressed: () => setSheet(() => nivel = 0),
            child: const Icon(
              CupertinoIcons.chevron_back,
              size: 20,
              color: AmColors.text,
            ),
          ),
          AppText(titulo,
            style: const TextStyle(
              fontSize: 17,
              fontWeight: FontWeight.w700,
              color: AmColors.text,
            ),
          ),
        ],
      );

      if (nivel == 1) {
        return ListView(
          shrinkWrap: true,
          children: [
            cabecalho('Objeto 3D'),
            for (final kind in Element3DKind.values)
              LinhaDoEstudio(
                key: ValueKey('adicionar-${kind.name}'),
                icone: CupertinoIcons.cube,
                titulo: element3DLabel(kind),
                onTap: () {
                  controller.addSceneNode(layerId, kind);
                  final novo = camada()?.scene.nodes.lastOrNull;
                  Navigator.pop(ctx);
                  if (novo != null) aoCriarNo(novo.id);
                },
              ),
            LinhaDoEstudio(
              icone: CupertinoIcons.circle_grid_hex,
              titulo: 'Nulo (pivo de grupo)',
              subtitulo: 'Um ponto que so transforma: pai dos outros.',
              onTap: () {
                final id = controller.addSceneNull(layerId);
                Navigator.pop(ctx);
                if (id.isNotEmpty) aoCriarNo(id);
              },
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(18, 8, 18, 18),
              child: ModelImportButton(
                layerId: layerId,
                onImported: (id) {
                  Navigator.pop(ctx);
                  aoCriarNo(id);
                },
              ),
            ),
          ],
        );
      }
      if (nivel == 2) {
        return ListView(
          shrinkWrap: true,
          children: [
            cabecalho('Luz'),
            for (final kind in Light3DKind.values)
              LinhaDoEstudio(
                key: ValueKey('adicionar-luz-${kind.name}'),
                icone: CupertinoIcons.lightbulb,
                titulo: luzLabel(kind),
                subtitulo: switch (kind) {
                  Light3DKind.directional =>
                    'Como o sol: uma direcao, sombras paralelas.',
                  Light3DKind.point =>
                    'Uma lampada: brilha em volta de um ponto.',
                  Light3DKind.ambient => 'Clareia tudo por igual, sem sombra.',
                  Light3DKind.spot => 'Um cone de luz, como um refletor.',
                },
                onTap: () {
                  controller.addSceneLight(layerId, kind);
                  final nova = camada()?.scene.lights.lastOrNull;
                  Navigator.pop(ctx);
                  if (nova != null) aoCriarLuz(nova.id);
                },
              ),
          ],
        );
      }
      return ListView(
        shrinkWrap: true,
        children: [
          const Padding(
            padding: EdgeInsets.fromLTRB(18, 10, 18, 4),
            child: AppText(
              'Adicionar',
              style: TextStyle(
                fontSize: 17,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
          ),
          LinhaDoEstudio(
            key: const ValueKey('adicionar-objeto'),
            icone: CupertinoIcons.cube,
            titulo: 'Objeto 3D',
            subtitulo:
                'Cubo, esfera, plano, cilindro, cone, toro e mais. '
                'Ou importe um modelo.',
            chevron: true,
            onTap: () => setSheet(() => nivel = 1),
          ),
          LinhaDoEstudio(
            key: const ValueKey('adicionar-camera'),
            icone: CupertinoIcons.videocam,
            titulo: 'Camera',
            subtitulo: 'Nasce olhando para onde a vista esta agora.',
            onTap: () {
              Navigator.pop(ctx);
              aoNovaCamera();
            },
          ),
          LinhaDoEstudio(
            key: const ValueKey('adicionar-luz'),
            icone: CupertinoIcons.lightbulb,
            titulo: 'Luz',
            subtitulo: 'Direcional, de ponto, ambiente ou spot.',
            chevron: true,
            onTap: () => setSheet(() => nivel = 2),
          ),
          LinhaDoEstudio(
            key: const ValueKey('adicionar-ambiente'),
            icone: CupertinoIcons.sun_haze,
            titulo: 'Ambiente',
            subtitulo: 'Fundo, ceu, chao, reflexos e nevoa.',
            onTap: () {
              Navigator.pop(ctx);
              aoAmbiente();
            },
          ),
          const SizedBox(height: 12),
        ],
      );
    },
  );
}

// ------------------------------------------------------------ hierarquia

/// O PAINEL DA CENA: tudo o que existe, em arvore, com busca. Tocar
/// escolhe; o olho esconde; o cadeado trava; os tres pontos abrem as
/// acoes (renomear, duplicar, excluir).
Future<void> showHierarquia(
  BuildContext context,
  WidgetRef ref,
  String layerId, {
  required BuildContext raiz,
  required Duration tempo,
  required String? selecionado,
  required String? cameraAtiva,
  required void Function(String id, TipoDeItem tipo) aoEscolher,
  required VoidCallback aoAlterar,
  bool buscar = false,
}) {
  var termo = '';
  return folhaDoEstudio<void>(
    context,
    alturaFator: 0.75,
    builder: (ctx, setSheet) {
      final project = ref.read(editorControllerProvider);
      final layer = project.layerById(layerId);
      if (layer is! Scene3DLayer) return const SizedBox.shrink();
      final controller = ref.read(editorControllerProvider.notifier);
      final itens = buscarNaCena(
        hierarquiaDaCena(
          layer.scene,
          luzes: layer.scene.lights,
          cameras: layer.allCameras,
          cameraAtiva: cameraAtiva,
          selecionado: selecionado,
        ),
        termo,
      );

      IconData icone(ItemDaCena i) => switch (i.tipo) {
        TipoDeItem.no => i.grupo ? CupertinoIcons.folder : CupertinoIcons.cube,
        TipoDeItem.luz => CupertinoIcons.lightbulb,
        TipoDeItem.camera => CupertinoIcons.videocam,
      };

      return Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(18, 10, 18, 6),
            child: Row(
              children: [
                const Expanded(
                  child: AppText(
                    'Cena',
                    style: TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                ),
                AppText(
                  '${layer.scene.nodes.length} objetos · '
                  '${layer.scene.lights.length} luzes · '
                  '${layer.allCameras.length} cameras',
                  style: const TextStyle(fontSize: 11, color: AmColors.muted),
                ),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(18, 0, 18, 6),
            child: CupertinoSearchTextField(
              key: const ValueKey('estudio-busca'),
              autofocus: buscar,
              placeholder: translate(context, 'Buscar na cena'),
              style: const TextStyle(color: AmColors.text, fontSize: 14),
              backgroundColor: AmColors.chip,
              onChanged: (v) => setSheet(() => termo = v),
            ),
          ),
          Flexible(
            child: itens.isEmpty
                ? const Padding(
                    padding: EdgeInsets.all(24),
                    child: AppText('Nada com esse nome.',
                      textAlign: TextAlign.center,
                      style: TextStyle(color: AmColors.muted),
                    ),
                  )
                : ListView.builder(
                    shrinkWrap: true,
                    itemCount: itens.length,
                    itemBuilder: (_, i) {
                      final item = itens[i];
                      return Padding(
                        padding: EdgeInsets.only(left: 14.0 * item.nivel),
                        child: LinhaDoEstudio(
                          key: ValueKey('cena-item-${item.id}'),
                          icone: icone(item),
                          titulo: item.nome,
                          ativo: item.ativo,
                          subtitulo: item.tipo == TipoDeItem.camera
                              ? (item.ativo ? 'No ar' : null)
                              : null,
                          onTap: () {
                            Navigator.pop(ctx);
                            aoEscolher(item.id, item.tipo);
                          },
                          trailing: Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              if (item.tipo == TipoDeItem.no) ...[
                                _BotaoMiudo(
                                  key: ValueKey('cena-olho-${item.id}'),
                                  icone: item.visivel
                                      ? CupertinoIcons.eye
                                      : CupertinoIcons.eye_slash,
                                  apagado: !item.visivel,
                                  onTap: () {
                                    controller.setSceneNodeVisible(
                                      layerId,
                                      item.id,
                                      !item.visivel,
                                    );
                                    aoAlterar();
                                    setSheet(() {});
                                  },
                                ),
                                _BotaoMiudo(
                                  key: ValueKey('cena-cadeado-${item.id}'),
                                  icone: item.travado
                                      ? CupertinoIcons.lock_fill
                                      : CupertinoIcons.lock_open,
                                  apagado: !item.travado,
                                  onTap: () {
                                    controller.setSceneNodeLocked(
                                      layerId,
                                      item.id,
                                      !item.travado,
                                    );
                                    aoAlterar();
                                    setSheet(() {});
                                  },
                                ),
                              ],
                              _BotaoMiudo(
                                key: ValueKey('cena-acoes-${item.id}'),
                                icone: CupertinoIcons.chevron_right,
                                onTap: () async {
                                  await showAcoesDoItem(
                                    ctx,
                                    ref,
                                    layerId,
                                    raiz: raiz,
                                    item: item,
                                    tempo: tempo,
                                    aoEscolher: (id, tipo) {
                                      if (ctx.mounted) Navigator.pop(ctx);
                                      aoEscolher(id, tipo);
                                    },
                                    aoAlterar: aoAlterar,
                                  );
                                  if (ctx.mounted) setSheet(() {});
                                },
                              ),
                            ],
                          ),
                        ),
                      );
                    },
                  ),
          ),
        ],
      );
    },
  );
}

class _BotaoMiudo extends StatelessWidget {
  const _BotaoMiudo({
    super.key,
    required this.icone,
    required this.onTap,
    this.apagado = false,
  });
  final IconData icone;
  final VoidCallback onTap;
  final bool apagado;

  @override
  Widget build(BuildContext context) => GestureDetector(
    behavior: HitTestBehavior.opaque,
    onTap: onTap,
    child: Padding(
      padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 4),
      child: Icon(
        icone,
        size: 18,
        color: apagado ? AmColors.muted.withValues(alpha: .6) : AmColors.text,
      ),
    ),
  );
}

// ------------------------------------------------------------ acoes

/// AS ACOES DE UM ITEM (objeto, luz ou camera): o mesmo menu de
/// qualquer lugar — da barra de contexto ou do painel da cena.
Future<void> showAcoesDoItem(
  BuildContext context,
  WidgetRef ref,
  String layerId, {
  required BuildContext raiz,
  required ItemDaCena item,
  required Duration tempo,
  required void Function(String id, TipoDeItem tipo) aoEscolher,
  required VoidCallback aoAlterar,
  VoidCallback? aoExcluir,
  VoidCallback? aoUsarCamera,
}) {
  return folhaDoEstudio<void>(
    context,
    alturaFator: 0.7,
    builder: (ctx, setSheet) {
      final project = ref.read(editorControllerProvider);
      final layer = project.layerById(layerId);
      if (layer is! Scene3DLayer) return const SizedBox.shrink();
      final controller = ref.read(editorControllerProvider.notifier);

      final linhas = <Widget>[
        Padding(
          padding: const EdgeInsets.fromLTRB(18, 10, 18, 4),
          child: AppText(item.nome,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(
              fontSize: 17,
              fontWeight: FontWeight.w700,
              color: AmColors.text,
            ),
          ),
        ),
      ];

      switch (item.tipo) {
        case TipoDeItem.no:
          final node = layer.scene.nodeById(item.id);
          if (node == null) return const SizedBox.shrink();
          linhas.addAll([
            LinhaDoEstudio(
              key: const ValueKey('acao-propriedades'),
              icone: CupertinoIcons.slider_horizontal_3,
              titulo: 'Propriedades',
              subtitulo:
                  'Posicao, rotacao e escala em numeros, material completo, '
                  'instancias e o resto.',
              chevron: true,
              onTap: () {
                fecharFolhas(ctx, raiz);
                showScene3DSheet(
                  raiz,
                  ref,
                  layerId,
                  abaInicial: 0,
                  noInicial: item.id,
                );
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-renomear'),
              icone: CupertinoIcons.pencil,
              titulo: 'Renomear',
              onTap: () async {
                final nome = await pedirNome(
                  ctx,
                  titulo: 'Nome do objeto',
                  atual: node.name,
                );
                if (nome == null) return;
                controller.renameSceneNode(layerId, item.id, nome);
                aoAlterar();
                if (ctx.mounted) Navigator.pop(ctx);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-duplicar'),
              icone: CupertinoIcons.plus_square_on_square,
              titulo: 'Duplicar',
              onTap: () {
                final novo = controller.duplicateSceneNode(layerId, item.id);
                Navigator.pop(ctx);
                if (novo.isNotEmpty) aoEscolher(novo, TipoDeItem.no);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-olhar'),
              icone: CupertinoIcons.scope,
              titulo: 'A camera olha para este',
              subtitulo: 'A camera no ar acompanha o objeto onde ele for.',
              onTap: () {
                final cam = _cameraNoAr(layer, tempo);
                controller.setCameraLookAt(layerId, cam.id, item.id);
                aoAlterar();
                Navigator.pop(ctx);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-esconder'),
              icone: node.visible
                  ? CupertinoIcons.eye_slash
                  : CupertinoIcons.eye,
              titulo: node.visible ? 'Esconder' : 'Mostrar',
              onTap: () {
                controller.setSceneNodeVisible(layerId, item.id, !node.visible);
                aoAlterar();
                Navigator.pop(ctx);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-travar'),
              icone: node.locked
                  ? CupertinoIcons.lock_open
                  : CupertinoIcons.lock,
              titulo: node.locked ? 'Destravar' : 'Travar',
              subtitulo: 'Travado, nao se move nem se apaga sem querer.',
              onTap: () {
                controller.setSceneNodeLocked(layerId, item.id, !node.locked);
                aoAlterar();
                Navigator.pop(ctx);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-excluir'),
              icone: CupertinoIcons.trash,
              titulo: 'Excluir',
              perigo: true,
              onTap: node.locked
                  ? null
                  : () {
                      controller.removeSceneNode(layerId, item.id);
                      Navigator.pop(ctx);
                      (aoExcluir ?? aoAlterar)();
                    },
            ),
          ]);
        case TipoDeItem.luz:
          linhas.addAll([
            LinhaDoEstudio(
              key: const ValueKey('acao-ajustar-luz'),
              icone: CupertinoIcons.slider_horizontal_3,
              titulo: 'Ajustar',
              subtitulo: 'Intensidade, cor, tipo e sombras.',
              chevron: true,
              onTap: () {
                fecharFolhas(ctx, raiz);
                showLuzSimples(raiz, ref, layerId, item.id);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-excluir'),
              icone: CupertinoIcons.trash,
              titulo: 'Excluir',
              perigo: true,
              onTap: () {
                controller.removeSceneLight(layerId, item.id);
                Navigator.pop(ctx);
                (aoExcluir ?? aoAlterar)();
              },
            ),
          ]);
        case TipoDeItem.camera:
          final principal = layer.camera.id == item.id;
          linhas.addAll([
            LinhaDoEstudio(
              key: const ValueKey('acao-ativar'),
              icone: CupertinoIcons.play_circle,
              titulo: 'Usar esta camera',
              subtitulo: 'Ela entra no ar a partir do instante atual.',
              onTap: () {
                Navigator.pop(ctx);
                if (aoUsarCamera != null) {
                  aoUsarCamera();
                } else {
                  aoEscolher(item.id, TipoDeItem.camera);
                }
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-lente'),
              icone: CupertinoIcons.circle_lefthalf_fill,
              titulo: 'Lente',
              subtitulo: 'Distancia focal e projecao ortografica.',
              chevron: true,
              onTap: () {
                fecharFolhas(ctx, raiz);
                showLente(raiz, ref, layerId, item.id, tempo: tempo);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-olhar-para'),
              icone: CupertinoIcons.scope,
              titulo: 'Olhar para…',
              subtitulo: 'A camera acompanha um objeto.',
              chevron: true,
              onTap: () {
                fecharFolhas(ctx, raiz);
                showOlharPara(
                  raiz,
                  ref,
                  layerId,
                  item.id,
                  aoAlterar: aoAlterar,
                );
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-renomear'),
              icone: CupertinoIcons.pencil,
              titulo: 'Renomear',
              onTap: () async {
                final cam = layer.allCameras
                    .where((c) => c.id == item.id)
                    .firstOrNull;
                if (cam == null) return;
                final nome = await pedirNome(
                  ctx,
                  titulo: 'Nome da camera',
                  atual: cam.name,
                );
                if (nome == null) return;
                controller.renameSceneCamera(layerId, item.id, nome);
                aoAlterar();
                if (ctx.mounted) Navigator.pop(ctx);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-duplicar'),
              icone: CupertinoIcons.plus_square_on_square,
              titulo: 'Duplicar',
              onTap: () {
                final nova = controller.duplicateSceneCamera(layerId, item.id);
                Navigator.pop(ctx);
                if (nova.isNotEmpty) aoEscolher(nova, TipoDeItem.camera);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-propriedades'),
              icone: CupertinoIcons.slider_horizontal_3,
              titulo: 'Propriedades da camera',
              subtitulo: 'Tudo: tipo, orientacao, foco, auto-orientacao.',
              chevron: true,
              onTap: () {
                fecharFolhas(ctx, raiz);
                showScene3DSheet(raiz, ref, layerId, abaInicial: 3);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('acao-excluir'),
              icone: CupertinoIcons.trash,
              titulo: principal
                  ? 'A camera principal nao se exclui'
                  : 'Excluir',
              perigo: !principal,
              onTap: principal
                  ? null
                  : () {
                      controller.removeScene3DCamera(layerId, item.id);
                      Navigator.pop(ctx);
                      (aoExcluir ?? aoAlterar)();
                    },
            ),
          ]);
      }
      linhas.add(const SizedBox(height: 10));
      return ListView(shrinkWrap: true, children: linhas);
    },
  );
}

Camera3D _cameraNoAr(Scene3DLayer layer, Duration tempo) {
  final shots = sortedShots(layer.shots);
  final shot = shotAt(shots, tempo) ?? shots.firstOrNull;
  return layer.allCameras.where((c) => c.id == shot?.cameraId).firstOrNull ??
      layer.camera;
}

// ------------------------------------------------------------ material

/// MATERIAL SIMPLES: cor, metal, rugosidade. O resto esta em Avancado.
Future<void> showMaterialSimples(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  String nodeId,
) {
  return folhaDoEstudio<void>(
    context,
    titulo: 'Material',
    alturaFator: 0.5,
    builder: (ctx, setSheet) {
      final layer = ref.read(editorControllerProvider).layerById(layerId);
      if (layer is! Scene3DLayer) return const SizedBox.shrink();
      final node = layer.scene.nodeById(nodeId);
      if (node == null) return const SizedBox.shrink();
      final controller = ref.read(editorControllerProvider.notifier);
      final m = node.material;

      void editar(Material3D Function(Material3D) fn) {
        controller.updateSceneNode(
          layerId,
          nodeId,
          (n) => n.copyWith(material: fn(n.material)),
        );
        setSheet(() {});
      }

      return ListView(
        shrinkWrap: true,
        children: [
          LinhaDoEstudio(
            key: const ValueKey('material-cor'),
            titulo: 'Cor base',
            trailing: Container(
              width: 34,
              height: 22,
              decoration: BoxDecoration(
                color: m.baseColor,
                borderRadius: BorderRadius.circular(7),
              ),
            ),
            onTap: () => showColorPicker(
              ctx,
              initial: m.baseColor,
              withAlpha: false,
              onChanged: (c) => editar((mat) => mat.copyWith(baseColor: c)),
            ),
          ),
          ReguaDoEstudio(
            key: const ValueKey('material-metalico'),
            label: 'Metalico',
            value: m.metallic,
            min: 0,
            max: 1,
            onChanged: (v) => editar((mat) => mat.copyWith(metallic: v)),
          ),
          ReguaDoEstudio(
            key: const ValueKey('material-rugosidade'),
            label: 'Rugosidade',
            value: m.roughness,
            min: 0,
            max: 1,
            onChanged: (v) => editar((mat) => mat.copyWith(roughness: v)),
          ),
          ReguaDoEstudio(
            label: 'Brilho proprio',
            value: m.emissive,
            min: 0,
            max: 4,
            onChanged: (v) => editar((mat) => mat.copyWith(emissive: v)),
          ),
          const SizedBox(height: 6),
          LinhaDoEstudio(
            key: const ValueKey('material-avancado'),
            icone: CupertinoIcons.slider_horizontal_3,
            titulo: 'Avancado',
            subtitulo:
                'Textura, faces, opacidade, normal, oclusao, dupla face.',
            chevron: true,
            onTap: () {
              Navigator.pop(ctx);
              showScene3DSheet(
                context,
                ref,
                layerId,
                abaInicial: 0,
                noInicial: nodeId,
              );
            },
          ),
          const SizedBox(height: 10),
        ],
      );
    },
  );
}

// ------------------------------------------------------------ luz

/// LUZ SIMPLES: intensidade, cor, tipo, sombras.
Future<void> showLuzSimples(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  String lightId,
) {
  return folhaDoEstudio<void>(
    context,
    titulo: 'Luz',
    alturaFator: 0.55,
    builder: (ctx, setSheet) {
      final layer = ref.read(editorControllerProvider).layerById(layerId);
      if (layer is! Scene3DLayer) return const SizedBox.shrink();
      final luz = layer.scene.lights.where((l) => l.id == lightId).firstOrNull;
      if (luz == null) return const SizedBox.shrink();
      final controller = ref.read(editorControllerProvider.notifier);

      void editar(Light3D Function(Light3D) fn) {
        controller.updateSceneLight(layerId, lightId, fn);
        setSheet(() {});
      }

      return ListView(
        shrinkWrap: true,
        children: [
          ReguaDoEstudio(
            key: const ValueKey('luz-intensidade'),
            label: 'Intensidade',
            value: luz.intensity.base,
            min: 0,
            max: 5,
            onChanged: (v) =>
                editar((l) => l.copyWith(intensity: l.intensity.withBase(v))),
          ),
          LinhaDoEstudio(
            key: const ValueKey('luz-cor'),
            titulo: 'Cor',
            trailing: Container(
              width: 34,
              height: 22,
              decoration: BoxDecoration(
                color: luz.color,
                borderRadius: BorderRadius.circular(7),
              ),
            ),
            onTap: () => showColorPicker(
              ctx,
              initial: luz.color,
              withAlpha: false,
              onChanged: (c) => editar((l) => l.copyWith(color: c)),
            ),
          ),
          const SecaoDoEstudio('Tipo'),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 18),
            child: Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (final k in Light3DKind.values)
                  ChipDoEstudio(
                    key: ValueKey('luz-tipo-${k.name}'),
                    label: luzLabel(k).replaceFirst('Luz ', ''),
                    aceso: luz.kind == k,
                    onTap: () => editar((l) => l.copyWith(kind: k)),
                  ),
              ],
            ),
          ),
          const SizedBox(height: 6),
          LinhaDoEstudio(
            key: const ValueKey('luz-sombras'),
            titulo: 'Sombras',
            subtitulo: 'Custa GPU: uma luz com sombra costuma bastar.',
            ligado: luz.castsShadow,
            onTap: () => editar((l) => l.copyWith(castsShadow: !l.castsShadow)),
          ),
          LinhaDoEstudio(
            key: const ValueKey('luz-avancado'),
            icone: CupertinoIcons.slider_horizontal_3,
            titulo: 'Avancado',
            subtitulo: 'Direcao, posicao, alcance, cone, suavidade.',
            chevron: true,
            onTap: () {
              Navigator.pop(ctx);
              showScene3DSheet(context, ref, layerId, abaInicial: 1);
            },
          ),
          const SizedBox(height: 10),
        ],
      );
    },
  );
}

/// AS LUZES DA CENA: escolher uma para ajustar, ou criar.
Future<void> showLuzes(
  BuildContext context,
  WidgetRef ref,
  String layerId, {
  required ValueChanged<String> aoEscolher,
  required VoidCallback aoNova,
}) {
  return folhaDoEstudio<void>(
    context,
    titulo: 'Luzes',
    alturaFator: 0.55,
    builder: (ctx, setSheet) {
      final layer = ref.read(editorControllerProvider).layerById(layerId);
      if (layer is! Scene3DLayer) return const SizedBox.shrink();
      return ListView(
        shrinkWrap: true,
        children: [
          if (layer.scene.lights.isEmpty)
            const Padding(
              padding: EdgeInsets.fromLTRB(18, 6, 18, 6),
              child: AppText('A cena usa a luz padrao do ambiente. Crie uma luz para '
                'controlar direcao, cor e sombra.',
                style: TextStyle(
                  fontSize: 12.5,
                  height: 1.35,
                  color: AmColors.muted,
                ),
              ),
            ),
          for (final l in layer.scene.lights)
            LinhaDoEstudio(
              key: ValueKey('luz-${l.id}'),
              icone: CupertinoIcons.lightbulb,
              titulo: luzLabel(l.kind),
              subtitulo: l.castsShadow ? 'Com sombra' : null,
              chevron: true,
              onTap: () {
                Navigator.pop(ctx);
                aoEscolher(l.id);
              },
            ),
          LinhaDoEstudio(
            key: const ValueKey('luz-nova'),
            icone: CupertinoIcons.add,
            titulo: 'Nova luz',
            ativo: false,
            onTap: () {
              Navigator.pop(ctx);
              aoNova();
            },
          ),
          const SizedBox(height: 10),
        ],
      );
    },
  );
}

// ------------------------------------------------------------ lente

/// A LENTE: distancia focal (com as predefinicoes) e ortografica.
Future<void> showLente(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  String cameraId, {
  required Duration tempo,
  bool autoKey = true,
}) {
  return folhaDoEstudio<void>(
    context,
    titulo: 'Lente',
    alturaFator: 0.45,
    builder: (ctx, setSheet) {
      final layer = ref.read(editorControllerProvider).layerById(layerId);
      if (layer is! Scene3DLayer) return const SizedBox.shrink();
      final cam = layer.allCameras.where((c) => c.id == cameraId).firstOrNull;
      if (cam == null) return const SizedBox.shrink();
      final controller = ref.read(editorControllerProvider.notifier);
      final focal = cam.focalLength.valueAt(tempo);

      void editar(Camera3D Function(Camera3D) fn) {
        controller.updateSceneCameraById(
          layerId,
          editCameraMotion(cam, tempo, fn),
        );
        setSheet(() {});
      }

      return ListView(
        shrinkWrap: true,
        children: [
          ReguaDoEstudio(
            key: const ValueKey('lente-focal'),
            label: 'Distancia focal',
            value: focal,
            min: 8,
            max: 300,
            formato: (v) => '${v.round()} mm',
            onChanged: (v) => editar(
              (c) => c.copyWith(
                focalLength: editMotionValue(
                  c.focalLength,
                  tempo,
                  v,
                ),
              ),
            ),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(18, 8, 18, 4),
            child: Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (final mm in lensPresets)
                  ChipDoEstudio(
                    label: '${mm.round()}',
                    compacto: true,
                    aceso: (focal - mm).abs() < .5,
                    onTap: () => editar(
                      (c) => c.copyWith(
                        focalLength: editMotionValue(
                          c.focalLength,
                          tempo,
                          mm,
                        ),
                      ),
                    ),
                  ),
              ],
            ),
          ),
          LinhaDoEstudio(
            key: const ValueKey('lente-ortografica'),
            titulo: 'Ortografica',
            subtitulo: 'Sem perspectiva: linhas paralelas ficam paralelas.',
            ligado: cam.orthographic,
            onTap: () => controller.updateSceneCameraById(
              layerId,
              cam.copyWith(orthographic: !cam.orthographic),
            ),
          ),
          const SizedBox(height: 10),
        ],
      );
    },
  );
}

// ------------------------------------------------------------ olhar para

/// OLHAR PARA: a camera passa a acompanhar um objeto.
Future<void> showOlharPara(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  String cameraId, {
  required VoidCallback aoAlterar,
}) {
  return folhaDoEstudio<void>(
    context,
    titulo: 'Olhar para',
    alturaFator: 0.6,
    builder: (ctx, setSheet) {
      final layer = ref.read(editorControllerProvider).layerById(layerId);
      if (layer is! Scene3DLayer) return const SizedBox.shrink();
      final cam = layer.allCameras.where((c) => c.id == cameraId).firstOrNull;
      if (cam == null) return const SizedBox.shrink();
      final controller = ref.read(editorControllerProvider.notifier);
      return ListView(
        shrinkWrap: true,
        children: [
          LinhaDoEstudio(
            key: const ValueKey('olhar-nenhum'),
            icone: CupertinoIcons.xmark_circle,
            titulo: 'Nenhum',
            subtitulo: 'A camera olha para o ponto de interesse dela.',
            ativo: cam.lookAtNodeId == null,
            onTap: () {
              controller.setCameraLookAt(layerId, cameraId, null);
              aoAlterar();
              Navigator.pop(ctx);
            },
          ),
          for (final n in layer.scene.nodes)
            LinhaDoEstudio(
              key: ValueKey('olhar-${n.id}'),
              icone: n.isNull ? CupertinoIcons.folder : CupertinoIcons.cube,
              titulo: n.name,
              ativo: cam.lookAtNodeId == n.id,
              onTap: () {
                controller.setCameraLookAt(layerId, cameraId, n.id);
                aoAlterar();
                Navigator.pop(ctx);
              },
            ),
          const SizedBox(height: 10),
        ],
      );
    },
  );
}

// ------------------------------------------------------------ cameras

/// O MENU DA CAMERA (o botao do meio da barra de cima): as cameras
/// com a que esta no ar marcada, a nova, as vistas e o enquadrar.
Future<void> showMenuDaCamera(
  BuildContext context,
  WidgetRef ref,
  String layerId, {
  required BuildContext raiz,
  required Duration tempo,
  required String cameraAtiva,
  required SceneView vista,
  required bool temSelecao,
  required ValueChanged<String> aoUsarCamera,
  required VoidCallback aoNovaCamera,
  required ValueChanged<SceneView> aoVerVista,
  required VoidCallback aoEnquadrarTudo,
  required VoidCallback aoEnquadrarSelecionado,
  required VoidCallback aoAlterar,
}) {
  return folhaDoEstudio<void>(
    context,
    alturaFator: 0.8,
    builder: (ctx, setSheet) {
      final layer = ref.read(editorControllerProvider).layerById(layerId);
      if (layer is! Scene3DLayer) return const SizedBox.shrink();
      final controller = ref.read(editorControllerProvider.notifier);
      return ListView(
        shrinkWrap: true,
        children: [
          const SecaoDoEstudio('Cameras'),
          for (final c in layer.allCameras)
            LinhaDoEstudio(
              key: ValueKey('camera-${c.id}'),
              icone: CupertinoIcons.videocam,
              titulo: c.name,
              subtitulo: c.id == cameraAtiva && vista == SceneView.camera
                  ? 'No ar'
                  : null,
              ativo: c.id == cameraAtiva && vista == SceneView.camera,
              trailing: _BotaoMiudo(
                key: ValueKey('camera-acoes-${c.id}'),
                icone: CupertinoIcons.chevron_right,
                onTap: () async {
                  await showAcoesDoItem(
                    ctx,
                    ref,
                    layerId,
                    raiz: raiz,
                    item: ItemDaCena(
                      id: c.id,
                      nome: c.name,
                      tipo: TipoDeItem.camera,
                      ativo: c.id == cameraAtiva,
                    ),
                    tempo: tempo,
                    aoEscolher: (id, _) {
                      if (ctx.mounted) Navigator.pop(ctx);
                      aoUsarCamera(id);
                    },
                    aoUsarCamera: () {
                      if (ctx.mounted) Navigator.pop(ctx);
                      aoUsarCamera(c.id);
                    },
                    aoAlterar: aoAlterar,
                  );
                  if (ctx.mounted) setSheet(() {});
                },
              ),
              onTap: () {
                Navigator.pop(ctx);
                aoUsarCamera(c.id);
              },
            ),
          LinhaDoEstudio(
            key: const ValueKey('camera-nova'),
            icone: CupertinoIcons.add,
            titulo: 'Nova camera',
            subtitulo: 'Nasce com o enquadramento de agora e entra no ar.',
            onTap: () {
              Navigator.pop(ctx);
              aoNovaCamera();
            },
          ),
          const SecaoDoEstudio('Vistas'),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 18),
            child: Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                ChipDoEstudio(
                  key: const ValueKey('vista-camera'),
                  label: 'Pela camera',
                  icone: CupertinoIcons.videocam,
                  aceso: vista == SceneView.camera,
                  onTap: () {
                    Navigator.pop(ctx);
                    aoVerVista(SceneView.camera);
                  },
                ),
                for (final v in vistasPredefinidas)
                  ChipDoEstudio(
                    key: ValueKey('vista-${v.name}'),
                    label: sceneViewLabel(v),
                    aceso: vista == v,
                    onTap: () {
                      Navigator.pop(ctx);
                      aoVerVista(v);
                    },
                  ),
                ChipDoEstudio(
                  key: const ValueKey('vista-livre'),
                  label: 'Livre',
                  icone: CupertinoIcons.move,
                  aceso:
                      vista == SceneView.custom1 || vista == SceneView.custom2,
                  onTap: () {
                    Navigator.pop(ctx);
                    aoVerVista(SceneView.custom1);
                  },
                ),
              ],
            ),
          ),
          const Padding(
            padding: EdgeInsets.fromLTRB(18, 8, 18, 0),
            child: AppText('As vistas fixas sao ortograficas e nao mexem na camera. '
              'Para a camera assumir uma vista, use "Alinhar camera a '
              'vista" no menu de tres pontos.',
              style: TextStyle(
                fontSize: 11.5,
                height: 1.35,
                color: AmColors.muted,
              ),
            ),
          ),
          const SecaoDoEstudio('Enquadrar'),
          LinhaDoEstudio(
            key: const ValueKey('enquadrar-cena'),
            icone: CupertinoIcons.fullscreen,
            titulo: 'Enquadrar a cena',
            onTap: () {
              Navigator.pop(ctx);
              aoEnquadrarTudo();
            },
          ),
          LinhaDoEstudio(
            key: const ValueKey('enquadrar-objeto'),
            icone: CupertinoIcons.viewfinder,
            titulo: 'Enquadrar o objeto selecionado',
            onTap: temSelecao
                ? () {
                    Navigator.pop(ctx);
                    aoEnquadrarSelecionado();
                  }
                : null,
          ),
          if (layer.scene.savedViews.isNotEmpty) ...[
            const SecaoDoEstudio('Vistas salvas'),
            for (final v in layer.scene.savedViews)
              LinhaDoEstudio(
                icone: CupertinoIcons.bookmark,
                titulo: v.name,
                onTap: () {
                  controller.applySavedView(layerId, v);
                  Navigator.pop(ctx);
                  aoVerVista(SceneView.camera);
                },
              ),
          ],
          const SizedBox(height: 12),
        ],
      );
    },
  );
}
