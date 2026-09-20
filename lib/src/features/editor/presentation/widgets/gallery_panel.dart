import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show ScrollCacheExtent;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:image_picker/image_picker.dart';

import '../../../../core/storage/prefs.dart';
import '../../../media/application/gallery_service.dart';
import '../../../media/application/media_import_service.dart';
import '../../../media/application/midias_recentes.dart';
import '../am/am_colors.dart';

/// Embedded media browser; opening albums never covers or resizes the preview.
/// Uma midia pronta para entrar em lote: o arquivo ja copiado, se e
/// video e quanto dura (imagem usa a duracao escolhida na barra).
typedef MidiaDoLote = ({XFile file, bool video, Duration duration});

class GalleryPanel extends ConsumerStatefulWidget {
  const GalleryPanel({super.key, required this.onImport, this.onImportLote});
  final Future<void> Function(XFile file, bool video, Duration duration)
  onImport;

  /// VARIAS DE UMA VEZ: a lista na ordem em que foram marcadas.
  /// [emSequencia] = uma comeca onde a outra acaba; senao todas juntas
  /// no cabecote. Nulo = o painel nem oferece o modo.
  final Future<void> Function(
    List<MidiaDoLote> midias, {
    required bool emSequencia,
  })?
  onImportLote;

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

  /// A ORDEM IMPORTA: em sequencia, a primeira marcada entra primeiro.
  final _marcados = <GalleryAsset>[];
  double _duracaoDaImagem = 3.0;
  static const _kDuracaoDaImagem = 'galeria.duracao_da_imagem';

  /// O ALBUM LEMBRADO. O estado do painel morre quando a folha fecha (o
  /// provedor da galeria e autoDispose), e toda abertura caia em "Todos":
  /// quem monta um projeto com dez fotos da mesma pasta voltava a procurar
  /// a pasta dez vezes. O id do album e estavel (bucket do MediaStore no
  /// Android, localIdentifier no iOS), entao basta guardar o id. Um so: a
  /// galeria propria mistura foto e video no mesmo album.
  static const _kUltimoAlbum = 'galeria.ultimo_album';
  String? _albumLembrado;

  /// OS RECENTES DA AUREA como album virtual. Nao e um album de verdade (o
  /// [_album] continua sendo o real, para o relogio devolver a pessoa a
  /// ele) e por isso nao e o que fica lembrado entre aberturas.
  static const _albumRecentes = GalleryAlbum('aurea:recentes', 'Recentes');
  bool _recentes = false;

  /// Um recado que NAO pede "tentar novamente" (o [_error] pede).
  String? _aviso;
  int _generation = 0, _page = 0;
  bool _refreshing = false;
  static const _pageSize = 60;
  GalleryService get _service => ref.read(galleryServiceProvider);

  @override
  void initState() {
    super.initState();
    try {
      final prefs = ref.read(sharedPreferencesProvider);
      _duracaoDaImagem = prefs.getDouble(_kDuracaoDaImagem) ?? 3.0;
      _albumLembrado = prefs.getString(_kUltimoAlbum);
    } catch (_) {}
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
        // O que estava aberto; na primeira carga, o lembrado. Album que
        // sumiu do aparelho nao casa com nenhum e cai no primeiro.
        final alvo = _album?.id ?? _albumLembrado;
        await _chooseAlbum(
          albums.where((a) => a.id == alvo).firstOrNull ?? albums.first,
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

  /// A ESCOLHA DA PESSOA no menu: e ela que fica lembrada, e nao o album
  /// em que o painel caiu sozinho.
  Future<void> _escolhidoNoMenu(GalleryAlbum album) async {
    if (album.id == _albumRecentes.id) {
      setState(() {
        _recentes = true;
        _aviso = null;
      });
      return;
    }
    _recentes = false;
    _albumLembrado = album.id;
    try {
      ref.read(sharedPreferencesProvider).setString(_kUltimoAlbum, album.id);
    } catch (_) {}
    await _chooseAlbum(album);
  }

  Future<void> _chooseAlbum(GalleryAlbum album) async {
    _generation++;
    setState(() {
      _album = album;
      _aviso = null;
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
    GalleryAsset? asset,
  }) async {
    if (_importing) return;
    setState(() {
      _importing = true;
      _error = null;
      _aviso = null;
    });
    // LIDOS ANTES DE IMPORTAR: `onImport` costuma fechar a folha, e o `ref`
    // de um painel desmontado nao pode mais ser usado.
    final recentes = ref.read(midiasRecentesProvider.notifier);
    final servico = _service;
    try {
      // JA COPIEI ESTA? A mesma foto em dez projetos ocupa o espaco de uma.
      final jaTenho = recentes.daOrigem(asset?.origem);
      final XFile saved;
      if (jaTenho != null) {
        saved = arquivoJaImportado(jaTenho.caminho, jaTenho.nome);
      } else {
        final file = await pick();
        if (file == null) return;
        if (!mounted) return;
        saved = persisted
            ? file
            : await ref
                  .read(mediaImportServiceProvider)
                  .persist(file, image: !video);
        if (!mounted) return;
      }
      await widget.onImport(saved, video, duration);
      await registrarMidiaImportada(
        recentes,
        caminho: saved.path,
        nome: saved.name,
        video: video,
        duracao: duration,
        origem: asset?.origem,
        miniaturaPronta: asset == null ? null : () => servico.thumbnail(asset),
      );
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

  bool get _selecionando => _marcados.isNotEmpty;

  void _marcarAsset(GalleryAsset asset) {
    setState(() {
      final i = _marcados.indexWhere((a) => a.id == asset.id);
      if (i >= 0) {
        _marcados.removeAt(i);
      } else {
        _marcados.add(asset);
      }
    });
  }

  void _mudarDuracaoDaImagem(double delta) {
    setState(() {
      _duracaoDaImagem = (_duracaoDaImagem + delta).clamp(0.5, 30.0);
    });
    try {
      ref
          .read(sharedPreferencesProvider)
          .setDouble(_kDuracaoDaImagem, _duracaoDaImagem);
    } catch (_) {}
  }

  /// O LOTE: copia cada marcada e entrega tudo de uma vez para quem
  /// monta as camadas. A imagem leva a duracao da barra.
  Future<void> _importarLote({required bool emSequencia}) async {
    final lote = widget.onImportLote;
    if (lote == null || _importing || _marcados.isEmpty) return;
    setState(() {
      _importing = true;
      _error = null;
      _aviso = null;
    });
    final recentes = ref.read(midiasRecentesProvider.notifier);
    final servico = _service;
    try {
      final midias = <MidiaDoLote>[];
      final origens = <GalleryAsset>[];
      for (final asset in List.of(_marcados)) {
        final XFile saved;
        final jaTenho = recentes.daOrigem(asset.origem);
        if (jaTenho != null) {
          saved = arquivoJaImportado(jaTenho.caminho, jaTenho.nome);
        } else {
          final file = await servico.file(asset);
          if (file == null) continue;
          saved = await ref
              .read(mediaImportServiceProvider)
              .persist(XFile(file.path), image: !asset.video);
        }
        origens.add(asset);
        midias.add((
          file: saved,
          video: asset.video,
          duration: asset.video
              ? asset.duration
              : Duration(milliseconds: (_duracaoDaImagem * 1000).round()),
        ));
      }
      if (!mounted || midias.isEmpty) return;
      await lote(midias, emSequencia: emSequencia);
      if (mounted) setState(_marcados.clear);
      // De tras para a frente: a PRIMEIRA marcada termina na frente da
      // lista, que e a ordem em que a pessoa pensou o lote.
      for (var i = midias.length - 1; i >= 0; i--) {
        final asset = origens[i];
        await registrarMidiaImportada(
          recentes,
          caminho: midias[i].file.path,
          nome: midias[i].file.name,
          video: asset.video,
          duracao: asset.video ? asset.duration : Duration.zero,
          origem: asset.origem,
          miniaturaPronta: () => servico.thumbnail(asset),
        );
      }
    } catch (_) {
      if (mounted) {
        setState(() => _error = 'Não foi possível importar as marcadas.');
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
    asset: asset,
  );

  /// UM RECENTE ENTRA DIRETO: o arquivo ja esta copiado dentro do app, entao
  /// nao ha seletor, nem `persist`, nem arquivo novo em disco.
  Future<void> _usarRecente(MidiaRecente midia) async {
    if (_importing) return;
    final recentes = ref.read(midiasRecentesProvider.notifier);
    if (!File(midia.caminho).existsSync()) {
      recentes.tirar(midia.caminho);
      setState(() => _aviso = 'Essa mídia não está mais no aparelho.');
      return;
    }
    setState(() {
      _importing = true;
      _error = null;
      _aviso = null;
    });
    try {
      await widget.onImport(
        arquivoJaImportado(midia.caminho, midia.nome),
        midia.video,
        midia.duracao,
      );
      // Usou de novo: volta para a frente da fila.
      try {
        recentes.registrar(midia);
      } catch (_) {}
    } catch (_) {
      if (mounted) {
        setState(() => _error = 'Não foi possível importar.');
      }
    } finally {
      if (mounted) setState(() => _importing = false);
    }
  }

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
    final listaDeRecentes = ref.watch(midiasRecentesProvider);
    // Lista que esvaziou (tudo apagado do disco) devolve a grade normal.
    final vendoRecentes = _recentes && listaDeRecentes.isNotEmpty;
    if (_selecionando && widget.onImportLote != null) {
      final temImagem = _marcados.any((a) => !a.video);
      return Stack(
        children: [
          Column(
            children: [
              SizedBox(
                height: 32,
                child: Row(
                  children: [
                    Expanded(
                      child: AppTextMoldado(
                        '{0} marcadas', [_marcados.length],
                        key: const ValueKey('galeria-marcadas'),
                        style: const TextStyle(
                          color: AmColors.text,
                          fontSize: 12,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ),
                    IconButton(
                      key: const ValueKey('galeria-lote-juntas'),
                      tooltip: 'Adicionar todas juntas, no cabeçote',
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(minWidth: 34),
                      iconSize: 18,
                      onPressed: () => _importarLote(emSequencia: false),
                      icon: const Icon(
                        CupertinoIcons.square_stack_3d_down_right,
                        color: AmColors.text,
                      ),
                    ),
                    IconButton(
                      key: const ValueKey('galeria-lote-sequencia'),
                      tooltip: 'Adicionar em sequência, uma após a outra',
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(minWidth: 34),
                      iconSize: 18,
                      onPressed: () => _importarLote(emSequencia: true),
                      icon: const Icon(
                        CupertinoIcons.arrow_right_to_line,
                        color: AmColors.text,
                      ),
                    ),
                    IconButton(
                      key: const ValueKey('galeria-lote-sair'),
                      tooltip: 'Sair da seleção',
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(minWidth: 34),
                      iconSize: 18,
                      onPressed: () => setState(_marcados.clear),
                      icon: const Icon(
                        CupertinoIcons.xmark,
                        color: AmColors.text,
                      ),
                    ),
                  ],
                ),
              ),
              if (temImagem)
                SizedBox(
                  height: 26,
                  child: Row(
                    children: [
                      const AppText(
                        'Cada imagem fica',
                        style: TextStyle(color: AmColors.muted, fontSize: 11),
                      ),
                      const Spacer(),
                      IconButton(
                        key: const ValueKey('galeria-imagem-menos'),
                        padding: EdgeInsets.zero,
                        constraints: const BoxConstraints(minWidth: 28),
                        iconSize: 14,
                        onPressed: () => _mudarDuracaoDaImagem(-.5),
                        icon: const Icon(
                          CupertinoIcons.minus,
                          color: AmColors.text,
                        ),
                      ),
                      AppText(
                        '${_duracaoDaImagem.toStringAsFixed(1)} s',
                        key: const ValueKey('galeria-imagem-duracao'),
                        style: TextStyle(
                          color: AmColors.accent,
                          fontSize: 12,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                      IconButton(
                        key: const ValueKey('galeria-imagem-mais'),
                        padding: EdgeInsets.zero,
                        constraints: const BoxConstraints(minWidth: 28),
                        iconSize: 14,
                        onPressed: () => _mudarDuracaoDaImagem(.5),
                        icon: const Icon(
                          CupertinoIcons.plus,
                          color: AmColors.text,
                        ),
                      ),
                    ],
                  ),
                ),
              Expanded(child: _grade()),
            ],
          ),
          if (_importing) _veuDeImportacao(),
        ],
      );
    }
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
                        ? AppText(
                            vendoRecentes ? 'Recentes' : 'Galeria',
                            style: const TextStyle(
                              color: AmColors.text,
                              fontSize: 12,
                            ),
                          )
                        : PopupMenuButton<GalleryAlbum>(
                            tooltip: 'Selecionar álbum',
                            onSelected: _escolhidoNoMenu,
                            itemBuilder: (_) => [
                              if (listaDeRecentes.isNotEmpty)
                                PopupMenuItem(
                                  key: const ValueKey('galeria-album-recentes'),
                                  value: _albumRecentes,
                                  child: AppText(_albumRecentes.name),
                                ),
                              for (final album in _albums)
                                PopupMenuItem(
                                  value: album,
                                  child: AppText(album.name),
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
                                  child: AppText(
                                    vendoRecentes
                                        ? _albumRecentes.name
                                        : _album?.name ?? 'Todos',
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
                  // ACESSO RAPIDO AOS RECENTES: um toque, sem abrir o menu
                  // de albuns. O mesmo botao devolve ao album de antes. So
                  // existe quando ha o que mostrar.
                  if (listaDeRecentes.isNotEmpty)
                    IconButton(
                      key: const ValueKey('galeria-recentes'),
                      tooltip: translate(context, 'Recentes'),
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(minWidth: 36),
                      iconSize: 18,
                      onPressed: () => setState(() {
                        _recentes = !vendoRecentes;
                        _aviso = null;
                      }),
                      icon: Icon(
                        CupertinoIcons.clock,
                        color: vendoRecentes ? AmColors.accent : AmColors.text,
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
                    tooltip: translate(context, 'Vídeos do sistema'),
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
                child: Padding(
                  padding: EdgeInsets.symmetric(vertical: 4),
                  child: AppText(
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
                  child: AppTextMoldado(
                    '{0} Tentar novamente', [_error],
                    maxLines: 3,
                    style: TextStyle(color: AmColors.pink, fontSize: 11),
                  ),
                ),
              ),
            if (_aviso != null)
              Padding(
                padding: const EdgeInsets.all(4),
                child: AppText(
                  _aviso!,
                  key: const ValueKey('galeria-aviso'),
                  maxLines: 2,
                  style: const TextStyle(color: AmColors.muted, fontSize: 11),
                ),
              ),
            Expanded(
              child: vendoRecentes
                  ? _gradeDeRecentes(listaDeRecentes)
                  : _grade(),
            ),
          ],
        ),
        if (_importing) _veuDeImportacao(),
      ],
    );
  }

  /// A mesma grade de tres colunas, com o que ja esta dentro do app. Sem
  /// lote aqui: marcar varias e coisa da galeria do aparelho.
  Widget _gradeDeRecentes(List<MidiaRecente> lista) => GridView.builder(
    key: const ValueKey('galeria-grade-recentes'),
    padding: EdgeInsets.zero,
    scrollCacheExtent: const ScrollCacheExtent.pixels(100),
    addAutomaticKeepAlives: false,
    gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
      crossAxisCount: 3,
      mainAxisSpacing: 1,
      crossAxisSpacing: 1,
    ),
    itemCount: lista.length,
    itemBuilder: (context, index) {
      final midia = lista[index];
      return _MiniaturaRecente(
        key: ValueKey('recente-${midia.caminho}'),
        midia: midia,
        onTap: () => _usarRecente(midia),
      );
    },
  );

  Widget _grade() => _loading
      ? const Center(child: CupertinoActivityIndicator())
      : _assets.isEmpty && !_paging
      ? _empty()
      : GridView.builder(
          key: const ValueKey('gallery-grid'),
          controller: _scroll,
          padding: EdgeInsets.zero,
          scrollCacheExtent: const ScrollCacheExtent.pixels(100),
          addAutomaticKeepAlives: false,
          gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
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
            final ordem = _marcados.indexWhere((a) => a.id == asset.id);
            return _GalleryThumbnail(
              key: ValueKey(asset.id),
              asset: asset,
              load: () => _service.thumbnail(asset),
              ordem: ordem < 0 ? null : ordem + 1,
              onTap: () =>
                  _selecionando ? _marcarAsset(asset) : _pickAsset(asset),
              onLongPress: widget.onImportLote == null
                  ? null
                  : () => _marcarAsset(asset),
            );
          },
        );

  Widget _veuDeImportacao() => Positioned.fill(
    child: ColoredBox(
      color: const Color(0xDD17191D),
      child: const Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            CupertinoActivityIndicator(),
            SizedBox(height: 10),
            AppText(
              'Carregando mídia…',
              style: TextStyle(color: AmColors.text, fontSize: 12),
            ),
          ],
        ),
      ),
    ),
  );

  Widget _empty() => Center(
    child: SingleChildScrollView(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          AppText(
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
              child: const AppText('Abrir ajustes'),
            ),
          TextButton(onPressed: _refresh, child: const AppText('Atualizar')),
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
    this.onLongPress,
    this.ordem,
  });
  final GalleryAsset asset;
  final Future<Uint8List?> Function() load;
  final VoidCallback onTap;

  /// Segurar comeca a marcar (quando o lote existe).
  final VoidCallback? onLongPress;

  /// A posicao na selecao (1, 2, 3...); nula = fora dela.
  final int? ordem;
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
      onLongPress: widget.onLongPress,
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
              child: AppText(
                '${widget.asset.duration.inMinutes}:${(widget.asset.duration.inSeconds % 60).toString().padLeft(2, '0')}',
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 10,
                  shadows: [Shadow(blurRadius: 3, color: Colors.black)],
                ),
              ),
            ),
          ],
          // A ORDEM DA MARCA: em sequencia, e a ordem em que entram.
          if (widget.ordem != null)
            Positioned(
              left: 3,
              top: 3,
              child: Container(
                key: ValueKey('galeria-ordem-${widget.asset.id}'),
                width: 20,
                height: 20,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: AmColors.accent,
                  shape: BoxShape.circle,
                ),
                child: AppText(
                  '${widget.ordem}',
                  style: const TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w700,
                    color: AureaColors.onAccent,
                  ),
                ),
              ),
            ),
        ],
      ),
    ),
  );
}

/// A miniatura de um recente. Imagem usa o proprio arquivo (reduzido na
/// decodificacao); video usa o jpg guardado na importacao, e sem ele fica o
/// icone — miniatura que falhou nao esconde o item.
class _MiniaturaRecente extends StatelessWidget {
  const _MiniaturaRecente({
    super.key,
    required this.midia,
    required this.onTap,
  });
  final MidiaRecente midia;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final imagem = midia.video ? midia.miniatura : midia.caminho;
    final d = midia.duracao;
    return Semantics(
      label: midia.video ? 'Reusar vídeo' : 'Reusar foto',
      button: true,
      child: InkWell(
        onTap: onTap,
        child: Stack(
          fit: StackFit.expand,
          children: [
            ColoredBox(
              color: AmColors.chip,
              child: imagem == null
                  ? const Icon(
                      CupertinoIcons.videocam,
                      color: AmColors.muted,
                      size: 22,
                    )
                  : Image.file(
                      File(imagem),
                      fit: BoxFit.cover,
                      cacheWidth: 200,
                      gaplessPlayback: true,
                      errorBuilder: (_, _, _) => Icon(
                        midia.video
                            ? CupertinoIcons.videocam
                            : Icons.broken_image_outlined,
                        color: AmColors.muted,
                      ),
                    ),
            ),
            if (midia.video) ...[
              const Center(
                child: Icon(Icons.play_arrow, color: Colors.white, size: 26),
              ),
              if (d > Duration.zero)
                Positioned(
                  right: 3,
                  bottom: 2,
                  child: AppText(
                    '${d.inMinutes}:${(d.inSeconds % 60).toString().padLeft(2, '0')}',
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
}
