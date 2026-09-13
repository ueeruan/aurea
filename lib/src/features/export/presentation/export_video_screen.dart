import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:gal/gal.dart';

import '../../editor/application/editor_controller.dart';
import '../../editor/application/panorama_cache.dart';
import '../../editor/application/texture_cache.dart';
import '../../editor/application/video_layer_manager.dart';
import '../../editor/domain/cut_ops.dart';
import '../../editor/domain/layer.dart';
import '../../../core/ui/am_colors.dart';
import '../../editor/presentation/widgets/dither_layer.dart';
import '../../editor/presentation/widgets/pixel_effect_engine.dart';
import '../../editor/presentation/widgets/preview_stage.dart';
import '../../editor/application/duck_service.dart';
import '../../editor/application/media_preview_service.dart';
import '../application/export_engine.dart';
import '../application/platform_encoder.dart';
import '../domain/export_settings.dart';
import '../../settings/application/settings_controller.dart';

/// EXPORTAR VIDEO.
///
/// A composicao e desenhada em Flutter, entao exportar e re-renderizar
/// a MESMA arvore quadro a quadro e mandar para o FFmpeg. Nada de
/// "gravar a tela": cada quadro sai na resolucao do projeto, mesmo que
/// o aparelho mostre bem menor.
class ExportVideoScreen extends ConsumerStatefulWidget {
  const ExportVideoScreen({super.key, this.settings = const ExportSettings()});

  /// COM O QUE A TELA ABRE. Quem manda depois e o que a pessoa escolher
  /// na fase de ajustes — antes nao havia fase nenhuma, e este valor,
  /// que nenhum chamador passava, decidia a exportacao inteira.
  final ExportSettings settings;

  @override
  ConsumerState<ExportVideoScreen> createState() => _ExportVideoScreenState();
}

enum _Fase {
  /// ANTES DE COMECAR: o que vai sair. Esta fase nao existia, e por
  /// isso resolucao, fps, formato, codec e qualidade — todos escritos,
  /// testados e prontos em `ExportSettings` — eram inalcancaveis: o
  /// unico construtor real era `const ExportVideoScreen()`, sem ajuste
  /// nenhum, e a exportacao saia sempre no tamanho do projeto, em
  /// H.264, na qualidade media.
  ajustes,

  preparando,
  lendoVideos,
  desenhando,
  codificando,
  pronto,
  erro,
}

class _ExportVideoScreenState extends ConsumerState<ExportVideoScreen> {
  final GlobalKey _boundary = GlobalKey();
  final ValueNotifier<Duration> _time = ValueNotifier(Duration.zero);
  final VideoLayerManager _videos = VideoLayerManager();

  ExportEngine? _engine;
  _Fase _fase = _Fase.ajustes;

  /// O que a pessoa escolheu nesta tela.
  late ExportSettings _ajustes = widget.settings;
  double _progresso = 0;
  String _detalhe = '';
  String? _erro;
  File? _saida;

  /// O QUE ACONTECEU COM A GALERIA. Ate agora o video terminava dentro
  /// da pasta do app e a tela oferecia "copiar caminho" — num celular
  /// isso nao leva a lugar nenhum, e os testadores exportaram tres
  /// videos sem achar nenhum. Agora ele vai para a galeria e esta linha
  /// diz se foi, ou por que nao foi.
  String? _galeria;

  /// Quadros das camadas de video: pasta por camada + imagem do quadro
  /// atual, ja decodificada.
  final Map<String, Directory> _pastas = {};
  final Map<String, int> _contagem = {};
  final Map<String, Duration> _inicioDosQuadros = {};
  final Map<String, ui.Image> _quadroAtual = {};

  @override
  void initState() {
    super.initState();
    // O shader precisa estar carregado antes do primeiro quadro. Ele
    // aquece agora, enquanto a pessoa escolhe os ajustes — quando ela
    // tocar em "Exportar" ja vai estar pronto.
    // O ERRO DO AQUECIMENTO NAO PODE ESTOURAR AQUI. Ele agora e
    // esperado la na frente, quando a pessoa manda exportar; um shader
    // que nao carrega vira erro DAQUELA fase, com a mensagem certa, e
    // nao uma excecao solta enquanto ela escolhe o tamanho.
    _aquecendo = Future.wait([DitherLayer.warmUp(), PixelEffectEngine.warmUp()])
        .catchError((Object _) => const <void>[]);
  }

  @override
  void dispose() {
    for (final img in _quadroAtual.values) {
      img.dispose();
    }
    _engine?.cancel();
    _time.dispose();
    _videos.dispose();
    super.dispose();
  }

  void _passo(_Fase f, double p, String d) {
    if (!mounted) return;
    setState(() {
      _fase = f;
      _progresso = p.clamp(0.0, 1.0);
      _detalhe = d;
    });
  }

  Future<void>? _aquecendo;

  Future<void> _rodar() async {
    await _aquecendo;
    if (!mounted) return;
    _passo(_Fase.preparando, 0, 'Preparando...');
    // O projeto completo (dobrando o que esta aberto dentro de grupos) e
    // sem as camadas de olho fechado.
    final project = ref
        .read(editorControllerProvider.notifier)
        .projetoParaExportar;
    // Os mesmos envelopes que o preview usou: o arquivo tem de sair com
    // o abaixamento que a pessoa acabou de ouvir, nao com um recalculado.
    final engine = ExportEngine(
      project,
      _ajustes,
      buildProjectDuckEnvelopes(
        project.layers,
        MediaPreviewService.instance.peaksOf,
      ),
    );
    _engine = engine;

    try {
      if (project.layers.isEmpty) {
        throw ExportException('O projeto esta vazio.');
      }
      final total = engine.frameCount;
      if (total <= 0) {
        throw ExportException('A composicao tem duracao zero.');
      }

      // CORTE PURO: um clipe so, sem nada por cima. Copiar as trilhas em
      // vez de redesenhar 150 quadros e a diferenca entre instantaneo e
      // um minuto de espera.
      // O atalho de copiar so vale quando a saida e igual a entrada:
      // pedir 720p, HEVC ou sequencia PNG e pedir para RENDERIZAR.
      final podeCopiar =
          _ajustes.format == ExportFormat.mp4 &&
          _ajustes.size == ExportSize.original &&
          _ajustes.codec == ExportCodec.h264 &&
          _ajustes.fps == null &&
          _ajustes.bitrateMbps == null;
      _passo(_Fase.preparando, 0.1, 'Verificando se da para copiar...');
      final atalho = podeCopiar ? await engine.tryPureCut() : null;
      if (atalho != null) {
        if (!mounted) return;
        setState(() {
          _fase = _Fase.pronto;
          _progresso = 1;
          _saida = atalho;
          _detalhe = 'Corte puro: copiado sem recodificar.';
        });
        return;
      }

      // O pintor 3D consulta caches sincronamente. Depois de reabrir um
      // projeto eles ainda estao vazios; aguardar aqui impede que os
      // primeiros quadros sejam capturados com cor lisa ou sem panorama.
      await _prepararRecursos3D(project.layers);
      if (engine.cancelled) return;

      // 1. Quadros de cada camada de video.
      final videoLayers = project.layers.whereType<VideoLayer>().toList();
      for (var i = 0; i < videoLayers.length; i++) {
        if (engine.cancelled) return;
        final l = videoLayers[i];
        _passo(
          _Fase.lendoVideos,
          (i + 0.5) / (videoLayers.length + 1),
          'Lendo "${l.name}" (${i + 1} de ${videoLayers.length})',
        );
        final dir = await engine.extractVideoFrames(l);
        _pastas[l.id] = dir;
        _inicioDosQuadros[l.id] = engine.videoFrameRange(l).$1;
        _contagem[l.id] = dir
            .listSync()
            .whereType<File>()
            .where((f) => f.path.endsWith('.png'))
            .length;
      }

      // 2. Desenha a composicao quadro a quadro.
      final work = await engine.workDir();
      final framesDir = Directory('${work.path}/frames');
      framesDir.createSync(recursive: true);

      // EM FLUXO OU EM ARQUIVOS?
      //
      // Em fluxo, cada quadro sai da GPU e entra no codificador da
      // plataforma, na memoria. Nada de PNG, nada de disco, nada de
      // segunda passada. E o caminho normal.
      //
      // Em arquivos so quando nao ha escolha: a sequencia PNG, que E
      // arquivos por definicao, e o aparelho sem codificador de
      // hardware, onde quem codifica e o FFmpeg lendo do disco.
      final sequencia = _ajustes.format == ExportFormat.pngSequence;
      final emFluxo = !sequencia && await PlatformEncoder.available;

      // A composicao e desenhada no tamanho dela; a saida pode ser
      // menor (720p de um projeto 1080p). A razao entre as duas vira a
      // escala da captura.
      final escala = project.outputHeight <= 0
          ? 1.0
          : engine.height / project.outputHeight;

      // ESPACO ANTES DE COMECAR, nao no fim.
      _passo(_Fase.preparando, .2, 'Conferindo o espaco em disco...');
      await engine.conferirEspaco(emFluxo: emFluxo);

      File? mudo;
      if (emFluxo) {
        mudo = File('${work.path}/mudo.mp4');
        await PlatformEncoder.start(
          path: mudo.path,
          width: engine.width,
          height: engine.height,
          fps: engine.fps,
          bitrate: engine.taxaDeBits(),
          hevc: _ajustes.codec == ExportCodec.hevc,
        );
      }

      // Use the same compositor as preview for every output format. The
      // experimental native bridge does not serialize all layer properties.
      for (var i = 0; i < total; i++) {
        if (engine.cancelled) return;
        final t = engine.timeOfFrame(i);
        await _prepararQuadrosDeVideo(videoLayers, t);
        _time.value = t;
        if (!mounted) return;
        setState(() {});
        await WidgetsBinding.instance.endOfFrame;
        if (!mounted || engine.cancelled) return;

        if (emFluxo) {
          final quadro = await _capturarCru(escala);
          if (quadro == null) {
            throw ExportException('Nao consegui desenhar o quadro $i.');
          }
          await PlatformEncoder.frameRgba(
            quadro.bytes,
            quadro.largura,
            quadro.altura,
          );
        } else {
          final png = await _capturar();
          if (png == null) {
            throw ExportException('Nao consegui desenhar o quadro $i.');
          }
          final name = i.toString().padLeft(6, '0');
          File('${framesDir.path}/$name.png').writeAsBytesSync(png);
        }
        _passo(_Fase.desenhando, (i + 1) / total, 'Quadro ${i + 1} de $total');
      }

      // 3. EM FLUXO o video ja esta pronto quando o laco acaba: so falta
      // fechar o codificador e juntar o audio.
      if (emFluxo) {
        _passo(_Fase.codificando, .4, 'Fechando o video...');
        if (!await PlatformEncoder.finish()) {
          throw ExportException(
            'O codificador do aparelho nao fechou o arquivo. '
            'Tente exportar em H.264 ou numa resolucao menor.',
          );
        }
        _passo(_Fase.codificando, .7, 'Juntando o audio...');
        final file = await engine.juntarAudio(mudo!);
        await engine.cleanup();
        final naGaleria = await _salvarNaGaleria(file);
        if (!mounted) return;
        setState(() {
          _fase = _Fase.pronto;
          _progresso = 1;
          _saida = file;
          _detalhe = file.path;
          _galeria = naGaleria;
        });
        return;
      }

      // 4. Sequencia PNG para quando termina aqui: nao ha o que
      // codificar, so onde guardar.
      if (_ajustes.format == ExportFormat.pngSequence) {
        _passo(_Fase.codificando, 0.5, 'Salvando a sequencia...');
        final pasta = await engine.saveSequence(framesDir);
        await engine.cleanup();
        if (!mounted) return;
        setState(() {
          _fase = _Fase.pronto;
          _progresso = 1;
          _saida = File('${pasta.path}/000000.png');
          _detalhe = '$total imagens em ${pasta.path}';
        });
        return;
      }

      _passo(
        _Fase.codificando,
        0.05,
        'Codificando no codificador do aparelho...',
      );
      final file = await engine.encode(
        framesDir: framesDir,
        onProgress: (p) => _passo(
          _Fase.codificando,
          p,
          p < 0.9 ? 'Codificando video...' : 'Juntando o audio...',
        ),
      );
      await engine.cleanup();
      final naGaleria = await _salvarNaGaleria(file);
      if (!mounted) return;
      setState(() {
        _fase = _Fase.pronto;
        _progresso = 1;
        _saida = file;
        _detalhe = file.path;
        _galeria = naGaleria;
      });
    } on ExportException catch (e) {
      await _abandonar(engine);
      if (!mounted) return;
      setState(() {
        _fase = _Fase.erro;
        _erro = e.message;
      });
    } catch (e) {
      await _abandonar(engine);
      if (!mounted) return;
      setState(() {
        _fase = _Fase.erro;
        // UM ERRO SEM CAUSA NAO SERVE PARA NINGUEM. A frase abaixo diz
        // em que ETAPA parou e o que o sistema respondeu — e quando da,
        // o que fazer a respeito.
        _erro = _explicar(e);
      });
    }
  }

  /// LARGA A EXPORTACAO NO MEIO sem deixar nada aberto.
  ///
  /// Em fluxo o codificador da plataforma esta ABERTO quando algo falha
  /// no meio do laco. Sem fecha-lo, o proximo `start` encontra um
  /// escritor vivo e a exportacao seguinte falha tambem — um erro vira
  /// dois, e o segundo nao tem nada a ver com a causa.
  Future<void> _abandonar(ExportEngine engine) async {
    await PlatformEncoder.cancel();
    await engine.cleanup();
  }

  /// Traduz o que veio de baixo para uma frase que diz o que houve.
  String _explicar(Object e) {
    final texto = '$e';
    final baixo = texto.toLowerCase();
    final causa = switch (baixo) {
      _ when baixo.contains('no space') || baixo.contains('enospc') =>
        'O disco encheu durante a exportacao.',
      _ when baixo.contains('permission') || baixo.contains('eacces') =>
        'O aplicativo nao teve permissao para gravar o arquivo.',
      _ when baixo.contains('out of memory') || baixo.contains('oom') =>
        'O aparelho ficou sem memoria. Tente uma resolucao menor.',
      _ when baixo.contains('codec') || baixo.contains('encoder') =>
        'O codificador do aparelho falhou. Tente H.264 em vez de HEVC.',
      _ when baixo.contains('missingplugin') =>
        'O codificador do aparelho nao respondeu.',
      _ => 'Falha inesperada na exportacao.',
    };
    return '$causa\n\n$texto';
  }

  Future<void> _prepararRecursos3D(List<Layer> layers) async {
    final panoramas = <Scene3DLayer>[];
    final texturePaths = <String>{};

    for (final layer in layers) {
      if (layer is Element3DLayer) {
        final path = layer.imagePath;
        if (path != null && path.isNotEmpty) texturePaths.add(path);
      }
      if (layer is! Scene3DLayer) continue;
      final panorama = layer.scene.panorama;
      if (panorama.hasImage) {
        panoramas.add(layer);
        if (panorama.showBackground) texturePaths.add(panorama.sourcePath!);
      }
      for (final node in layer.scene.nodes) {
        for (final m in node.modelAsset?.data['materials'] as List? ?? []) {
          if (m['image'] != null) texturePaths.add(m['image'] as String);
        }
        final material = node.material;
        final imagePath = material.imagePath;
        if (imagePath != null && imagePath.isNotEmpty) {
          texturePaths.add(imagePath);
        }
        texturePaths.addAll(
          material.faceImagePaths.values.where((path) => path.isNotEmpty),
        );
      }
    }

    if (panoramas.isEmpty && texturePaths.isEmpty) return;
    _passo(_Fase.preparando, 0.18, 'Preparando panorama e texturas 3D...');

    final labels = <String>[
      for (final layer in panoramas) 'panorama de "${layer.name}"',
      for (final path in texturePaths)
        path.startsWith('data:')
            ? 'textura embutida do modelo'
            : 'textura "$path"',
    ];
    final jobs = <Future<bool>>[
      for (final layer in panoramas)
        PanoramaCache.instance.prepare(layer.scene.panorama),
      for (final path in texturePaths) TextureCache.instance.prepare(path),
    ];

    late final List<bool> ready;
    try {
      ready = await Future.wait(jobs).timeout(const Duration(seconds: 30));
    } on TimeoutException {
      throw ExportException(
        'Panorama ou textura 3D demorou demais para preparar. '
        'Tente exportar novamente.',
      );
    }

    final failed = <String>[
      for (var i = 0; i < ready.length; i++)
        if (!ready[i]) labels[i],
    ];
    if (failed.isNotEmpty) {
      final first = failed.take(3).join(', ');
      final remaining = failed.length > 3 ? failed.length - 3 : 0;
      throw ExportException(
        'Nao foi possivel preparar $first'
        '${remaining > 0 ? ' e mais $remaining recurso(s)' : ''}.',
      );
    }
  }

  /// Decodifica o quadro certo de cada camada de video para o instante
  /// [t] — so o que a camada esta mostrando agora.
  Future<void> _prepararQuadrosDeVideo(
    List<VideoLayer> videoLayers,
    Duration t,
  ) async {
    // A exportacao trabalha sobre o snapshot que criou o engine. Ler o
    // provider a cada quadro permitiria que uma alteracao de estado no
    // meio do processo misturasse duas curvas de source-time no arquivo.
    final exportLayers = _engine!.project.layers;
    for (final l in videoLayers) {
      final dir = _pastas[l.id];
      final count = _contagem[l.id] ?? 0;
      if (dir == null || count == 0) continue;

      final dentro = visibleForCut(exportLayers, l, t);
      if (!dentro) {
        _quadroAtual.remove(l.id)?.dispose();
        continue;
      }
      final transition = transitionContextAt(exportLayers, t);
      final local = localTimeForCut(l, t, transition);
      final source = videoAbsoluteSourceTimeAt(l, local);
      final extractedFrom = _inicioDosQuadros[l.id] ?? l.sourceOffset;
      // A MESMA TAXA DA EXTRACAO: com interpolacao ligada ha mais
      // quadros no disco do que a composicao tem, e o indice segue a
      // taxa em que eles foram escritos.
      final taxa = _engine!.fpsDeExtracao(l);
      final idx = ((source - extractedFrom).inMicroseconds * taxa / 1000000)
          .floor()
          .clamp(0, count - 1);
      final file = File('${dir.path}/${idx.toString().padLeft(6, '0')}.png');
      if (!file.existsSync()) continue;

      _quadroAtual.remove(l.id)?.dispose();
      final bytes = await file.readAsBytes();
      final codec = await ui.instantiateImageCodec(bytes);
      final frame = await codec.getNextFrame();
      codec.dispose();
      _quadroAtual[l.id] = frame.image;
    }
  }

  /// CAPTURA CRUA: os pixels como saem da composicao, sem compressao.
  ///
  /// `rawRgba` e o formato que o codificador quer. Pedir PNG aqui — que
  /// era o que se fazia — significava passar o quadro inteiro por um
  /// zlib na CPU para logo em seguida descomprimi-lo do outro lado da
  /// ponte. Era o maior custo por quadro da exportacao inteira, e nao
  /// servia a nenhum proposito de imagem.
  Future<({Uint8List bytes, int largura, int altura})?> _capturarCru(
    double escala,
  ) async {
    final obj = _boundary.currentContext?.findRenderObject();
    if (obj is! RenderRepaintBoundary) return null;
    // A ESCALA ACONTECE NA GPU. Exportar 720p de uma composicao 1080p
    // nao pode custar um redimensionamento por quadro na CPU: pedir a
    // captura ja na razao certa e de graca, e a qualidade e melhor que
    // a de qualquer reamostragem depois.
    final image = await obj.toImage(pixelRatio: escala);
    final data = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
    final l = image.width, a = image.height;
    image.dispose();
    if (data == null) return null;
    return (bytes: data.buffer.asUint8List(), largura: l, altura: a);
  }

  Future<Uint8List?> _capturar() async {
    final obj = _boundary.currentContext?.findRenderObject();
    if (obj is! RenderRepaintBoundary) return null;
    // pixelRatio 1: o RepaintBoundary ja tem o tamanho logico da
    // composicao, entao 1 pixel logico = 1 pixel do arquivo.
    final image = await obj.toImage(pixelRatio: 1);
    final data = await image.toByteData(format: ui.ImageByteFormat.png);
    image.dispose();
    return data?.buffer.asUint8List();
  }

  /// LEVA O VIDEO PARA A GALERIA.
  ///
  /// Devolve a frase que a tela mostra — nunca lanca. Uma exportacao que
  /// terminou nao pode virar erro porque a permissao da galeria foi
  /// negada: o arquivo existe, e o caminho continua na tela.
  Future<String?> _salvarNaGaleria(File file) async {
    if (!ref.read(settingsControllerProvider).saveToGallery) return null;
    if (!(Platform.isAndroid || Platform.isIOS)) {
      return 'Galeria so no celular — o arquivo esta na pasta abaixo';
    }
    try {
      if (!await Gal.hasAccess(toAlbum: true) &&
          !await Gal.requestAccess(toAlbum: true)) {
        return 'Sem permissao para a galeria. O arquivo esta na pasta abaixo';
      }
      await Gal.putVideo(file.path, album: 'Aurea');
      return 'Salvo na galeria, no album Aurea';
    } on GalException catch (e) {
      return 'Nao deu para salvar na galeria: ${e.type.message}';
    } catch (_) {
      return 'Nao deu para salvar na galeria. O arquivo esta na pasta abaixo';
    }
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final w = project.outputWidth.toDouble();
    final h = project.outputHeight.toDouble();

    return Scaffold(
      backgroundColor: AmColors.bg,
      body: SafeArea(
        child: Column(
          children: [
            _barra(),
            Expanded(
              child: Center(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: FittedBox(
                    fit: BoxFit.contain,
                    // O boundary tem o tamanho REAL da composicao; o
                    // FittedBox so encolhe o que aparece na tela.
                    child: RepaintBoundary(
                      key: _boundary,
                      child: SizedBox(
                        width: w,
                        height: h,
                        child: ClipRect(
                          key: const ValueKey('export-composition-clip'),
                          child: ColoredBox(
                            color: project.backgroundColor,
                            child: DitherLayer(
                              time: _time.value,
                              child: CompositionView(
                                time: _time,
                                videos: _videos,
                                selectedId: null,
                                exportFrames: _quadroAtual,
                                exporting: true,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ),
            _rodape(),
          ],
        ),
      ),
    );
  }

  Widget _barra() => Container(
    height: 50,
    padding: const EdgeInsets.symmetric(horizontal: 6),
    color: AmColors.topBar,
    child: Row(
      children: [
        CupertinoButton(
          padding: const EdgeInsets.symmetric(horizontal: 10),
          onPressed: () {
            _engine?.cancel();
            Navigator.of(context).maybePop();
          },
          child: const Icon(
            CupertinoIcons.xmark,
            size: 19,
            color: AmColors.text,
          ),
        ),
        const AppText(
          'Exportar video',
          style: TextStyle(
            fontSize: 15,
            fontWeight: FontWeight.w600,
            color: AmColors.text,
          ),
        ),
      ],
    ),
  );

  /// A FICHA DOS AJUSTES, antes de comecar.
  Widget _ajustesDaExportacao(int w, int h, Duration duracao, int fpsProjeto) {
    final (sw, sh) = _ajustes.resolve(w, h);
    final fps = _ajustes.resolveFps(fpsProjeto);
    final mb = _ajustes.estimatedMegabytes(sw, sh, fps, duracao);
    final sequencia = _ajustes.format == ExportFormat.pngSequence;

    return Container(
      width: double.infinity,
      color: AmColors.panel,
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _Escolhas<ExportFormat>(
              titulo: 'Formato',
              itens: [
                for (final f in ExportFormat.values) (exportFormatLabel(f), f),
              ],
              atual: _ajustes.format,
              aoEscolher: (f) => setState(() {
                _ajustes = _ajustes.copyWith(format: f);
              }),
            ),
            _Escolhas<ExportSize>(
              titulo: 'Tamanho',
              itens: [
                for (final t in ExportSize.values) (exportSizeLabel(t), t),
              ],
              atual: _ajustes.size,
              aoEscolher: (t) => setState(() {
                _ajustes = _ajustes.copyWith(size: t);
              }),
            ),
            _Escolhas<int?>(
              titulo: 'Quadros por segundo',
              itens: [
                ('Do projeto ($fpsProjeto)', null),
                ...const [('24', 24), ('30', 30), ('60', 60)],
              ],
              atual: _ajustes.fps,
              aoEscolher: (f) => setState(() {
                _ajustes = f == null
                    ? _ajustes.copyWith(clearFps: true)
                    : _ajustes.copyWith(fps: f);
              }),
            ),
            // O CODEC E A QUALIDADE SO VALEM NO MP4. Sequencia PNG nao
            // passa por codificador nenhum — mostrar os dois botoes ali
            // seria oferecer escolha que o arquivo ignora.
            if (!sequencia) ...[
              _Escolhas<ExportCodec>(
                titulo: 'Codec',
                itens: [
                  for (final c in ExportCodec.values) (exportCodecLabel(c), c),
                ],
                atual: _ajustes.codec,
                aoEscolher: (c) => setState(() {
                  _ajustes = _ajustes.copyWith(codec: c);
                }),
              ),
              _Escolhas<String>(
                titulo: 'Qualidade',
                itens: const [
                  ('Baixa', 'baixa'),
                  ('Media', 'media'),
                  ('Alta', 'alta'),
                ],
                atual: _ajustes.quality,
                aoEscolher: (q) => setState(() {
                  _ajustes = _ajustes.copyWith(quality: q);
                }),
              ),
            ],
            const SizedBox(height: 4),
            AppText(
              sequencia
                  ? '$sw x $sh · $fps qps · guarda transparencia'
                  : '$sw x $sh · $fps qps · cerca de '
                        '${mb < 1000 ? '${mb.round()} MB' : '${(mb / 1024).toStringAsFixed(1)} GB'}',
              style: const TextStyle(
                fontSize: 11,
                height: 1.4,
                color: AmColors.muted,
              ),
            ),
            const SizedBox(height: 12),
            _BotaoGrande(rotulo: 'Exportar', aoTocar: _rodar),
          ],
        ),
      ),
    );
  }

  Widget _rodape() {
    if (_fase == _Fase.ajustes) {
      final p = ref.read(editorControllerProvider);
      return _ajustesDaExportacao(
        p.outputWidth,
        p.outputHeight,
        p.duration,
        p.fps,
      );
    }
    if (_fase == _Fase.erro) {
      return Container(
        width: double.infinity,
        padding: const EdgeInsets.fromLTRB(18, 14, 18, 20),
        color: AmColors.panel,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Row(
              children: [
                Icon(
                  CupertinoIcons.exclamationmark_triangle,
                  size: 17,
                  color: AmColors.pink,
                ),
                SizedBox(width: 8),
                AppText('Nao deu para exportar',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                    color: AmColors.pink,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            AppText(
              _erro ?? '',
              style: const TextStyle(
                fontSize: 11,
                height: 1.4,
                color: AmColors.muted,
              ),
            ),
            const SizedBox(height: 12),
            // TENTAR DE NOVO VOLTA AOS AJUSTES, e nao repete a mesma
            // tentativa. Quase todo erro daqui — espaco, tamanho, codec
            // que o aparelho recusou — se resolve mudando um ajuste, e
            // repetir igual daria o mesmo erro.
            _BotaoGrande(
              rotulo: 'Mudar os ajustes e tentar de novo',
              aoTocar: () => setState(() {
                _erro = null;
                _fase = _Fase.ajustes;
                _progresso = 0;
              }),
            ),
          ],
        ),
      );
    }

    if (_fase == _Fase.pronto) {
      return Container(
        width: double.infinity,
        padding: const EdgeInsets.fromLTRB(18, 14, 18, 20),
        color: AmColors.panel,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Row(
              children: [
                Icon(
                  CupertinoIcons.checkmark_seal_fill,
                  size: 17,
                  color: AmColors.accent,
                ),
                SizedBox(width: 8),
                AppText('Video pronto',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                    color: AmColors.accent,
                  ),
                ),
              ],
            ),
            if (_galeria != null) ...[
              const SizedBox(height: 6),
              AppText(
                _galeria!,
                style: const TextStyle(
                  fontSize: 12,
                  height: 1.35,
                  color: AmColors.text,
                ),
              ),
            ],
            const SizedBox(height: 6),
            AppText(
              _saida?.path ?? '',
              style: const TextStyle(
                fontSize: 10,
                height: 1.4,
                color: AmColors.muted,
              ),
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: CupertinoButton(
                    color: AmColors.chip,
                    borderRadius: BorderRadius.circular(12),
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    onPressed: () async {
                      final messenger = ScaffoldMessenger.maybeOf(context);
                      await Clipboard.setData(
                        ClipboardData(text: _saida?.path ?? ''),
                      );
                      messenger?.showSnackBar(
                        const SnackBar(
                          content: AppText('Caminho copiado'),
                          duration: Duration(seconds: 2),
                          behavior: SnackBarBehavior.floating,
                        ),
                      );
                    },
                    child: const AppText('Copiar caminho',
                      style: TextStyle(fontSize: 13, color: AmColors.accent),
                    ),
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: CupertinoButton(
                    color: AmColors.accent,
                    borderRadius: BorderRadius.circular(12),
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    onPressed: () => Navigator.of(context).maybePop(),
                    child: const AppText('Concluir',
                      style: TextStyle(
                        fontSize: 13,
                        fontWeight: FontWeight.w700,
                        color: Color(0xFF10151D),
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      );
    }

    final rotulo = switch (_fase) {
      _Fase.preparando => 'Preparando...',
      _Fase.lendoVideos => 'Lendo os videos',
      _Fase.desenhando => 'Desenhando os quadros',
      _Fase.codificando => 'Codificando',
      _ => '',
    };

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.fromLTRB(18, 16, 18, 20),
      color: AmColors.panel,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              AppText(
                rotulo,
                style: const TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w600,
                  color: AmColors.text,
                ),
              ),
              const Spacer(),
              AppText(
                '${(_progresso * 100).round()}%',
                style: const TextStyle(fontSize: 13, color: AmColors.accent),
              ),
            ],
          ),
          const SizedBox(height: 8),
          ClipRRect(
            borderRadius: BorderRadius.circular(6),
            child: LinearProgressIndicator(
              value: _progresso,
              minHeight: 6,
              backgroundColor: AmColors.chip,
              valueColor: const AlwaysStoppedAnimation<Color>(AmColors.accent),
            ),
          ),
          const SizedBox(height: 8),
          AppText(
            _detalhe,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(fontSize: 11, color: AmColors.muted),
          ),
          const SizedBox(height: 4),
          const AppText('Deixe o app aberto nesta tela ate terminar.',
            style: TextStyle(fontSize: 10, color: AmColors.muted),
          ),
        ],
      ),
    );
  }
}

/// UMA FILEIRA DE ESCOLHAS, com a vigente acesa.
///
/// Sem `Slider` e sem menu suspenso: as listas daqui tem tres a seis
/// itens e cabem todas na tela. Um menu esconderia atras de um toque a
/// unica informacao que importa aqui — o que esta escolhido.
class _Escolhas<T> extends StatelessWidget {
  const _Escolhas({
    required this.titulo,
    required this.itens,
    required this.atual,
    required this.aoEscolher,
  });

  final String titulo;
  final List<(String, T)> itens;
  final T atual;
  final void Function(T) aoEscolher;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 10),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.only(bottom: 6),
          child: AppText(titulo,
            style: const TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w700,
              color: AmColors.muted,
            ),
          ),
        ),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            for (final (nome, valor) in itens)
              Semantics(
                container: true,
                excludeSemantics: true,
                button: true,
                selected: valor == atual,
                label: '$titulo $nome',
                child: GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: () => aoEscolher(valor),
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 12,
                      vertical: 9,
                    ),
                    decoration: BoxDecoration(
                      color: valor == atual ? AmColors.chip : null,
                      borderRadius: BorderRadius.circular(8),
                      border: valor == atual
                          ? null
                          : Border.all(color: AmColors.hairline),
                    ),
                    child: AppText(nome,
                      style: TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                        color: valor == atual ? AmColors.accent : AmColors.text,
                      ),
                    ),
                  ),
                ),
              ),
          ],
        ),
      ],
    ),
  );
}

class _BotaoGrande extends StatelessWidget {
  const _BotaoGrande({required this.rotulo, required this.aoTocar});

  final String rotulo;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Container(
        height: 46,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: AmColors.action,
          borderRadius: BorderRadius.circular(11),
        ),
        child: AppText(
          rotulo,
          style: const TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w700,
            color: AmColors.onAction,
          ),
        ),
      ),
    ),
  );
}
