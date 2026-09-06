import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' show BlendMode, Offset;

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/domain/cut.dart';
import '../../editor/domain/cut_ops.dart';
import '../../editor/domain/audio_mix.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/mask.dart';
import '../../editor/domain/video_project.dart';
import '../domain/export_settings.dart';
import 'platform_encoder.dart';

/// EXPORTACAO DE VIDEO — as partes que nao dependem da tela.
///
/// O problema: a composicao da Aurea e desenhada em Flutter (texto,
/// formas, 3D, efeitos), e video de verdade vive numa textura de
/// plataforma, que NAO entra no `toImage` de um RepaintBoundary. Entao
/// nao da para simplesmente "gravar a tela".
///
/// A saida e inverter: o FFmpeg extrai os quadros de cada camada de
/// video para o disco, o Flutter compoe cada quadro da composicao com
/// esses quadros ja decodificados, e o CODIFICADOR DA PLATAFORMA grava o
/// arquivo. Assim TUDO que aparece no preview aparece no arquivo —
/// inclusive efeito aplicado em cima de video.
///
/// Quem codifica e o MediaCodec (Android) ou o AVAssetWriter (iOS), nao
/// o x264: e hardware, e nao arrasta a GPL para dentro do aplicativo. O
/// FFmpeg segue no app so para DECODIFICAR e para juntar o audio.
class ExportEngine {
  ExportEngine(
    this.project, [
    this.settings = const ExportSettings(),
    this.duckEnvelopes = const <String, DuckEnvelope>{},
  ]);

  final VideoProject project;
  final ExportSettings settings;

  /// O ENVELOPE DE DUCKING de cada trilha, ja calculado pelo mesmo codigo
  /// que o preview usa. Vem de fora de proposito: o motor de exportacao
  /// nao pode CALCULAR o abaixamento, senao calcularia diferente do
  /// tocador — o contrato e que os dois leiam os mesmos pontos.
  final Map<String, DuckEnvelope> duckEnvelopes;

  Directory? _work;
  bool _cancelled = false;

  void cancel() {
    _cancelled = true;
    PlatformEncoder.cancel();
  }

  bool get cancelled => _cancelled;

  int get fps => settings.resolveFps(project.fps);

  /// Tamanho de SAIDA (pode ser diferente do projeto). A composicao
  /// continua sendo desenhada no tamanho dela; quem redimensiona e o
  /// codificador, com a proporcao preservada.
  int get width =>
      settings.resolve(project.outputWidth, project.outputHeight).$1;
  int get height =>
      settings.resolve(project.outputWidth, project.outputHeight).$2;

  /// Quantos quadros a composicao inteira tem.
  int get frameCount {
    final us = project.duration.inMicroseconds;
    if (us <= 0) return 0;
    return (us * fps / 1000000).round().clamp(1, 60 * 60 * 60);
  }

  Duration timeOfFrame(int i) =>
      Duration(microseconds: (i * 1000000 / fps).round());

  Future<Directory> workDir() async {
    if (_work != null) return _work!;
    final tmp = await getTemporaryDirectory();
    final d = Directory('${tmp.path}/aurea_export');
    if (d.existsSync()) d.deleteSync(recursive: true);
    d.createSync(recursive: true);
    return _work = d;
  }

  Future<void> cleanup() async {
    try {
      _work?.deleteSync(recursive: true);
    } catch (_) {}
  }

  // ------------------------------------------------- CORTE PURO

  /// CORTE PURO: a composicao e um clipe so, sem nada por cima. Nesse
  /// caso recodificar e desperdicio — da para copiar as trilhas e sair
  /// quase instantaneo, com a qualidade intacta.
  ///
  /// Basta UMA coisa fora do lugar (um efeito, uma mascara, opacidade
  /// diferente, transformacao mexida) para deixar de valer, porque ai o
  /// arquivo teria de mostrar algo que a fonte nao tem.
  VideoLayer? get pureCutSource {
    if (project.layers.length != 1) return null;
    final l = project.layers.first;
    if (l is! VideoLayer) return null;

    // O remux nao desenha a composicao: o clipe precisa cobrir exatamente
    // todo o relogio. Sem isto, um clipe deslocado perderia o preto inicial
    // (ou o preenchimento final imposto pela duracao minima do projeto).
    if (l.startTime != Duration.zero || l.endTime != project.duration) {
      return null;
    }

    // Conservador de proposito: qualquer estado visual que o remux nao
    // consegue reproduzir manda a exportacao para o compositor de quadros.
    if (l.effects.isNotEmpty) return null;
    if (l.masks.isNotEmpty) return null;
    if (l.matteMode != MatteMode.none || l.matteSourceId != null) return null;
    if (l.blendMode != BlendMode.srcOver || l.customBlend != null) return null;
    // Velocidade diferente de 1 nao e copia: tem de renderizar.
    if (l.speed != 1.0) return null;
    if (l.reverse || l.speedBlur || hasTimeRemap(l)) return null;
    if (l.transitionIn != null) return null;

    // Remux tambem copiaria o audio original sem estes ajustes.
    if (l.volume != 1.0 || !l.audio.isNeutral) return null;

    // Vinculos, dados e metadados podem mudar o resultado fora da camada.
    if (project.links.isNotEmpty) return null;
    if (project.bindings.any((binding) => binding.layerId == l.id)) return null;
    if (!project.metaOf(l.id).isEmpty) return null;

    // Qualquer transformacao mexida muda o quadro: nao e mais copia.
    if (l.opacity.isAnimated || l.opacity.base != 1) return null;
    if (l.position.base != Offset.zero) return null;
    if (l.scaleX.isAnimated || l.scaleX.base != 1) return null;
    if (l.scaleY.isAnimated || l.scaleY.base != 1) return null;
    if (l.rotation.isAnimated || l.rotation.base != 0) return null;
    if (l.position.isAnimated) return null;
    if (l.rotationX.isAnimated || l.rotationX.base != 0) return null;
    if (l.rotationY.isAnimated || l.rotationY.base != 0) return null;
    if (l.skewX.isAnimated || l.skewX.base != 0) return null;
    if (l.skewY.isAnimated || l.skewY.base != 0) return null;
    if (l.pivot.isAnimated || l.pivot.base != Offset.zero) return null;
    if (l.is3D || l.positionZ.isAnimated || l.positionZ.base != 0) return null;

    return l;
  }

  /// Tenta o caminho rapido. Devolve o arquivo, ou null se nao coube.
  Future<File?> tryPureCut() async {
    // A tela faz a mesma triagem, mas o motor tambem se protege para que
    // nenhum outro chamador remuxe ignorando resolucao, fps ou codec pedidos.
    if (settings.format != ExportFormat.mp4 ||
        settings.size != ExportSize.original ||
        settings.codec != ExportCodec.h264 ||
        settings.fps != null ||
        settings.bitrateMbps != null) {
      return null;
    }
    final l = pureCutSource;
    if (l == null) return null;
    if (!await PlatformEncoder.available) return null;

    final file = await _outputFile();
    final ok = await PlatformEncoder.remux(
      source: l.sourcePath,
      target: file.path,
      start: l.sourceOffset,
      end: l.sourceOffset + l.duration,
    );
    if (!ok || !file.existsSync() || file.lengthSync() < 1024) return null;
    return file;
  }

  // ------------------------------------------- quadros das camadas

  /// Extrai os quadros de UMA camada de video, ja no fps da composicao e
  /// so o trecho usado. Devolve a pasta com `%06d.jpg`.
  ///
  /// Isto e DECODIFICACAO — nao precisa de codec GPL.
  Future<Directory> extractVideoFrames(
    VideoLayer layer, {
    void Function(double p)? onProgress,
  }) async {
    final work = await workDir();
    final dir = Directory('${work.path}/v_${layer.id}');
    dir.createSync(recursive: true);

    final range = videoFrameRange(layer);
    final start = range.$1.inMicroseconds / 1000000.0;
    final dur = (range.$2 - range.$1).inMicroseconds / 1000000.0;

    // Escala para caber na composicao mantendo proporcao — quadro maior
    // que isso e memoria jogada fora.
    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-ss',
      start.toStringAsFixed(6),
      '-t',
      dur.toStringAsFixed(6),
      '-i',
      layer.sourcePath,
      '-vf',
      'fps=$fps,scale=$width:$height:force_original_aspect_ratio='
          'decrease',
      '-q:v',
      '3',
      '-start_number',
      '0',
      '${dir.path}/%06d.jpg',
    ]);
    if (!ReturnCode.isSuccess(await session.getReturnCode())) {
      final log = await session.getAllLogsAsString();
      throw ExportException(
        'Falha ao ler o video "${layer.name}".\n${_tail(log)}',
      );
    }
    onProgress?.call(1);
    return dir;
  }

  /// Trecho bruto necessario. A escolha de quadro fica para a funcao pura
  /// de Time Remap; extrair sem setpts cobre rampa, reverso e handles.
  (Duration, Duration) videoFrameRange(VideoLayer layer) {
    var firstLocal = Duration.zero;
    var lastLocal = layer.duration;
    final incoming = layer.transitionIn;
    if (incoming != null &&
        incoming.enabled &&
        videoAfter(project.layers, incoming.outgoingLayerId)?.id == layer.id) {
      final w = incoming.windowAt(layer.startTime);
      if (w.start < layer.startTime && !incoming.freezeEdges) {
        firstLocal = w.start - layer.startTime;
      }
    }
    for (final candidate in project.layers.whereType<VideoLayer>()) {
      final transition = candidate.transitionIn;
      if (transition?.outgoingLayerId != layer.id ||
          transition == null ||
          !transition.enabled ||
          videoAfter(project.layers, layer.id)?.id != candidate.id) {
        continue;
      }
      final w = transition.windowAt(candidate.startTime);
      if (w.end > layer.endTime && !transition.freezeEdges) {
        lastLocal = w.end - layer.startTime;
      }
    }

    Duration? lo;
    Duration? hi;

    void include(Duration value) {
      if (lo == null || value < lo!) lo = value;
      if (hi == null || value > hi!) hi = value;
    }

    // Usa exatamente a mesma grade global que sera desenhada. Uma rampa
    // curta (ou um hold entre dois keyframes proximos) pode desaparecer
    // numa amostragem fixa de 96 pontos; percorrer os quadros de saida
    // garante que todo source-time pedido pela tela foi extraido.
    final globalStart = layer.startTime + firstLocal;
    final globalEnd = layer.startTime + lastLocal;
    final firstFrame = math
        .max(0, (globalStart.inMicroseconds * fps / 1000000).floor() - 1)
        .toInt();
    final lastFrame = math
        .min(
          math.max(0, frameCount - 1),
          (globalEnd.inMicroseconds * fps / 1000000).ceil() + 1,
        )
        .toInt();
    for (var i = firstFrame; i <= lastFrame; i++) {
      final global = timeOfFrame(i);
      if (!visibleForCut(project.layers, layer, global)) continue;
      final transition = transitionContextAt(project.layers, global);
      final local = localTimeForCut(layer, global, transition);
      include(videoAbsoluteSourceTimeAt(layer, local));
    }
    // Fallback para camadas menores que um quadro e inclui os limites
    // matematicos usados por handles fora da area visivel.
    include(videoAbsoluteSourceTimeAt(layer, firstLocal));
    include(videoAbsoluteSourceTimeAt(layer, lastLocal));

    final frame = Duration(microseconds: (1000000 / fps).ceil());
    final start = lo! < Duration.zero ? Duration.zero : lo!;
    final end = hi! + frame;
    return (start, end <= start ? start + frame : end);
  }

  // --------------------------------------------------------- audio

  /// A corrente de `atempo` para uma velocidade qualquer.
  ///
  /// O filtro so aceita 0,5..2 de cada vez: 4x sai de duas etapas de 2x.
  /// Uma etapa fora da faixa e ignorada em silencio pelo FFmpeg — e o
  /// audio fica fora de sincronia com o video sem ninguem entender por
  /// que.
  static String _atempo(double v) {
    if (v <= 0 || (v - 1).abs() < 0.001) return '';
    var resto = v;
    final etapas = <String>[];
    while (resto > 2.0 && etapas.length < 12) {
      etapas.add('atempo=2.0');
      resto /= 2.0;
    }
    while (resto < 0.5 && etapas.length < 12) {
      etapas.add('atempo=0.5');
      resto /= 0.5;
    }
    etapas.add('atempo=${resto.toStringAsFixed(4)}');
    return '${etapas.join(',')},';
  }

  static String _retimeAudio(double speed, bool preservePitch) {
    final v = speed.abs();
    if (v <= 0 || (v - 1).abs() < 0.001) return '';
    if (preservePitch) return _atempo(v);
    return 'asetrate=${(44100 * v).round()},aresample=44100,';
  }

  /// Aproxima a curva fonte-tempo em trechos curtos. Keyframes entram
  /// obrigatoriamente na particao; amostras intermediarias acompanham o
  /// easing sem depender de filtros de tempo variavel do dispositivo.
  List<({Duration timelineDuration, Duration sourceStart, Duration sourceEnd})>
  _audioRemapSegments(
    VideoLayer layer,
    Duration localStart,
    Duration localEnd, {
    bool freezeBefore = false,
    bool freezeAfter = false,
  }) {
    if (localEnd <= localStart) return const [];
    final times = <Duration>{localStart, localEnd};
    final track = timeRemapTrackOf(layer);
    if (track != null) {
      for (final keyframe in track.keyframes) {
        if (keyframe.time > localStart && keyframe.time < localEnd) {
          times.add(keyframe.time);
        }
      }
    }
    final spanUs = (localEnd - localStart).inMicroseconds;
    final steps = math.max(1, (spanUs / 200000).ceil());
    for (var i = 1; i < steps; i++) {
      times.add(
        localStart + Duration(microseconds: (spanUs * i / steps).round()),
      );
    }
    final sorted = times.toList()..sort();
    Duration sourceAt(Duration local) {
      var value = local;
      if (freezeBefore && value < Duration.zero) value = Duration.zero;
      if (freezeAfter && value > layer.duration) value = layer.duration;
      return videoAbsoluteSourceTimeAt(layer, value);
    }

    return [
      for (var i = 0; i + 1 < sorted.length; i++)
        (
          timelineDuration: sorted[i + 1] - sorted[i],
          sourceStart: sourceAt(sorted[i]),
          sourceEnd: sourceAt(sorted[i + 1]),
        ),
    ];
  }

  /// Camadas que carregam som. Mudo sai da conta aqui — nao adianta
  /// mixar uma faixa em volume zero e pagar por ela.
  List<Layer> get audioSources => [
    for (final l in project.layers)
      if (_specOf(l) != null && !_specOf(l)!.muted)
        if (l is AudioLayer || (l is VideoLayer && l.volume > 0.001)) l,
  ];

  static AudioSpec? _specOf(Layer l) => switch (l) {
    AudioLayer a => a.audio,
    VideoLayer v => v.audio,
    _ => null,
  };

  /// Monta as entradas e o grafo de mixagem. Cada faixa e cortada no
  /// trecho usado, atrasada ate a posicao dela na linha do tempo e
  /// ajustada no volume.
  ({List<String> inputs, String? filter, String? outLabel}) audioGraph(
    int firstInputIndex,
  ) {
    final sources = audioSources;
    if (sources.isEmpty) {
      return (inputs: <String>[], filter: null, outLabel: null);
    }

    final inputs = <String>[];
    final chains = <String>[];
    final labels = <String>[];
    final porCamada = <String, String>{};
    final duckAlvo = <String, String?>{};
    var idx = firstInputIndex;

    for (final l in sources) {
      final path = l is AudioLayer
          ? l.sourcePath
          : (l as VideoLayer).sourcePath;
      final volume = l is AudioLayer ? l.volume : (l as VideoLayer).volume;
      var offset = l is VideoLayer
          ? l.sourceOffset
          : (l as AudioLayer).sourceOffset;
      var timelineStart = l.startTime;
      var timelineEnd = l.endTime;
      var sourceDuration = switch (l) {
        VideoLayer v => videoSourceSpan(v),
        AudioLayer a => a.sourceSpan,
        _ => l.duration,
      };
      List<
        ({Duration timelineDuration, Duration sourceStart, Duration sourceEnd})
      >?
      remapSegments;
      ClipTransition? transitionIn;
      ClipTransition? transitionOut;
      if (l is VideoLayer) {
        final linkedIncoming = l.transitionIn;
        transitionIn =
            linkedIncoming != null &&
                linkedIncoming.enabled &&
                linkedIncoming.crossfadeAudio &&
                videoAfter(
                      project.layers,
                      linkedIncoming.outgoingLayerId,
                    )?.id ==
                    l.id
            ? linkedIncoming
            : null;
        if (transitionIn != null && transitionIn.enabled) {
          final w = transitionIn.windowAt(l.startTime);
          if (w.start < timelineStart) timelineStart = w.start;
        }
        for (final incoming in project.layers.whereType<VideoLayer>()) {
          final candidate = incoming.transitionIn;
          if (candidate?.outgoingLayerId == l.id &&
              candidate!.crossfadeAudio &&
              candidate.enabled &&
              videoAfter(project.layers, l.id)?.id == incoming.id) {
            transitionOut = candidate;
            final w = candidate.windowAt(incoming.startTime);
            if (w.end > timelineEnd) timelineEnd = w.end;
            break;
          }
        }
        var localStart = timelineStart - l.startTime;
        var localEnd = timelineEnd - l.startTime;
        final freezeBefore = transitionIn?.freezeEdges == true;
        final freezeAfter = transitionOut?.freezeEdges == true;
        if (hasTimeRemap(l) || l.reverse || freezeBefore || freezeAfter) {
          remapSegments = _audioRemapSegments(
            l,
            localStart,
            localEnd,
            freezeBefore: freezeBefore,
            freezeAfter: freezeAfter,
          );
          if (remapSegments.isNotEmpty) {
            var lo = remapSegments.first.sourceStart;
            var hi = lo;
            for (final segment in remapSegments) {
              for (final value in [segment.sourceStart, segment.sourceEnd]) {
                if (value < lo) lo = value;
                if (value > hi) hi = value;
              }
            }
            offset = lo;
            sourceDuration = hi - lo;
          }
        } else {
          final sourceA = videoAbsoluteSourceTimeAt(l, localStart);
          final sourceB = videoAbsoluteSourceTimeAt(l, localEnd);
          offset = sourceA <= sourceB ? sourceA : sourceB;
          sourceDuration = (sourceB - sourceA).abs();
        }
        if (sourceDuration < const Duration(milliseconds: 34)) {
          sourceDuration = const Duration(milliseconds: 34);
        }
      }
      final dur = (timelineEnd - timelineStart).inMicroseconds / 1000000.0;
      final delayMs = timelineStart.inMilliseconds.clamp(0, 1 << 31);

      inputs.addAll([
        '-ss',
        (offset.inMicroseconds / 1000000.0).toStringAsFixed(6),
        '-t',
        (sourceDuration.inMicroseconds / 1000000.0).toStringAsFixed(6),
        '-i',
        path,
      ]);

      final spec = _specOf(l) ?? const AudioSpec();
      final ganho = (volume * spec.gain).clamp(0.0, 12.0);

      // FADE de igual potencia: linear soa como buraco no meio, porque
      // o ouvido responde a potencia.
      final fades = <String>[];
      if (spec.fadeIn > Duration.zero) {
        final d = spec.fadeIn.inMilliseconds / 1000.0;
        fades.add('afade=t=in:st=0:d=${d.toStringAsFixed(3)}:curve=qsin');
      }
      if (spec.fadeOut > Duration.zero) {
        final d = spec.fadeOut.inMilliseconds / 1000.0;
        final st = (dur - d).clamp(0.0, dur);
        fades.add(
          'afade=t=out:st=${st.toStringAsFixed(3)}'
          ':d=${d.toStringAsFixed(3)}:curve=qsin',
        );
      }
      if (transitionIn != null) {
        final w = transitionIn.windowAt(l.startTime);
        final st = (w.start - timelineStart).inMicroseconds / 1000000.0;
        final d = w.duration.inMicroseconds / 1000000.0;
        fades.add(
          'afade=t=in:st=${st.toStringAsFixed(3)}'
          ':d=${d.toStringAsFixed(3)}:curve=qsin',
        );
      }
      if (transitionOut != null) {
        VideoLayer? incoming;
        for (final candidate in project.layers.whereType<VideoLayer>()) {
          if (candidate.transitionIn == transitionOut) incoming = candidate;
        }
        if (incoming != null) {
          final w = transitionOut.windowAt(incoming.startTime);
          final st = (w.start - timelineStart).inMicroseconds / 1000000.0;
          final d = w.duration.inMicroseconds / 1000000.0;
          fades.add(
            'afade=t=out:st=${st.toStringAsFixed(3)}'
            ':d=${d.toStringAsFixed(3)}:curve=qsin',
          );
        }
      }

      final vel = switch (l) {
        VideoLayer _ =>
          sourceDuration.inMicroseconds /
              math.max(1, (timelineEnd - timelineStart).inMicroseconds),
        AudioLayer a => a.speed,
        _ => 1.0,
      };
      final label = 'a$idx';
      final post =
          'volume=${ganho.toStringAsFixed(3)}'
          '${fades.isEmpty ? '' : ',${fades.join(',')}'},'
          'adelay=$delayMs|$delayMs,'
          'apad=whole_dur=${_total.toStringAsFixed(3)}';
      if (l is VideoLayer &&
          remapSegments != null &&
          remapSegments.isNotEmpty) {
        final mediaSegments = [
          for (final segment in remapSegments)
            if ((segment.sourceEnd - segment.sourceStart).abs() >=
                const Duration(microseconds: 24))
              segment,
        ];
        final rawLabels = <String>[];
        if (mediaSegments.length == 1) {
          rawLabels.add('araw_${idx}_0');
          chains.add(
            '[$idx:a]aresample=44100,asetpts=PTS-STARTPTS,'
            'aformat=sample_rates=44100:channel_layouts=stereo'
            '[${rawLabels.first}]',
          );
        } else if (mediaSegments.length > 1) {
          for (var i = 0; i < mediaSegments.length; i++) {
            rawLabels.add('araw_${idx}_$i');
          }
          chains.add(
            '[$idx:a]aresample=44100,asetpts=PTS-STARTPTS,'
            'aformat=sample_rates=44100:channel_layouts=stereo,'
            'asplit=${rawLabels.length}'
            '${rawLabels.map((name) => '[$name]').join()}',
          );
        }

        final segmentLabels = <String>[];
        var mediaIndex = 0;
        for (var i = 0; i < remapSegments.length; i++) {
          final segment = remapSegments[i];
          final dt = segment.timelineDuration.inMicroseconds / 1000000.0;
          final sourceDelta = segment.sourceEnd - segment.sourceStart;
          final ds = sourceDelta.inMicroseconds.abs() / 1000000.0;
          final segmentLabel = 'aseg_${idx}_$i';
          segmentLabels.add(segmentLabel);
          // Hold nao repete uma amostra (o que produziria um tom); ele
          // gera silencio com a duracao exata. O limiar e uma amostra em
          // 44,1 kHz, preservando inclusive rampas muito lentas.
          if (ds < 0.000024 || dt <= 0) {
            chains.add(
              'anullsrc=r=44100:cl=stereo:d=${dt.toStringAsFixed(6)}'
              '[$segmentLabel]',
            );
            continue;
          }
          final sourceFirst = segment.sourceStart <= segment.sourceEnd
              ? segment.sourceStart
              : segment.sourceEnd;
          final sourceLast = segment.sourceStart <= segment.sourceEnd
              ? segment.sourceEnd
              : segment.sourceStart;
          final relativeFirst =
              (sourceFirst - offset).inMicroseconds / 1000000.0;
          final relativeLast = (sourceLast - offset).inMicroseconds / 1000000.0;
          final rate = ds / dt;
          final direction = sourceDelta < Duration.zero ? 'areverse,' : '';
          final tempo = _retimeAudio(rate, spec.preservePitch);
          final raw = rawLabels[mediaIndex++];
          chains.add(
            '[$raw]atrim=start=${relativeFirst.toStringAsFixed(6)}:'
            'end=${relativeLast.toStringAsFixed(6)},'
            'asetpts=PTS-STARTPTS,$direction$tempo'
            'atrim=duration=${dt.toStringAsFixed(6)},'
            'apad=whole_dur=${dt.toStringAsFixed(6)}[$segmentLabel]',
          );
        }
        final remapped = 'aremapped_$idx';
        chains.add(
          '${segmentLabels.map((name) => '[$name]').join()}'
          'concat=n=${segmentLabels.length}:v=0:a=1[$remapped]',
        );
        chains.add('[$remapped]$post[$label]');
      } else {
        // atempo so aceita 0,5..2 por etapa; velocidades maiores viram
        // uma corrente de etapas.
        final tempo = _retimeAudio(vel, spec.preservePitch);
        chains.add('[$idx:a]aresample=44100,$tempo$post[$label]');
      }
      labels.add('[$label]');
      porCamada[l.id] = label;
      duckAlvo[l.id] = spec.duckAgainstId;
      idx++;
    }

    // ABAIXAR PELA VOZ, pelo envelope pre-calculado.
    //
    // O sidechaincompress do FFmpeg fazia isso sozinho, com o ataque e o
    // repouso DELE — e o preview fazia com os do aplicativo. Duas contas
    // para a mesma coisa e a musica cedendo de um jeito no aparelho e de
    // outro no arquivo. Agora os dois leem os mesmos pontos, e o filtro
    // so desenha a curva que ja foi decidida.
    for (final entry in duckAlvo.entries) {
      if (entry.value == null) continue;
      final musicaLabel = porCamada[entry.key];
      if (musicaLabel == null) continue;
      final env = duckEnvelopes[entry.key];
      if (env == null || env.isNeutral) continue;
      final expr = ffmpegVolumeExpr(env);
      if (expr == null) continue;
      final saida = 'dk_${entry.key.hashCode.abs()}';
      chains.add("[$musicaLabel]volume=volume='$expr':eval=frame[$saida]");
      final i = labels.indexOf('[$musicaLabel]');
      if (i >= 0) labels[i] = '[$saida]';
    }

    // A soma NAO e dividida pelo numero de trilhas (normalize=0): dividir
    // faz cada trilha nova baixar as anteriores, e o volume que a pessoa
    // ajustou deixa de valer. Quem cuida do estouro e o limitador.
    final somaLabel = labels.length == 1 ? null : 'amixed';
    if (somaLabel != null) {
      chains.add(
        '${labels.join()}amix=inputs=${labels.length}:normalize=0'
        ':duration=longest:dropout_transition=0[$somaLabel]',
      );
    }
    final entrada = somaLabel == null ? labels.first : '[$somaLabel]';

    // LIMITADOR NO FIM, e so quando pode fazer falta: uma trilha unica em
    // 0 dB tem de sair do arquivo identica ao que entrou, amostra por
    // amostra, e um limitador sempre ligado quebraria isso.
    final mix = _precisaLimitador(sources)
        ? '${entrada}alimiter=limit=$kTetoDoBarramento'
            ':attack=5:release=50:level=disabled[aout]'
        : '${entrada}anull[aout]';

    return (
      inputs: inputs,
      filter: '${chains.join(';')};$mix',
      outLabel: '[aout]',
    );
  }

  double get _total => project.duration.inMicroseconds / 1000000.0;

  /// So entra limitador quando a soma pode passar do teto: mais de uma
  /// trilha, ou alguma com ganho acima de 0 dB.
  static bool _precisaLimitador(List<Layer> sources) {
    if (sources.length > 1) return true;
    for (final l in sources) {
      final spec = _specOf(l);
      if (spec != null && spec.gain > 1.0) return true;
      final v = switch (l) {
        AudioLayer a => a.volume,
        VideoLayer v => v.volume,
        _ => 1.0,
      };
      if (v > 1.0) return true;
    }
    return false;
  }

  // ------------------------------------------------------ codificar

  Future<File> _outputFile() async {
    final docs = await getApplicationDocumentsDirectory();
    final out = Directory('${docs.path}/exports');
    if (!out.existsSync()) out.createSync(recursive: true);
    final stamp = project.name
        .replaceAll(RegExp(r'[^A-Za-z0-9_-]'), '_')
        .toLowerCase();
    final file = File(
      '${out.path}/aurea_'
      '${stamp.isEmpty ? 'video' : stamp}_${frameCount}f.mp4',
    );
    if (file.existsSync()) file.deleteSync();
    return file;
  }

  /// SEQUENCIA PNG: leva os quadros para uma pasta que a pessoa acha.
  ///
  /// E o unico caminho com TRANSPARENCIA de verdade — MP4 com alfa so
  /// toca em um punhado de programas. Serve para levar a arte pronta
  /// para outro editor sem perder nada.
  Future<Directory> saveSequence(Directory framesDir) async {
    final base = await getApplicationDocumentsDirectory();
    final stamp = project.name.replaceAll(RegExp(r'[^A-Za-z0-9_-]'), '_');
    final destino = Directory('${base.path}/exports/${stamp}_png');
    if (destino.existsSync()) destino.deleteSync(recursive: true);
    destino.createSync(recursive: true);

    final frames =
        framesDir
            .listSync()
            .whereType<File>()
            .where((f) => f.path.endsWith('.png'))
            .toList()
          ..sort((a, b) => a.path.compareTo(b.path));
    if (frames.isEmpty) {
      throw ExportException('Nenhum quadro foi desenhado.');
    }
    for (var i = 0; i < frames.length; i++) {
      final nome = i.toString().padLeft(6, '0');
      frames[i].copySync('${destino.path}/$nome.png');
    }
    return destino;
  }

  /// Codifica a sequencia de quadros com o CODIFICADOR DA PLATAFORMA e,
  /// se houver som, junta o audio depois sem tocar no video.
  Future<File> encode({
    required Directory framesDir,
    required String quality,
    void Function(double p)? onProgress,
  }) async {
    final file = await _outputFile();
    final frames =
        framesDir
            .listSync()
            .whereType<File>()
            .where((f) => f.path.endsWith('.png'))
            .map((f) => f.path)
            .toList()
          ..sort();
    if (frames.isEmpty) {
      throw ExportException('Nenhum quadro foi desenhado.');
    }

    final bitrate =
        settings.bitrateMbps != null ||
            settings.codec == ExportCodec.hevc ||
            settings.size != ExportSize.original
        ? settings.bitrateFor(width, height, fps)
        : PlatformEncoder.bitrateFor(width, height, fps, quality);
    final silent = File('${framesDir.parent.path}/mudo.mp4');

    if (await PlatformEncoder.available) {
      await PlatformEncoder.start(
        path: silent.path,
        width: width,
        height: height,
        fps: fps,
        bitrate: bitrate,
        hevc: settings.codec == ExportCodec.hevc,
      );
      // Em lotes: atravessar a ponte por quadro custa mais que codificar.
      const batch = 12;
      for (var i = 0; i < frames.length; i += batch) {
        if (_cancelled) throw ExportException('Cancelado.');
        final end = (i + batch).clamp(0, frames.length);
        await PlatformEncoder.frames(frames.sublist(i, end));
        onProgress?.call(end / frames.length * 0.9);
      }
      await PlatformEncoder.finish();
    } else {
      // Aparelho sem codificador de hardware. Nao caimos no x264: ele e
      // GPL e nem existe mais no pacote. MPEG-4 parte 2 e LGPL e sai do
      // apuro com arquivo maior.
      await _encodeFallback(frames, framesDir, silent, bitrate);
    }

    if (!silent.existsSync() || silent.lengthSync() < 1024) {
      throw ExportException('O codificador nao produziu video.');
    }

    final audio = audioGraph(1);
    if (audio.outLabel == null) {
      silent.renameSync(file.path);
      onProgress?.call(1);
      return file;
    }

    // Junta o audio SEM recodificar o video.
    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-i',
      silent.path,
      ...audio.inputs,
      '-filter_complex',
      audio.filter!,
      '-map',
      '0:v',
      '-map',
      audio.outLabel!,
      '-c:v',
      'copy',
      '-c:a',
      'aac',
      '-b:a',
      '192k',
      '-movflags',
      '+faststart',
      '-t',
      _total.toStringAsFixed(3),
      file.path,
    ]);
    if (!ReturnCode.isSuccess(await session.getReturnCode())) {
      final log = await session.getAllLogsAsString();
      throw ExportException('Falha ao juntar o audio.\n${_tail(log)}');
    }
    onProgress?.call(1);
    return file;
  }

  Future<void> _encodeFallback(
    List<String> frames,
    Directory framesDir,
    File target,
    int bitrate,
  ) async {
    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-framerate',
      '$fps',
      '-i',
      '${framesDir.path}/%06d.png',
      '-c:v',
      'mpeg4',
      '-b:v',
      '$bitrate',
      '-pix_fmt',
      'yuv420p',
      '-r',
      '$fps',
      target.path,
    ]);
    if (!ReturnCode.isSuccess(await session.getReturnCode())) {
      final log = await session.getAllLogsAsString();
      throw ExportException('Falha ao codificar o video.\n${_tail(log)}');
    }
  }

  static String _tail(String? log) {
    if (log == null || log.isEmpty) return '';
    final lines = log.trim().split('\n');
    return lines.length <= 12
        ? lines.join('\n')
        : lines.sublist(lines.length - 12).join('\n');
  }
}

class ExportException implements Exception {
  ExportException(this.message);
  final String message;
  @override
  String toString() => message;
}
