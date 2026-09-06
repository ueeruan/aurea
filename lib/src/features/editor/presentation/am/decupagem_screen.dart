import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/utils/time_format.dart';
import '../../application/editor_controller.dart';
import '../../application/media_preview_service.dart';
import '../../application/scene_cut_service.dart';
import '../../domain/cut_ops.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'clip_preview_painters.dart';

/// MODO DECUPAGEM: a tela de escolher o que fica — do jeito do Premiere
/// e do CapCut.
///
/// O clipe ocupa a tela inteira como uma TIRA DE MINIATURAS com a forma
/// de onda por baixo. Entrada e saida sao ALCAS que se arrastam na
/// propria tira (nao botoes que copiam o cursor), o cursor anda quadro a
/// quadro pelos botoes de transporte, e "Dividir" corta onde o cursor
/// esta. Tudo que vai sair aparece ANTES de sair: o trecho fora das
/// alcas escurece, as pausas ficam vermelhas, os cortes de cena ganham
/// um risco.
///
/// "Cortar sozinho" e a decupagem automatica: por MUDANCA DE CENA (o
/// detector de cena do FFmpeg, o mesmo principio do Scene Edit Detection
/// do Premiere) ou por SILENCIO (as pausas entre falas).
Future<void> openDecupagem(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  await Navigator.of(context).push(
    MaterialPageRoute<void>(
      fullscreenDialog: true,
      builder: (_) => DecupagemScreen(layerId: layerId),
    ),
  );
}

class DecupagemScreen extends ConsumerStatefulWidget {
  const DecupagemScreen({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<DecupagemScreen> createState() => _DecupagemScreenState();
}

enum _Auto { nenhum, cena, silencio }

class _DecupagemScreenState extends ConsumerState<DecupagemScreen> {
  final _service = MediaPreviewService.instance;

  /// Posicao dentro do CLIPE (0 .. duracao), nao na linha do tempo.
  Duration _cursor = Duration.zero;
  Duration? _entrada;
  Duration? _saida;

  _Auto _auto = _Auto.nenhum;
  double _limiar = 0.035;
  double _minPausa = 350;
  bool _arrasto = true;

  /// Mudanca de cena: 0 = so cortes secos, 1 = qualquer mexida.
  double _sensibilidade = 0.5;
  List<Duration>? _cortes;
  bool _buscando = false;
  int _pedido = 0;
  Timer? _debounce;

  @override
  void initState() {
    super.initState();
    final l = _layer;
    if (l is AudioLayer) {
      _service.ensureWaveform(l.sourcePath);
    } else if (l is VideoLayer) {
      _service.ensureWaveform(l.sourcePath);
      _service.ensureFilmstrip(
        l.sourcePath,
        l.sourceOffset + videoSourceSpan(l),
      );
    }
  }

  @override
  void dispose() {
    _debounce?.cancel();
    super.dispose();
  }

  Layer? get _layer =>
      ref.read(editorControllerProvider).layerById(widget.layerId);

  Duration get _fonteInicio => switch (_layer) {
    VideoLayer v => v.sourceOffset,
    AudioLayer a => a.sourceOffset,
    _ => Duration.zero,
  };

  Duration get _fonteFim => switch (_layer) {
    VideoLayer v => v.sourceOffset + videoSourceSpan(v),
    AudioLayer a => a.sourceOffset + a.sourceSpan,
    Layer l => _fonteInicio + l.duration,
    _ => Duration.zero,
  };

  Duration _fonteEm(Duration local) => switch (_layer) {
    VideoLayer v => videoAbsoluteSourceTimeAt(v, local),
    AudioLayer a =>
      a.sourceOffset +
          Duration(microseconds: (local.inMicroseconds * a.speed).round()),
    _ => _fonteInicio + local,
  };

  String? get _caminho => switch (_layer) {
    VideoLayer v => v.sourcePath,
    AudioLayer a => a.sourcePath,
    _ => null,
  };

  Duration get _quadro => ref.read(editorControllerProvider).frameDuration;

  Duration _naGrade(Duration t) {
    final q = _quadro.inMicroseconds;
    if (q <= 0) return t;
    return Duration(microseconds: (t.inMicroseconds / q).round() * q);
  }

  void _irPara(Duration t) {
    final l = _layer;
    if (l == null) return;
    var v = t;
    if (v < Duration.zero) v = Duration.zero;
    if (v > l.duration) v = l.duration;
    setState(() => _cursor = v);
  }

  void _passo(int n) => _irPara(_naGrade(_cursor) + _quadro * n);

  // ------------------------------------------------------------ pausas

  /// Pausas dentro deste clipe, em tempo do CLIPE (0 = inicio da barra).
  List<(Duration, Duration)> _pausas() {
    final l = _layer;
    if (l == null) return const [];
    final r = ref
        .read(editorControllerProvider.notifier)
        .silenceRangesOf(
          widget.layerId,
          threshold: _limiar,
          minSilence: Duration(milliseconds: _minPausa.round()),
        );
    if (r == null) return const [];
    return [for (final p in r) (p.$1 - l.startTime, p.$2 - l.startTime)];
  }

  void _aplicarSilencio() {
    final l = _layer;
    if (l == null) return;
    final pausas = _pausas();
    if (pausas.isEmpty) {
      AureaSnack.show(context, 'Nenhuma pausa nesse limiar');
      return;
    }
    final total = pausas.fold<Duration>(
      Duration.zero,
      (a, p) => a + (p.$2 - p.$1),
    );
    final naLinha = [
      for (final p in pausas) (l.startTime + p.$1, l.startTime + p.$2),
    ];
    ref
        .read(editorControllerProvider.notifier)
        .cutRangesOf(widget.layerId, naLinha, ripple: _arrasto);
    if (!mounted) return;
    Navigator.of(context).pop();
    AureaSnack.show(
      context,
      '${pausas.length} ${pausas.length == 1 ? "pausa removida" : "pausas removidas"}'
      ' (${formatTime(total)})',
      actionLabel: 'Desfazer',
      onAction: ref.read(editorControllerProvider.notifier).undo,
    );
  }

  // ------------------------------------------------------------- cenas

  void _agendarCenas() {
    _debounce?.cancel();
    _debounce = Timer(const Duration(milliseconds: 450), _detectarCenas);
  }

  Future<void> _detectarCenas() async {
    final l = _layer;
    if (l is! VideoLayer) return;
    final id = ++_pedido;
    setState(() => _buscando = true);
    // Sensibilidade vira limiar: 0 -> 0,65 (so cortes secos), 1 -> 0,15.
    final limiar = 0.65 - _sensibilidade * 0.5;
    final cortes = await SceneCutService.instance.detect(
      l.sourcePath,
      start: l.sourceOffset,
      duration: videoSourceSpan(l),
      threshold: limiar,
    );
    if (!mounted || id != _pedido) return;
    setState(() {
      _buscando = false;
      _cortes = [
        for (final sourceTime in cortes) videoLocalTimeForSource(l, sourceTime),
      ]..sort();
    });
  }

  List<Duration> get _cortesValidos {
    final l = _layer;
    final lista = _cortes;
    if (l == null || lista == null) return const [];
    return [
      for (final c in lista)
        if (c > const Duration(milliseconds: 100) &&
            c < l.duration - const Duration(milliseconds: 100))
          c,
    ];
  }

  void _cortarNasCenas() {
    final l = _layer;
    final cortes = _cortesValidos;
    if (l == null || cortes.isEmpty) return;
    final ctrl = ref.read(editorControllerProvider.notifier);
    final pedacos = ctrl.splitLayerAtTimes(widget.layerId, [
      for (final c in cortes) l.startTime + c,
    ]);
    if (!mounted) return;
    Navigator.of(context).pop();
    AureaSnack.show(
      context,
      'Decupado em ${pedacos.length} '
      '${pedacos.length == 1 ? "pedaco" : "pedacos"}',
      actionLabel: 'Desfazer',
      onAction: ctrl.undo,
    );
  }

  void _marcarCenas() {
    final l = _layer;
    final cortes = _cortesValidos;
    if (l == null || cortes.isEmpty) return;
    final ctrl = ref.read(editorControllerProvider.notifier);
    ctrl.addMarkers([for (final c in cortes) l.startTime + c], label: 'Cena');
    if (!mounted) return;
    AureaSnack.show(
      context,
      '${cortes.length} marcas na regua',
      actionLabel: 'Desfazer',
      onAction: ctrl.undo,
    );
  }

  // ------------------------------------------------------------ trecho

  void _aplicarTrecho({required bool manter}) {
    final l = _layer;
    if (l == null) return;
    final de = _entrada ?? Duration.zero;
    final ate = _saida ?? l.duration;
    if (ate <= de) {
      AureaSnack.show(context, 'Marque a entrada antes da saida');
      return;
    }
    final ctrl = ref.read(editorControllerProvider.notifier);
    if (manter) {
      // Ficar so com o miolo: tira as duas pontas.
      ctrl.cutRangesOf(widget.layerId, [
        if (de > Duration.zero) (l.startTime, l.startTime + de),
        if (ate < l.duration) (l.startTime + ate, l.endTime),
      ], ripple: _arrasto);
    } else {
      ctrl.cutRangesOf(widget.layerId, [
        (l.startTime + de, l.startTime + ate),
      ], ripple: _arrasto);
    }
    if (!mounted) return;
    Navigator.of(context).pop();
    AureaSnack.show(
      context,
      manter ? 'Sobrou so o trecho marcado' : 'Trecho removido',
      actionLabel: 'Desfazer',
      onAction: ctrl.undo,
    );
  }

  void _dividirNoCursor() {
    final l = _layer;
    if (l == null) return;
    final ctrl = ref.read(editorControllerProvider.notifier);
    final antes = ref.read(editorControllerProvider).layers.length;
    ctrl.splitLayer(widget.layerId, l.startTime + _naGrade(_cursor));
    final depois = ref.read(editorControllerProvider).layers.length;
    if (depois == antes) {
      AureaSnack.show(context, 'Perto demais da ponta para dividir');
      return;
    }
    if (!mounted) return;
    Navigator.of(context).pop();
    AureaSnack.show(
      context,
      'Dividido em ${formatTime(_cursor)}',
      actionLabel: 'Desfazer',
      onAction: ctrl.undo,
    );
  }

  // ------------------------------------------------------------- build

  @override
  Widget build(BuildContext context) {
    ref.watch(editorControllerProvider);
    final l = _layer;
    if (l == null) return const SizedBox.shrink();

    final dur = l.duration;
    final path = _caminho;
    final quadro = _quadro;
    final pausas = _auto == _Auto.silencio
        ? _pausas()
        : const <(Duration, Duration)>[];
    final cortes = _auto == _Auto.cena ? _cortesValidos : const <Duration>[];

    return Scaffold(
      backgroundColor: AmColors.bg,
      appBar: AppBar(
        backgroundColor: AmColors.topBar,
        elevation: 0,
        leading: IconButton(
          icon: const Icon(CupertinoIcons.xmark, color: AmColors.text),
          onPressed: () => Navigator.of(context).pop(),
        ),
        title: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Decupagem',
              style: TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
            Text(
              l.name,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontSize: 11, color: AmColors.muted),
            ),
          ],
        ),
      ),
      body: ValueListenableBuilder<int>(
        valueListenable: _service.revision,
        builder: (context, _, _) {
          final peaks = path == null ? null : _service.peaksOf(path);
          final strip = path == null ? null : _service.stripOf(path);

          return Column(
            children: [
              Expanded(
                child: _Visor(
                  frames: strip,
                  sourceStart: _fonteInicio,
                  sourceEnd: _fonteFim,
                  sourceAt: _fonteEm,
                  cursor: _cursor,
                  duration: dur,
                  temVideo: l is VideoLayer,
                ),
              ),
              _Tira(
                frames: strip,
                peaks: peaks,
                sourceStart: _fonteInicio,
                sourceEnd: _fonteFim,
                sourceAt: _fonteEm,
                duration: dur,
                quadro: quadro,
                cursor: _cursor,
                entrada: _entrada,
                saida: _saida,
                pausas: pausas,
                cortes: cortes,
                temVideo: l is VideoLayer,
                onCursor: _irPara,
                onEntrada: (t) => setState(() {
                  _entrada = t;
                  if (_saida != null && _saida! <= t) _saida = null;
                }),
                onSaida: (t) => setState(() {
                  _saida = t;
                  if (_entrada != null && _entrada! >= t) _entrada = null;
                }),
              ),
              _Transporte(
                cursor: _cursor,
                duration: dur,
                quadro: quadro,
                onAnterior: () => _passo(-1),
                onProximo: () => _passo(1),
                onInicio: () => _irPara(Duration.zero),
                onFim: () => _irPara(dur),
              ),
              Expanded(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.fromLTRB(14, 4, 14, 18),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Row(
                        children: [
                          Expanded(
                            child: _Botao(
                              rotulo: 'Entrada aqui',
                              detalhe: _entrada == null
                                  ? '—'
                                  : formatTime(_entrada!),
                              aceso: _entrada != null,
                              onTap: () => setState(() {
                                _entrada = _naGrade(_cursor);
                                if (_saida != null && _saida! <= _cursor) {
                                  _saida = null;
                                }
                              }),
                            ),
                          ),
                          const SizedBox(width: 8),
                          Expanded(
                            child: _Botao(
                              rotulo: 'Saida aqui',
                              detalhe: _saida == null
                                  ? '—'
                                  : formatTime(_saida!),
                              aceso: _saida != null,
                              onTap: () => setState(() {
                                _saida = _naGrade(_cursor);
                                if (_entrada != null && _entrada! >= _cursor) {
                                  _entrada = null;
                                }
                              }),
                            ),
                          ),
                          const SizedBox(width: 8),
                          Expanded(
                            child: _Botao(
                              rotulo: 'Limpar',
                              detalhe: 'alcas',
                              onTap: () => setState(() {
                                _entrada = null;
                                _saida = null;
                              }),
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 8),
                      Row(
                        children: [
                          Expanded(
                            child: _Acao(
                              'Dividir',
                              _dividirNoCursor,
                              icone: CupertinoIcons.scissors,
                              destaque: true,
                            ),
                          ),
                          const SizedBox(width: 8),
                          Expanded(
                            child: _Acao(
                              'Manter trecho',
                              () => _aplicarTrecho(manter: true),
                            ),
                          ),
                          const SizedBox(width: 8),
                          Expanded(
                            child: _Acao(
                              'Remover trecho',
                              () => _aplicarTrecho(manter: false),
                            ),
                          ),
                        ],
                      ),

                      const SizedBox(height: 14),
                      const Divider(color: AmColors.hairline, height: 1),
                      const SizedBox(height: 10),

                      const Text(
                        'Cortar sozinho',
                        style: TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.w700,
                          color: AmColors.text,
                        ),
                      ),
                      const SizedBox(height: 8),
                      _ChipRow(
                        opcoes: const [
                          'Desligado',
                          'Mudanca de cena',
                          'Silencio',
                        ],
                        indice: _auto.index,
                        onChanged: (i) {
                          setState(() => _auto = _Auto.values[i]);
                          if (_auto == _Auto.cena && _cortes == null) {
                            _detectarCenas();
                          }
                        },
                      ),
                      if (_auto == _Auto.cena) ...[
                        const SizedBox(height: 6),
                        if (l is! VideoLayer)
                          const Padding(
                            padding: EdgeInsets.symmetric(vertical: 8),
                            child: Text(
                              'Mudanca de cena e so para video.',
                              style: TextStyle(
                                fontSize: 11,
                                color: AmColors.muted,
                              ),
                            ),
                          )
                        else ...[
                          _Deslize(
                            rotulo: 'Sensibilidade',
                            valor: _sensibilidade,
                            min: 0,
                            max: 1,
                            texto: '${(_sensibilidade * 100).round()}%',
                            onChanged: (v) {
                              setState(() => _sensibilidade = v);
                              _agendarCenas();
                            },
                          ),
                          Text(
                            _buscando
                                ? 'Procurando onde a cena muda...'
                                : cortes.isEmpty
                                ? (_cortes == null
                                      ? ''
                                      : 'Nenhuma mudanca de cena nessa sensibilidade.')
                                : '${cortes.length} '
                                      '${cortes.length == 1 ? "corte" : "cortes"}'
                                      ' — ${cortes.length + 1} pedacos',
                            style: const TextStyle(
                              fontSize: 11,
                              color: AmColors.muted,
                            ),
                          ),
                          const SizedBox(height: 6),
                          Row(
                            children: [
                              Expanded(
                                child: _Acao(
                                  'Cortar nas cenas',
                                  cortes.isEmpty ? null : _cortarNasCenas,
                                  icone: CupertinoIcons.scissors,
                                  destaque: cortes.isNotEmpty,
                                ),
                              ),
                              const SizedBox(width: 8),
                              Expanded(
                                child: _Acao(
                                  'So marcar',
                                  cortes.isEmpty ? null : _marcarCenas,
                                  icone: CupertinoIcons.bookmark,
                                ),
                              ),
                            ],
                          ),
                        ],
                      ],
                      if (_auto == _Auto.silencio) ...[
                        const SizedBox(height: 6),
                        if (peaks == null || peaks.isEmpty)
                          const Padding(
                            padding: EdgeInsets.symmetric(vertical: 8),
                            child: Text(
                              'Lendo o som do arquivo...',
                              style: TextStyle(
                                fontSize: 11,
                                color: AmColors.muted,
                              ),
                            ),
                          )
                        else ...[
                          _Deslize(
                            rotulo: 'Limiar',
                            valor: _limiar,
                            min: 0.005,
                            max: 0.2,
                            texto: _limiar.toStringAsFixed(3),
                            onChanged: (v) => setState(() => _limiar = v),
                          ),
                          _Deslize(
                            rotulo: 'Pausa minima',
                            valor: _minPausa,
                            min: 100,
                            max: 2000,
                            texto: '${_minPausa.round()} ms',
                            onChanged: (v) => setState(() => _minPausa = v),
                          ),
                          Text(
                            pausas.isEmpty
                                ? 'Nenhuma pausa nesse limiar.'
                                : '${pausas.length} '
                                      '${pausas.length == 1 ? "pausa" : "pausas"}'
                                      ' — sai ${formatTime(pausas.fold<Duration>(Duration.zero, (a, p) => a + (p.$2 - p.$1)))}'
                                      ' de ${formatTime(dur)}',
                            style: const TextStyle(
                              fontSize: 11,
                              color: AmColors.muted,
                            ),
                          ),
                          const SizedBox(height: 6),
                          _Acao(
                            'Remover as pausas',
                            pausas.isEmpty ? null : _aplicarSilencio,
                            icone: CupertinoIcons.scissors,
                            destaque: pausas.isNotEmpty,
                          ),
                        ],
                      ],

                      const SizedBox(height: 12),
                      _Chave(
                        rotulo: 'Encostar o que vem depois',
                        valor: _arrasto,
                        onChanged: (v) => setState(() => _arrasto = v),
                      ),
                      const Text(
                        'Desligado, o corte deixa o buraco — e o que se '
                        'quer quando outra trilha tem de continuar no '
                        'mesmo lugar.',
                        style: TextStyle(
                          fontSize: 11,
                          height: 1.35,
                          color: AmColors.muted,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

/// Quadro do cursor: a miniatura mais proxima da tira.
class _Visor extends StatelessWidget {
  const _Visor({
    required this.frames,
    required this.sourceStart,
    required this.sourceEnd,
    required this.sourceAt,
    required this.cursor,
    required this.duration,
    required this.temVideo,
  });

  final List<ui.Image>? frames;
  final Duration sourceStart;
  final Duration sourceEnd;
  final Duration Function(Duration) sourceAt;
  final Duration cursor;
  final Duration duration;
  final bool temVideo;

  @override
  Widget build(BuildContext context) {
    final lista = frames;
    if (!temVideo || lista == null || lista.isEmpty) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              temVideo ? CupertinoIcons.film : CupertinoIcons.waveform,
              size: 34,
              color: AmColors.muted,
            ),
            const SizedBox(height: 8),
            Text(
              temVideo ? 'Montando as miniaturas...' : 'Faixa de som',
              style: const TextStyle(fontSize: 12, color: AmColors.muted),
            ),
          ],
        ),
      );
    }

    // As miniaturas cobrem o arquivo inteiro; o cursor anda dentro do
    // pedaco que a camada usa.
    final totalUs = sourceEnd.inMicroseconds;
    final noArquivo = sourceAt(cursor);
    final f = totalUs <= 0 ? 0.0 : noArquivo.inMicroseconds / totalUs;
    final i = (f * lista.length).floor().clamp(0, lista.length - 1);

    return Padding(
      padding: const EdgeInsets.all(10),
      child: RawImage(image: lista[i], fit: BoxFit.contain),
    );
  }
}

enum _Alvo { cursor, entrada, saida }

/// A TIRA: miniaturas com a forma de onda por baixo, as alcas de
/// entrada/saida arrastaveis em cima, o cursor, e tudo que vai sair.
class _Tira extends StatefulWidget {
  const _Tira({
    required this.frames,
    required this.peaks,
    required this.sourceStart,
    required this.sourceEnd,
    required this.sourceAt,
    required this.duration,
    required this.quadro,
    required this.cursor,
    required this.entrada,
    required this.saida,
    required this.pausas,
    required this.cortes,
    required this.temVideo,
    required this.onCursor,
    required this.onEntrada,
    required this.onSaida,
  });

  final List<ui.Image>? frames;
  final Float32List? peaks;
  final Duration sourceStart;
  final Duration sourceEnd;
  final Duration Function(Duration) sourceAt;
  final Duration duration;
  final Duration quadro;
  final Duration cursor;
  final Duration? entrada;
  final Duration? saida;
  final List<(Duration, Duration)> pausas;
  final List<Duration> cortes;
  final bool temVideo;
  final ValueChanged<Duration> onCursor;
  final ValueChanged<Duration> onEntrada;
  final ValueChanged<Duration> onSaida;

  @override
  State<_Tira> createState() => _TiraState();
}

class _TiraState extends State<_Tira> {
  _Alvo? _alvo;

  static const double _altura = 128;
  static const double _pegada = 24;

  double _x(Duration t, double w) {
    final total = widget.duration.inMicroseconds;
    if (total <= 0) return 0;
    return (t.inMicroseconds / total).clamp(0.0, 1.0) * w;
  }

  Duration _t(double x, double w) {
    final f = (x / w).clamp(0.0, 1.0);
    return Duration(microseconds: (widget.duration.inMicroseconds * f).round());
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, c) {
      final w = c.maxWidth - 20;

      _Alvo alvoEm(double x) {
        final e = widget.entrada, s = widget.saida;
        if (e != null && (x - _x(e, w)).abs() <= _pegada) {
          return _Alvo.entrada;
        }
        if (s != null && (x - _x(s, w)).abs() <= _pegada) {
          return _Alvo.saida;
        }
        return _Alvo.cursor;
      }

      void mover(_Alvo alvo, double x) {
        final t = _t(x, w);
        switch (alvo) {
          case _Alvo.cursor:
            widget.onCursor(t);
          case _Alvo.entrada:
            final s = widget.saida;
            final lim = s == null ? widget.duration : s - widget.quadro;
            widget.onEntrada(t > lim ? lim : t);
            widget.onCursor(t > lim ? lim : t);
          case _Alvo.saida:
            final e = widget.entrada;
            final lim = e == null ? Duration.zero : e + widget.quadro;
            widget.onSaida(t < lim ? lim : t);
            widget.onCursor(t < lim ? lim : t);
        }
      }

      return GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTapDown: (d) {
          final x = d.localPosition.dx - 10;
          if (alvoEm(x) == _Alvo.cursor) widget.onCursor(_t(x, w));
        },
        onHorizontalDragStart: (d) => _alvo = alvoEm(d.localPosition.dx - 10),
        onHorizontalDragUpdate: (d) =>
            mover(_alvo ?? _Alvo.cursor, d.localPosition.dx - 10),
        onHorizontalDragEnd: (_) => _alvo = null,
        onHorizontalDragCancel: () => _alvo = null,
        child: Container(
          height: _altura,
          margin: const EdgeInsets.symmetric(horizontal: 10),
          decoration: BoxDecoration(
            color: AmColors.panel,
            borderRadius: BorderRadius.circular(10),
          ),
          clipBehavior: Clip.antiAlias,
          child: Stack(
            children: [
              if (widget.temVideo)
                Positioned.fill(
                  child: CustomPaint(
                    painter: _TiraPainter(
                      frames: widget.frames,
                      sourceStart: widget.sourceStart,
                      sourceEnd: widget.sourceEnd,
                      sourceAt: widget.sourceAt,
                      duration: widget.duration,
                    ),
                  ),
                ),
              if (widget.peaks != null && widget.peaks!.isNotEmpty)
                Positioned(
                  left: 0,
                  right: 0,
                  bottom: 0,
                  height: widget.temVideo ? 34 : _altura,
                  child: Opacity(
                    opacity: widget.temVideo ? 0.75 : 1,
                    child: CustomPaint(
                      painter: WaveformPainter(
                        peaks: widget.peaks!,
                        start: widget.sourceStart,
                        end: widget.sourceEnd,
                        color: AmColors.tealBright,
                      ),
                    ),
                  ),
                ),
              Positioned.fill(
                child: CustomPaint(
                  painter: _MarcasPainter(
                    duration: widget.duration,
                    cursor: widget.cursor,
                    entrada: widget.entrada,
                    saida: widget.saida,
                    pausas: widget.pausas,
                    cortes: widget.cortes,
                  ),
                ),
              ),
            ],
          ),
        ),
      );
    },
  );
}

/// As miniaturas lado a lado, cada uma no seu instante.
class _TiraPainter extends CustomPainter {
  const _TiraPainter({
    required this.frames,
    required this.sourceStart,
    required this.sourceEnd,
    required this.sourceAt,
    required this.duration,
  });

  final List<ui.Image>? frames;
  final Duration sourceStart;
  final Duration sourceEnd;
  final Duration Function(Duration) sourceAt;
  final Duration duration;

  @override
  void paint(Canvas canvas, Size size) {
    final lista = frames;
    if (lista == null || lista.isEmpty) return;
    final primeira = lista.first;
    final h = size.height;
    var tileW = primeira.height <= 0
        ? 48.0
        : h * primeira.width / primeira.height;
    if (tileW < 24) tileW = 24;
    final n = (size.width / tileW).ceil();
    final totalUs = sourceEnd.inMicroseconds;
    final paint = Paint()..filterQuality = FilterQuality.low;
    for (var k = 0; k < n; k++) {
      final t = duration * ((k + 0.5) / n);
      final noArquivo = sourceAt(t);
      final f = totalUs <= 0 ? 0.0 : noArquivo.inMicroseconds / totalUs;
      final i = (f * lista.length).floor().clamp(0, lista.length - 1);
      final img = lista[i];
      canvas.drawImageRect(
        img,
        Rect.fromLTWH(0, 0, img.width.toDouble(), img.height.toDouble()),
        Rect.fromLTWH(k * tileW, 0, tileW, h),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(_TiraPainter old) =>
      old.frames != frames ||
      old.sourceStart != sourceStart ||
      old.sourceEnd != sourceEnd ||
      old.sourceAt != sourceAt ||
      old.duration != duration;
}

/// Alcas, veu, pausas, cortes de cena e cursor — por cima da tira.
class _MarcasPainter extends CustomPainter {
  const _MarcasPainter({
    required this.duration,
    required this.cursor,
    required this.entrada,
    required this.saida,
    required this.pausas,
    required this.cortes,
  });

  final Duration duration;
  final Duration cursor;
  final Duration? entrada;
  final Duration? saida;
  final List<(Duration, Duration)> pausas;
  final List<Duration> cortes;

  double _x(Duration t, double w) {
    final total = duration.inMicroseconds;
    if (total <= 0) return 0;
    return (t.inMicroseconds / total).clamp(0.0, 1.0) * w;
  }

  @override
  void paint(Canvas canvas, Size size) {
    // O que sai, em vermelho translucido: o corte se ve antes de
    // acontecer.
    final fora = Paint()..color = AmColors.pink.withValues(alpha: 0.32);
    for (final p in pausas) {
      final a = _x(p.$1, size.width);
      final b = _x(p.$2, size.width);
      if (b > a) canvas.drawRect(Rect.fromLTRB(a, 0, b, size.height), fora);
    }

    // Cortes de cena: um risco com a pontinha de tesoura em cima.
    if (cortes.isNotEmpty) {
      final risco = Paint()
        ..color = AmColors.accent
        ..strokeWidth = 2;
      final ponta = Paint()..color = AmColors.accent;
      for (final c in cortes) {
        final x = _x(c, size.width);
        canvas.drawLine(Offset(x, 0), Offset(x, size.height), risco);
        final tri = Path()
          ..moveTo(x - 5, 0)
          ..lineTo(x + 5, 0)
          ..lineTo(x, 7)
          ..close();
        canvas.drawPath(tri, ponta);
      }
    }

    // Fora das alcas fica escurecido — o trecho marcado e o unico que
    // continua legivel. As alcas sao pegadas largas, do jeito do CapCut.
    if (entrada != null || saida != null) {
      final veu = Paint()..color = const Color(0xB30B0E12);
      final a = entrada == null ? 0.0 : _x(entrada!, size.width);
      final b = saida == null ? size.width : _x(saida!, size.width);
      if (a > 0) canvas.drawRect(Rect.fromLTRB(0, 0, a, size.height), veu);
      if (b < size.width) {
        canvas.drawRect(Rect.fromLTRB(b, 0, size.width, size.height), veu);
      }
      final moldura = Paint()
        ..color = AmColors.accent
        ..strokeWidth = 2.5
        ..style = PaintingStyle.stroke;
      canvas.drawRect(Rect.fromLTRB(a, 1.25, b, size.height - 1.25), moldura);
      void alca(double x, bool esquerda) {
        final r = RRect.fromRectAndCorners(
          Rect.fromLTWH(esquerda ? x : x - 14, 0, 14, size.height),
          topLeft: Radius.circular(esquerda ? 8 : 0),
          bottomLeft: Radius.circular(esquerda ? 8 : 0),
          topRight: Radius.circular(esquerda ? 0 : 8),
          bottomRight: Radius.circular(esquerda ? 0 : 8),
        );
        canvas.drawRRect(r, Paint()..color = AmColors.accent);
        final cx = esquerda ? x + 7 : x - 7;
        canvas.drawLine(
          Offset(cx, size.height / 2 - 12),
          Offset(cx, size.height / 2 + 12),
          Paint()
            ..color = const Color(0xFF12151A)
            ..strokeWidth = 2.5
            ..strokeCap = StrokeCap.round,
        );
      }

      if (entrada != null) alca(a, true);
      if (saida != null) alca(b, false);
    }

    // O cursor: linha branca com a cabeca em cima.
    final x = _x(cursor, size.width);
    canvas.drawLine(
      Offset(x, 0),
      Offset(x, size.height),
      Paint()
        ..color = Colors.white
        ..strokeWidth = 2,
    );
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromCenter(center: Offset(x, 6), width: 10, height: 12),
        const Radius.circular(3),
      ),
      Paint()..color = Colors.white,
    );
  }

  @override
  bool shouldRepaint(_MarcasPainter old) =>
      old.cursor != cursor ||
      old.entrada != entrada ||
      old.saida != saida ||
      old.duration != duration ||
      old.pausas.length != pausas.length ||
      old.cortes.length != cortes.length;
}

/// Transporte: inicio, um quadro atras, tempo + numero do quadro, um
/// quadro a frente, fim. Quadro a quadro e o que o Premiere da nas
/// setas; e o que faz um corte cair NO quadro.
class _Transporte extends StatelessWidget {
  const _Transporte({
    required this.cursor,
    required this.duration,
    required this.quadro,
    required this.onAnterior,
    required this.onProximo,
    required this.onInicio,
    required this.onFim,
  });

  final Duration cursor;
  final Duration duration;
  final Duration quadro;
  final VoidCallback onAnterior;
  final VoidCallback onProximo;
  final VoidCallback onInicio;
  final VoidCallback onFim;

  @override
  Widget build(BuildContext context) {
    final q = quadro.inMicroseconds <= 0
        ? 0
        : (cursor.inMicroseconds / quadro.inMicroseconds).round();
    Widget botao(IconData icone, VoidCallback onTap) => GestureDetector(
      onTap: () {
        onTap();
        HapticFeedback.selectionClick();
      },
      behavior: HitTestBehavior.opaque,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
        child: Icon(icone, size: 22, color: AmColors.text),
      ),
    );
    return Padding(
      padding: const EdgeInsets.fromLTRB(10, 6, 10, 2),
      child: Row(
        children: [
          botao(CupertinoIcons.backward_end_fill, onInicio),
          botao(CupertinoIcons.chevron_left, onAnterior),
          Expanded(
            child: Column(
              children: [
                Text(
                  formatTime(cursor),
                  style: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w700,
                    color: AmColors.accent,
                  ),
                ),
                Text(
                  'quadro $q  ·  ${formatTime(duration)}',
                  style: const TextStyle(fontSize: 11, color: AmColors.muted),
                ),
              ],
            ),
          ),
          botao(CupertinoIcons.chevron_right, onProximo),
          botao(CupertinoIcons.forward_end_fill, onFim),
        ],
      ),
    );
  }
}

class _Botao extends StatelessWidget {
  const _Botao({
    required this.rotulo,
    required this.detalhe,
    required this.onTap,
    this.aceso = false,
  });

  final String rotulo;
  final String detalhe;
  final VoidCallback onTap;
  final bool aceso;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(vertical: 8),
      decoration: BoxDecoration(
        color: aceso ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: Column(
        children: [
          Text(
            rotulo,
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: aceso ? AmColors.accent : AmColors.text,
            ),
          ),
          Text(
            detalhe,
            style: const TextStyle(fontSize: 10, color: AmColors.muted),
          ),
        ],
      ),
    ),
  );
}

class _Acao extends StatelessWidget {
  const _Acao(this.rotulo, this.onTap, {this.icone, this.destaque = false});

  final String rotulo;
  final VoidCallback? onTap;
  final IconData? icone;
  final bool destaque;

  @override
  Widget build(BuildContext context) {
    final ativo = onTap != null;
    final cor = !ativo
        ? AmColors.muted
        : destaque
        ? AmColors.accent
        : AmColors.text;
    return GestureDetector(
      onTap: onTap,
      child: Opacity(
        opacity: ativo ? 1 : 0.5,
        child: Container(
          margin: const EdgeInsets.symmetric(vertical: 3),
          padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 6),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: destaque && ativo ? AmColors.accentDim : AmColors.panelHigh,
            borderRadius: BorderRadius.circular(9),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (icone != null) ...[
                Icon(icone, size: 15, color: cor),
                const SizedBox(width: 6),
              ],
              Flexible(
                child: Text(
                  rotulo,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: cor,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ChipRow extends StatelessWidget {
  const _ChipRow({
    required this.opcoes,
    required this.indice,
    required this.onChanged,
  });

  final List<String> opcoes;
  final int indice;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) => Wrap(
    spacing: 8,
    runSpacing: 8,
    children: [
      for (var i = 0; i < opcoes.length; i++)
        GestureDetector(
          onTap: () => onChanged(i),
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            decoration: BoxDecoration(
              color: i == indice ? AmColors.accentDim : AmColors.chip,
              borderRadius: BorderRadius.circular(9),
            ),
            child: Text(
              opcoes[i],
              style: TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
                color: i == indice ? AmColors.accent : AmColors.text,
              ),
            ),
          ),
        ),
    ],
  );
}

class _Chave extends StatelessWidget {
  const _Chave({
    required this.rotulo,
    required this.valor,
    required this.onChanged,
  });

  final String rotulo;
  final bool valor;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      Expanded(
        child: Text(
          rotulo,
          style: const TextStyle(fontSize: 13, color: AmColors.text),
        ),
      ),
      CupertinoSwitch(
        value: valor,
        activeTrackColor: AmColors.accent,
        onChanged: onChanged,
      ),
    ],
  );
}

class _Deslize extends StatelessWidget {
  const _Deslize({
    required this.rotulo,
    required this.valor,
    required this.min,
    required this.max,
    required this.texto,
    required this.onChanged,
  });

  final String rotulo;
  final double valor;
  final double min;
  final double max;
  final String texto;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      SizedBox(
        width: 92,
        child: Text(
          rotulo,
          style: const TextStyle(fontSize: 12, color: AmColors.muted),
        ),
      ),
      Expanded(
        child: AmTickRuler(
          value: valor.clamp(min, max),
          min: min,
          max: max,
          unitsPerPixel: (max - min) / 420,
          height: 40,
          onChanged: onChanged,
        ),
      ),
      SizedBox(
        width: 62,
        child: Text(
          texto,
          textAlign: TextAlign.right,
          style: const TextStyle(fontSize: 11, color: AmColors.text),
        ),
      ),
    ],
  );
}
