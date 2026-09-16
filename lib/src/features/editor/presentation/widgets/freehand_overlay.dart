import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../core/l10n/app_language.dart';
import '../../../../core/storage/prefs.dart';
import '../../application/editor_controller.dart';
import '../../application/freehand_session.dart';
import '../../application/playback_controller.dart';
import '../../domain/desenho_livre.dart';
import '../am/am_colors.dart';
import '../am/color_picker_sheet.dart';
import '../context/parameter_row.dart';
export '../../application/freehand_session.dart' show freehandRequestProvider;

/// O tamanho da grade do balde: o contorno da regiao sai com esta
/// resolucao, e a borda de tinta fecha a fresta que ela deixa.
const celulaDoBalde = 4.0;

const _chaveDaFerramenta = 'desenho.ferramenta';
const _chaveDaCor = 'desenho.cor';
const _chaveDaEspessura = 'desenho.espessura';
const _chaveDaDureza = 'desenho.dureza';
const _chaveDaOpacidade = 'desenho.opacidade';

T _lido<T>(Ref ref, T padrao, T? Function(SharedPreferences prefs) ler) {
  try {
    return ler(ref.read(sharedPreferencesProvider)) ?? padrao;
  } catch (_) {
    return padrao;
  }
}

void _lembrar(WidgetRef ref, void Function(SharedPreferences prefs) gravar) {
  try {
    gravar(ref.read(sharedPreferencesProvider));
  } catch (_) {}
}

/// A ferramenta, a cor e os numeros do desenho — lembrados entre sessoes.
final ferramentaDoDesenhoProvider = StateProvider<FerramentaDeDesenho>(
  (ref) => _lido(ref, FerramentaDeDesenho.caneta, (p) {
    final i = p.getInt(_chaveDaFerramenta);
    return i == null || i < 0 || i >= FerramentaDeDesenho.values.length
        ? null
        : FerramentaDeDesenho.values[i];
  }),
);

final corDoDesenhoProvider = StateProvider<Color>(
  (ref) => _lido(ref, const Color(0xFFFFFFFF), (p) {
    final v = p.getInt(_chaveDaCor);
    return v == null ? null : Color(v);
  }),
);

final espessuraDoDesenhoProvider = StateProvider<double>(
  (ref) => _lido(ref, 12.0, (p) => p.getDouble(_chaveDaEspessura)),
);

final durezaDoDesenhoProvider = StateProvider<double>(
  (ref) => _lido(ref, 0.8, (p) => p.getDouble(_chaveDaDureza)),
);

final opacidadeDoDesenhoProvider = StateProvider<double>(
  (ref) => _lido(ref, 1.0, (p) => p.getDouble(_chaveDaOpacidade)),
);

/// A CAMADA que esta recebendo os tracos desta sessao: a barra precisa
/// dela para desfazer, e ela mora aqui porque a barra fica no palco (em
/// pixels da tela) e o dedo desenha dentro da composicao.
final camadaDoDesenhoProvider = StateProvider<String?>((ref) => null);

/// DESENHO A MAO LIVRE: cobre a composicao enquanto a sessao esta
/// ligada. Cada traco entra na MESMA camada de desenho — caneta, pincel
/// macio, balde que pinta a regiao cercada e borracha que tira tinta —
/// ate o ✓ concluir.
class FreehandOverlay extends ConsumerStatefulWidget {
  const FreehandOverlay({super.key, required this.playback});

  final PlaybackController playback;

  @override
  ConsumerState<FreehandOverlay> createState() => _FreehandOverlayState();
}

class _FreehandOverlayState extends ConsumerState<FreehandOverlay> {
  final List<Offset> _pontos = [];
  String? _projectId;
  Duration _startTime = Duration.zero;

  /// O dedo desenha em coordenadas da COMPOSICAO; a camada mora no
  /// centro dela, e o traco guarda a diferenca.
  Offset get _centroDaComposicao {
    final p = ref.read(editorControllerProvider);
    return Offset(p.outputWidth / 2, p.outputHeight / 2);
  }

  /// ONDE MORA A CAMADA que recebe o traco: continuar um desenho que ja
  /// foi arrastado para o canto nao pode jogar a tinta nova no meio.
  Offset _origemDaCamada(String id) {
    final l = ref.read(editorControllerProvider).layerById(id);
    if (l == null) return _centroDaComposicao;
    return l.position.valueAt(l.localTime(widget.playback.time.value));
  }

  Rect get _areaDaComposicao {
    final p = ref.read(editorControllerProvider);
    return Rect.fromCenter(
      center: Offset.zero,
      width: p.outputWidth.toDouble(),
      height: p.outputHeight.toDouble(),
    );
  }

  TracoDoDesenho _traco(FerramentaDeDesenho f, List<Offset> pontos) =>
      TracoDoDesenho(
        ferramenta: f,
        pontos: pontos,
        cor: ref.read(corDoDesenhoProvider),
        espessura: f == FerramentaDeDesenho.balde
            ? celulaDoBalde * 2
            : ref.read(espessuraDoDesenhoProvider),
        dureza: ref.read(durezaDoDesenhoProvider),
        opacidade: ref.read(opacidadeDoDesenhoProvider),
      );

  /// A camada da sessao: a mesma ate o ✓, criada no primeiro traco.
  String _camada() {
    final atual = ref.read(camadaDoDesenhoProvider);
    if (atual != null &&
        ref.read(editorControllerProvider).layerById(atual) != null) {
      return atual;
    }
    final controller = ref.read(editorControllerProvider.notifier);
    // CONTINUAR UM DESENHO: se a camada escolhida ja e um desenho, os
    // tracos novos entram nela em vez de virar outra camada por cima.
    final escolhida = ref.read(selectedLayerProvider);
    if (escolhida != null && controller.desenhoDaCamada(escolhida) != null) {
      ref.read(camadaDoDesenhoProvider.notifier).state = escolhida;
      return escolhida;
    }
    final novo = ref
        .read(editorControllerProvider.notifier)
        .criarCamadaDeDesenho(_startTime, nome: 'Desenho livre');
    ref.read(camadaDoDesenhoProvider.notifier).state = novo;
    return novo;
  }

  void _cancelarTraco() {
    _pontos.clear();
    _projectId = null;
    if (mounted) setState(() {});
  }

  void _fimDoTraco() {
    final pts = List<Offset>.of(_pontos);
    final mesmoProjeto = _projectId == ref.read(editorControllerProvider).id;
    _pontos.clear();
    _projectId = null;
    // Um gesto interrompido pela troca de projeto nao pode gravar no proximo.
    if (!ref.read(freehandRequestProvider) || !mesmoProjeto) return;
    if (pts.isEmpty) {
      setState(() {});
      return;
    }
    final ferramenta = ref.read(ferramentaDoDesenhoProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    if (ferramenta == FerramentaDeDesenho.balde) {
      final id = _camada();
      final centro = _origemDaCamada(id);
      final regiao = regiaoDoBalde(
        controller.desenhoDaCamada(id)?.tracos ?? const [],
        pts.last - centro,
        _areaDaComposicao.shift(_centroDaComposicao - centro),
        celula: celulaDoBalde,
      );
      if (regiao != null) {
        controller.adicionarTracoAoDesenho(
          id,
          _traco(FerramentaDeDesenho.balde, regiao),
        );
      }
      setState(() {});
      return;
    }
    if (pts.length < 2) {
      setState(() {});
      return;
    }
    final id = _camada();
    final centro = _origemDaCamada(id);
    controller.adicionarTracoAoDesenho(
      id,
      _traco(ferramenta, [for (final p in pts) p - centro]),
    );
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    ref.listen(freehandRequestProvider, (_, ativo) {
      if (!ativo) {
        _pontos.clear();
        _projectId = null;
        ref.read(camadaDoDesenhoProvider.notifier).state = null;
      }
    });
    final ligado = ref.watch(freehandRequestProvider);
    if (!ligado) return const SizedBox.shrink();
    final ferramenta = ref.watch(ferramentaDoDesenhoProvider);
    return Listener(
      onPointerCancel: (_) => _cancelarTraco(),
      child: GestureDetector(
        key: const ValueKey('freehand-canvas'),
        behavior: HitTestBehavior.opaque,
        dragStartBehavior: DragStartBehavior.down,
        onTapDown: (d) {
          widget.playback.pause();
          _projectId = ref.read(editorControllerProvider).id;
          _startTime = widget.playback.time.value;
          _pontos
            ..clear()
            ..add(d.localPosition);
        },
        // O BALDE trabalha no toque: um pingo de tinta na regiao cercada.
        onTapUp: (_) => _fimDoTraco(),
        onTapCancel: _cancelarTraco,
        onPanStart: (d) {
          widget.playback.pause();
          _projectId = ref.read(editorControllerProvider).id;
          _startTime = widget.playback.time.value;
          setState(
            () => _pontos
              ..clear()
              ..add(d.localPosition),
          );
        },
        onPanUpdate: (d) => setState(() => _pontos.add(d.localPosition)),
        onPanEnd: (_) => _fimDoTraco(),
        onPanCancel: _cancelarTraco,
        child: CustomPaint(
          painter: _RabiscoPainter(
            pontos: _pontos,
            cor: ferramenta == FerramentaDeDesenho.borracha
                ? const Color(0x88FFFFFF)
                : ref.watch(corDoDesenhoProvider),
            espessura: ferramenta == FerramentaDeDesenho.balde
                ? 2
                : ref.watch(espessuraDoDesenhoProvider),
          ),
          child: const SizedBox.expand(),
        ),
      ),
    );
  }
}

/// A BARRA DO DESENHO: ferramenta, cor, numeros, desfazer e concluir.
/// Fica no palco, e nao dentro da composicao — assim o zoom da previa
/// nao muda o tamanho dos botoes.
class BarraDoDesenho extends ConsumerWidget {
  const BarraDoDesenho({super.key, required this.playback});

  final PlaybackController playback;

  /// O desenho de cada ferramenta. A borracha e nossa, desenhada a mao:
  /// nenhum dos dois conjuntos de icones tem uma, e curativo ou varinha
  /// de magica nao dizem "apagar".
  static Widget _icone(FerramentaDeDesenho f, Color cor) => switch (f) {
    FerramentaDeDesenho.borracha => SizedBox(
      width: 16,
      height: 16,
      child: CustomPaint(painter: _PintorDaBorracha(cor)),
    ),
    _ => Icon(
      switch (f) {
        FerramentaDeDesenho.caneta => CupertinoIcons.pencil,
        FerramentaDeDesenho.pincel => CupertinoIcons.paintbrush_fill,
        _ => Icons.format_color_fill,
      },
      size: 16,
      color: cor,
    ),
  };

  Future<void> _ajustes(BuildContext context, WidgetRef ref) async {
    playback.pause();
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: AmColors.panel,
      barrierColor: Colors.black26,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
      ),
      builder: (folha) => Consumer(
        builder: (folha, ref, _) {
          final pincel =
              ref.watch(ferramentaDoDesenhoProvider) ==
              FerramentaDeDesenho.pincel;
          return SafeArea(
            child: Padding(
              padding: const EdgeInsets.fromLTRB(16, 14, 16, 16),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const AppText(
                    'Ajustes do desenho',
                    style: TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                  const SizedBox(height: 8),
                  ParameterRow(
                    label: 'Espessura',
                    value: ref.watch(espessuraDoDesenhoProvider),
                    min: 1,
                    max: 200,
                    unitsPerPixel: .5,
                    decimals: 0,
                    unit: 'px',
                    valueKey: const ValueKey('desenho-espessura'),
                    onChanged: (v) {
                      ref.read(espessuraDoDesenhoProvider.notifier).state = v;
                      _lembrar(ref, (p) => p.setDouble(_chaveDaEspessura, v));
                    },
                  ),
                  if (pincel)
                    ParameterRow(
                      label: 'Dureza',
                      value: ref.watch(durezaDoDesenhoProvider) * 100,
                      min: 0,
                      max: 100,
                      unitsPerPixel: .35,
                      decimals: 0,
                      unit: '%',
                      valueKey: const ValueKey('desenho-dureza'),
                      onChanged: (v) {
                        ref.read(durezaDoDesenhoProvider.notifier).state =
                            v / 100;
                        _lembrar(
                          ref,
                          (p) => p.setDouble(_chaveDaDureza, v / 100),
                        );
                      },
                    ),
                  ParameterRow(
                    label: 'Opacidade',
                    value: ref.watch(opacidadeDoDesenhoProvider) * 100,
                    min: 0,
                    max: 100,
                    unitsPerPixel: .35,
                    decimals: 0,
                    unit: '%',
                    valueKey: const ValueKey('desenho-opacidade'),
                    onChanged: (v) {
                      ref.read(opacidadeDoDesenhoProvider.notifier).state =
                          v / 100;
                      _lembrar(
                        ref,
                        (p) => p.setDouble(_chaveDaOpacidade, v / 100),
                      );
                    },
                  ),
                ],
              ),
            ),
          );
        },
      ),
    );
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final ferramenta = ref.watch(ferramentaDoDesenhoProvider);
    final cor = ref.watch(corDoDesenhoProvider);
    final camada = ref.watch(camadaDoDesenhoProvider);
    return Material(
      color: const Color(0xE620242B),
      borderRadius: BorderRadius.circular(10),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 3),
        child: Row(
          children: [
            for (final f in FerramentaDeDesenho.values)
              Tooltip(
                message: rotuloDaFerramenta(f),
                child: GestureDetector(
                  key: ValueKey('desenho-${f.name}'),
                  behavior: HitTestBehavior.opaque,
                  onTap: () {
                    ref.read(ferramentaDoDesenhoProvider.notifier).state = f;
                    _lembrar(ref, (p) => p.setInt(_chaveDaFerramenta, f.index));
                  },
                  child: Container(
                    width: 32,
                    height: 32,
                    alignment: Alignment.center,
                    decoration: BoxDecoration(
                      color: f == ferramenta
                          ? AmColors.accent.withValues(alpha: .22)
                          : Colors.transparent,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: _icone(
                      f,
                      f == ferramenta ? AmColors.accent : Colors.white70,
                    ),
                  ),
                ),
              ),
            const SizedBox(width: 2),
            Tooltip(
              message: 'Cor do desenho',
              child: GestureDetector(
                key: const ValueKey('desenho-cor'),
                behavior: HitTestBehavior.opaque,
                onTap: () async {
                  playback.pause();
                  final nova = await showColorPicker(
                    context,
                    initial: cor,
                    onChanged: (c) =>
                        ref.read(corDoDesenhoProvider.notifier).state = c,
                  );
                  if (nova != null) {
                    ref.read(corDoDesenhoProvider.notifier).state = nova;
                  }
                  _lembrar(
                    ref,
                    (p) => p.setInt(
                      _chaveDaCor,
                      ref.read(corDoDesenhoProvider).toARGB32(),
                    ),
                  );
                },
                child: Container(
                  width: 22,
                  height: 22,
                  decoration: BoxDecoration(
                    color: cor,
                    shape: BoxShape.circle,
                    border: Border.all(color: Colors.white38, width: 1.5),
                  ),
                ),
              ),
            ),
            const SizedBox(width: 6),
            Tooltip(
              message: 'Espessura, dureza e opacidade',
              child: GestureDetector(
                key: const ValueKey('desenho-ajustes'),
                behavior: HitTestBehavior.opaque,
                onTap: () => _ajustes(context, ref),
                child: Container(
                  height: 24,
                  padding: const EdgeInsets.symmetric(horizontal: 9),
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    color: Colors.white10,
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Text(
                    ref.watch(espessuraDoDesenhoProvider).round().toString(),
                    style: const TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                      color: Colors.white,
                    ),
                  ),
                ),
              ),
            ),
            const Spacer(),
            IconButton(
              key: const ValueKey('desenho-desfazer'),
              tooltip: 'Desfazer o último traço',
              padding: EdgeInsets.zero,
              constraints: const BoxConstraints.tightFor(width: 34, height: 34),
              onPressed: camada == null
                  ? null
                  : () => ref
                        .read(editorControllerProvider.notifier)
                        .tirarUltimoTracoDoDesenho(camada),
              icon: const Icon(
                CupertinoIcons.arrow_uturn_left,
                size: 17,
                color: Colors.white,
              ),
            ),
            IconButton(
              key: const ValueKey('desenho-concluir'),
              tooltip: 'Cancelar desenho livre',
              padding: EdgeInsets.zero,
              constraints: const BoxConstraints.tightFor(width: 34, height: 34),
              onPressed: () =>
                  ref.read(freehandRequestProvider.notifier).state = false,
              icon: const Icon(
                CupertinoIcons.checkmark_alt,
                size: 19,
                color: AmColors.accent,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// A BORRACHA da barra: um bloco inclinado com a ponta cheia, apoiado
/// na linha do papel.
class _PintorDaBorracha extends CustomPainter {
  const _PintorDaBorracha(this.cor);

  final Color cor;

  @override
  void paint(Canvas canvas, Size size) {
    final s = size.shortestSide;
    final traco = Paint()
      ..color = cor
      ..style = PaintingStyle.stroke
      ..strokeWidth = s * .11
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;
    canvas.save();
    canvas.translate(size.width * .5, size.height * .42);
    canvas.rotate(-math.pi / 4);
    final corpo = Path()
      ..addRRect(
        RRect.fromRectAndRadius(
          Rect.fromCenter(center: Offset.zero, width: s * .62, height: s * .36),
          Radius.circular(s * .07),
        ),
      );
    // A metade que gasta o papel vai cheia.
    canvas.drawPath(
      Path.combine(
        PathOperation.intersect,
        corpo,
        Path()..addRect(Rect.fromLTRB(-s, 0, s, s)),
      ),
      Paint()..color = cor,
    );
    canvas.drawPath(corpo, traco);
    canvas.restore();
    canvas.drawLine(
      Offset(s * .12, size.height * .88),
      Offset(size.width - s * .12, size.height * .88),
      traco,
    );
  }

  @override
  bool shouldRepaint(_PintorDaBorracha old) => old.cor != cor;
}

class _RabiscoPainter extends CustomPainter {
  const _RabiscoPainter({
    required this.pontos,
    required this.cor,
    required this.espessura,
  });

  final List<Offset> pontos;
  final Color cor;
  final double espessura;

  @override
  void paint(Canvas canvas, Size size) {
    if (pontos.length < 2) return;
    canvas.drawPath(
      caminhoDoTraco(pontos),
      Paint()
        ..color = cor
        ..style = PaintingStyle.stroke
        ..strokeWidth = espessura
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(_RabiscoPainter old) => true;
}
