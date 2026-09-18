import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/model_import_service.dart';
import '../../domain/element3d.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import 'estado_do_estudio.dart';
import 'folhas_do_estudio.dart';
import 'scene3d_theme.dart';

/// Abre a folha modal de Adicionar Objeto (Tela 5 do mockup).
Future<void> abrirFolhaDeAdicionarNovo(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required Duration tempo,
  required void Function(String) aoAvisar,
}) => mostrarFolhaScene3D<void>(
  context,
  title: 'Adicionar Objeto',
  body: FolhaDeAdicionar(layerId: layerId, tempo: tempo, aoAvisar: aoAvisar),
);

class FolhaDeAdicionar extends ConsumerStatefulWidget {
  const FolhaDeAdicionar({
    super.key,
    required this.layerId,
    required this.tempo,
    required this.aoAvisar,
  });

  final String layerId;
  final Duration tempo;
  final void Function(String) aoAvisar;

  @override
  ConsumerState<FolhaDeAdicionar> createState() => _FolhaDeAdicionarState();
}

enum _CategoriaAdicionar { modelos, luzes, cameras }

class _FolhaDeAdicionarState extends ConsumerState<FolhaDeAdicionar> {
  _CategoriaAdicionar _categoria = _CategoriaAdicionar.modelos;
  String _busca = '';
  int _itemSelecionado = 0;
  bool _importando = false;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  void _adicionarItemEscolhido() async {
    final camada = ref.read(editorControllerProvider).layerById(widget.layerId);
    if (camada is! Scene3DLayer) return;

    if (_categoria == _CategoriaAdicionar.modelos) {
      final itens = _modelosFiltrados;
      if (itens.isEmpty) return;
      final escolhido = itens[_itemSelecionado.clamp(0, itens.length - 1)];

      if (escolhido.isImportar) {
        setState(() => _importando = true);
        try {
          final caminhos = await ref.read(escolherModeloProvider)();
          if (!mounted || caminhos.isEmpty) return;
          final modelo = await readModel3DFiles(caminhos);
          if (!mounted) return;
          final id = _c.addModel3D(widget.layerId, modelo);
          if (id.isEmpty) return;
          ref.read(noSelecionadoProvider.notifier).state = id;
          ref.read(luzSelecionadaProvider.notifier).state = null;
          ref.read(cameraSelecionadaProvider.notifier).state = null;
          widget.aoAvisar('${modelo.name} adicionado.');
          Navigator.of(context).pop();
        } catch (error) {
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: AppTextMoldado('Não foi possível importar o modelo: {0}', [error]),
              ),
            );
          }
        } finally {
          if (mounted) setState(() => _importando = false);
        }
        return;
      }

      // Adiciona o elemento 3D correspondente
      _c.addSceneNode(widget.layerId, escolhido.kind);
      final camadaAposNo = ref
          .read(editorControllerProvider)
          .layerById(widget.layerId);
      if (camadaAposNo is Scene3DLayer && camadaAposNo.scene.nodes.isNotEmpty) {
        final novoNo = camadaAposNo.scene.nodes.last;
        _c.renameSceneNode(widget.layerId, novoNo.id, escolhido.nome);
        ref.read(noSelecionadoProvider.notifier).state = novoNo.id;
        ref.read(luzSelecionadaProvider.notifier).state = null;
        ref.read(cameraSelecionadaProvider.notifier).state = null;
      }
      widget.aoAvisar('${escolhido.nome} adicionado.');
      Navigator.of(context).pop();
    } else if (_categoria == _CategoriaAdicionar.luzes) {
      final luzes = [
        (Light3DKind.directional, 'Luz Direcional'),
        (Light3DKind.point, 'Luz Pontual'),
        (Light3DKind.ambient, 'Luz Ambiente'),
        (Light3DKind.spot, 'Spotlight'),
      ];
      final l = luzes[_itemSelecionado.clamp(0, luzes.length - 1)];
      _c.addSceneLight(widget.layerId, l.$1);
      final camadaAposLuz = ref
          .read(editorControllerProvider)
          .layerById(widget.layerId);
      if (camadaAposLuz is Scene3DLayer &&
          camadaAposLuz.scene.lights.isNotEmpty) {
        final novaLuz = camadaAposLuz.scene.lights.last;
        ref.read(luzSelecionadaProvider.notifier).state = novaLuz.id;
        ref.read(noSelecionadoProvider.notifier).state = null;
        ref.read(cameraSelecionadaProvider.notifier).state = null;
      }
      widget.aoAvisar('${l.$2} adicionada.');
      Navigator.of(context).pop();
    } else {
      final id = _c.addScene3DCamera(widget.layerId);
      if (id.isNotEmpty) {
        _c.setCameraShot(widget.layerId, widget.tempo, id);
        ref.read(cameraSelecionadaProvider.notifier).state = id;
        ref.read(noSelecionadoProvider.notifier).state = null;
        ref.read(luzSelecionadaProvider.notifier).state = null;
      }
      widget.aoAvisar('Câmera criada e adicionada.');
      Navigator.of(context).pop();
    }
  }

  List<_ItemModeloCard> get _todosModelos => [
    for (final kind in Element3DKind.values)
      _ItemModeloCard(element3DLabel(kind), Icons.view_in_ar_rounded, kind),
    const _ItemModeloCard(
      'Importar Arquivo...',
      Icons.folder_open_rounded,
      Element3DKind.cube,
      isImportar: true,
    ),
  ];

  List<_ItemModeloCard> get _modelosFiltrados {
    if (_busca.isEmpty) return _todosModelos;
    return _todosModelos
        .where((m) => m.nome.toLowerCase().contains(_busca.toLowerCase()))
        .toList();
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // Abas de Categoria: Modelos, Luzes, Câmeras
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Row(
            children: [
              _buildCategoryTab('Modelos', _CategoriaAdicionar.modelos),
              const SizedBox(width: 8),
              _buildCategoryTab('Luzes', _CategoriaAdicionar.luzes),
              const SizedBox(width: 8),
              _buildCategoryTab('Câmeras', _CategoriaAdicionar.cameras),
            ],
          ),
        ),
        const SizedBox(height: 14),

        // Barra de Busca
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Container(
            height: 44,
            decoration: Scene3DTheme.cardDecoration(
              borderRadius: 12,
              color: Scene3DTheme.panelElevated,
            ),
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Row(
              children: [
                const Icon(
                  Icons.search_rounded,
                  color: Scene3DTheme.textMuted,
                  size: 20,
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: TextField(
                    key: const ValueKey('estudio-busca'),
                    onChanged: (v) => setState(() => _busca = v),
                    style: const TextStyle(
                      fontSize: 14,
                      color: Scene3DTheme.text,
                    ),
                    decoration: InputDecoration(
                      hintText: translate(context, 'Buscar modelo...'),
                      hintStyle: TextStyle(
                        fontSize: 14,
                        color: Scene3DTheme.textSubtle,
                      ),
                      border: InputBorder.none,
                      isDense: true,
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 14),

        // Grid 2 Colunas com Cards Visuais
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: _categoria == _CategoriaAdicionar.modelos
              ? _buildModelosGrid()
              : (_categoria == _CategoriaAdicionar.luzes
                    ? _buildLuzesGrid()
                    : _buildCamerasGrid()),
        ),
        const SizedBox(height: 18),

        // Botão de Ação Inferior: + Adicionar
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Scene3DActionButton(
            key: const ValueKey('estudio-adicionar'),
            label: _importando ? 'Importando...' : 'Adicionar',
            icon: Icons.add_rounded,
            onPressed: _importando ? null : _adicionarItemEscolhido,
          ),
        ),
        const SizedBox(height: 10),
      ],
    );
  }

  Widget _buildCategoryTab(String label, _CategoriaAdicionar cat) {
    final active = _categoria == cat;
    return Expanded(
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: () => setState(() {
          _categoria = cat;
          _itemSelecionado = 0;
        }),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 150),
          height: 38,
          decoration: Scene3DTheme.pillDecoration(active: active),
          child: Center(
            child: AppText(
              label,
              style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w700,
                color: active ? Scene3DTheme.onAccent : Scene3DTheme.textMuted,
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildModelosGrid() {
    final itens = _modelosFiltrados;
    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 2,
        mainAxisSpacing: 10,
        crossAxisSpacing: 10,
        childAspectRatio: 1.35,
      ),
      itemCount: itens.length,
      itemBuilder: (context, idx) {
        final item = itens[idx];
        final isSelected = _itemSelecionado == idx;
        return _buildCardItem(
          title: item.nome,
          icon: item.icon,
          isSelected: isSelected,
          onTap: () => setState(() => _itemSelecionado = idx),
        );
      },
    );
  }

  Widget _buildLuzesGrid() {
    final luzes = [
      ('Luz Direcional', Icons.wb_sunny_rounded),
      ('Luz Pontual', Icons.lightbulb_rounded),
      ('Luz Ambiente', Icons.public_rounded),
      ('Spotlight', Icons.highlight_rounded),
    ];
    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 2,
        mainAxisSpacing: 10,
        crossAxisSpacing: 10,
        childAspectRatio: 1.35,
      ),
      itemCount: luzes.length,
      itemBuilder: (context, idx) {
        final l = luzes[idx];
        final isSelected = _itemSelecionado == idx;
        return _buildCardItem(
          title: l.$1,
          icon: l.$2,
          isSelected: isSelected,
          onTap: () => setState(() => _itemSelecionado = idx),
        );
      },
    );
  }

  Widget _buildCamerasGrid() {
    final cameras = [
      ('Câmera Livre', Icons.videocam_rounded),
      ('Câmera Órbita', Icons.camera_enhance_rounded),
      ('Câmera Fixa', Icons.photo_camera_rounded),
    ];
    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 2,
        mainAxisSpacing: 10,
        crossAxisSpacing: 10,
        childAspectRatio: 1.35,
      ),
      itemCount: cameras.length,
      itemBuilder: (context, idx) {
        final c = cameras[idx];
        final isSelected = _itemSelecionado == idx;
        return _buildCardItem(
          title: c.$1,
          icon: c.$2,
          isSelected: isSelected,
          onTap: () => setState(() => _itemSelecionado = idx),
        );
      },
    );
  }

  Widget _buildCardItem({
    required String title,
    required IconData icon,
    required bool isSelected,
    required VoidCallback onTap,
  }) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 150),
        decoration: Scene3DTheme.cardDecoration(
          borderRadius: 14,
          isSelected: isSelected,
          color: isSelected
              ? const Color(0xFF162520)
              : Scene3DTheme.panelElevated,
        ),
        padding: const EdgeInsets.all(12),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Container(
              width: 48,
              height: 48,
              decoration: BoxDecoration(
                color: isSelected
                    ? Scene3DTheme.accentDim
                    : const Color(0xFF242A35),
                borderRadius: BorderRadius.circular(12),
              ),
              child: Icon(
                icon,
                size: 26,
                color: isSelected
                    ? Scene3DTheme.accent
                    : Scene3DTheme.textMuted,
              ),
            ),
            const SizedBox(height: 8),
            AppText(title,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 13,
                fontWeight: isSelected ? FontWeight.w700 : FontWeight.w500,
                color: isSelected ? Scene3DTheme.accent : Scene3DTheme.text,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _ItemModeloCard {
  const _ItemModeloCard(
    this.nome,
    this.icon,
    this.kind, {
    this.isImportar = false,
  });
  final String nome;
  final IconData icon;
  final Element3DKind kind;
  final bool isImportar;
}
