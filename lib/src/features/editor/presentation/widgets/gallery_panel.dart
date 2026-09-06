import 'dart:typed_data';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show ScrollCacheExtent;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:image_picker/image_picker.dart';

import '../../../media/application/gallery_service.dart';
import '../../../media/application/media_import_service.dart';
import '../am/am_colors.dart';

/// Embedded media browser; opening albums never covers or resizes the preview.
class GalleryPanel extends ConsumerStatefulWidget {
  const GalleryPanel({super.key, required this.onImport});
  final Future<void> Function(XFile file, bool video, Duration duration)
  onImport;

  @override
  ConsumerState<GalleryPanel> createState() => _GalleryPanelState();
}

class _GalleryPanelState extends ConsumerState<GalleryPanel>
    with WidgetsBindingObserver {
  final _scroll = ScrollController();
  List<GalleryAlbum> _albums = [];
  final _assets = <GalleryAsset>[];
  GalleryAlbum? _album;
  GalleryAccess? _access;
  String? _error;
  bool _loading = true, _paging = false, _more = true, _importing = false;
  int _generation = 0, _page = 0;
  bool _refreshing = false;
  static const _pageSize = 60;
  GalleryService get _service => ref.read(galleryServiceProvider);

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _scroll.addListener(() {
      if (_scroll.position.extentAfter < 240) _nextPage();
    });
    _refresh();
  }

  @override
  void dispose() {
    _generation++;
    WidgetsBinding.instance.removeObserver(this);
    _scroll.dispose();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed && !_refreshing && !_importing) {
      _refresh();
    }
  }

  Future<void> _refresh() async {
    if (_refreshing) return;
    _refreshing = true;
    final generation = ++_generation;
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final access = await _service.requestAccess();
      final albums =
          (access == GalleryAccess.full || access == GalleryAccess.limited)
          ? await _service.albums()
          : <GalleryAlbum>[];
      if (!mounted || generation != _generation) return;
      setState(() {
        _access = access;
        _albums = albums;
        _loading = false;
      });
      if (albums.isNotEmpty) {
        await _chooseAlbum(
          albums.where((a) => a.id == _album?.id).firstOrNull ?? albums.first,
        );
      } else {
        setState(() {
          _assets.clear();
          _album = null;
        });
      }
    } catch (_) {
      if (mounted && generation == _generation) {
        setState(() {
          _loading = false;
          _error = 'Não foi possível carregar a galeria.';
        });
      }
    } finally {
      _refreshing = false;
    }
  }

  Future<void> _chooseAlbum(GalleryAlbum album) async {
    _generation++;
    setState(() {
      _album = album;
      _assets.clear();
      _page = 0;
      _more = true;
      _paging = false;
      _error = null;
    });
    if (_scroll.hasClients) _scroll.jumpTo(0);
    await _nextPage();
  }

  Future<void> _nextPage() async {
    if (_paging || !_more || _album == null || _importing) return;
    final generation = _generation;
    setState(() => _paging = true);
    try {
      final assets = await _service.page(_album!.id, _page, _pageSize);
      if (!mounted || generation != _generation) return;
      setState(() {
        final ids = _assets.map((a) => a.id).toSet();
        _assets.addAll(assets.where((a) => ids.add(a.id)));
        _page++;
        _more = assets.length == _pageSize;
      });
    } catch (_) {
      if (mounted && generation == _generation) {
        setState(
          () => _error = 'Falha ao carregar. Toque em tentar novamente.',
        );
      }
    } finally {
      if (mounted && generation == _generation) setState(() => _paging = false);
    }
  }

  Future<void> _import(
    Future<XFile?> Function() pick,
    bool video,
    Duration duration, {
    bool persisted = false,
  }) async {
    if (_importing) return;
    setState(() {
      _importing = true;
      _error = null;
    });
    try {
      final file = await pick();
      if (file == null) return;
      if (!mounted) return;
      final saved = persisted
          ? file
          : await ref
                .read(mediaImportServiceProvider)
                .persist(file, image: !video);
      if (!mounted) return;
      await widget.onImport(saved, video, duration);
    } catch (_) {
      if (mounted) {
        setState(
          () => _error = 'Não foi possível importar. Se estiver na nuvem, confira a conexão ou escolha pelo seletor do sistema.',
        );
      }
    } finally {
      if (mounted) setState(() => _importing = false);
    }
  }

  Future<void> _pickAsset(GalleryAsset asset) => _import(
    () async {
      final file = await _service.file(asset);
      if (file == null) throw const FormatException('Mídia indisponível');
      return XFile(file.path);
    },
    asset.video,
    asset.duration,
  );

  Future<void> _selectMore() async {
    try {
      await _service.selectMore();
      if (mounted) await _refresh();
    } catch (_) {
      if (mounted) {
        setState(() => _error = 'Não foi possível alterar o acesso às fotos.');
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    ref.watch(galleryServiceProvider);
    final importer = ref.read(mediaImportServiceProvider);
    return Stack(
      children: [
        Column(
          children: [
            SizedBox(
              height: 32,
              child: Row(
                children: [
                  Expanded(
                    child: _albums.isEmpty
                        ? const Text(
                            'Galeria',
                            style: TextStyle(
                              color: AmColors.text,
                              fontSize: 12,
                            ),
                          )
                        : PopupMenuButton<GalleryAlbum>(
                            tooltip: 'Selecionar álbum',
                            onSelected: _chooseAlbum,
                            itemBuilder: (_) => [
                              for (final album in _albums)
                                PopupMenuItem(
                                  value: album,
                                  child: Text(album.name),
                                ),
                            ],
                            child: Row(
                              children: [
                                const Icon(
                                  Icons.grid_view,
                                  size: 16,
                                  color: AmColors.text,
                                ),
                                const SizedBox(width: 5),
                                Expanded(
                                  child: Text(
                                    _album?.name ?? 'Todos',
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                    style: const TextStyle(
                                      color: AmColors.text,
                                      fontSize: 12,
                                    ),
                                  ),
                                ),
                                const Icon(
                                  Icons.arrow_drop_down,
                                  size: 18,
                                  color: AmColors.text,
                                ),
                              ],
                            ),
                          ),
                  ),
                  IconButton(
                    tooltip: 'Fotos do sistema',
                    padding: EdgeInsets.zero,
                    constraints: const BoxConstraints(minWidth: 36),
                    iconSize: 18,
                    onPressed: () => _import(
                      importer.pickImageFromGallery,
                      false,
                      Duration.zero,
                      persisted: true,
                    ),
                    icon: const Icon(
                      CupertinoIcons.photo,
                      color: AmColors.text,
                    ),
                  ),
                  IconButton(
                    tooltip: 'Vídeos do sistema',
                    padding: EdgeInsets.zero,
                    constraints: const BoxConstraints(minWidth: 36),
                    iconSize: 18,
                    onPressed: () => _import(
                      importer.pickVideoFromGallery,
                      true,
                      Duration.zero,
                      persisted: true,
                    ),
                    icon: const Icon(
                      CupertinoIcons.videocam,
                      color: AmColors.text,
                    ),
                  ),
                ],
              ),
            ),
            if (_access == GalleryAccess.limited)
              InkWell(
                onTap: _selectMore,
                child: const Padding(
                  padding: EdgeInsets.symmetric(vertical: 4),
                  child: Text(
                    'Acesso limitado · Selecionar mais fotos',
                    style: TextStyle(color: AmColors.accent, fontSize: 11),
                  ),
                ),
              ),
            if (_error != null)
              InkWell(
                onTap: _refresh,
                child: Padding(
                  padding: const EdgeInsets.all(4),
                  child: Text(
                    '$_error Tentar novamente',
                    maxLines: 3,
                    style: const TextStyle(color: AmColors.pink, fontSize: 11),
                  ),
                ),
              ),
            Expanded(
              child: _loading
                  ? const Center(child: CupertinoActivityIndicator())
                  : _assets.isEmpty && !_paging
                  ? _empty()
                  : GridView.builder(
                      key: const ValueKey('gallery-grid'),
                      controller: _scroll,
                      padding: EdgeInsets.zero,
                      scrollCacheExtent: const ScrollCacheExtent.pixels(100),
                      addAutomaticKeepAlives: false,
                      gridDelegate:
                          const SliverGridDelegateWithFixedCrossAxisCount(
                            crossAxisCount: 3,
                            mainAxisSpacing: 1,
                            crossAxisSpacing: 1,
                          ),
                      itemCount: _assets.length + (_more ? 1 : 0),
                      itemBuilder: (context, index) {
                        if (index == _assets.length) {
                          return Center(
                            child: _paging
                                ? const CupertinoActivityIndicator()
                                : IconButton(
                                    tooltip: 'Carregar mais',
                                    onPressed: _nextPage,
                                    icon: const Icon(Icons.more_horiz),
                                  ),
                          );
                        }
                        final asset = _assets[index];
                        return _GalleryThumbnail(
                          key: ValueKey(asset.id),
                          asset: asset,
                          load: () => _service.thumbnail(asset),
                          onTap: () => _pickAsset(asset),
                        );
                      },
                    ),
            ),
          ],
        ),
        if (_importing)
          Positioned.fill(
            child: ColoredBox(
              color: const Color(0xDD17191D),
              child: const Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    CupertinoActivityIndicator(),
                    SizedBox(height: 10),
                    Text(
                      'Carregando mídia…',
                      style: TextStyle(color: AmColors.text, fontSize: 12),
                    ),
                  ],
                ),
              ),
            ),
          ),
      ],
    );
  }

  Widget _empty() => Center(
    child: SingleChildScrollView(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            switch (_access) {
              GalleryAccess.denied => 'Permita acesso para ver as fotos aqui.',
              GalleryAccess.unavailable =>
                'Escolha fotos ou vídeos pelos botões acima.',
              GalleryAccess.limited => 'Nenhuma mídia nesta seleção.',
              _ => 'Nenhuma mídia neste álbum.',
            },
            textAlign: TextAlign.center,
            style: const TextStyle(color: AmColors.muted, fontSize: 12),
          ),
          if (_access == GalleryAccess.denied)
            TextButton(
              onPressed: _service.settings,
              child: const Text('Abrir ajustes'),
            ),
          TextButton(onPressed: _refresh, child: const Text('Atualizar')),
        ],
      ),
    ),
  );
}

class _GalleryThumbnail extends StatefulWidget {
  const _GalleryThumbnail({
    super.key,
    required this.asset,
    required this.load,
    required this.onTap,
  });
  final GalleryAsset asset;
  final Future<Uint8List?> Function() load;
  final VoidCallback onTap;
  @override
  State<_GalleryThumbnail> createState() => _GalleryThumbnailState();
}

class _GalleryThumbnailState extends State<_GalleryThumbnail> {
  late final Future<Uint8List?> _bytes = widget.load();
  @override
  Widget build(BuildContext context) => Semantics(
    label: widget.asset.video ? 'Importar vídeo' : 'Importar foto',
    button: true,
    child: InkWell(
      onTap: widget.onTap,
      child: Stack(
        fit: StackFit.expand,
        children: [
          ColoredBox(
            color: AmColors.chip,
            child: FutureBuilder<Uint8List?>(
              future: _bytes,
              builder: (context, snapshot) => snapshot.hasData
                  ? Image.memory(
                      snapshot.data!,
                      fit: BoxFit.cover,
                      gaplessPlayback: true,
                      errorBuilder: (_, _, _) => const Icon(
                        Icons.broken_image_outlined,
                        color: AmColors.muted,
                      ),
                    )
                  : Icon(
                      snapshot.connectionState == ConnectionState.waiting
                          ? Icons.photo_outlined
                          : Icons.cloud_download_outlined,
                      color: AmColors.muted,
                      size: 22,
                    ),
            ),
          ),
          if (widget.asset.video) ...[
            const Center(
              child: Icon(Icons.play_arrow, color: Colors.white, size: 26),
            ),
            Positioned(
              right: 3,
              bottom: 2,
              child: Text(
                '${widget.asset.duration.inMinutes}:${(widget.asset.duration.inSeconds % 60).toString().padLeft(2, '0')}',
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 10,
                  shadows: [Shadow(blurRadius: 3, color: Colors.black)],
                ),
              ),
            ),
          ],
        ],
      ),
    ),
  );
}
