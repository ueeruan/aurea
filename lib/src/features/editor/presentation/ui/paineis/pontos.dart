import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/editor_session.dart';
import '../../../domain/layer.dart';
import '../../../domain/mask.dart';
import '../../../domain/path_edit.dart';
import '../../widgets/mask_node_editor.dart';
import '../shell/contrato.dart';
import 'comum.dart';

// EDITAR PONTOS — os nos do caminho da FORMA ou da MASCARA.
//
// Abrir e fechar vieram intactos do editor antigo (`_abrirEditPoints`,
// `_abrirMaskEditPoints`, `_fecharEditPoints`): o palco desenha os nos
// quando `pathEditTargetProvider` aponta um caminho E a sessao diz que o
// painel de pontos esta aberto — por isso abrir e fechar aqui espelham a
// sessao.
//
// O PAINEL E UM TRACKPAD (a descoberta do editor antigo que continua
// valendo): o dedo nunca cobre o desenho. Desliza-se no painel e um
// cursor em cruz anda no palco; o toque crava ou seleciona.

/// ABRE O EDITOR DE PONTOS DA FORMA [layerId]: a geometria vira caminho
/// (se ainda nao e), o palco passa a mirar nela e o painel abre. Devolve
/// se abriu.
bool abrirEditarPontosDaForma(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
  String layerId,
) {
  final controller = ref.read(editorControllerProvider.notifier);
  // BLOQUEADA NAO ABRE, e diz por que: o cadeado recusa criar o contorno,
  // e o erro generico ("sem caminho editavel") mentiria.
  if (controller.isLocked(layerId)) {
    AureaSnack.show(
      context,
      'Camada bloqueada: desbloqueie para editar os pontos',
      actionLabel: 'Desbloquear',
      onAction: () => controller.toggleLocked(layerId),
    );
    return false;
  }
  final itemId = controller.ensureShapeBezierGeometry(
    layerId,
    playback.time.value,
  );
  if (itemId == null) {
    AureaSnack.show(context, 'Esta camada não tem caminho editável');
    return false;
  }
  playback.pause();
  ref.read(selectedLayerProvider.notifier).state = layerId;
  _mirar(ref, PathEditTarget(layerId, itemId, forma: true));
  ref
      .read(editorSessionProvider.notifier)
      .openEditPoints(itemId, returnTo: EditorPanel.editShape);
  ref.read(painelAbertoProvider.notifier).state = PainelId.pontos;
  return true;
}

/// ABRE O EDITOR DE PONTOS DA MASCARA [maskId] da camada selecionada.
bool abrirEditarPontosDaMascara(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
  String maskId,
) {
  final id = ref.read(selectedLayerProvider);
  if (id == null) return false;
  final layer = ref.read(editorControllerProvider).layerById(id);
  if (layer == null || !layer.masks.any((m) => m.id == maskId)) {
    AureaSnack.show(context, 'Esta máscara não existe mais');
    return false;
  }
  playback.pause();
  _mirar(ref, PathEditTarget(id, maskId, forma: false));
  ref
      .read(editorSessionProvider.notifier)
      .openEditPoints(maskId, returnTo: EditorPanel.blending);
  ref.read(painelAbertoProvider.notifier).state = PainelId.pontos;
  return true;
}

void _mirar(WidgetRef ref, PathEditTarget alvo) {
  ref.read(pathEditTargetProvider.notifier).state = alvo;
  ref.read(pathEditSelectedProvider.notifier).state = null;
  ref.read(pathEditCursorProvider.notifier).state = null;
  ref.read(pathEditModeProvider.notifier).state = PointsMode.move;
}

/// Solta o alvo do editor de nos (o palco para de desenhar os pontos).
void fecharEditarPontos(WidgetRef ref) {
  ref.read(pathEditTargetProvider.notifier).state = null;
  ref.read(pathEditSelectedProvider.notifier).state = null;
  ref.read(pathEditCursorProvider.notifier).state = null;
}

/// O PAINEL DE PONTOS: modos (Mover, Alca, Novo) no topo, o trackpad no
/// meio e as acoes com nome embaixo; o losango do cabecalho crava a forma
/// do contorno no quadro atual (e assim que o contorno faz morph).
class PainelPontos extends ConsumerStatefulWidget {
  const PainelPontos({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelPontos> createState() => _PainelPontosState();
}

class _PainelPontosState extends ConsumerState<PainelPontos> {
  /// Raio de "perto" em px do caminho.
  static const double _raio = 26;

  /// Qual alca o modo Alca puxa.
  Handle _lado = Handle.saida;

  /// No ponto suave, a alca oposta espelha tambem o tamanho.
  bool _alcasIguais = false;

  /// O trackpad anda com o contorno inteiro.
  bool _moverTodos = false;

  PlaybackController get _pb => EscopoDoEditor.of(context).playback;
  Duration get _t => _pb.time.value;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  PathEditTarget? get _alvo => ref.read(pathEditTargetProvider);
  bool get _editaForma => _alvo?.forma ?? true;
  String get _itemId => _alvo?.maskId ?? '';

  /// A CAMADA DE ONDE SE LE a escala e o giro (a que se ve).
  Layer? get _camada =>
      ref.read(projetoVisivelProvider).layerById(widget.layerId);

  /// A TRILHA DO CAMINHO, GRAVADA: e dela que `editShapeBezier` e
  /// `editMaskPath` partem, entao e dela que a conta parte tambem.
  AnimatedPath? _trilha() {
    if (_editaForma) {
      return _c.shapeBezierOf(widget.layerId, _itemId)?.path;
    }
    final l = ref.read(editorControllerProvider).layerById(widget.layerId);
    for (final m in l?.masks ?? const <LayerMask>[]) {
      if (m.id == _itemId) return m.path;
    }
    return null;
  }

  BezierPath? _caminho() {
    final l = _camada;
    if (l == null) return null;
    return _trilha()?.valueAt(l.localTime(_t));
  }

  void _editar(BezierPath Function(BezierPath) fn) {
    Interacao.marcar();
    if (_editaForma) {
      _c.editShapeBezier(widget.layerId, _itemId, _t, fn);
    } else {
      _c.editMaskPath(widget.layerId, _itemId, _t, fn);
    }
  }

  Offset _cursor() {
    final c = ref.read(pathEditCursorProvider);
    if (c != null) return c;
    final caminho = _caminho();
    final sel = ref.read(pathEditSelectedProvider);
    if (caminho != null && sel != null && sel < caminho.vertices.length) {
      return caminho.vertices[sel].p;
    }
    if (caminho != null && caminho.vertices.isNotEmpty) {
      return caminho.vertices.last.p;
    }
    return Offset.zero;
  }

  void _setCursor(Offset p) =>
      ref.read(pathEditCursorProvider.notifier).state = p;

  /// Delta do dedo -> delta no espaco do caminho (desfaz a escala e o giro
  /// da camada, para o cursor andar junto com o dedo).
  Offset _paraCaminho(Offset delta) {
    final l = _camada;
    if (l == null) return delta;
    final local = l.localTime(_t);
    final sx = l.scaleX.valueAt(local), sy = l.scaleY.valueAt(local);
    final giro = -l.rotation.valueAt(local) * math.pi / 180;
    final c = math.cos(giro), s = math.sin(giro);
    final r = Offset(delta.dx * c - delta.dy * s, delta.dx * s + delta.dy * c);
    return Offset(
      sx.abs() < 1e-6 ? r.dx : r.dx / sx,
      sy.abs() < 1e-6 ? r.dy : r.dy / sy,
    );
  }

  // ------------------------------------------------------------ acoes

  void _alternarKeyframe() {
    if (_editaForma) {
      _c.toggleShapeBezierKeyframe(widget.layerId, _itemId, _t);
    } else {
      _c.toggleMaskPathKeyframe(widget.layerId, _itemId, _t);
    }
  }

  /// Crava um ponto no cursor. Caminho aberto: perto do primeiro ponto
  /// fecha; senao anexa no fim. Caminho fechado: entra no segmento mais
  /// proximo.
  void _novoPonto() {
    final caminho = _caminho();
    if (caminho == null) return;
    final p = _cursor();
    final n = caminho.vertices.length;
    final sel = ref.read(pathEditSelectedProvider.notifier);
    if (!caminho.closed &&
        n >= 3 &&
        (caminho.vertices.first.p - p).distance <= _raio) {
      _editar((c) => BezierPath(vertices: c.vertices, closed: true));
      sel.state = 0;
      return;
    }
    if (caminho.closed && n >= 2) {
      final hit = nearestOnPath(caminho, p);
      if (hit != null) {
        _editar(
          (c) => moveVertex(
            insertVertex(c, hit.segment, hit.t),
            hit.segment + 1,
            p,
          ),
        );
        sel.state = hit.segment + 1;
        return;
      }
    }
    _editar(
      (c) => BezierPath(
        vertices: [
          ...c.vertices,
          PathVertex(p: p, corner: false),
        ],
        closed: c.closed,
      ),
    );
    sel.state = n;
  }

  void _selecionarNoCursor() {
    final caminho = _caminho();
    if (caminho == null) return;
    final no = vertexAt(caminho, _cursor(), _raio);
    ref.read(pathEditSelectedProvider.notifier).state = no;
    if (no != null) _setCursor(caminho.vertices[no].p);
  }

  /// O ponto em que as acoes agem: o selecionado, ou — sem selecao — o
  /// ULTIMO (o que a pessoa acabou de cravar; acao desligada por falta
  /// de selecao parecia botao quebrado).
  int? _pontoAlvo() {
    final caminho = _caminho();
    if (caminho == null || caminho.vertices.isEmpty) return null;
    final sel = ref.read(pathEditSelectedProvider);
    if (sel != null && sel < caminho.vertices.length) return sel;
    return caminho.vertices.length - 1;
  }

  void _alternarCanto() {
    final alvo = _pontoAlvo();
    if (alvo == null) return;
    ref.read(pathEditSelectedProvider.notifier).state = alvo;
    _editar((c) => toggleCorner(c, alvo));
  }

  void _apagar() {
    final alvo = _pontoAlvo();
    if (alvo == null) return;
    _editar((c) => removeVertex(c, alvo));
    ref.read(pathEditSelectedProvider.notifier).state = null;
  }

  void _fecharAbrir() =>
      _editar((c) => BezierPath(vertices: c.vertices, closed: !c.closed));

  void _selecionar(int i) {
    final caminho = _caminho();
    if (caminho == null || caminho.vertices.isEmpty) return;
    final n = caminho.vertices.length;
    final j = ((i % n) + n) % n;
    ref.read(pathEditSelectedProvider.notifier).state = j;
    _setCursor(caminho.vertices[j].p);
  }

  void _abrirContorno(String itemId) {
    ref.read(pathEditTargetProvider.notifier).state = PathEditTarget(
      widget.layerId,
      itemId,
      forma: true,
    );
    ref.read(pathEditSelectedProvider.notifier).state = null;
    ref.read(pathEditCursorProvider.notifier).state = null;
    ref
        .read(editorSessionProvider.notifier)
        .openEditPoints(itemId, returnTo: EditorPanel.editShape);
  }

  // --------------------------------------------------------- trackpad

  void _arrasto(Offset delta) {
    final modo = ref.read(pathEditModeProvider);
    final caminho = _caminho();
    final sel = ref.read(pathEditSelectedProvider);
    final d = _paraCaminho(delta);
    final temSel =
        caminho != null && sel != null && sel < caminho.vertices.length;
    if (modo == PointsMode.move &&
        _moverTodos &&
        caminho != null &&
        caminho.vertices.isNotEmpty) {
      _editar((c) => moverTodosOsPontos(c, d));
      _setCursor(_cursor() + d);
      return;
    }
    if (modo == PointsMode.move && temSel) {
      final alvo = caminho.vertices[sel].p + d;
      _editar((c) => moveVertex(c, sel, alvo));
      _setCursor(alvo);
      return;
    }
    if (modo == PointsMode.handle && temSel) {
      final v = caminho.vertices[sel];
      final alvo = v.p + (_lado == Handle.saida ? v.outT : v.inT) + d;
      final iguais = _alcasIguais;
      final lado = _lado;
      _editar((c) => moveHandle(c, sel, lado, alvo, alcasIguais: iguais));
      _setCursor(alvo);
      return;
    }
    Interacao.marcar();
    _setCursor(_cursor() + d);
  }

  void _toque() {
    switch (ref.read(pathEditModeProvider)) {
      case PointsMode.add:
        _novoPonto();
      case PointsMode.move:
      case PointsMode.handle:
        _selecionarNoCursor();
    }
  }

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final alvo = ref.watch(pathEditTargetProvider);
    final chave = 'painel-${PainelId.pontos.name}';
    if (alvo == null || alvo.layerId != widget.layerId) {
      final camada = camadaVisivel(ref, widget.layerId);
      return PainelDePortas(
        titulo: 'Editar pontos',
        chave: chave,
        aviso: 'Escolha uma forma ou máscara para editar os pontos.',
        portas: [
          if (camada is ShapeLayer)
            LinhaDePorta(
              key: const ValueKey('pontos-abrir-forma'),
              rotulo: 'Editar pontos desta forma',
              icone: CupertinoIcons.scribble,
              aoTocar: () => abrirEditarPontosDaForma(
                context,
                ref,
                escopo.playback,
                widget.layerId,
              ),
            ),
        ],
      );
    }
    // A CAMADA EDITADA, nao o projeto: o caminho sai dela.
    ref.watch(
      projetoVisivelProvider.select((p) => p.layerById(widget.layerId)),
    );
    final gravada = camadaGravada(ref, widget.layerId);
    final modo = ref.watch(pathEditModeProvider);
    final sel = ref.watch(pathEditSelectedProvider);
    final caminho = _caminho();
    final n = caminho?.vertices.length ?? 0;
    final temSel = sel != null && sel < n;
    final temPontos = n > 0;
    final contornos = _editaForma
        ? _c.contornosDaForma(widget.layerId)
        : const <String>[];
    final indiceDoContorno = contornos.indexOf(alvo.maskId);

    void fechar() {
      fecharEditarPontos(ref);
      escopo.fecharPainel();
    }

    return AureaPanel(
      titulo: 'Pontos',
      chave: chave,
      aoFechar: fechar,
      acoes: [
        if (gravada != null)
          NoCabecote(
            construir: (context, t) {
              final kf = losangoDasMarcas(
                marcasUs: [
                  for (final k in _trilha()?.keyframes ?? const [])
                    k.time.inMicroseconds,
                ],
                camada: gravada,
                t: t,
                playback: escopo.playback,
                aoAlternar: _alternarKeyframe,
              );
              return AureaKeyframeButton(
                estado: kf.estado,
                aoAnterior: kf.anterior,
                aoProximo: kf.proximo,
                chave: 'pontos-kf',
              );
            },
          ),
      ],
      corpo: Padding(
        padding: const EdgeInsets.fromLTRB(
          AureaDims.margemDoPainel,
          0,
          AureaDims.margemDoPainel,
          AureaDims.e8,
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            SizedBox(
              height: 36,
              child: ListView(
                scrollDirection: Axis.horizontal,
                children: [
                  for (final (m, rotulo, icone) in const [
                    (PointsMode.move, 'Mover', CupertinoIcons.hand_draw),
                    (
                      PointsMode.handle,
                      'Alça',
                      CupertinoIcons.arrow_up_right_diamond,
                    ),
                    (PointsMode.add, 'Novo', CupertinoIcons.plus_circle),
                  ])
                    Padding(
                      padding: const EdgeInsets.only(right: AureaDims.e6),
                      child: Center(
                        child: AureaChip(
                          key: ValueKey('pontos-modo-${m.name}'),
                          rotulo: rotulo,
                          icone: icone,
                          ativo: modo == m,
                          aoTocar: () =>
                              ref.read(pathEditModeProvider.notifier).state = m,
                        ),
                      ),
                    ),
                ],
              ),
            ),
            const SizedBox(height: AureaDims.e4),
            Expanded(
              child: GestureDetector(
                key: const ValueKey('pontos-trackpad'),
                behavior: HitTestBehavior.opaque,
                onPanStart: (_) => _c.beginGesture(),
                onPanUpdate: (d) => _arrasto(d.delta),
                onPanEnd: (_) {
                  _c.endGesture();
                  Interacao.soltar();
                },
                onPanCancel: _c.endGesture,
                onTap: _toque,
                onDoubleTap: _alternarCanto,
                child: CustomPaint(
                  painter: _PintorDoTrackpad(
                    fundo: AureaCores.campo,
                    marca: AureaCores.textoSecundario,
                    cruz: modo == PointsMode.add
                        ? AureaCores.destaque
                        : AureaCores.textoSecundario,
                  ),
                  child: Padding(
                    padding: const EdgeInsets.all(AureaDims.e8),
                    child: Align(
                      alignment: Alignment.topLeft,
                      child: AppText(
                        switch (modo) {
                          PointsMode.move =>
                            temSel
                                ? 'Deslize para mover o ponto'
                                : 'Deslize até um ponto e toque',
                          PointsMode.handle =>
                            temSel
                                ? 'Deslize para puxar a alça'
                                : 'Selecione um ponto antes',
                          PointsMode.add => 'Deslize e toque para cravar',
                        },
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: AureaEstilos.rotulo,
                      ),
                    ),
                  ),
                ),
              ),
            ),
            const SizedBox(height: AureaDims.e6),
            SizedBox(
              height: 32,
              child: ListView(
                key: const ValueKey('pontos-acoes'),
                scrollDirection: Axis.horizontal,
                children: [
                  _acao(
                    'pontos-anterior',
                    '',
                    CupertinoIcons.chevron_left,
                    temPontos ? () => _selecionar((sel ?? n) - 1) : null,
                  ),
                  Center(
                    child: Padding(
                      padding: const EdgeInsets.symmetric(
                        horizontal: AureaDims.e4,
                      ),
                      // "2/5" e numero, nao frase.
                      child: Text(
                        temSel ? '${sel + 1}/$n' : '–/$n',
                        key: const ValueKey('pontos-indice'),
                        style: AureaEstilos.valor,
                      ),
                    ),
                  ),
                  _acao(
                    'pontos-proximo',
                    '',
                    CupertinoIcons.chevron_right,
                    temPontos ? () => _selecionar((sel ?? -1) + 1) : null,
                  ),
                  _acao(
                    'pontos-canto',
                    temSel && caminho!.vertices[sel].corner
                        ? 'Suavizar'
                        : 'Canto',
                    CupertinoIcons.arrow_turn_up_right,
                    temPontos ? _alternarCanto : null,
                  ),
                  _acao(
                    'pontos-apagar',
                    'Apagar',
                    CupertinoIcons.trash,
                    temPontos ? _apagar : null,
                  ),
                  _acao(
                    'pontos-fechar',
                    (caminho?.closed ?? true) ? 'Abrir' : 'Fechar',
                    (caminho?.closed ?? true)
                        ? CupertinoIcons.lock_open
                        : CupertinoIcons.lock,
                    temPontos ? _fecharAbrir : null,
                  ),
                  if (modo == PointsMode.handle) ...[
                    _acao(
                      'pontos-lado-da-alca',
                      _lado == Handle.saida ? 'Saída' : 'Entrada',
                      CupertinoIcons.arrow_right_arrow_left,
                      () => setState(
                        () => _lado = _lado == Handle.saida
                            ? Handle.entrada
                            : Handle.saida,
                      ),
                    ),
                    _acao(
                      'pontos-alcas-iguais',
                      'Alças iguais',
                      CupertinoIcons.equal,
                      () => setState(() => _alcasIguais = !_alcasIguais),
                      ativo: _alcasIguais,
                    ),
                  ],
                  if (modo == PointsMode.move)
                    _acao(
                      'pontos-mover-todos',
                      'Mover tudo',
                      CupertinoIcons.move,
                      () => setState(() => _moverTodos = !_moverTodos),
                      ativo: _moverTodos,
                    ),
                  if (_editaForma) ...[
                    _acao(
                      'pontos-contorno-anterior',
                      '',
                      CupertinoIcons.square_stack,
                      indiceDoContorno > 0
                          ? () =>
                                _abrirContorno(contornos[indiceDoContorno - 1])
                          : null,
                    ),
                    Center(
                      child: Text(
                        '${indiceDoContorno + 1}/${contornos.length}',
                        key: const ValueKey('pontos-contorno'),
                        style: AureaEstilos.valor,
                      ),
                    ),
                    _acao(
                      'pontos-contorno-proximo',
                      '',
                      CupertinoIcons.square_stack_fill,
                      indiceDoContorno >= 0 &&
                              indiceDoContorno < contornos.length - 1
                          ? () =>
                                _abrirContorno(contornos[indiceDoContorno + 1])
                          : null,
                    ),
                    _acao(
                      'pontos-contorno-novo',
                      'Contorno',
                      CupertinoIcons.plus_square_on_square,
                      () {
                        final novo = _c.adicionarContorno(widget.layerId);
                        if (novo == null) return;
                        ref.read(pathEditModeProvider.notifier).state =
                            PointsMode.add;
                        _abrirContorno(novo);
                      },
                    ),
                  ],
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// Uma acao da fileira de baixo: pilula com icone e nome (ou so icone).
  Widget _acao(
    String chave,
    String rotulo,
    IconData icone,
    VoidCallback? aoTocar, {
    bool ativo = false,
  }) {
    final ligada = aoTocar != null;
    return Padding(
      padding: const EdgeInsets.only(right: AureaDims.e6),
      child: Center(
        child: Opacity(
          opacity: ligada ? 1 : .4,
          child: rotulo.isEmpty
              ? Tocavel(
                  key: ValueKey(chave),
                  onTap: aoTocar,
                  child: SizedBox(
                    width: 32,
                    height: 32,
                    child: Icon(
                      icone,
                      size: AureaDims.iconeSm,
                      color: AureaCores.texto,
                    ),
                  ),
                )
              : AureaChip(
                  key: ValueKey(chave),
                  rotulo: rotulo,
                  icone: icone,
                  ativo: ativo,
                  aoTocar: aoTocar,
                ),
        ),
      ),
    );
  }
}

/// O trackpad: cantos marcados e uma cruz no meio como lembrete. As cores
/// chegam de fora (getters do tema lidos no build).
class _PintorDoTrackpad extends CustomPainter {
  const _PintorDoTrackpad({
    required this.fundo,
    required this.marca,
    required this.cruz,
  });

  final Color fundo;
  final Color marca;
  final Color cruz;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Offset.zero & size,
        const Radius.circular(AureaDims.raioXl),
      ),
      Paint()..color = fundo,
    );
    final traco = Paint()
      ..color = marca.withValues(alpha: .7)
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke;
    const m = 8.0, l = 14.0;
    final w = size.width, h = size.height;
    for (final (x, y, sx, sy) in [
      (m, m, 1.0, 1.0),
      (w - m, m, -1.0, 1.0),
      (m, h - m, 1.0, -1.0),
      (w - m, h - m, -1.0, -1.0),
    ]) {
      canvas.drawLine(Offset(x, y), Offset(x + l * sx, y), traco);
      canvas.drawLine(Offset(x, y), Offset(x, y + l * sy), traco);
    }
    final c = Offset(w / 2, h / 2);
    final p = Paint()
      ..color = cruz.withValues(alpha: .6)
      ..strokeWidth = 1.5;
    canvas.drawLine(c - const Offset(12, 0), c + const Offset(12, 0), p);
    canvas.drawLine(c - const Offset(0, 12), c + const Offset(0, 12), p);
    canvas.drawCircle(c, 4, p..style = PaintingStyle.stroke);
  }

  @override
  bool shouldRepaint(_PintorDoTrackpad old) =>
      old.fundo != fundo || old.marca != marca || old.cruz != cruz;
}
