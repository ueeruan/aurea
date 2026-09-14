import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:math' as math;
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';

import '../../domain/modelo_do_texto3d.dart';
import '../am/scene3d_studio_ux.dart' show pedirNome;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/transcription_service.dart';
import '../../application/transcricao_em_andamento.dart';
import '../../../settings/application/settings_controller.dart';
import '../../domain/caption.dart';
import '../../domain/element3d.dart';
import '../../domain/layer.dart';
import '../../domain/keyframe.dart';
import '../../../../core/ui/snack.dart';
import '../../domain/shape.dart';
import '../../domain/svg_document.dart';
import '../../domain/shape_library.dart';
import '../am/points_panel.dart' show editPointsRequestProvider;
import 'freehand_overlay.dart' show freehandRequestProvider;
import '../am/am_colors.dart';
import 'gallery_panel.dart';
import '../context/add_toolbar.dart' show AddTarget;

/// Sheet "+" do editor: escolher o tipo de camada.
Future<void> showAddLayerSheet(
  BuildContext context,
  WidgetRef ref,
  Duration playhead,
) {
  // FORMA NA MESMA FOLHA. Criar um retangulo era "+", folha de tipos,
  // OUTRA folha com quinze formas, toque — e a segunda folha subindo por
  // cima da primeira era o que fazia parecer que o app tinha "camadas
  // demais" para uma coisa simples. Agora "Forma" troca o conteudo da
  // mesma folha por quatro formas basicas; o resto fica atras de "Mais".
  return showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) => SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(14, 12, 14, 14),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // O FECHAR mora no cabecalho, ao lado da alca, e nao no fim
              // do trilho: la ele obrigava o trilho a ser mais alto que o
              // conteudo, e era o trilho que definia a altura da folha.
              Row(
                children: [
                  const SizedBox(width: 28),
                  Expanded(
                    child: Center(
                      child: Container(
                        width: 34,
                        height: 4,
                        decoration: BoxDecoration(
                          color: Colors.white24,
                          borderRadius: BorderRadius.circular(2),
                        ),
                      ),
                    ),
                  ),
                  GestureDetector(
                    behavior: HitTestBehavior.opaque,
                    onTap: () => Navigator.of(sheetContext).pop(),
                    child: const SizedBox(
                      width: 28,
                      height: 28,
                      child: Icon(
                        CupertinoIcons.xmark,
                        size: 20,
                        color: AmColors.accent,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              SizedBox(
                // A FOLHA NAO PODE COBRIR A LINHA DO TEMPO.
                //
                // A altura fixa de 270 nasceu de um aparelho grande. Numa
                // tela baixa a folha passava de 40% dela, e o que fica
                // atras — a timeline — e justamente o que a pessoa esta
                // olhando quando escolhe onde inserir a camada. A grade
                // encolhe antes de a folha invadir.
                height: math.min(
                  270.0,
                  MediaQuery.sizeOf(context).height * 0.31,
                ),
                child: AddLayerPanel(
                  onClose: () => Navigator.of(sheetContext).pop(),
                  playhead: playhead,
                ),
              ),
            ],
          ),
        ),
      ),
    ),
  );
}

/// Aba ELEMENTOS 3D: solidos nativos (cubo, esfera, diamante...) que
/// giram de verdade no espaco e se vinculam a nulos como qualquer
/// camada. Cada tile mostra o proprio solido renderizado.

/// FORMAS, na mesma folha: as quatro basicas na frente, o resto atras de
/// "Mais formas". Um toque cria a camada e fecha.
class _SecaoFormas extends StatefulWidget {
  const _SecaoFormas({required this.onVoltar, required this.onEscolher});

  final VoidCallback onVoltar;
  final void Function(List<ShapeItem> Function() build, String nome) onEscolher;

  @override
  State<_SecaoFormas> createState() => _SecaoFormasState();
}

class _SecaoFormasState extends State<_SecaoFormas> {
  bool _mais = false;

  static final _basicas = <(String, IconData, List<ShapeItem> Function())>[
    ('Retangulo', CupertinoIcons.square_fill, ShapePresets.paramRect),
    ('Circulo', CupertinoIcons.circle_fill, ShapePresets.paramEllipse),
    ('Poligono', CupertinoIcons.hexagon_fill, ShapePresets.paramPolygon),
    ('Estrela', CupertinoIcons.star_fill, ShapePresets.paramStar),
  ];

  static final _outras = <(String, IconData, List<ShapeItem> Function())>[
    ('Anel', CupertinoIcons.circle, ShapePresets.paramRing),
    ('Setor', CupertinoIcons.moon, ShapePresets.paramSector),
    ('Onda', CupertinoIcons.waveform_path, ShapePresets.wave),
    ('Coracao', CupertinoIcons.heart_fill, ShapePresets.heart),
    ('Engrenagem', CupertinoIcons.gear_alt_fill, ShapePresets.gear),
    ('Seta', CupertinoIcons.arrow_right, ShapePresets.arrow),
    ('Check', CupertinoIcons.checkmark, ShapePresets.check),
    ('Faisca', CupertinoIcons.sparkles, ShapePresets.sparkle),
    ('Gota', CupertinoIcons.drop_fill, ShapePresets.drop),
    ('Flor', CupertinoIcons.smallcircle_circle, ShapePresets.flower),
  ];

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            GestureDetector(
              onTap: widget.onVoltar,
              behavior: HitTestBehavior.opaque,
              child: const Padding(
                padding: EdgeInsets.only(right: 10),
                child: Icon(
                  CupertinoIcons.chevron_back,
                  size: 22,
                  color: AmColors.text,
                ),
              ),
            ),
            const AppText(
              'Forma',
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
          ],
        ),
        const SizedBox(height: 14),
        Row(
          children: [
            for (final (nome, icone, build) in _basicas)
              _AddOption(
                icon: icone,
                label: nome,
                onTap: () => widget.onEscolher(build, nome),
              ),
          ],
        ),
        const SizedBox(height: 12),
        if (!_mais)
          GestureDetector(
            onTap: () => setState(() => _mais = true),
            behavior: HitTestBehavior.opaque,
            child: const Padding(
              padding: EdgeInsets.symmetric(vertical: 6),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  AppText('Mais formas',
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                      color: AmColors.accent,
                    ),
                  ),
                  SizedBox(width: 4),
                  Icon(
                    CupertinoIcons.chevron_down,
                    size: 14,
                    color: AmColors.accent,
                  ),
                ],
              ),
            ),
          )
        else
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (final (nome, icone, build) in _outras)
                GestureDetector(
                  onTap: () => widget.onEscolher(build, nome),
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 12,
                      vertical: 8,
                    ),
                    decoration: BoxDecoration(
                      color: AmColors.chip,
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(icone, size: 15, color: AmColors.accent),
                        const SizedBox(width: 6),
                        AppText(nome,
                          style: const TextStyle(
                            fontSize: 13,
                            color: AmColors.text,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
            ],
          ),
      ],
    );
  }
}

/// Legendas: transcricao automatica — na nuvem (Groq Whisper, pelo
/// servidor do Aurea) ou no aparelho (whisper.cpp) — ou SRT colado.
///
/// A transcricao nao trava o editor: roda num [TranscricaoEmAndamento]
/// que sobrevive ao fechamento desta folha. Quem fecha a folha no meio
/// ve a camada de legenda aparecer na timeline quando ficar pronta; quem
/// reabre ve o andamento (ou o erro) de onde parou.
///
/// O audio so sai do aparelho quando a pessoa toca em Transcrever com a
/// nuvem escolhida — nunca sozinho.
Future<void> showCaptionCreationSheet(
  BuildContext context,
  WidgetRef ref,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  final textController = TextEditingController();
  final job = TranscricaoEmAndamento.instance;
  // Uma transcricao pronta de antes ja virou camada: comeca limpo.
  if (job.estado.value is TranscricaoPronta) job.limpar();
  var mode = CaptionMode.frases;
  var modo = ref.read(settingsControllerProvider).modoDeTranscricao;

  Future<void> transcrever(
    BuildContext sheetContext, {
    ModoDeTranscricao? forcar,
  }) async {
    final media = controller.firstTranscribableMediaPath();
    if (media == null) {
      job.falhar('Adicione um vídeo ao projeto primeiro.');
      return;
    }
    final entrou = await job.rodar(
      () => ref
          .read(transcriptionServiceProvider)
          .transcribeMedia(
            media,
            mode: mode,
            modo: forcar ?? modo,
            onStatus: job.status,
          ),
      aoTerminar: (falas) => controller.addCaptionLayer(falas) == 0
          ? 'Nenhuma fala detectada no áudio.'
          : null,
    );
    // Deu certo e a folha ainda esta aberta: fecha. Se a pessoa a fechou
    // no meio, a camada ja esta na timeline.
    if (entrou && sheetContext.mounted) Navigator.of(sheetContext).pop();
  }

  Widget chip({
    Key? key,
    required String rotulo,
    required bool selected,
    required VoidCallback? onTap,
  }) => GestureDetector(
    key: key,
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: selected ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: AppText(
        rotulo,
        style: const TextStyle(fontSize: 12, color: AmColors.accent),
      ),
    ),
  );

  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    builder: (sheetContext) => ValueListenableBuilder<EstadoDaTranscricao>(
      valueListenable: job.estado,
      builder: (sheetContext, estado, _) => StatefulBuilder(
        builder: (sheetContext, setSheetState) {
          final busy = estado is TranscricaoRodando;
          final falha = estado is TranscricaoFalhou ? estado : null;
          return Padding(
            padding: EdgeInsets.fromLTRB(
              20,
              16,
              20,
              16 + MediaQuery.of(sheetContext).viewInsets.bottom,
            ),
            child: SingleChildScrollView(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Expanded(
                        child: AppText('Legendas',
                          style: TextStyle(
                            fontSize: 18,
                            fontWeight: FontWeight.w700,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      GestureDetector(
                        onTap: () => Navigator.of(sheetContext).pop(),
                        child: const Icon(
                          CupertinoIcons.xmark,
                          size: 18,
                          color: AmColors.muted,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 10),
                  // Config da legenda: como o texto e segmentado no tempo.
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      for (final m in CaptionMode.values)
                        chip(
                          rotulo: captionModeLabel(m),
                          selected: mode == m,
                          onTap: busy
                              ? null
                              : () => setSheetState(() => mode = m),
                        ),
                    ],
                  ),
                  const SizedBox(height: 12),
                  // ONDE TRANSCREVER. A escolha fica nos Ajustes tambem;
                  // aqui e onde ela importa.
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      for (final m in ModoDeTranscricao.values)
                        chip(
                          key: ValueKey('transcricao-modo-${m.name}'),
                          rotulo: m.emPalavras,
                          selected: modo == m,
                          onTap: busy
                              ? null
                              : () {
                                  setSheetState(() => modo = m);
                                  ref
                                      .read(settingsControllerProvider.notifier)
                                      .setModoDeTranscricao(m);
                                },
                        ),
                    ],
                  ),
                  const SizedBox(height: 6),
                  AppText(
                    modo.explicacao,
                    style: const TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                  const SizedBox(height: 10),
                  SizedBox(
                    width: double.infinity,
                    child: CupertinoButton(
                      key: const ValueKey('transcrever'),
                      color: AmColors.accent,
                      borderRadius: BorderRadius.circular(12),
                      onPressed: busy ? null : () => transcrever(sheetContext),
                      child: Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          if (busy)
                            const Padding(
                              padding: EdgeInsets.only(right: 10),
                              child: CupertinoActivityIndicator(
                                color: Color(0xFF0B0E12),
                              ),
                            )
                          else
                            const Padding(
                              padding: EdgeInsets.only(right: 8),
                              child: Icon(
                                CupertinoIcons.waveform,
                                size: 18,
                                color: Color(0xFF0B0E12),
                              ),
                            ),
                          AppText(
                            busy ? 'Transcrevendo...' : 'Transcrever',
                            style: const TextStyle(
                              fontSize: 14,
                              fontWeight: FontWeight.w700,
                              color: Color(0xFF0B0E12),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                  if (estado is TranscricaoRodando) ...[
                    Padding(
                      padding: const EdgeInsets.only(top: 8),
                      child: AppText(
                        estado.status,
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.muted,
                        ),
                      ),
                    ),
                    // NAO TRAVA O EDITOR: da para fechar e continuar
                    // mexendo; a legenda entra na timeline quando ficar
                    // pronta.
                    CupertinoButton(
                      key: const ValueKey('segundo-plano'),
                      padding: EdgeInsets.zero,
                      onPressed: () => Navigator.of(sheetContext).pop(),
                      child: const AppText('Continuar em segundo plano',
                        style: TextStyle(fontSize: 12, color: AmColors.accent),
                      ),
                    ),
                  ],
                  if (falha != null) ...[
                    Padding(
                      padding: const EdgeInsets.only(top: 8),
                      child: AppText(falha.mensagem,
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.pink,
                        ),
                      ),
                    ),
                    const SizedBox(height: 8),
                    Wrap(
                      spacing: 8,
                      runSpacing: 8,
                      children: [
                        if (!falha.semInternet)
                          chip(
                            key: const ValueKey('tentar-de-novo'),
                            rotulo: 'Tentar de novo',
                            selected: false,
                            onTap: () => transcrever(sheetContext),
                          ),
                        if (falha.ofereceLocal)
                          chip(
                            key: const ValueKey('usar-local'),
                            rotulo: 'Usar o Whisper do aparelho',
                            selected: true,
                            onTap: () => transcrever(
                              sheetContext,
                              forcar: ModoDeTranscricao.local,
                            ),
                          ),
                      ],
                    ),
                  ],
                  if (estado is TranscricaoPronta)
                    Padding(
                      padding: const EdgeInsets.only(top: 8),
                      child: AppText(
                        'Legendas prontas: ${estado.falas} falas.',
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.accent,
                        ),
                      ),
                    ),
                  const SizedBox(height: 14),
                  const AppText('ou cole um SRT:',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
                  const SizedBox(height: 8),
                  CupertinoTextField(
                    controller: textController,
                    maxLines: 6,
                    minLines: 3,
                    enabled: !busy,
                    placeholder: translate(context, '1\n00:00:00,000 --> 00:00:02,000\nSua primeira fala...'),
                    style: const TextStyle(fontSize: 13, color: AmColors.text),
                    placeholderStyle: const TextStyle(
                      fontSize: 13,
                      color: AmColors.muted,
                    ),
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: AmColors.chip,
                      borderRadius: BorderRadius.circular(12),
                    ),
                  ),
                  const SizedBox(height: 10),
                  SizedBox(
                    width: double.infinity,
                    child: CupertinoButton(
                      color: AmColors.chip,
                      borderRadius: BorderRadius.circular(12),
                      onPressed: busy
                          ? null
                          : () {
                              controller.addCaptionLayerFromSrt(
                                textController.text,
                              );
                              Navigator.of(sheetContext).pop();
                            },
                      child: const AppText('Criar do SRT colado',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          );
        },
      ),
    ),
  );
  textController.dispose();
}

class _AddOption extends StatelessWidget {
  const _AddOption({
    required this.icon,
    required this.label,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final enabled = onTap != null;
    return Expanded(
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Opacity(
          opacity: enabled ? 1 : 0.35,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                width: 60,
                height: 60,
                decoration: BoxDecoration(
                  color: AmColors.chip,
                  borderRadius: BorderRadius.circular(18),
                ),
                child: Icon(icon, color: AmColors.accent, size: 28),
              ),
              const SizedBox(height: 6),
              // 12 e nao 13: em 13 o rotulo mais longo (Elementos 3D)
              // quebra em duas linhas e a fileira inteira cresce com ele,
              // empurrando a folha por cima da linha do tempo.
              AppText(
                label,
                textAlign: TextAlign.center,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(fontSize: 12, color: AmColors.text),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// As abas do menu de adicionar (modelo Alight Motion).
enum _AbaAdd { forma, midia, audio, objeto, mais }

/// MENU DE ADICIONAR NO MODELO AM: abas horizontais (Forma · Midia ·
/// Audio · Objeto · Modelo) e um trilho vertical a direita com os MODOS
/// de criar (Desenho livre · Desenho vetorial · Texto) — desenho e texto
/// nao sao itens de escolher, sao jeitos de comecar.
/// A aba em que o menu de adicionar abre (o E1 aponta para uma delas).
enum AddTab { forma, midia, audio, objeto }

class AddLayerPanel extends ConsumerStatefulWidget {
  const AddLayerPanel({
    super.key,
    required this.onClose,
    required this.playhead,
    this.initialTab,
    this.onProjectAction,
  });

  final VoidCallback onClose;
  final Duration playhead;
  final AddTab? initialTab;
  final ValueChanged<AddTarget>? onProjectAction;

  @override
  ConsumerState<AddLayerPanel> createState() => _AddMenuAmState();
}

class _AddMenuAmState extends ConsumerState<AddLayerPanel> {
  bool _importingAudio = false;

  Future<void> _importAudio({bool fromVideo = false}) async {
    if (_importingAudio) return;
    setState(() => _importingAudio = true);
    final controller = _controller;
    final at = widget.playhead;
    try {
      await controller.importAudioFile(at, fromVideo: fromVideo);
      if (mounted) _fecha();
    } catch (error) {
      if (mounted) {
        AureaSnack.show(
          context,
          error is FormatException
              ? error.message.toString()
              : 'Nao consegui importar esse audio. Tente outro arquivo.',
        );
      }
    } finally {
      if (mounted) setState(() => _importingAudio = false);
    }
  }

  late _AbaAdd _aba = switch (widget.initialTab) {
    AddTab.midia => _AbaAdd.midia,
    AddTab.audio => _AbaAdd.audio,
    AddTab.objeto => _AbaAdd.objeto,
    AddTab.forma || null => _AbaAdd.forma,
  };
  int _pagina = 0;
  final _pager = PageController();

  /// O QUE A FAIXA DO RODAPE ESTA EXPLICANDO AGORA.
  ///
  /// Objeto conceitual precisa de explicacao; forma nao precisa — o icone
  /// ja diz tudo. Descricao fixa no card ensina na primeira vez e vira
  /// ruido na centesima, entao ela mora numa faixa fina que mostra o item
  /// sob o dedo. Nulo quando ninguem esta segurando nada.
  String? _explicando;

  /// 5 silhuetas por linha (grade 5x3) conforme referência oficial AM.
  static const int _porLinha = 5;
  static const int _linhas = 3;
  static const int _porPagina = _porLinha * _linhas;

  EditorController get _controller =>
      ref.read(editorControllerProvider.notifier);

  /// Toda funcionalidade esta no aplicativo: as abas nao dependem de
  /// interruptor nenhum.
  List<_AbaAdd> get _abasVisiveis => _AbaAdd.values;

  void _fecha() => widget.onClose();

  /// Cria a camada e devolve o id dela (a nova e a que nao existia).
  String? _criaForma(List<ShapeItem> Function() build, String nome) {
    final antes = {
      for (final l in ref.read(editorControllerProvider).layers) l.id,
    };
    _controller.addShapeLayer(widget.playhead, contents: build(), name: nome);
    for (final l in ref.read(editorControllerProvider).layers) {
      if (!antes.contains(l.id)) return l.id;
    }
    return null;
  }

  /// A cena 3D da composicao, se houver. Camera e Luz so fazem sentido
  /// dentro de uma — fora dela seriam controles inertes.
  Scene3DLayer? get _cena {
    for (final l in ref.read(editorControllerProvider).layers) {
      if (l is Scene3DLayer) return l;
    }
    return null;
  }

  @override
  void dispose() {
    _pager.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ColoredBox(
      color: AmColors.panel,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Expanded(
            child: Column(
              children: [
                _abas(),
                Expanded(
                  child: (_abaVisivel == _AbaAdd.forma || _abaVisivel == _AbaAdd.objeto)
                      ? Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                          child: _conteudo(),
                        )
                      : (_abaVisivel == _AbaAdd.midia
                          ? _conteudo()
                          : SingleChildScrollView(
                              padding: const EdgeInsets.all(6),
                              child: _conteudo(),
                            )),
                ),
                if (_abaVisivel == _AbaAdd.objeto && _explicando != null)
                  _faixaDeDescricao(),
              ],
            ),
          ),
          SizedBox(
            width: 52,
            child: Column(
              children: [
                _atalho(CupertinoIcons.scribble, 'Desenho à\nmão livre', () {
                  _fecha();
                  ref.read(freehandRequestProvider.notifier).state = true;
                }),
                _atalho(CupertinoIcons.pencil_outline, 'Desenho\nvetorial', () {
                  final id = _criaForma(
                    () => [
                      ShapeStroke(
                        color: const Color(0xFFFFFFFF),
                        width: AnimatedDouble(10),
                      ),
                    ],
                    'Desenho',
                  );
                  _fecha();
                  if (id != null) {
                    ref.read(editPointsRequestProvider.notifier).state = id;
                  }
                }),
                _atalho(CupertinoIcons.textformat, 'Texto', () {
                  _fecha();
                  _controller.addTextLayer(widget.playhead);
                }),
                SizedBox(
                  height: 44,
                  child: IconButton(
                    tooltip: 'Fechar adicionar',
                    onPressed: _fecha,
                    icon: const Icon(
                      CupertinoIcons.xmark,
                      size: 20,
                      color: AmColors.text,
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  /// A FAIXA DE DESCRICAO — a peca que deixa os cards compactos sem
  /// perder a didatica. Nunca fica vazia: sem ninguem segurando nada, ela
  /// diz o que a aba faz.
  Widget _faixaDeDescricao() {
    const nomes = {
      _AbaAdd.forma: 'Formas para animar. Toque para adicionar.',
      _AbaAdd.midia: 'Video, imagem e legenda do seu aparelho.',
      _AbaAdd.audio: 'Musica e locucao.',
      _AbaAdd.objeto: 'Controladores, cena 3D e solidos.',
      _AbaAdd.mais: 'Desenho e ferramentas do projeto.',
    };
    final texto = _explicando ?? nomes[_abaVisivel]!;
    return Padding(
      padding: const EdgeInsets.only(top: 10),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(
            _explicando == null
                ? CupertinoIcons.info_circle
                : CupertinoIcons.info_circle_fill,
            size: 14,
            color: _explicando == null ? AmColors.muted : AmColors.accent,
          ),
          const SizedBox(width: 6),
          Expanded(
            child: AppText(texto,
              maxLines: 2,
              style: TextStyle(
                fontSize: 11,
                height: 1.25,
                color: _explicando == null ? AmColors.muted : AmColors.text,
              ),
            ),
          ),
        ],
      ),
    );
  }

  _AbaAdd get _abaVisivel =>
      _abasVisiveis.contains(_aba) ? _aba : _abasVisiveis.first;

  Widget _abas() {
    // ICONE EM CIMA, ROTULO EMBAIXO. So texto obriga a ler para escolher;
    // com o icone a aba se reconhece de relance, e e a mesma anatomia dos
    // tiles da grade logo abaixo.
    const nomes = {
      _AbaAdd.forma: ('Forma', CupertinoIcons.square_on_circle),
      _AbaAdd.midia: ('Mídia', CupertinoIcons.photo_on_rectangle),
      _AbaAdd.audio: ('Áudio', CupertinoIcons.music_note_2),
      _AbaAdd.objeto: ('Objeto / Elemento', CupertinoIcons.circle_grid_hex),
      _AbaAdd.mais: ('Modelo', CupertinoIcons.rectangle_split_3x1),
    };
    return SizedBox(
      height: 54,
      child: Row(
        children: [
          for (final a in _abasVisiveis)
            Expanded(
              child: GestureDetector(
                key: ValueKey('add-tab-${a.name}'),
                behavior: HitTestBehavior.opaque,
                onTap: () => setState(() {
                  _aba = a;
                  _explicando = null;
                }),
                child: Container(
                  decoration: const BoxDecoration(color: AmColors.panel),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Icon(
                        nomes[a]!.$2,
                        size: 17,
                        color: _abaVisivel == a
                            ? AmColors.accent
                            : AmColors.text,
                      ),
                      const SizedBox(height: 2),
                      AppText(
                        nomes[a]!.$1,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          fontSize: 9.5,
                          fontWeight: FontWeight.w600,
                          color: _abaVisivel == a
                              ? AmColors.accent
                              : AmColors.text,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }

  Widget _atalho(IconData icon, String label, VoidCallback onTap) => Expanded(
    child: InkWell(
      onTap: onTap,
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(icon, size: 18, color: AmColors.text),
          const SizedBox(height: 2),
          Flexible(
            child: AppText(
              label,
              maxLines: 3,
              overflow: TextOverflow.ellipsis,
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 9, color: AmColors.text),
            ),
          ),
        ],
      ),
    ),
  );

  Widget _mais() {
    Widget item(IconData icon, String label, VoidCallback onTap) => ListTile(
      dense: true,
      leading: Icon(icon, color: AmColors.text, size: 21),
      title: AppText(
        label,
        style: const TextStyle(color: AmColors.text, fontSize: 14),
      ),
      onTap: onTap,
    );
    return Column(
      children: [
        item(CupertinoIcons.scribble, 'Desenho livre', () {
          _fecha();
          ref.read(freehandRequestProvider.notifier).state = true;
        }),
        item(CupertinoIcons.pencil_outline, 'Desenho vetorial', () {
          final id = _criaForma(
            () => [
              ShapeStroke(
                color: const Color(0xFFFFFFFF),
                width: AnimatedDouble(10),
              ),
            ],
            'Desenho',
          );
          _fecha();
          if (id != null) {
            ref.read(editPointsRequestProvider.notifier).state = id;
          }
        }),
        item(CupertinoIcons.doc_text, 'Importar SVG', _importarSvg),
        item(
          CupertinoIcons.captions_bubble,
          'Legendas',
          () => showCaptionCreationSheet(context, ref),
        ),
        if (widget.onProjectAction != null)
          for (final entry in const [
            (AddTarget.efeito, CupertinoIcons.wand_stars, 'Camada de ajuste'),
            (AddTarget.grupo, CupertinoIcons.folder, 'Agrupar camadas'),
            (AddTarget.marcas, CupertinoIcons.bookmark, 'Marcas'),
            (AddTarget.batidas, CupertinoIcons.metronome, 'Detectar batidas'),
            (AddTarget.ajuda, CupertinoIcons.question_circle, 'Como editar'),
          ])
            item(entry.$2, entry.$3, () => widget.onProjectAction!(entry.$1)),
      ],
    );
  }

  /// ARQUIVO SVG: entra como forma editavel, com a cor de cada desenho.
  Future<void> _importarSvg() async {
    String? caminho;
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['svg'],
      );
      caminho = r?.files.single.path;
    } catch (_) {
      caminho = null;
    }
    if (caminho == null || !mounted) return;
    SvgImportado svg;
    try {
      svg = lerSvg(await File(caminho).readAsString());
    } on SvgException catch (e) {
      if (mounted) AureaSnack.show(context, e.message);
      return;
    } catch (e) {
      if (mounted) AureaSnack.show(context, 'Nao consegui ler esse SVG.');
      return;
    }
    if (!mounted) return;
    final nome = caminho
        .split(RegExp(r'[\\/]'))
        .last
        .replaceAll(RegExp(r'\.svg$', caseSensitive: false), '');
    _controller.addSvgLayers(svg, widget.playhead, nome: nome);
    _fecha();
    if (!mounted) return;
    AureaSnack.show(
      context,
      svg.ignorados.isEmpty
          ? '${svg.formas.length} desenho(s) do SVG, editaveis'
          : '${svg.formas.length} desenho(s); ficou de fora: ${svg.ignorados.join(', ')}',
    );
  }

  Widget _conteudo() {
    switch (_abaVisivel) {
      case _AbaAdd.forma:
        return _formas();
      case _AbaAdd.midia:
        return GalleryPanel(
          onImport: (file, video, duration) async {
            if (video) {
              if (duration > Duration.zero) {
                _controller.addVideoLayer(
                  widget.playhead,
                  file.path,
                  file.name,
                  duration,
                );
              } else {
                await _controller.importVideoAwaitingDuration(
                  widget.playhead,
                  file.path,
                  file.name,
                );
              }
            } else {
              _controller.addImageLayer(widget.playhead, file.path, file.name);
            }
            if (mounted) _fecha();
          },
        );
      case _AbaAdd.audio:
        if (_importingAudio) {
          return const Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                CupertinoActivityIndicator(),
                SizedBox(height: 12),
                AppText('Preparando audio...',
                  style: TextStyle(color: AmColors.text),
                ),
              ],
            ),
          );
        }
        return Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _AddOption(
              icon: CupertinoIcons.music_note,
              label: 'Arquivo de audio',
              onTap: () => _importAudio(),
            ),
            _AddOption(
              icon: CupertinoIcons.film,
              label: 'Extrair de video',
              onTap: () => _importAudio(fromVideo: true),
            ),
            const Spacer(flex: 5),
          ],
        );
      case _AbaAdd.objeto:
        return _objetos();
      case _AbaAdd.mais:
        return _mais();
    }
  }

  // ------------------------------------------------------------ objeto

  /// A ABA OBJETO EM DUAS ZONAS.
  ///
  /// Objeto conceitual (nulo, grade, cena) e solido geometrico sao coisas
  /// diferentes: um precisa de nome e explicacao, o outro se reconhece
  /// pela silhueta. Misturar os dois numa lista so foi o que fez a aba
  /// virar cinco cards gigantes que nao cabiam na tela.
  /// Pede o texto e o metal, e cria o Texto 3D no cabecote.
  Future<void> _criarTexto3D() async {
    final playhead = widget.playhead;
    final controller = _controller;
    final raiz = Navigator.of(context, rootNavigator: true).context;
    _fecha();
    final texto = await pedirNome(raiz, titulo: 'Texto 3D', atual: 'TEXTO 3D');
    if (texto == null || texto.trim().isEmpty || !raiz.mounted) return;
    final estilo = await showCupertinoModalPopup<EstiloDoTexto3D>(
      context: raiz,
      builder: (c) => CupertinoActionSheet(
        title: const AppText('Material'),
        actions: [
          for (final e in EstiloDoTexto3D.values)
            CupertinoActionSheetAction(
              key: ValueKey('texto3d-estilo-${e.name}'),
              onPressed: () => Navigator.of(c).pop(e),
              child: AppText(nomeDoEstiloDoTexto3D(e)),
            ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(c).pop(),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    if (estilo == null || !raiz.mounted) return;
    final no = await controller.addTexto3D(playhead, texto, estilo);
    if (no == null && raiz.mounted) {
      AureaSnack.show(raiz, 'Não consegui criar o texto 3D com essa fonte.');
    }
  }

  Widget _objetos() {
    Widget cardItem({
      Key? key,
      required Widget iconWidget,
      required String label,
      required VoidCallback onTap,
      Widget? badge,
    }) {
      return GestureDetector(
        key: key,
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          decoration: BoxDecoration(
            color: const Color(0xFF0D0E12),
            borderRadius: BorderRadius.circular(12),
          ),
          child: Stack(
            alignment: Alignment.center,
            children: [
              if (badge != null)
                Positioned(
                  top: 6,
                  child: badge,
                ),
              Padding(
                padding: const EdgeInsets.all(4),
                child: FittedBox(
                  fit: BoxFit.scaleDown,
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      if (badge != null) const SizedBox(height: 10),
                      iconWidget,
                      const SizedBox(height: 6),
                      AppText(
                        label,
                        textAlign: TextAlign.center,
                        style: const TextStyle(
                          color: Colors.white,
                          fontSize: 12,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
      );
    }

    final cartoes = <Widget>[
              // 1. Scene 3D com badge PROVAR
              cardItem(
                badge: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
                  decoration: BoxDecoration(
                    color: const Color(0xFF00FFB2),
                    borderRadius: BorderRadius.circular(4),
                  ),
                  child: const AppText('PROVAR',
                    style: TextStyle(
                      color: Colors.black,
                      fontSize: 8.5,
                      fontWeight: FontWeight.w900,
                    ),
                  ),
                ),
                iconWidget: const Icon(
                  CupertinoIcons.videocam,
                  size: 38,
                  color: Colors.white,
                ),
                label: 'Scene 3D',
                onTap: () {
                  _fecha();
                  final cena = _cena;
                  if (cena != null) {
                    _controller.addScene3DCamera(cena.id);
                  } else {
                    _controller.addScene3DLayer(widget.playhead);
                  }
                },
              ),
              // 2. Grupo Vazio
              cardItem(
                iconWidget: CustomPaint(
                  size: const Size(36, 36),
                  painter: _DashedRectWithHandlesPainter(),
                ),
                label: 'Grupo Vazio',
                onTap: () {
                  _fecha();
                  _controller.addEmptyGroup(widget.playhead);
                },
              ),
              // 3. Nulo
              cardItem(
                iconWidget: CustomPaint(
                  size: const Size(34, 34),
                  painter: _NullLayerIconPainter(),
                ),
                label: 'Nulo',
                onTap: () {
                  _fecha();
                  _controller.addNullLayer(widget.playhead);
                },
              ),
              // 4. Elemento / Projeto
              cardItem(
                iconWidget: CustomPaint(
                  size: const Size(36, 36),
                  painter: _ElementProjectIconPainter(),
                ),
                label: 'Elemento / Projeto',
                onTap: () {
                  _fecha();
                  _controller.addElement3DLayer(widget.playhead, Element3DKind.cube);
                },
              ),
              // 5. Particulas. O botao sumiu quando esta folha foi refeita
              // (c7216c9) e a camada ficou sem porta de entrada, embora o
              // codigo dela continuasse inteiro. Testadores pediram de volta.
              cardItem(
                key: const ValueKey('add-particulas'),
                iconWidget: const Icon(
                  CupertinoIcons.sparkles,
                  size: 36,
                  color: Colors.white,
                ),
                label: 'Partículas',
                onTap: () {
                  _fecha();
                  _controller.addParticlesLayer(widget.playhead);
                },
              ),
              // 6. Texto 3D estilo Element 3D: pede o texto e o metal e
              // cria as letras extrudadas na cena, presas a um nulo.
              cardItem(
                key: const ValueKey('add-texto3d'),
                iconWidget: const Icon(
                  CupertinoIcons.textformat_alt,
                  size: 36,
                  color: Color(0xFFFFD36B),
                ),
                label: 'Texto 3D',
                onTap: _criarTexto3D,
              ),
    ];

    return LayoutBuilder(
      builder: (context, constraints) {
        // DUAS FILEIRAS DE TRES. Em duas colunas, o quinto cartao abria uma
        // terceira fileira que ficava cortada: a grade nao rolava e o painel
        // tem 48% da tela. A altura sai do espaco que existe, e so rola se o
        // aparelho for baixo demais para duas fileiras.
        const colunas = 3;
        const espaco = 8.0;
        final fileiras = (cartoes.length / colunas).ceil();
        final largura =
            (constraints.maxWidth - 16 - espaco * (colunas - 1)) / colunas;
        final altura = constraints.hasBoundedHeight
            ? ((constraints.maxHeight - 8 - espaco * (fileiras - 1)) /
                      fileiras)
                  .clamp(52.0, 118.0)
            : 100.0;
        return Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
          child: GridView.count(
            crossAxisCount: colunas,
            mainAxisSpacing: espaco,
            crossAxisSpacing: espaco,
            childAspectRatio: largura / altura,
            physics: const ClampingScrollPhysics(),
            children: cartoes,
          ),
        );
      },
    );
  }

  // ------------------------------------------------------------ formas

  Widget _formas() {
    final total = shapeLibrary.length;
    final paginas = (total / _porPagina).ceil();
    return Column(
      children: [
        Expanded(
          child: PageView.builder(
            controller: _pager,
            itemCount: paginas,
            onPageChanged: (i) => setState(() => _pagina = i),
            itemBuilder: (context, pagina) {
              final ini = pagina * _porPagina;
              final fim = (ini + _porPagina).clamp(0, total);
              return GridView.count(
                crossAxisCount: _porLinha,
                mainAxisSpacing: 6,
                crossAxisSpacing: 6,
                physics: const NeverScrollableScrollPhysics(),
                children: [
                  for (var i = ini; i < fim; i++)
                    _TileForma(
                      entrada: shapeLibrary[i],
                      onTap: () {
                        final e = shapeLibrary[i];
                        _criaForma(e.build, e.nome);
                        _fecha();
                      },
                    ),
                ],
              );
            },
          ),
        ),
        const SizedBox(height: 6),
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            for (var i = 0; i < 4; i++)
              Container(
                width: 6,
                height: 6,
                margin: const EdgeInsets.symmetric(horizontal: 4),
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: i == _pagina ? Colors.white : const Color(0xFF484E5C),
                ),
              ),
          ],
        ),
        const SizedBox(height: 4),
      ],
    );
  }
}

/// Um tile da grade: a forma desenhada, do tamanho do tile.
class _TileForma extends StatelessWidget {
  const _TileForma({required this.entrada, required this.onTap});

  final ShapeLibraryEntry entrada;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final itens = entrada.build();
    return Tooltip(
      message: entrada.nome,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: DecoratedBox(
          decoration: const BoxDecoration(color: Color(0xFF000000)),
          child: Padding(
            padding: const EdgeInsets.all(7),
            child: CustomPaint(
              painter: _FormaPainter(
                shapeLibraryPreviewPath(itens),
                stroke: shapeLibraryIsStrokeOnly(itens),
                nome: entrada.nome,
              ),
              child: const SizedBox.expand(),
            ),
          ),
        ),
      ),
    );
  }
}

class _FormaPainter extends CustomPainter {
  const _FormaPainter(
    this.path, {
    required this.stroke,
    this.nome = '',
  });

  final Path path;
  final bool stroke;
  final String nome;

  @override
  void paint(Canvas canvas, Size size) {
    final b = path.getBounds();
    if (b.longestSide <= 0) return;
    final k = 0.82 * (size.shortestSide / b.longestSide);
    canvas.save();
    canvas.translate(size.width / 2, size.height / 2);
    canvas.scale(k, k);
    canvas.translate(-b.center.dx, -b.center.dy);
    path.fillType = PathFillType.evenOdd;
    final paint = Paint()..color = const Color(0xFF9E9E9E);
    if (stroke) {
      paint
        ..style = PaintingStyle.stroke
        ..strokeWidth = 14 / k
        ..strokeCap = StrokeCap.round;
    }
    canvas.drawPath(path, paint);

    // Pontinhos brancos nos vértices para formas geométricas conforme Screenshot 1
    if (nome == 'Triangulo' ||
        nome == 'Seta' ||
        nome == 'Poligono' ||
        nome == 'Linha' ||
        nome == 'Triangulo reto') {
      final dotPaint = Paint()..color = Colors.white;
      final dotRadius = 3.5 / k;
      final metrics = path.computeMetrics();
      for (final m in metrics) {
        final pStart = m.getTangentForOffset(0)?.position;
        final pMid = m.getTangentForOffset(m.length * 0.5)?.position;
        final pEnd = m.getTangentForOffset(m.length)?.position;
        if (pStart != null) canvas.drawCircle(pStart, dotRadius, dotPaint);
        if (pMid != null && nome != 'Linha') {
          canvas.drawCircle(pMid, dotRadius, dotPaint);
        }
        if (pEnd != null) canvas.drawCircle(pEnd, dotRadius, dotPaint);
      }
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(_FormaPainter old) => old.path != path || old.nome != nome;
}

/// Ícone de retângulo tracejado com alças para 'Grupo Vazio' (Screenshot 2).
class _DashedRectWithHandlesPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final rect = Rect.fromCenter(
      center: Offset(size.width / 2, size.height / 2),
      width: 34,
      height: 28,
    );
    final paint = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.6;

    const dashWidth = 3.5;
    const dashSpace = 2.5;

    void drawDashedLine(Offset p1, Offset p2) {
      final dx = p2.dx - p1.dx;
      final dy = p2.dy - p1.dy;
      final dist = math.sqrt(dx * dx + dy * dy);
      final count = (dist / (dashWidth + dashSpace)).floor();
      for (var i = 0; i < count; i++) {
        final startDist = i * (dashWidth + dashSpace);
        final endDist = math.min(startDist + dashWidth, dist);
        final t1 = startDist / dist;
        final t2 = endDist / dist;
        canvas.drawLine(
          Offset(p1.dx + dx * t1, p1.dy + dy * t1),
          Offset(p1.dx + dx * t2, p1.dy + dy * t2),
          paint,
        );
      }
    }

    drawDashedLine(rect.topLeft, rect.topRight);
    drawDashedLine(rect.topRight, rect.bottomRight);
    drawDashedLine(rect.bottomRight, rect.bottomLeft);
    drawDashedLine(rect.bottomLeft, rect.topLeft);

    final handlePaint = Paint()..color = Colors.white;
    const hSize = 4.0;
    for (final p in [
      rect.topLeft,
      rect.topCenter,
      rect.topRight,
      rect.centerRight,
      rect.bottomRight,
      rect.bottomCenter,
      rect.bottomLeft,
      rect.centerLeft,
    ]) {
      canvas.drawRect(
        Rect.fromCenter(center: p, width: hSize, height: hSize),
        handlePaint,
      );
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}

/// Ícone de quadrado com linha diagonal para 'Nulo' (Screenshot 2).
class _NullLayerIconPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final rect = Rect.fromCenter(
      center: Offset(size.width / 2, size.height / 2),
      width: 32,
      height: 32,
    );
    final rrect = RRect.fromRectAndRadius(rect, const Radius.circular(5));
    final strokePaint = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.0;

    canvas.drawRRect(rrect, strokePaint);
    canvas.drawLine(
      Offset(rect.left + 3, rect.bottom - 3),
      Offset(rect.right - 3, rect.top + 3),
      strokePaint,
    );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}

/// Ícone de triângulo sobreposto a círculo para 'Elemento / Projeto' (Screenshot 2).
class _ElementProjectIconPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final paint = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.0;

    final tri = Path()
      ..moveTo(center.dx - 6, center.dy - 12)
      ..lineTo(center.dx - 16, center.dy + 8)
      ..lineTo(center.dx + 4, center.dy + 8)
      ..close();
    canvas.drawPath(tri, paint);

    canvas.drawCircle(Offset(center.dx + 6, center.dy + 2), 9, paint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
