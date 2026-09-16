import 'package:aurea/src/core/l10n/app_language.dart';

import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../core/storage/prefs.dart';
import '../../../core/theme/app_theme.dart';
import '../../editor/application/editor_controller.dart';
import '../../editor/application/playback_controller.dart';
import '../../editor/application/preview_stats.dart';
import '../../editor/application/qualidade3d_controller.dart';
import '../../editor/application/scene3d_gpu.dart';
import '../../editor/application/video_layer_manager.dart';
import '../../editor/domain/bancada_do_nucleo.dart';
import '../../editor/domain/effect.dart';
import '../../editor/domain/estresse3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/orcamento_render.dart';
import '../../editor/domain/video_project.dart';
import '../../editor/presentation/widgets/preview_stage.dart';

/// O TESTE DE ESTRESSE DO MOTOR 3D — roda no aparelho de verdade, e
/// entrega um relatorio para colar numa mensagem.
///
/// Nove cenas (ver estresse3d.dart), cada uma tocando por doze segundos
/// no MESMO palco do editor (a `CompositionView`, com o mesmo motor, os
/// mesmos efeitos e o mesmo player de video), enquanto se mede: tempo de
/// quadro (mediana, p95, pior, travadas), memoria do processo (inicio,
/// pico, fim), o nivel de qualidade e a pressao que o controlador
/// escolheu, e o que o motor desenhou. O que o pedido chama de
/// "estabilidade" e o que se le aqui: o app voltou de cada cena?
///
/// A MIGALHA: antes de cada cena o nome dela vai para o disco; se o app
/// nao voltar (jetsam, watchdog, GPU), a proxima abertura desta tela
/// encontra a migalha e escreve no relatorio "o app fechou durante X" —
/// o crash vira dado, em vez de sumir com a sessao.
class Estresse3DScreen extends StatefulWidget {
  const Estresse3DScreen({super.key});

  @override
  State<Estresse3DScreen> createState() => _Estresse3DScreenState();
}

class ResultadoDeEstresse {
  ResultadoDeEstresse({
    required this.numero,
    required this.titulo,
    required this.sobreviveu,
    this.quadros = 0,
    this.medianaMs = 0,
    this.p95Ms = 0,
    this.piorMs = 0,
    this.travadas = 0,
    this.rssInicioMb = 0,
    this.rssPicoMb = 0,
    this.rssFimMb = 0,
    this.nivelInicial = '',
    this.nivelFinal = '',
    this.pressaoPior = '',
    this.motor = '',
    this.gpuEstimada = '',
    this.desenhou = '',
    this.transicoes = const [],
    this.observacao = '',
  });

  final int numero;
  final String titulo;
  final bool sobreviveu;
  final int quadros;
  final double medianaMs, p95Ms, piorMs;
  final int travadas;
  final int rssInicioMb, rssPicoMb, rssFimMb;
  final String nivelInicial,
      nivelFinal,
      pressaoPior,
      motor,
      gpuEstimada,
      desenhou;
  final List<String> transicoes;
  final String observacao;

  String get linha {
    if (!sobreviveu) {
      return 'TESTE $numero — $titulo: O APP FECHOU durante a cena '
          '(nao voltou; migalha encontrada na abertura seguinte). $observacao';
    }
    final fps = medianaMs > 0 ? (1000 / medianaMs).toStringAsFixed(0) : '?';
    return 'TESTE $numero — $titulo: viveu. '
        '$quadros quadros · mediana ${medianaMs.toStringAsFixed(1)} ms (~$fps fps) · '
        'p95 ${p95Ms.toStringAsFixed(1)} ms · pior ${piorMs.toStringAsFixed(0)} ms · '
        '$travadas travadas\n'
        '   memoria: inicio $rssInicioMb MB · pico $rssPicoMb MB · fim $rssFimMb MB\n'
        '   qualidade: $nivelInicial -> $nivelFinal · pior pressao $pressaoPior · '
        'motor $motor · GPU estimada $gpuEstimada\n'
        '   desenhou: $desenhou'
        '${transicoes.isEmpty ? '' : '\n   transicoes: ${transicoes.join(' | ')}'}'
        '${observacao.isEmpty ? '' : '\n   obs: $observacao'}';
  }
}

class _Estresse3DScreenState extends State<Estresse3DScreen>
    with TickerProviderStateMixin {
  static const _kRodando = 'estresse_rodando';
  static const _kRelatorio = 'estresse_relatorio';
  static const _duracaoDoTeste = Duration(seconds: 12);

  SharedPreferences? _prefs;
  final List<ResultadoDeEstresse> _resultados = [];
  String? _relatorioAnterior;
  bool _rodando = false;
  bool _cancelar = false;
  int _indice = -1;
  String _status = 'Pronto. Nove cenas, cerca de tres minutos.';
  List<String> _texturas = const [];
  String? _video;

  // O palco em teste.
  ProviderContainer? _container;
  PlaybackController? _playback;
  VideoLayerManager? _videos;
  List<Layer> _layers = const [];
  VoidCallback? _ouvinteDoTempo;

  // As amostras da cena corrente.
  final List<double> _quadrosMs = [];
  int _rssPico = 0;
  int _rssInicio = 0;
  NivelDePressao _pressaoPior = NivelDePressao.seguro;
  TimingsCallback? _cb;
  Timer? _amostrador;

  // A BANCADA DO NUCLEO (cenas A-E): o motor atual medido no aparelho,
  // antes de cada pedaco ir para o C++.
  bool _modoBancada = false;
  final List<String> _linhasDaBancada = [];
  Ticker? _arrasto;

  @override
  void initState() {
    super.initState();
    unawaited(_abrir());
  }

  Future<void> _abrir() async {
    final prefs = await SharedPreferences.getInstance();
    _prefs = prefs;
    final caiu = prefs.getString(_kRodando);
    _relatorioAnterior = prefs.getString(_kRelatorio);
    if (caiu != null) {
      // A sessao anterior nao voltou de uma cena: vira dado.
      await prefs.remove(_kRodando);
      final anterior = _relatorioAnterior ?? '';
      final linha =
          'TESTE $caiu: O APP FECHOU durante a cena (nao voltou; migalha '
          'encontrada na abertura seguinte).';
      _relatorioAnterior = anterior.isEmpty ? linha : '$anterior\n$linha';
      await prefs.setString(_kRelatorio, _relatorioAnterior!);
    }
    if (mounted) setState(() {});
  }

  /// Durante o dispose o State ainda esta montado, mas setState nele ja e
  /// erro: sair da tela no meio de uma cena disparava a assercao.
  bool _descartando = false;

  @override
  void dispose() {
    _cancelar = true;
    _descartando = true;
    _desmontar();
    super.dispose();
  }

  // ---------------------------------------------------------- arquivos

  Future<Directory> _pasta() async {
    final tmp = await getTemporaryDirectory();
    final d = Directory('${tmp.path}/estresse3d');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  /// Seis PNGs de 2048 x 2048 desenhados na hora.
  Future<List<String>> _gerarTexturas() async {
    final d = await _pasta();
    final caminhos = <String>[];
    for (var i = 0; i < 6; i++) {
      final f = File('${d.path}/tex_$i.png');
      if (!f.existsSync()) {
        final rec = ui.PictureRecorder();
        final c = ui.Canvas(rec);
        const lado = 2048.0;
        final cores = [
          const Color(0xFFE94F64),
          const Color(0xFF52B9F2),
          const Color(0xFF6ED37A),
          const Color(0xFFF2A950),
          const Color(0xFFA57BF2),
          const Color(0xFFF2F2F2),
        ];
        c.drawRect(
          const ui.Rect.fromLTWH(0, 0, lado, lado),
          ui.Paint()
            ..shader = ui.Gradient.linear(
              ui.Offset.zero,
              const ui.Offset(lado, lado),
              [cores[i], cores[(i + 2) % cores.length]],
            ),
        );
        final rnd = math.Random(i * 97 + 1);
        final p = ui.Paint();
        for (var k = 0; k < 900; k++) {
          p.color = cores[rnd.nextInt(cores.length)].withValues(alpha: .55);
          c.drawCircle(
            ui.Offset(rnd.nextDouble() * lado, rnd.nextDouble() * lado),
            8 + rnd.nextDouble() * 70,
            p,
          );
        }
        final img = await rec.endRecording().toImage(2048, 2048);
        final png = await img.toByteData(format: ui.ImageByteFormat.png);
        img.dispose();
        if (png == null) continue;
        await f.writeAsBytes(png.buffer.asUint8List(), flush: true);
      }
      caminhos.add(f.path);
    }
    return caminhos;
  }

  /// Dez segundos de `testsrc2` em 720p, MPEG-4 parte 2 (que o FFmpeg
  /// "full" tem e os dois players tocam).
  Future<String?> _gerarVideo() async {
    final d = await _pasta();
    final f = File('${d.path}/video.mp4');
    if (f.existsSync() && f.lengthSync() > 10000) return f.path;
    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-f',
      'lavfi',
      '-i',
      'testsrc2=size=1280x720:rate=30',
      '-t',
      '10',
      '-c:v',
      'mpeg4',
      '-q:v',
      '3',
      '-pix_fmt',
      'yuv420p',
      f.path,
    ]);
    if (ReturnCode.isSuccess(await session.getReturnCode()) && f.existsSync()) {
      return f.path;
    }
    return null;
  }

  // ------------------------------------------------- bancada do nucleo

  /// Monta [projeto] no palco desta tela, num container so dele.
  Future<void> _montarProjeto(VideoProject projeto) async {
    final container = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(_prefs!)],
    );
    container.read(editorControllerProvider.notifier).openProject(projeto);
    _layers = container.read(editorControllerProvider).layers;
    final playback = PlaybackController(
      vsync: this,
      durationOf: () => duracaoDaBancada,
    );
    final videos = VideoLayerManager();
    _ouvinteDoTempo = () {
      videos.sync(_layers, playback.time.value, playback.playing.value);
    };
    playback.time.addListener(_ouvinteDoTempo!);
    playback.loop.value = true;
    setState(() {
      _container = container;
      _playback = playback;
      _videos = videos;
    });
    await Future<void>.delayed(const Duration(milliseconds: 800));
  }

  Future<void> _rodarBancada() async {
    if (_rodando) return;
    final prefs = _prefs;
    if (prefs == null) return;
    setState(() {
      _rodando = true;
      _cancelar = false;
      _modoBancada = true;
      _resultados.clear();
      _linhasDaBancada.clear();
      _status = 'Preparando o video de teste...';
    });
    await prefs.remove(_kRelatorio);
    try {
      _video = await _gerarVideo();
    } catch (_) {
      _video = null;
    }
    for (var i = 0; i < receitasDaBancada.length; i++) {
      if (_cancelar || !mounted) break;
      final r = receitasDaBancada[i];
      setState(() {
        _indice = i;
        _status = 'Bancada ${r.letra}: ${r.titulo}';
      });
      await prefs.setString(_kRodando, 'bancada ${r.letra} (${r.titulo})');
      _linhasDaBancada.addAll(await _medirCenaDaBancada(r));
      await prefs.remove(_kRodando);
      await prefs.setString(_kRelatorio, _relatorioDaBancada());
      if (mounted) setState(() {});
    }

    // VAZAMENTO: a mesma cena montada e desmontada oito vezes. O que se le
    // e a tendencia da memoria, nao o valor.
    if (!_cancelar && mounted) {
      setState(() {
        _indice = receitasDaBancada.length;
        _status = 'Ciclos de memoria: cena D montada e desmontada 8 vezes';
      });
      await prefs.setString(_kRodando, 'bancada: ciclos de memoria (cena D)');
      final rss = <int>[];
      for (var k = 0; k < 8 && !_cancelar && mounted; k++) {
        await _montarProjeto(
          montarCenaDaBancada(CenaDaBancada.d, video: _video),
        );
        _playback?.play();
        await Future<void>.delayed(const Duration(seconds: 2));
        _playback?.pause();
        _desmontar();
        await Future<void>.delayed(const Duration(milliseconds: 1500));
        rss.add(_rssMb());
      }
      await prefs.remove(_kRodando);
      _linhasDaBancada.add(CiclosDeMemoria(rss).linha);
    }
    await prefs.setString(_kRelatorio, _relatorioDaBancada());
    if (mounted) {
      setState(() {
        _rodando = false;
        _indice = -1;
        _status = _cancelar
            ? 'Cancelado.'
            : 'Terminado. Copie o relatorio e envie.';
      });
    }
  }

  /// Uma cena: oito segundos tocando, seis arrastando o cursor.
  Future<List<String>> _medirCenaDaBancada(ReceitaDaBancada r) async {
    final controlador = ControladorDeQualidade3D.instancia;
    controlador.zerar();
    final semVideo = r.video && _video == null;
    await _montarProjeto(montarCenaDaBancada(r.id, video: _video));
    final playback = _playback!;
    final rssInicio = _rssMb();
    _rssPico = rssInicio;
    _amostrador = Timer.periodic(const Duration(milliseconds: 500), (_) {
      final rss = _rssMb();
      if (rss > _rssPico) _rssPico = rss;
    });

    var fase = <QuadroMedido>[];
    _cb = (timings) {
      for (final t in timings) {
        fase.add(
          QuadroMedido(
            buildMs: t.buildDuration.inMicroseconds / 1000.0,
            rasterMs: t.rasterDuration.inMicroseconds / 1000.0,
            totalMs: t.totalSpan.inMicroseconds / 1000.0,
          ),
        );
      }
    };
    SchedulerBinding.instance.addTimingsCallback(_cb!);

    // Os tempos de quadro chegam em lote (no release, cerca de um por
    // segundo): depois de cada fase o palco fica parado ate o lote dela
    // chegar, e so entao a lista troca.
    Future<MedidaDaBancada> medir(Future<void> Function() corpo) async {
      final quadros = <QuadroMedido>[];
      fase = quadros;
      final relogio = Stopwatch()..start();
      await corpo();
      relogio.stop();
      await Future<void>.delayed(const Duration(milliseconds: 1600));
      return MedidaDaBancada(
        quadros,
        segundos: relogio.elapsedMicroseconds / 1e6,
      );
    }

    final tocando = await medir(() async {
      playback.play();
      await Future<void>.delayed(const Duration(seconds: 8));
      playback.pause();
    });
    final arrastando = await medir(() async {
      final arrasto = createTicker(
        (decorrido) => playback.seek(tempoDoArrasto(decorrido)),
      );
      _arrasto = arrasto;
      arrasto.start();
      await Future<void>.delayed(const Duration(seconds: 6));
      arrasto.dispose();
      _arrasto = null;
    });
    final motor = r.id == CenaDaBancada.d || r.id == CenaDaBancada.e
        ? (PreviewStats.cena3d.value?.toString() ?? Scene3DGpu.comoDesenha)
        : null;
    _desmontar();
    await Future<void>.delayed(const Duration(milliseconds: 1500));
    return [
      '${r.letra} — ${r.titulo}${semVideo ? ' (SEM VIDEO: o FFmpeg nao gerou)' : ''}',
      '   ${tocando.linha('tocando')}',
      '   ${arrastando.linha('arrastando')}',
      '   memoria: inicio $rssInicio MB · pico $_rssPico MB · fim ${_rssMb()} MB',
      if (motor != null) '   3D: $motor',
    ];
  }

  String _relatorioDaBancada() {
    final c = ControladorDeQualidade3D.instancia;
    final b = StringBuffer()
      ..writeln('AUREA — BANCADA DO NUCLEO (motor atual)')
      ..writeln(
        '${Platform.operatingSystem} ${Platform.operatingSystemVersion}',
      )
      ..writeln(
        'RAM ${c.ramBytes > 0 ? bytesLegiveis(c.ramBytes) : '?'} · '
        'motor 3D ${Scene3DGpu.comoDesenha} · '
        'video de teste: ${_video != null ? 'sim' : 'nao'}',
      )
      ..writeln('UI = fio da interface (build); raster = fio da GPU')
      ..writeln('');
    for (final l in _linhasDaBancada) {
      b.writeln(l);
    }
    return b.toString();
  }

  // ------------------------------------------------------------ o laco

  Future<void> _rodarTudo() async {
    if (_rodando) return;
    final prefs = _prefs;
    if (prefs == null) return;
    setState(() {
      _rodando = true;
      _cancelar = false;
      _modoBancada = false;
      _resultados.clear();
      _status = 'Preparando texturas e video de teste...';
    });
    await prefs.remove(_kRelatorio);
    try {
      _texturas = await _gerarTexturas();
    } catch (e) {
      _texturas = const [];
    }
    try {
      _video = await _gerarVideo();
    } catch (_) {
      _video = null;
    }
    final receitas = receitasDeEstresse(texturas: _texturas);
    for (var i = 0; i < receitas.length; i++) {
      if (_cancelar || !mounted) break;
      setState(() {
        _indice = i;
        _status = 'Teste ${i + 1} de ${receitas.length}: ${receitas[i].titulo}';
      });
      final r = await _rodar(receitas[i]);
      _resultados.add(r);
      await prefs.setString(_kRelatorio, _relatorio());
      if (mounted) setState(() {});
    }
    if (mounted) {
      setState(() {
        _rodando = false;
        _indice = -1;
        _status = _cancelar
            ? 'Cancelado.'
            : 'Terminado. Copie o relatorio e envie.';
      });
    }
  }

  Future<ResultadoDeEstresse> _rodar(ReceitaDeEstresse receita) async {
    final prefs = _prefs!;
    final controlador = ControladorDeQualidade3D.instancia;
    await prefs.setString(_kRodando, '${receita.numero} (${receita.titulo})');
    controlador.zerar();

    // O projeto, num container so dele: o editor da pessoa nao e tocado.
    final container = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
    );
    final c = container.read(editorControllerProvider.notifier);
    final obs = <String>[];
    if (receita.video) {
      final v = _video;
      if (v != null) {
        c.addVideoLayer(
          Duration.zero,
          v,
          'Video de teste',
          const Duration(seconds: 10),
        );
      } else {
        obs.add('sem video (o FFmpeg nao gerou o arquivo)');
      }
    }
    c.addScene3DLayer(Duration.zero);
    final id3d = container.read(editorControllerProvider).layers.last.id;
    c.updateScene3D(id3d, (_) => receita.cena);
    if (receita.texto) {
      c.addTextLayer(Duration.zero, text: 'AUREA · TESTE DE ESTRESSE');
    }
    if (receita.efeitos || receita.motionGraph) {
      c.addShapeLayer(Duration.zero, name: 'Estresse');
      final p = container.read(editorControllerProvider);
      c.openProject(
        p.copyWith(
          layers: [
            for (final l in p.layers)
              if (l is ShapeLayer && l.name.startsWith('Estresse'))
                l.copyLayer(
                  // Efeitos que EXISTEM no catalogo de hoje. Light Glow, Glow
                  // Volumetrico e Film Grain sairam dele quando o catalogo foi
                  // refeito, e o EffectInstance deles lancava: as cenas com
                  // efeito paravam no meio sem relatorio.
                  effects: receita.efeitos
                      ? [
                          EffectInstance(type: EffectType.unsharpMask),
                          EffectInstance(type: EffectType.vignette),
                          EffectInstance(type: EffectType.vhsDamage),
                        ]
                      : const [],
                  rotation: receita.motionGraph
                      ? AnimatedDouble(
                          0,
                          [
                            for (var k = 0; k <= 50; k++)
                              Keyframe(
                                time: Duration(milliseconds: k * 100),
                                value: (k.isEven ? 1 : -1) * 20.0 * (k % 7),
                              ),
                          ],
                          LoopSpec.none,
                          'value + wiggle(3, 40)',
                        )
                      : null,
                )
              else
                l,
          ],
        ),
      );
    }
    if (receita.id == TesteDeEstresse.texturasGrandes && _texturas.isEmpty) {
      obs.add('sem texturas (nao foi possivel gerar os PNGs)');
    }

    final projeto = container.read(editorControllerProvider);
    var duracao = Duration.zero;
    for (final l in projeto.layers) {
      final fim = l.startTime + l.duration;
      if (fim > duracao) duracao = fim;
    }
    if (duracao <= Duration.zero) duracao = const Duration(seconds: 5);
    _layers = projeto.layers;

    final playback = PlaybackController(vsync: this, durationOf: () => duracao);
    final videos = VideoLayerManager();
    _ouvinteDoTempo = () {
      videos.sync(_layers, playback.time.value, playback.playing.value);
    };
    playback.time.addListener(_ouvinteDoTempo!);
    playback.loop.value = true;

    // Monta o palco e comeca a medir.
    _quadrosMs.clear();
    _rssInicio = _rssMb();
    _rssPico = _rssInicio;
    _pressaoPior = NivelDePressao.seguro;
    final nivelInicial = controlador.nivel.value;
    _cb = (timings) {
      for (final t in timings) {
        _quadrosMs.add(t.totalSpan.inMicroseconds / 1000.0);
      }
    };
    SchedulerBinding.instance.addTimingsCallback(_cb!);
    _amostrador = Timer.periodic(const Duration(milliseconds: 500), (_) {
      final rss = _rssMb();
      if (rss > _rssPico) _rssPico = rss;
      final p = controlador.pressao.value;
      if (p.index > _pressaoPior.index) _pressaoPior = p;
    });
    setState(() {
      _container = container;
      _playback = playback;
      _videos = videos;
    });
    await Future<void>.delayed(const Duration(milliseconds: 600));
    playback.play();
    final fim = DateTime.now().add(_duracaoDoTeste);
    while (DateTime.now().isBefore(fim) && !_cancelar && mounted) {
      await Future<void>.delayed(const Duration(milliseconds: 250));
    }
    playback.pause();
    final desenhou =
        PreviewStats.cena3d.value?.toString() ??
        'pintor em CPU (${Scene3DGpu.comoDesenha}: ${Scene3DGpu.motivo})';
    final motor = PreviewStats.cena3d.value?.motor ?? Scene3DGpu.comoDesenha;
    final estimada = bytesLegiveis(controlador.estimativa.value.total);
    final transicoes = List<String>.from(controlador.historico);
    _desmontar();
    await Future<void>.delayed(const Duration(milliseconds: 1500));
    final rssFim = _rssMb();

    final ordenados = List<double>.from(_quadrosMs)..sort();
    double em(double f) => ordenados.isEmpty
        ? 0
        : ordenados[(f * (ordenados.length - 1)).round().clamp(
            0,
            ordenados.length - 1,
          )];
    final travadas = _quadrosMs.where((ms) => ms > 34).length;
    await prefs.remove(_kRodando);
    return ResultadoDeEstresse(
      numero: receita.numero,
      titulo: receita.titulo,
      sobreviveu: true,
      quadros: _quadrosMs.length,
      medianaMs: em(.5),
      p95Ms: em(.95),
      piorMs: ordenados.isEmpty ? 0 : ordenados.last,
      travadas: travadas,
      rssInicioMb: _rssInicio,
      rssPicoMb: _rssPico,
      rssFimMb: rssFim,
      nivelInicial: qualidade3dRotulo(nivelInicial),
      nivelFinal: qualidade3dRotulo(controlador.nivel.value),
      pressaoPior: nivelDePressaoRotulo(_pressaoPior),
      motor: motor,
      gpuEstimada: estimada,
      desenhou: desenhou,
      transicoes: transicoes,
      observacao: obs.join('; '),
    );
  }

  void _desmontar() {
    _arrasto?.dispose();
    _arrasto = null;
    final cb = _cb;
    if (cb != null) SchedulerBinding.instance.removeTimingsCallback(cb);
    _cb = null;
    _amostrador?.cancel();
    _amostrador = null;
    final playback = _playback;
    final videos = _videos;
    final container = _container;
    final ouvinte = _ouvinteDoTempo;
    if (playback != null && ouvinte != null) {
      playback.time.removeListener(ouvinte);
    }
    _ouvinteDoTempo = null;
    if (mounted && !_descartando) {
      setState(() {
        _container = null;
        _playback = null;
        _videos = null;
      });
    } else {
      _container = null;
      _playback = null;
      _videos = null;
    }
    // Descarta depois do quadro que tirou o palco da tela: o palco ainda
    // le o controlador e o player neste quadro.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      playback?.dispose();
      videos?.dispose();
      container?.dispose();
    });
    _layers = const [];
  }

  int _rssMb() {
    try {
      return ProcessInfo.currentRss ~/ (1024 * 1024);
    } catch (_) {
      return 0;
    }
  }

  // --------------------------------------------------------- relatorio

  String _relatorio() {
    final c = ControladorDeQualidade3D.instancia;
    final b = StringBuffer();
    b.writeln('AUREA — TESTE DE ESTRESSE DO MOTOR 3D');
    b.writeln('${Platform.operatingSystem} ${Platform.operatingSystemVersion}');
    b.writeln(
      'RAM ${c.ramBytes > 0 ? bytesLegiveis(c.ramBytes) : '?'} · '
      'orcamento de GPU ${bytesLegiveis(c.orcamentoBytes)} · '
      'teto ${tetoDeQualidade3dRotulo(c.teto)} · motor ${Scene3DGpu.comoDesenha}',
    );
    b.writeln(
      'texturas de teste: ${_texturas.length} · video de teste: ${_video != null ? 'sim' : 'nao'}',
    );
    b.writeln('');
    for (final r in _resultados) {
      b.writeln(r.linha);
    }
    final vivos = _resultados.where((r) => r.sobreviveu).length;
    b.writeln('');
    b.writeln(
      'RESUMO: $vivos de ${_resultados.length} cenas sem fechar o app.',
    );
    return b.toString();
  }

  // ---------------------------------------------------------------- UI

  @override
  Widget build(BuildContext context) {
    final container = _container;
    final playback = _playback;
    final videos = _videos;
    final relatorio = _modoBancada && _linhasDaBancada.isNotEmpty
        ? _relatorioDaBancada()
        : _resultados.isEmpty
        ? (_relatorioAnterior ?? '')
        : _relatorio();
    return Scaffold(
      appBar: AppBar(title: const AppText('Teste de estresse 3D')),
      body: Column(
        children: [
          if (container != null && playback != null && videos != null)
            AspectRatio(
              aspectRatio: 16 / 9,
              child: UncontrolledProviderScope(
                container: container,
                child: CompositionView(
                  time: playback.time,
                  videos: videos,
                  selectedId: null,
                ),
              ),
            )
          else
            AspectRatio(
              aspectRatio: 16 / 9,
              child: ColoredBox(
                color: Color(0xFF0B0E14),
                child: Center(
                  child: AppText(
                    'O palco aparece aqui durante cada cena',
                    style: TextStyle(color: AppColors.muted),
                  ),
                ),
              ),
            ),
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
            // Status numa linha, botoes na de baixo: dois botoes e o status
            // na mesma linha nao cabem num celular estreito.
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                AppText(
                  _status,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(fontSize: 13, color: AppColors.muted),
                ),
                const SizedBox(height: 8),
                if (_rodando)
                  Align(
                    alignment: Alignment.centerRight,
                    child: TextButton(
                      onPressed: () => _cancelar = true,
                      child: const AppText('Cancelar'),
                    ),
                  )
                else
                  Row(
                    children: [
                      Expanded(
                        child: OutlinedButton(
                          onPressed: _prefs == null ? null : _rodarBancada,
                          child: const AppText(
                            'Bancada A-E',
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: FilledButton(
                          onPressed: _prefs == null ? null : _rodarTudo,
                          child: AppText(
                            _resultados.isEmpty
                                ? 'Rodar os nove'
                                : 'Rodar de novo',
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                      ),
                    ],
                  ),
              ],
            ),
          ),
          if (_indice >= 0)
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16),
              child: LinearProgressIndicator(
                value:
                    (_indice + 1) /
                    (_modoBancada ? receitasDaBancada.length + 1 : 9),
              ),
            ),
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(16),
              child: SelectableText(
                relatorio.isEmpty
                    ? 'O relatorio aparece aqui. Se o app fechar durante uma '
                          'cena, reabra esta tela: a cena que derrubou o app '
                          'fica registrada.'
                    : relatorio,
                style: const TextStyle(
                  fontSize: 12,
                  height: 1.4,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ),
          ),
          SafeArea(
            top: false,
            child: Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
              child: Row(
                children: [
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: relatorio.isEmpty
                          ? null
                          : () async {
                              await Clipboard.setData(
                                ClipboardData(text: relatorio),
                              );
                              if (context.mounted) {
                                ScaffoldMessenger.of(context).showSnackBar(
                                  const SnackBar(
                                    content: AppText('Relatorio copiado.'),
                                  ),
                                );
                              }
                            },
                      icon: const Icon(Icons.copy_rounded),
                      label: const AppText('Copiar relatorio'),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
