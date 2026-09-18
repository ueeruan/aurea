import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/camera3d.dart';
import '../../domain/estudio_ux.dart';
import '../../domain/layer.dart';
import 'estado_do_estudio.dart';
import 'ficha_do_selecionado.dart';
import 'folhas_do_estudio.dart';
import 'scene3d_theme.dart';
import 'vista_da_cena.dart';

Future<void> abrirEstudioDaCena(
  BuildContext context, {
  required String layerId,
  required PlaybackController playback,
}) => Navigator.of(context).push(
  MaterialPageRoute<void>(
    builder: (_) => EstudioDaCena(layerId: layerId, playback: playback),
  ),
);

class EstudioDaCena extends ConsumerStatefulWidget {
  const EstudioDaCena({
    super.key,
    required this.layerId,
    required this.playback,
  });
  final String layerId;
  final PlaybackController playback;
  @override
  ConsumerState<EstudioDaCena> createState() => _EstudioDaCenaState();
}

class _EstudioDaCenaState extends ConsumerState<EstudioDaCena> {
  final _navegacao = NavegacaoDaVista();
  final _vista = GlobalKey<VistaDaCenaState>();
  int _tab = 0;
  bool _expanded = false;
  static const _tabs = ['Objetos', 'Transformar', 'Animar', 'Câmeras', 'Luz'];
  static const _icons = [
    Icons.layers_outlined,
    Icons.open_with,
    Icons.auto_graph,
    Icons.videocam_outlined,
    Icons.wb_sunny_outlined,
  ];
  @override
  void initState() {
    super.initState();
    widget.playback.pause();
  }

  @override
  void dispose() {
    _navegacao.dispose();
    super.dispose();
  }

  void _dizer(String text) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: AppText(text)));
  }

  void _adicionar(BuildContext context, Duration time) => abrirFolhaDeAdicionar(
    context,
    ref,
    layerId: widget.layerId,
    tempo: time,
    navegacao: _navegacao,
    aoAvisar: _dizer,
  );

  void _dicas(BuildContext context) => mostrarFolhaScene3D<void>(
    context,
    title: 'Dicas • Scene 3D',
    body: const Padding(
      padding: EdgeInsets.fromLTRB(20, 0, 20, 24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _Tip(
            'Criar',
            'Use + Adicionar para modelos, formas, nulos, luzes e câmeras. Toque em um objeto na cena ou na lista para selecioná-lo.',
          ),
          _Tip(
            'Mover, girar e tamanho',
            'Escolha uma ferramenta e arraste na cena. Em Transformar você ajusta números, materiais e o vínculo com um nulo. X, Y e Z limitam o movimento a um eixo.',
          ),
          _Tip(
            'AutoKey',
            'Nas propriedades já animadas, mudar um valor grava uma marca no instante atual. Comece pelo losango em Animar, avance o tempo e mude o valor. Desligue AutoKey para experimentar sem gravar novas marcas.',
          ),
          _Tip(
            'Linha do tempo',
            'Arraste a régua para escolher o instante. Os losangos mostram marcas do objeto selecionado; toque neles para voltar exatamente à marca.',
          ),
          _Tip(
            'Câmera no vídeo',
            'Escolher outra câmera no menu acima da prévia cria um corte no instante atual. A vista Livre serve para navegar; Câmera mostra o enquadramento que será exportado.',
          ),
          _Tip(
            'Luz e reflexos',
            'Combine uma luz principal com iluminação ambiente. Ative Reflexos da cena para capturar os objetos ao redor. Materiais metálicos com pouca rugosidade refletem mais.',
          ),
          _Tip(
            'Desempenho',
            'A qualidade da prévia se adapta ao aparelho. O primeiro carregamento prepara malhas e texturas; reutilizar objetos evita carregar várias cópias do mesmo modelo.',
          ),
        ],
      ),
    ),
  );

  @override
  Widget build(BuildContext context) => AnnotatedRegion<SystemUiOverlayStyle>(
    value: SystemUiOverlayStyle.light,
    child: Theme(
      data: Scene3DTheme.theme,
      child: Builder(builder: _buildStudio),
    ),
  );

  Widget _buildStudio(BuildContext context) {
    final project = ref.watch(projetoVisivelProvider);
    final layer = project.layerById(widget.layerId);
    if (layer is! Scene3DLayer) {
      return const Scaffold(body: Center(child: AppText('Cena indisponível')));
    }
    final autoKey = ref.watch(autoKeyframeProvider);
    final tool = ref.watch(ferramentaProvider);
    final marks = marcasDaSelecao(ref, layer);
    final controller = ref.read(editorControllerProvider.notifier);
    return Scaffold(
      backgroundColor: Scene3DTheme.bg,
      body: SafeArea(
        child: ValueListenableBuilder<Duration>(
          valueListenable: widget.playback.time,
          builder: (context, time, _) {
            final local = layer.localTime(time);
            final camera = cameraNoAr(layer, local);
            return Column(
              children: [
                SizedBox(
                  height: 48,
                  child: Row(
                    children: [
                      IconButton(
                        key: const ValueKey('estudio-voltar'),
                        tooltip: 'Voltar ao editor',
                        onPressed: () => Navigator.of(context).maybePop(),
                        icon: const Icon(Icons.arrow_back),
                      ),
                      const Expanded(
                        child: AppText(
                          'Scene 3D',
                          style: TextStyle(
                            fontSize: 18,
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                      ),
                      TextButton.icon(
                        onPressed: () => _dicas(context),
                        icon: const Icon(Icons.help_outline, size: 18),
                        label: const AppText('Dicas'),
                      ),
                      IconButton(
                        tooltip: 'Exportar cena',
                        onPressed: () => abrirFolhaDeExportar(
                          context,
                          ref,
                          layerId: widget.layerId,
                        ),
                        icon: const Icon(
                          Icons.ios_share,
                          color: Scene3DTheme.accent,
                        ),
                      ),
                    ],
                  ),
                ),
                Expanded(
                  child: LayoutBuilder(
                    builder: (context, bounds) {
                      final landscape =
                          bounds.maxWidth > bounds.maxHeight * 1.2;
                      final viewport = Column(
                        children: [
                          Container(
                            color: Scene3DTheme.panel,
                            padding: const EdgeInsets.symmetric(horizontal: 12),
                            child: Row(
                              children: [
                                const Icon(Icons.videocam_outlined, size: 18),
                                const SizedBox(width: 8),
                                Expanded(
                                  child: DropdownButton<String>(
                                    key: const ValueKey('scene-quick-camera'),
                                    isExpanded: true,
                                    underline: const SizedBox(),
                                    value: camera.id,
                                    items: [
                                      for (final cam in layer.allCameras)
                                        DropdownMenuItem(
                                          value: cam.id,
                                          child: AppTextMoldado(
                                            'No vídeo: {0}', [cam.name],
                                            overflow: TextOverflow.ellipsis,
                                          ),
                                        ),
                                    ],
                                    onChanged: (id) {
                                      if (id == null) return;
                                      controller.setCameraShot(
                                        layer.id,
                                        local,
                                        id,
                                      );
                                      ref
                                              .read(
                                                cameraSelecionadaProvider
                                                    .notifier,
                                              )
                                              .state =
                                          id;
                                      ref
                                              .read(
                                                noSelecionadoProvider.notifier,
                                              )
                                              .state =
                                          null;
                                      ref
                                              .read(
                                                luzSelecionadaProvider.notifier,
                                              )
                                              .state =
                                          null;
                                      _navegacao.verVista(SceneView.camera);
                                    },
                                  ),
                                ),
                                PopupMenuButton<SceneView>(
                                  tooltip: 'Vista de trabalho',
                                  icon: const Icon(Icons.view_in_ar_outlined),
                                  onSelected: (view) => _navegacao.verVista(
                                    view,
                                    camera: camera,
                                    tempo: local,
                                  ),
                                  itemBuilder: (_) => [
                                    for (final v in [
                                      SceneView.camera,
                                      SceneView.custom1,
                                      SceneView.front,
                                      SceneView.top,
                                      SceneView.right,
                                    ])
                                      PopupMenuItem(
                                        value: v,
                                        child: AppText(sceneViewLabel(v)),
                                      ),
                                  ],
                                ),
                              ],
                            ),
                          ),
                          Expanded(
                            child: ClipRect(
                              child: VistaDaCena(
                                key: _vista,
                                layerId: layer.id,
                                navegacao: _navegacao,
                                tempo: time,
                                aoTocarVazio: () {},
                              ),
                            ),
                          ),
                          if (landscape) _timeline(layer, local, marks),
                          SizedBox(
                            height: 46,
                            child: ListView(
                              scrollDirection: Axis.horizontal,
                              padding: const EdgeInsets.symmetric(
                                horizontal: 8,
                              ),
                              children: [
                                for (final f in FerramentaDoEstudio.values)
                                  Padding(
                                    padding: const EdgeInsets.only(right: 6),
                                    child: ChoiceChip(
                                      label: AppText(ferramentaLabel(f)),
                                      selected: tool == f,
                                      onSelected: (_) =>
                                          ref
                                                  .read(
                                                    ferramentaProvider.notifier,
                                                  )
                                                  .state =
                                              f,
                                    ),
                                  ),
                                IconButton(
                                  key: const ValueKey('estudio-focar'),
                                  tooltip: 'Enquadrar seleção',
                                  onPressed: () => _vista.currentState?.focar(),
                                  icon: const Icon(Icons.center_focus_strong),
                                ),
                                IconButton(
                                  tooltip: 'Mostrar grade',
                                  onPressed: () => controller.setSceneFloorGrid(
                                    layer.id,
                                    !layer.scene.showFloorGrid,
                                  ),
                                  icon: Icon(
                                    Icons.grid_4x4,
                                    color: layer.scene.showFloorGrid
                                        ? Scene3DTheme.accent
                                        : null,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ],
                      );
                      final panel = Material(
                        key: const ValueKey('scene-edit-panel'),
                        color: Scene3DTheme.panel,
                        child: Column(
                          children: [
                            Row(
                              children: [
                                const SizedBox(width: 8),
                                Expanded(
                                  child: FilterChip(
                                    label: const AppText('AutoKey',
                                      overflow: TextOverflow.ellipsis,
                                    ),
                                    selected: autoKey,
                                    onSelected: (v) =>
                                        ref
                                                .read(
                                                  autoKeyframeProvider.notifier,
                                                )
                                                .state =
                                            v,
                                  ),
                                ),
                                Expanded(
                                  child: TextButton.icon(
                                    key: const ValueKey('estudio-adicionar'),
                                    onPressed: () => _adicionar(context, time),
                                    icon: const Icon(Icons.add, size: 18),
                                    label: const AppText(
                                      'Adicionar',
                                      overflow: TextOverflow.ellipsis,
                                    ),
                                  ),
                                ),
                                IconButton(
                                  tooltip: _expanded
                                      ? 'Reduzir painel'
                                      : 'Ampliar painel',
                                  onPressed: () =>
                                      setState(() => _expanded = !_expanded),
                                  icon: Icon(
                                    _expanded
                                        ? Icons.expand_more
                                        : Icons.expand_less,
                                  ),
                                ),
                              ],
                            ),
                            SizedBox(
                              height: 48,
                              child: ListView(
                                scrollDirection: Axis.horizontal,
                                children: [
                                  for (var i = 0; i < _tabs.length; i++)
                                    TextButton.icon(
                                      key: ValueKey('scene-tab-$i'),
                                      onPressed: () => setState(() => _tab = i),
                                      style: TextButton.styleFrom(
                                        foregroundColor: _tab == i
                                            ? Scene3DTheme.accent
                                            : Scene3DTheme.textMuted,
                                      ),
                                      icon: Icon(_icons[i], size: 18),
                                      label: AppText(_tabs[i]),
                                    ),
                                ],
                              ),
                            ),
                            Expanded(child: _panel(context, layer, time)),
                          ],
                        ),
                      );
                      if (landscape) {
                        return Row(
                          children: [
                            Expanded(child: viewport),
                            SizedBox(
                              width: bounds.maxWidth * .45,
                              child: panel,
                            ),
                          ],
                        );
                      }
                      final panelHeight =
                          (bounds.maxHeight * (_expanded ? .62 : .44)).clamp(
                            175.0,
                            bounds.maxHeight * .72,
                          );
                      return Column(
                        children: [
                          Expanded(child: viewport),
                          _timeline(layer, local, marks),
                          SizedBox(height: panelHeight, child: panel),
                        ],
                      );
                    },
                  ),
                ),
              ],
            );
          },
        ),
      ),
    );
  }

  Widget _panel(BuildContext context, Scene3DLayer layer, Duration time) {
    if (_tab == 1) {
      final node = layer.scene.nodeById(ref.watch(noSelecionadoProvider) ?? '');
      bool canParent(String id) {
        final seen = <String>{};
        String? cursor = id;
        while (cursor != null && seen.add(cursor)) {
          if (cursor == node?.id) return false;
          cursor = layer.scene.nodeById(cursor)?.parentId;
        }
        return true;
      }

      return LayoutBuilder(
        builder: (_, box) => SingleChildScrollView(
          child: SizedBox(
            height: box.maxHeight < 380 ? 380 : box.maxHeight,
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Column(
                children: [
                  if (node != null)
                    DropdownButton<String>(
                      key: const ValueKey('scene-link-null'),
                      isExpanded: true,
                      value:
                          layer.scene.nodeById(node.parentId ?? '') != null &&
                              canParent(node.parentId!)
                          ? node.parentId
                          : '',
                      items: [
                        const DropdownMenuItem(
                          value: '',
                          child: AppText('Vincular a nulo: nenhum'),
                        ),
                        for (final parent in layer.scene.nodes)
                          if ((parent.isNull || parent.id == node.parentId) &&
                              canParent(parent.id))
                            DropdownMenuItem(
                              value: parent.id,
                              child: AppTextMoldado(
                                'Nulo: {0}', [parent.name],
                                overflow: TextOverflow.ellipsis,
                              ),
                            ),
                      ],
                      onChanged: (id) => ref
                          .read(editorControllerProvider.notifier)
                          .setSceneNodeParent(
                            layer.id,
                            node.id,
                            id == '' ? null : id,
                            preserveWorldAt: layer.localTime(time),
                          ),
                    ),
                  Expanded(
                    child: FichaDoSelecionado(
                      layerId: layer.id,
                      tempo: time,
                      abaInicial: AbaDaFicha.transformar,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      );
    }
    if (_tab == 2) {
      return SingleChildScrollView(
        child: FolhaDeAnimacao(layerId: layer.id, playback: widget.playback),
      );
    }
    if (_tab == 3) {
      return SingleChildScrollView(
        child: FolhaDeCamera(layerId: layer.id, tempo: time),
      );
    }
    if (_tab == 4) {
      return SingleChildScrollView(
        child: FolhaDeLuzes(layerId: layer.id, tempo: time),
      );
    }
    final selected = ref.watch(noSelecionadoProvider);
    return ListView(
      padding: const EdgeInsets.symmetric(horizontal: 8),
      children: [
        if (layer.scene.nodes.isEmpty)
          const Padding(
            padding: EdgeInsets.all(16),
            child: AppText('Sua cena começa aqui. Toque em Adicionar para criar ou importar um objeto.',
            ),
          ),
        for (final node in layer.scene.nodes)
          ListTile(
            dense: true,
            selected: selected == node.id,
            leading: Icon(
              node.isNull ? Icons.control_camera : Icons.view_in_ar_outlined,
            ),
            title: AppText(
              node.name,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
            subtitle: node.parentId == null
                ? null
                : AppText(
                    'Ligado a ${layer.scene.nodeById(node.parentId!)?.name ?? "nulo"}',
                  ),
            trailing: const Icon(Icons.tune, size: 20),
            onTap: () {
              ref.read(noSelecionadoProvider.notifier).state = node.id;
              ref.read(cameraSelecionadaProvider.notifier).state = null;
              ref.read(luzSelecionadaProvider.notifier).state = null;
              setState(() => _tab = 1);
            },
          ),
        TextButton.icon(
          key: const ValueKey('estudio-cena'),
          icon: const Icon(Icons.account_tree_outlined),
          label: const AppText('Organizar objetos, luzes e câmeras'),
          onPressed: () =>
              abrirFolhaDaCena(context, ref, layerId: layer.id, tempo: time),
        ),
      ],
    );
  }

  Widget _timeline(Scene3DLayer layer, Duration local, List<Duration> marks) {
    final end = layer.duration.inMicroseconds;
    final fraction = end <= 0
        ? 0.0
        : (local.inMicroseconds / end).clamp(0.0, 1.0);
    void seek(double f) {
      widget.playback.pause();
      widget.playback.seek(
        layer.startTime +
            Duration(microseconds: (f.clamp(0.0, 1.0) * end).round()),
      );
    }

    return SizedBox(
      height: 66,
      child: Row(
        children: [
          ValueListenableBuilder<bool>(
            valueListenable: widget.playback.playing,
            builder: (_, playing, _) => IconButton(
              tooltip: playing ? 'Pausar' : 'Reproduzir',
              onPressed: widget.playback.toggle,
              icon: Icon(playing ? Icons.pause : Icons.play_arrow),
            ),
          ),
          Expanded(
            child: Column(
              children: [
                AppText(
                  '${(local.inMilliseconds / 1000).toStringAsFixed(2)} / ${(end / 1000000).toStringAsFixed(2)} s',
                  style: const TextStyle(fontSize: 11),
                ),
                Expanded(
                  child: LayoutBuilder(
                    builder: (_, box) => GestureDetector(
                      behavior: HitTestBehavior.opaque,
                      onTapDown: (d) => seek(d.localPosition.dx / box.maxWidth),
                      onHorizontalDragUpdate: (d) =>
                          seek(d.localPosition.dx / box.maxWidth),
                      child: Stack(
                        clipBehavior: Clip.hardEdge,
                        children: [
                          const Positioned(
                            left: 0,
                            right: 0,
                            top: 20,
                            child: Divider(height: 1),
                          ),
                          for (final shot in layer.shots)
                            Positioned(
                              left: end <= 0
                                  ? 0
                                  : shot.time.inMicroseconds /
                                        end *
                                        box.maxWidth,
                              top: 5,
                              child: const Icon(
                                Icons.videocam,
                                size: 12,
                                color: Colors.white54,
                              ),
                            ),
                          for (final mark in marks)
                            Positioned(
                              left: end <= 0
                                  ? 0
                                  : (mark.inMicroseconds /
                                        end *
                                        (box.maxWidth - 22)),
                              top: 10,
                              child: GestureDetector(
                                onTap: () => seek(
                                  end <= 0 ? 0 : mark.inMicroseconds / end,
                                ),
                                child: const Icon(
                                  Icons.diamond,
                                  size: 22,
                                  color: Scene3DTheme.accent,
                                ),
                              ),
                            ),
                          Positioned(
                            left: fraction * (box.maxWidth - 2),
                            top: 0,
                            bottom: 0,
                            width: 2,
                            child: const ColoredBox(color: Colors.white),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 12),
        ],
      ),
    );
  }
}

class _Tip extends StatelessWidget {
  const _Tip(this.title, this.body);
  final String title, body;
  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 18),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        AppText(title,
          style: const TextStyle(
            fontWeight: FontWeight.w700,
            color: Scene3DTheme.accent,
          ),
        ),
        const SizedBox(height: 4),
        AppText(body),
      ],
    ),
  );
}
