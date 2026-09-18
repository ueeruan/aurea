import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../domain/layer.dart';
import '../../domain/mask.dart';
import '../../domain/path_edit.dart';
import '../widgets/mask_node_editor.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// As duas abas do Edit Points: mexer nos PONTOS ou nos KEYFRAMES deles.
enum AbaDosPontos { pontos, keyframes }

final abaDosPontosProvider = StateProvider<AbaDosPontos>(
  (ref) => AbaDosPontos.pontos,
);

/// Qual alca o modo Alca puxa.
final alcaDosPontosProvider = StateProvider<Handle>((ref) => Handle.saida);

/// ALCAS IGUAIS: no ponto suave, a alca oposta espelha tambem o tamanho.
final alcasIguaisProvider = StateProvider<bool>((ref) => false);

/// MOVER TODOS: o trackpad anda com o contorno inteiro.
final moverTodosOsPontosProvider = StateProvider<bool>((ref) => false);

/// EDIT POINTS (o editor de pontos do Alight Motion, em retrato).
///
/// A descoberta que muda tudo: o dedo NUNCA cobre o desenho. O painel e
/// um TRACKPAD — "deslize aqui para posicionar o proximo ponto, depois
/// toque aqui para crava-lo". Um cursor em cruz (⊹) anda no preview
/// enquanto se desliza; o toque no trackpad crava. Os pontos cravados
/// aparecem como ● no preview (o MaskNodeEditor desenha).
///
/// Trilho esquerdo: `←` voltar, `◌` selecionar/mover, `⌁` alca, `⊕`
/// adicionar, `⋯` (apagar ponto, fechar/abrir, canto/suave). No
/// cabecalho da tela: `◈` keyframe dos pontos (e assim que a AM faz
/// morph) e `⊕` adicionar no cursor.
class PointsPanel extends ConsumerStatefulWidget {
  const PointsPanel({
    super.key,
    required this.playback,
    required this.layerId,
    required this.itemId,
    required this.onBack,
  });

  final PlaybackController playback;
  final String layerId;
  final String itemId;
  final VoidCallback onBack;

  @override
  ConsumerState<PointsPanel> createState() => PointsPanelState();
}

class PointsPanelState extends ConsumerState<PointsPanel> {
  /// Raio de "perto" em px do caminho.
  static const double _raio = 26;

  Duration get _t => widget.playback.time.value;

  Layer? get _layer =>
      ref.read(projetoVisivelProvider).layerById(widget.layerId);

  bool get _editaForma => ref.read(pathEditTargetProvider)?.forma ?? true;

  AnimatedPath? _trilha() {
    final l = _layer;
    if (l == null) return null;
    if (_editaForma) {
      return ref
          .read(editorControllerProvider.notifier)
          .shapeBezierOf(widget.layerId, widget.itemId)
          ?.path;
    }
    for (final m in l.masks) {
      if (m.id == widget.itemId) return m.path;
    }
    return null;
  }

  BezierPath? _caminho() {
    final l = _layer;
    if (l == null) return null;
    return _trilha()?.valueAt(l.localTime(_t));
  }

  void _editar(BezierPath Function(BezierPath) fn) {
    final controller = ref.read(editorControllerProvider.notifier);
    if (_editaForma) {
      controller.editShapeBezier(widget.layerId, widget.itemId, _t, fn);
    } else {
      controller.editMaskPath(widget.layerId, widget.itemId, _t, fn);
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

  /// Delta do dedo no trackpad -> delta no espaco do caminho (desfaz a
  /// escala e o giro da camada, para o cursor andar junto com o dedo).
  Offset _paraCaminho(Offset delta) {
    final l = _layer;
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

  // ---------------------------------------------------------- acoes

  /// `◈` do cabecalho: keyframe nos pontos no tempo de agora.
  void toggleKeyframe() {
    final controller = ref.read(editorControllerProvider.notifier);
    if (_editaForma) {
      controller.toggleShapeBezierKeyframe(widget.layerId, widget.itemId, _t);
    } else {
      controller.toggleMaskPathKeyframe(widget.layerId, widget.itemId, _t);
    }
  }

  bool get temKeyframeAqui {
    final l = _layer;
    if (l == null) return false;
    return _trilha()?.hasKeyframeAt(l.localTime(_t)) ?? false;
  }

  bool get animado => _trilha()?.isAnimated ?? false;

  /// `⊕`: crava um ponto no cursor. Caminho aberto: perto do primeiro
  /// ponto fecha; senao anexa no fim. Caminho fechado: entra no
  /// segmento mais proximo.
  void addPoint() {
    final caminho = _caminho();
    if (caminho == null) return;
    final p = _cursor();
    final n = caminho.vertices.length;
    if (!caminho.closed &&
        n >= 3 &&
        (caminho.vertices.first.p - p).distance <= _raio) {
      _editar((c) => BezierPath(vertices: c.vertices, closed: true));
      ref.read(pathEditSelectedProvider.notifier).state = 0;
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
        ref.read(pathEditSelectedProvider.notifier).state = hit.segment + 1;
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
    ref.read(pathEditSelectedProvider.notifier).state = n;
  }

  void _selecionarNoCursor() {
    final caminho = _caminho();
    if (caminho == null) return;
    final no = vertexAt(caminho, _cursor(), _raio);
    ref.read(pathEditSelectedProvider.notifier).state = no;
    if (no != null) _setCursor(caminho.vertices[no].p);
  }

  /// O ponto em que as acoes agem: o selecionado, ou — se nao ha
  /// selecao — o ULTIMO ponto do caminho.
  ///
  /// Um testador relatou que Canto/Suave "nao funciona". Funcionava: so
  /// estava desligado, porque chegar no editor de pontos nao seleciona
  /// nada e quem acabou de cravar um ponto nao pensa "preciso
  /// selecionar antes". O ultimo ponto e justamente o que a pessoa
  /// acabou de por, entao e nele que ela espera mexer.
  int? _pontoAlvo() {
    final caminho = _caminho();
    if (caminho == null || caminho.vertices.isEmpty) return null;
    final sel = ref.read(pathEditSelectedProvider);
    if (sel != null && sel < caminho.vertices.length) return sel;
    return caminho.vertices.length - 1;
  }

  void _toggleCanto() {
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

  void _fecharAbrir() {
    _editar((c) => BezierPath(vertices: c.vertices, closed: !c.closed));
  }

  // -------------------------------------------------------- trackpad

  void _arrasto(Offset delta) {
    final modo = ref.read(pathEditModeProvider);
    final caminho = _caminho();
    final sel = ref.read(pathEditSelectedProvider);
    final d = _paraCaminho(delta);
    final temSel =
        caminho != null && sel != null && sel < caminho.vertices.length;
    if (modo == PointsMode.move &&
        ref.read(moverTodosOsPontosProvider) &&
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
      final lado = ref.read(alcaDosPontosProvider);
      final alvo = v.p + (lado == Handle.saida ? v.outT : v.inT) + d;
      final iguais = ref.read(alcasIguaisProvider);
      _editar((c) => moveHandle(c, sel, lado, alvo, alcasIguais: iguais));
      _setCursor(alvo);
      return;
    }
    _setCursor(_cursor() + d);
  }

  /// Seleciona o ponto [i] pela regua do contorno.
  void _selecionar(int i) {
    final caminho = _caminho();
    if (caminho == null || i < 0 || i >= caminho.vertices.length) return;
    ref.read(pathEditSelectedProvider.notifier).state = i;
    _setCursor(caminho.vertices[i].p);
  }

  /// Vai para outro contorno da mesma forma.
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

  /// Pula o cabecote para o keyframe anterior/seguinte dos pontos.
  void _pularKeyframe(int direcao) {
    final l = _layer;
    final trilha = _trilha();
    if (l == null || trilha == null || trilha.keyframes.isEmpty) return;
    final agora = l.localTime(_t);
    const folga = Duration(milliseconds: 5);
    final alvo = direcao < 0
        ? trilha.keyframes.lastWhere(
            (k) => k.time < agora - folga,
            orElse: () => trilha.keyframes.first,
          )
        : trilha.keyframes.firstWhere(
            (k) => k.time > agora + folga,
            orElse: () => trilha.keyframes.last,
          );
    widget.playback.pause();
    widget.playback.seek(l.startTime + alvo.time);
    setState(() {});
  }

  void _toque() {
    switch (ref.read(pathEditModeProvider)) {
      case PointsMode.add:
        addPoint();
      case PointsMode.move:
      case PointsMode.handle:
        _selecionarNoCursor();
    }
  }

  @override
  Widget build(BuildContext context) {
    ref.watch(editorControllerProvider);
    final modo = ref.watch(pathEditModeProvider);
    final sel = ref.watch(pathEditSelectedProvider);
    final aba = ref.watch(abaDosPontosProvider);
    final caminho = _caminho();
    final n = caminho?.vertices.length ?? 0;
    final temSel = sel != null && sel < n;

    final caminhoFechado = caminho?.closed ?? true;
    final temPontos = n > 0;
    final contornos = _editaForma
        ? ref
              .read(editorControllerProvider.notifier)
              .contornosDaForma(widget.layerId)
        : const <String>[];
    final indiceDoContorno = contornos.indexOf(widget.itemId);

    return ColoredBox(
      color: AmColors.panel,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // O TRILHO SO TEM OS TRES MODOS.
          //
          // Tinha seis botoes: tres modos e tres acoes, todos so com
          // icone. Num painel baixo os tres de baixo ficavam FORA DA
          // TELA — e um deles era o Canto/Suave que o testador disse que
          // "nao funciona". Ele nem estava aparecendo por inteiro. As
          // acoes desceram para uma fileira com nome, que cabe sempre.
          SizedBox(
            width: 62,
            child: Column(
              children: [
                _ModoBotao(
                  chave: 'pontos-modo-mover',
                  ativo: modo == PointsMode.move,
                  icon: CupertinoIcons.smallcircle_circle,
                  rotulo: 'Mover',
                  onTap: () => ref.read(pathEditModeProvider.notifier).state =
                      PointsMode.move,
                ),
                _ModoBotao(
                  chave: 'pontos-modo-alca',
                  ativo: modo == PointsMode.handle,
                  icon: CupertinoIcons.arrow_up_right_diamond,
                  rotulo: 'Alca',
                  onTap: () => ref.read(pathEditModeProvider.notifier).state =
                      PointsMode.handle,
                ),
                _ModoBotao(
                  chave: 'pontos-modo-add',
                  ativo: modo == PointsMode.add,
                  icon: CupertinoIcons.plus_circle,
                  rotulo: 'Novo',
                  onTap: () => ref.read(pathEditModeProvider.notifier).state =
                      PointsMode.add,
                ),
              ],
            ),
          ),
          Expanded(
            child: Padding(
              padding: const EdgeInsets.fromLTRB(4, 8, 12, 10),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  // ABA E CONTORNO: pontos ou keyframes; qual caminho da
                  // forma; um contorno novo (furo, segunda ilha).
                  SizedBox(
                    height: 34,
                    child: Row(
                      children: [
                        Expanded(
                          child: CupertinoSlidingSegmentedControl<AbaDosPontos>(
                            key: const ValueKey('pontos-abas'),
                            groupValue: aba,
                            thumbColor: AmColors.accentDim,
                            backgroundColor: AmColors.chip,
                            children: const {
                              AbaDosPontos.pontos: Padding(
                                key: ValueKey('pontos-aba-pontos'),
                                padding: EdgeInsets.symmetric(vertical: 5),
                                child: AppText(
                                  'Pontos',
                                  style: TextStyle(fontSize: 12, color: AmColors.text),
                                ),
                              ),
                              AbaDosPontos.keyframes: Padding(
                                key: ValueKey('pontos-aba-keyframes'),
                                padding: EdgeInsets.symmetric(vertical: 5),
                                child: AppText(
                                  'Keyframes',
                                  style: TextStyle(fontSize: 12, color: AmColors.text),
                                ),
                              ),
                            },
                            onValueChanged: (v) {
                              if (v != null) {
                                ref.read(abaDosPontosProvider.notifier).state = v;
                              }
                            },
                          ),
                        ),
                        if (_editaForma) ...[
                          _BotaoPequeno(
                            chave: 'pontos-contorno-anterior',
                            icone: CupertinoIcons.chevron_left,
                            onTap: indiceDoContorno > 0
                                ? () => _abrirContorno(contornos[indiceDoContorno - 1])
                                : null,
                          ),
                          AppText(
                            '${indiceDoContorno + 1}/${contornos.length}',
                            key: const ValueKey('pontos-contorno'),
                            style: const TextStyle(fontSize: 11.5, color: AmColors.muted),
                          ),
                          _BotaoPequeno(
                            chave: 'pontos-contorno-proximo',
                            icone: CupertinoIcons.chevron_right,
                            onTap: indiceDoContorno >= 0 &&
                                    indiceDoContorno < contornos.length - 1
                                ? () => _abrirContorno(contornos[indiceDoContorno + 1])
                                : null,
                          ),
                          _BotaoPequeno(
                            chave: 'pontos-contorno-novo',
                            icone: CupertinoIcons.plus_square_on_square,
                            onTap: () {
                              final novo = ref
                                  .read(editorControllerProvider.notifier)
                                  .adicionarContorno(widget.layerId);
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
                  const SizedBox(height: 6),
                  if (aba == AbaDosPontos.keyframes)
                    Expanded(
                      child: _AbaDeKeyframes(
                        animado: animado,
                        aqui: temKeyframeAqui,
                        quantos: _trilha()?.keyframes.length ?? 0,
                        onCravar: () {
                          toggleKeyframe();
                          setState(() {});
                        },
                        onAnterior: () => _pularKeyframe(-1),
                        onProximo: () => _pularKeyframe(1),
                      ),
                    )
                  else ...[
                  // A REGUA DO CONTORNO: os pontos em fila, o escolhido
                  // maior; tocar seleciona.
                  if (temPontos)
                    SizedBox(
                      height: 30,
                      child: ListView.separated(
                        key: const ValueKey('pontos-regua'),
                        scrollDirection: Axis.horizontal,
                        itemCount: n,
                        separatorBuilder: (_, _) => const SizedBox(width: 6),
                        itemBuilder: (_, i) => GestureDetector(
                          key: ValueKey('pontos-no-$i'),
                          behavior: HitTestBehavior.opaque,
                          onTap: () => _selecionar(i),
                          child: Container(
                            width: sel == i ? 30 : 24,
                            alignment: Alignment.center,
                            decoration: BoxDecoration(
                              color: sel == i ? AmColors.accent : AmColors.chip,
                              shape: BoxShape.circle,
                            ),
                            child: AppText(
                              '${i + 1}',
                              style: TextStyle(
                                fontSize: 11,
                                fontWeight: FontWeight.w700,
                                color: sel == i ? AmColors.onAction : AmColors.text,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ),
                  if (temPontos) const SizedBox(height: 6),
                  if (modo == PointsMode.handle)
                    SizedBox(
                      height: 32,
                      child: Row(
                        children: [
                          Expanded(
                            child: CupertinoSlidingSegmentedControl<Handle>(
                              key: const ValueKey('pontos-lado-da-alca'),
                              groupValue: ref.watch(alcaDosPontosProvider),
                              thumbColor: AmColors.accentDim,
                              backgroundColor: AmColors.chip,
                              children: const {
                                Handle.entrada: Padding(
                                  key: ValueKey('pontos-alca-entrada'),
                                  padding: EdgeInsets.symmetric(vertical: 4),
                                  child: AppText(
                                    'Entrada',
                                    style: TextStyle(fontSize: 11.5, color: AmColors.text),
                                  ),
                                ),
                                Handle.saida: Padding(
                                  key: ValueKey('pontos-alca-saida'),
                                  padding: EdgeInsets.symmetric(vertical: 4),
                                  child: AppText(
                                    'Saída',
                                    style: TextStyle(fontSize: 11.5, color: AmColors.text),
                                  ),
                                ),
                              },
                              onValueChanged: (v) {
                                if (v != null) {
                                  ref.read(alcaDosPontosProvider.notifier).state = v;
                                }
                              },
                            ),
                          ),
                          const SizedBox(width: 6),
                          _Alternador(
                            chave: 'pontos-alcas-iguais',
                            rotulo: 'Alças iguais',
                            ligado: ref.watch(alcasIguaisProvider),
                            onTap: () => ref
                                .read(alcasIguaisProvider.notifier)
                                .update((v) => !v),
                          ),
                        ],
                      ),
                    ),
                  if (modo == PointsMode.move)
                    Align(
                      alignment: Alignment.centerLeft,
                      child: _Alternador(
                        chave: 'pontos-mover-todos',
                        rotulo: 'Mover o contorno inteiro',
                        ligado: ref.watch(moverTodosOsPontosProvider),
                        onTap: () => ref
                            .read(moverTodosOsPontosProvider.notifier)
                            .update((v) => !v),
                      ),
                    ),
                  if (modo != PointsMode.add) const SizedBox(height: 6),
                  AppText(
                    switch (modo) {
                      PointsMode.move =>
                        temSel
                            ? 'Ponto ${sel + 1} de $n: deslize para mover. Toque duplo: canto/suave.'
                            : 'Deslize ate um ponto e toque para selecionar.',
                      PointsMode.handle =>
                        temSel
                            ? 'Deslize para puxar a alca do ponto ${sel + 1}.'
                            : 'Selecione um ponto antes de puxar a alca.',
                      PointsMode.add => 'Deslize aqui para posicionar o proximo ponto, depois toque aqui para crava-lo.',
                    },
                    style: const TextStyle(
                      fontSize: 12.5,
                      color: AmColors.muted,
                    ),
                  ),
                  const SizedBox(height: 8),
                  Expanded(
                    child: GestureDetector(
                      behavior: HitTestBehavior.opaque,
                      onPanUpdate: (d) => _arrasto(d.delta),
                      onTap: _toque,
                      onDoubleTap: _toggleCanto,
                      child: CustomPaint(
                        painter: _TrackpadPainter(modo: modo),
                        child: const SizedBox.expand(),
                      ),
                    ),
                  ),
                  const SizedBox(height: 8),
                  // AS ACOES DO PONTO, COM NOME E SEMPRE VISIVEIS.
                  //
                  // Sem selecao elas agem no ULTIMO ponto — o que a
                  // pessoa acabou de cravar. Ficar desligado por falta de
                  // selecao e o que fazia o botao parecer quebrado.
                  Row(
                    children: [
                      Expanded(
                        child: _AcaoDoPonto(
                          chave: 'pontos-canto',
                          icon: CupertinoIcons.arrow_turn_up_right,
                          rotulo: temSel && caminho!.vertices[sel].corner
                              ? 'Suavizar'
                              : 'Canto',
                          onTap: temPontos ? _toggleCanto : null,
                        ),
                      ),
                      const SizedBox(width: 6),
                      Expanded(
                        child: _AcaoDoPonto(
                          chave: 'pontos-apagar',
                          icon: CupertinoIcons.trash,
                          rotulo: 'Apagar',
                          onTap: temPontos ? _apagar : null,
                        ),
                      ),
                      const SizedBox(width: 6),
                      Expanded(
                        child: _AcaoDoPonto(
                          chave: 'pontos-fechar',
                          icon: caminhoFechado
                              ? CupertinoIcons.lock_open
                              : CupertinoIcons.lock,
                          rotulo: caminhoFechado ? 'Abrir' : 'Fechar',
                          onTap: temPontos ? _fecharAbrir : null,
                        ),
                      ),
                    ],
                  ),
                  ],
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// A ABA DE KEYFRAMES DOS PONTOS: cravar a forma de agora e andar entre
/// as formas cravadas (e assim que o contorno faz morph).
class _AbaDeKeyframes extends StatelessWidget {
  const _AbaDeKeyframes({
    required this.animado,
    required this.aqui,
    required this.quantos,
    required this.onCravar,
    required this.onAnterior,
    required this.onProximo,
  });

  final bool animado;
  final bool aqui;
  final int quantos;
  final VoidCallback onCravar;
  final VoidCallback onAnterior;
  final VoidCallback onProximo;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      AppText(
        animado
            ? '$quantos keyframes na forma dos pontos. Mova o cabeçote, mude os pontos e crave outra forma.'
            : 'Crave a forma de agora; depois mova o cabeçote e mude os pontos para animar o contorno.',
        style: const TextStyle(fontSize: 12, color: AmColors.muted),
      ),
      const SizedBox(height: 10),
      Row(
        children: [
          _BotaoPequeno(
            chave: 'pontos-kf-anterior',
            icone: CupertinoIcons.backward_end_fill,
            onTap: animado ? onAnterior : null,
          ),
          Expanded(
            child: _AcaoDoPonto(
              chave: 'pontos-kf',
              icon: aqui ? CupertinoIcons.rhombus_fill : CupertinoIcons.rhombus,
              rotulo: aqui ? 'Tirar keyframe daqui' : 'Cravar keyframe aqui',
              onTap: onCravar,
            ),
          ),
          _BotaoPequeno(
            chave: 'pontos-kf-proximo',
            icone: CupertinoIcons.forward_end_fill,
            onTap: animado ? onProximo : null,
          ),
        ],
      ),
    ],
  );
}

class _BotaoPequeno extends StatelessWidget {
  const _BotaoPequeno({required this.chave, required this.icone, this.onTap});

  final String chave;
  final IconData icone;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey(chave),
    behavior: HitTestBehavior.opaque,
    onTap: onTap,
    child: SizedBox(
      width: 34,
      height: 34,
      child: Icon(
        icone,
        size: 16,
        color: onTap == null ? AmColors.muted.withValues(alpha: .5) : AmColors.text,
      ),
    ),
  );
}

class _Alternador extends StatelessWidget {
  const _Alternador({
    required this.chave,
    required this.rotulo,
    required this.ligado,
    required this.onTap,
  });

  final String chave;
  final String rotulo;
  final bool ligado;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey(chave),
    behavior: HitTestBehavior.opaque,
    onTap: onTap,
    child: Container(
      height: 30,
      padding: const EdgeInsets.symmetric(horizontal: 10),
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: ligado ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(15),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            ligado ? CupertinoIcons.checkmark_alt : CupertinoIcons.circle,
            size: 13,
            color: ligado ? AmColors.accent : AmColors.muted,
          ),
          const SizedBox(width: 5),
          AppText(
            rotulo,
            style: TextStyle(
              fontSize: 11.5,
              color: ligado ? AmColors.accent : AmColors.text,
            ),
          ),
        ],
      ),
    ),
  );
}

/// Uma acao do ponto: icone e NOME, num alvo de 44 pt.
class _AcaoDoPonto extends StatelessWidget {
  const _AcaoDoPonto({
    required this.chave,
    required this.icon,
    required this.rotulo,
    required this.onTap,
  });

  final String chave;
  final IconData icon;
  final String rotulo;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final ligado = onTap != null;
    return GestureDetector(
      key: ValueKey(chave),
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        height: 44,
        decoration: BoxDecoration(
          color: AmColors.chip,
          borderRadius: BorderRadius.circular(10),
        ),
        child: FittedBox(
          fit: BoxFit.scaleDown,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  icon,
                  size: 15,
                  color: ligado ? AmColors.text : AmColors.muted,
                ),
                const SizedBox(width: 5),
                AppText(
                  rotulo,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: ligado ? AmColors.text : AmColors.muted,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _ModoBotao extends StatelessWidget {
  const _ModoBotao({
    required this.chave,
    required this.ativo,
    required this.icon,
    required this.rotulo,
    required this.onTap,
  });

  final String chave;
  final bool ativo;
  final IconData icon;
  final String rotulo;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final cor = ativo ? AmColors.accent : AmColors.text;
    return AmRailButton(
      key: ValueKey(chave),
      onTap: onTap,
      selected: ativo,
      // O NOME EMBAIXO DO ICONE. Tres icones parecidos num trilho
      // estreito ("bolinha", "diamante", "mais") nao dizem qual e qual;
      // com o nome, ninguem precisa descobrir por tentativa.
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 21, color: cor),
            const SizedBox(height: 1),
            AppText(
              rotulo,
              style: TextStyle(fontSize: 9.5, height: 1.1, color: cor),
            ),
          ],
        ),
      ),
    );
  }
}

/// O trackpad: cantos marcados, e um ⊹ no meio como lembrete.
class _TrackpadPainter extends CustomPainter {
  const _TrackpadPainter({required this.modo});

  final PointsMode modo;

  @override
  void paint(Canvas canvas, Size size) {
    final fundo = Paint()..color = AmColors.chip;
    final rr = RRect.fromRectAndRadius(
      Offset.zero & size,
      const Radius.circular(14),
    );
    canvas.drawRRect(rr, fundo);
    final traco = Paint()
      ..color = AmColors.muted.withValues(alpha: 0.7)
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke;
    const m = 12.0, l = 22.0;
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
    final cor = modo == PointsMode.add ? AmColors.accent : AmColors.muted;
    final cruz = Paint()
      ..color = cor.withValues(alpha: 0.55)
      ..strokeWidth = 1.5;
    canvas.drawLine(c - const Offset(14, 0), c + const Offset(14, 0), cruz);
    canvas.drawLine(c - const Offset(0, 14), c + const Offset(0, 14), cruz);
    canvas.drawCircle(c, 5, cruz..style = PaintingStyle.stroke);
  }

  @override
  bool shouldRepaint(_TrackpadPainter old) => old.modo != modo;
}

/// Pedido de abrir o Edit Points numa camada (o "Desenho vetorial" do
/// menu de adicionar): o editor escuta, abre e zera.
final editPointsRequestProvider = StateProvider<String?>((ref) => null);
