import 'package:flutter/foundation.dart' show ValueListenable, listEquals;
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../application/perfil3d.dart';
import '../../../application/playback_controller.dart';
import '../../am/layer_look.dart' show layerTypeColor;

// ===========================================================================
// A TIMELINE — ESQUELETO DA FUNDACAO
// ===========================================================================
//
// O minimo que a casca precisa para ser usavel: regua, uma linha de 28 por
// camada com o clipe, o cabecote FIXO no centro (rolar = scrub), toque no
// clipe seleciona e toque no vazio solta. A frente da timeline reescreve
// esta pasta inteira (trim, mover, reordenar, pinca, ima, keyframes
// arrastaveis, virtualizacao); o contrato com a casca e so o construtor de
// [TimelineDoEditor].
//
// DESEMPENHO, ja nesta versao: o cabecote NAO reconstroi widget nenhum. As
// linhas e a regua repintam pelo `repaint:` do relogio; o widget so se
// refaz quando muda a ESTRUTURA (camadas, inicio, duracao, nome) — um
// passo de slider nao chega aqui.

/// Uma faixa da timeline, so com o que o desenho usa (compara por valor).
@immutable
class _Faixa {
  const _Faixa({
    required this.id,
    required this.nome,
    required this.inicioUs,
    required this.duracaoUs,
    required this.cor,
    required this.oculta,
  });

  final String id;
  final String nome;
  final int inicioUs;
  final int duracaoUs;
  final Color cor;
  final bool oculta;

  @override
  bool operator ==(Object o) =>
      o is _Faixa &&
      o.id == id &&
      o.nome == nome &&
      o.inicioUs == inicioUs &&
      o.duracaoUs == duracaoUs &&
      o.cor == cor &&
      o.oculta == oculta;

  @override
  int get hashCode => Object.hash(id, nome, inicioUs, duracaoUs, cor, oculta);
}

@immutable
class _Faixas {
  const _Faixas(this.lista);

  final List<_Faixa> lista;

  @override
  bool operator ==(Object o) => o is _Faixas && listEquals(o.lista, lista);

  @override
  int get hashCode => Object.hashAll(lista);
}

/// OS INSTANTES COM MARCA da camada selecionada (tempo da composicao, µs).
@immutable
class _Marcas {
  const _Marcas(this.us);

  final List<int> us;

  @override
  bool operator ==(Object o) => o is _Marcas && listEquals(o.us, us);

  @override
  int get hashCode => Object.hashAll(us);
}

class TimelineDoEditor extends ConsumerStatefulWidget {
  const TimelineDoEditor({
    super.key,
    required this.playback,
    this.aoScrub,
    this.aoTocarNoVazio,
    this.aoTocarNaCamada,
  });

  final PlaybackController playback;

  /// A cada passo de scrub (o gerente de video toca lasquinhas de som).
  final VoidCallback? aoScrub;

  /// Toque fora de qualquer clipe: a casca solta a selecao.
  final VoidCallback? aoTocarNoVazio;

  /// Toque num clipe (depois de selecionar).
  final ValueChanged<String>? aoTocarNaCamada;

  @override
  ConsumerState<TimelineDoEditor> createState() => _TimelineDoEditorState();
}

class _TimelineDoEditorState extends ConsumerState<TimelineDoEditor> {
  static const double _pps = AureaDims.dpPorSegundo;

  Duration _tempoNoToque = Duration.zero;
  double _arrastado = 0;

  void _comecarScrub(DragStartDetails _) {
    widget.playback.pause();
    _tempoNoToque = widget.playback.time.value;
    _arrastado = 0;
  }

  void _scrub(DragUpdateDetails d, Duration total) {
    _arrastado += d.delta.dx;
    // O CABECOTE E FIXO: arrastar o conteudo para a DIREITA traz o passado
    // para baixo dele — o tempo volta.
    final us =
        _tempoNoToque.inMicroseconds - (_arrastado / _pps * 1e6).round();
    final limite = total.inMicroseconds;
    widget.playback.seek(Duration(microseconds: us.clamp(0, limite)));
    widget.aoScrub?.call();
  }

  void _tocarNaLinha(_Faixa f, Offset local, double largura) {
    final t = widget.playback.time.value.inMicroseconds;
    final centro = largura / 2;
    final x0 = centro + (f.inicioUs - t) / 1e6 * _pps;
    final x1 = x0 + f.duracaoUs / 1e6 * _pps;
    if (local.dx >= x0 && local.dx <= x1) {
      widget.playback.pause();
      ref.read(multiSelectProvider.notifier).state = const {};
      ref.read(selectedLayerProvider.notifier).state = f.id;
      widget.aoTocarNaCamada?.call(f.id);
    } else {
      widget.aoTocarNoVazio?.call();
    }
  }

  @override
  Widget build(BuildContext context) {
    Perfil3D.contar('build.timeline');
    final faixas = ref.watch(
      editorControllerProvider.select(
        (p) => _Faixas([
          for (final l in p.layers)
            _Faixa(
              id: l.id,
              nome: l.name,
              inicioUs: l.startTime.inMicroseconds,
              duracaoUs: l.duration.inMicroseconds,
              cor: layerTypeColor(l),
              oculta: p.metaOf(l.id).hidden,
            ),
        ]),
      ),
    );
    final total = ref.watch(editorControllerProvider.select((p) => p.duration));
    final fps = ref.watch(editorControllerProvider.select((p) => p.fps));
    final selecionada = ref.watch(selectedLayerProvider);
    final marcas = selecionada == null
        ? const _Marcas([])
        : ref.watch(
            editorControllerProvider.select((p) {
              final l = p.layerById(selecionada);
              if (l == null) return const _Marcas([]);
              return _Marcas([
                for (final k in l.keyframeTimes)
                  (l.startTime + k).inMicroseconds,
              ]);
            }),
          );

    // A FONTE DO APP nos pintores: `TextPainter` nao herda o tema, e sem
    // isto a regua e os nomes dos clipes sairiam na fonte crua do sistema.
    final base = DefaultTextStyle.of(context).style;

    return ColoredBox(
      key: const ValueKey('timeline-nova'),
      color: AureaCores.cromo,
      child: LayoutBuilder(
        builder: (context, c) {
          final largura = c.maxWidth;
          return GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: widget.aoTocarNoVazio,
            onHorizontalDragStart: _comecarScrub,
            onHorizontalDragUpdate: (d) => _scrub(d, total),
            child: Stack(
              children: [
                Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    SizedBox(
                      key: const ValueKey('timeline-regua'),
                      height: AureaDims.regua,
                      child: RepaintBoundary(
                        child: CustomPaint(
                          painter: _PintorDaRegua(
                            tempo: widget.playback.time,
                            fps: fps <= 0 ? 30 : fps,
                            risco: AureaCores.textoSecundario,
                            estilo: base.copyWith(
                              fontSize: 10,
                              color: AureaCores.textoSecundario,
                              decoration: TextDecoration.none,
                            ),
                          ),
                        ),
                      ),
                    ),
                    Expanded(
                      child: faixas.lista.isEmpty
                          ? Center(
                              child: AppText(
                                'Toque em + para adicionar a primeira camada',
                                style: AureaEstilos.propriedade,
                              ),
                            )
                          : ListView.builder(
                              key: const ValueKey('timeline-linhas'),
                              padding: const EdgeInsets.only(
                                bottom: AureaDims.botaoAdicionar,
                              ),
                              itemExtent: AureaDims.linhaDeCamada,
                              itemCount: faixas.lista.length,
                              itemBuilder: (context, i) {
                                final f = faixas.lista[i];
                                final escolhida = f.id == selecionada;
                                return GestureDetector(
                                  key: ValueKey('linha-${f.id}'),
                                  behavior: HitTestBehavior.opaque,
                                  onTapUp: (d) =>
                                      _tocarNaLinha(f, d.localPosition, largura),
                                  child: RepaintBoundary(
                                    child: CustomPaint(
                                      painter: _PintorDaLinha(
                                        faixa: f,
                                        tempo: widget.playback.time,
                                        escolhida: escolhida,
                                        marcasUs: escolhida
                                            ? marcas.us
                                            : const [],
                                        selecao: AureaCores.texto,
                                        keyframe: AureaCores.keyframe,
                                        rotulo: base.copyWith(
                                          fontSize: AureaDims.rotuloDoClipe,
                                          color: AureaCores.texto,
                                          decoration: TextDecoration.none,
                                        ),
                                      ),
                                    ),
                                  ),
                                );
                              },
                            ),
                    ),
                  ],
                ),
                // O CABECOTE: linha de 1,5 FIXA no centro, por cima de tudo
                // e sem pegar toque (quem rola e o conteudo).
                Positioned(
                  left: largura / 2 - AureaDims.cabecote / 2,
                  top: AureaDims.regua * .45,
                  bottom: 0,
                  width: AureaDims.cabecote,
                  child: IgnorePointer(
                    child: ColoredBox(
                      key: const ValueKey('timeline-cabecote'),
                      color: AureaCores.cabecote,
                    ),
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}

/// A REGUA: riscos de segundo (18), meio segundo (12) e quadro (5, so
/// quando cabem com 4 de vao), com o tempo de cada segundo.
class _PintorDaRegua extends CustomPainter {
  _PintorDaRegua({
    required this.tempo,
    required this.fps,
    required this.risco,
    required this.estilo,
  }) : super(repaint: tempo);

  final ValueListenable<Duration> tempo;
  final int fps;
  final Color risco;
  final TextStyle estilo;

  final Map<int, TextPainter> _rotulos = {};

  TextPainter _rotulo(int segundo) => _rotulos.putIfAbsent(segundo, () {
    final m = segundo ~/ 60;
    final s = segundo % 60;
    return TextPainter(
      text: TextSpan(
        text: '$m:${s.toString().padLeft(2, '0')}',
        style: estilo,
      ),
      textDirection: TextDirection.ltr,
    )..layout();
  });

  @override
  void paint(Canvas canvas, Size size) {
    const pps = AureaDims.dpPorSegundo;
    final t = tempo.value.inMicroseconds / 1e6;
    final centro = size.width / 2;
    final base = size.height;
    final pincel = Paint()
      ..color = risco
      ..strokeWidth = AureaDims.larguraDoRisco;
    final primeiro = (t - centro / pps).floor() - 1;
    final ultimo = (t + centro / pps).ceil() + 1;
    final passoDoQuadro = pps / fps;
    final comQuadros = passoDoQuadro >= AureaDims.vaoMinimoDoRisco;
    for (var s = primeiro < 0 ? 0 : primeiro; s <= ultimo; s++) {
      final x = centro + (s - t) * pps;
      canvas.drawLine(
        Offset(x, base - AureaDims.riscoDeSegundo),
        Offset(x, base),
        pincel,
      );
      _rotulo(s).paint(canvas, Offset(x + 3, base - AureaDims.riscoDeSegundo - 2 - 12));
      final meio = x + pps / 2;
      canvas.drawLine(
        Offset(meio, base - AureaDims.riscoDeMeioSegundo),
        Offset(meio, base),
        pincel,
      );
      if (comQuadros) {
        for (var q = 1; q < fps; q++) {
          final xq = x + q * passoDoQuadro;
          if ((xq - meio).abs() < 1) continue;
          canvas.drawLine(
            Offset(xq, base - AureaDims.riscoDeQuadro),
            Offset(xq, base),
            pincel,
          );
        }
      }
    }
  }

  @override
  bool shouldRepaint(_PintorDaRegua old) =>
      old.fps != fps || old.risco != risco || old.estilo != estilo;
}

/// UMA LINHA: o clipe (23 de altura, recuo 2,5, raio 1,5), o nome dentro
/// dele e, na escolhida, o contorno e os losangos.
class _PintorDaLinha extends CustomPainter {
  _PintorDaLinha({
    required this.faixa,
    required this.tempo,
    required this.escolhida,
    required this.marcasUs,
    required this.selecao,
    required this.keyframe,
    required this.rotulo,
  }) : super(repaint: tempo);

  final _Faixa faixa;
  final ValueListenable<Duration> tempo;
  final bool escolhida;
  final List<int> marcasUs;
  final Color selecao;
  final Color keyframe;
  final TextStyle rotulo;

  TextPainter? _nome;

  @override
  void paint(Canvas canvas, Size size) {
    const pps = AureaDims.dpPorSegundo;
    final t = tempo.value.inMicroseconds;
    final centro = size.width / 2;
    final x0 = centro + (faixa.inicioUs - t) / 1e6 * pps;
    final x1 = x0 + faixa.duracaoUs / 1e6 * pps;
    if (x1 < 0 || x0 > size.width) return;
    final caixa = Rect.fromLTRB(
      x0,
      AureaDims.recuoDoClipe,
      x1,
      size.height - AureaDims.recuoDoClipe,
    );
    final rr = RRect.fromRectAndRadius(
      caixa,
      const Radius.circular(AureaDims.raioDoClipe),
    );
    canvas.drawRRect(
      rr,
      Paint()
        ..color = faixa.cor.withValues(
          alpha: faixa.oculta ? .35 : (escolhida ? 1 : .8),
        ),
    );
    // O NOME acompanha o comeco visivel do clipe: com o inicio fora da
    // tela, ele gruda na borda esquerda em vez de sumir junto.
    final nome = _nome ??= TextPainter(
      text: TextSpan(text: faixa.nome, style: rotulo),
      maxLines: 1,
      ellipsis: '…',
      textDirection: TextDirection.ltr,
    )..layout();
    final xNome =
        (x0 < 0 ? 0.0 : x0) + AureaDims.recuoDoRotuloDoClipe / 2;
    if (x1 - xNome > 12) {
      canvas.save();
      canvas.clipRect(caixa);
      nome.paint(canvas, Offset(xNome, (size.height - nome.height) / 2));
      canvas.restore();
    }
    if (escolhida) {
      canvas.drawRRect(
        rr.deflate(AureaDims.tracoDeSelecao / 2),
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = AureaDims.tracoDeSelecao
          ..color = selecao,
      );
      final losango = Paint()..color = keyframe;
      const r = AureaDims.raioDoKeyframe;
      final y = size.height - AureaDims.recuoDoClipe - r - 1;
      for (final us in marcasUs) {
        final x = centro + (us - t) / 1e6 * pps;
        if (x < -r || x > size.width + r) continue;
        final p = Path()
          ..moveTo(x, y - r)
          ..lineTo(x + r, y)
          ..lineTo(x, y + r)
          ..lineTo(x - r, y)
          ..close();
        canvas.drawPath(p, losango);
      }
    }
  }

  @override
  bool shouldRepaint(_PintorDaLinha old) =>
      old.faixa != faixa ||
      old.escolhida != escolhida ||
      !listEquals(old.marcasUs, marcasUs) ||
      old.selecao != selecao ||
      old.keyframe != keyframe ||
      old.rotulo != rotulo;
}
