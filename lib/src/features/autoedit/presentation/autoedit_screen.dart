import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../editor/application/editor_controller.dart';
import '../../editor/domain/video_project.dart';
import '../../editor/presentation/am/am_colors.dart';
import '../../editor/presentation/am/am_widgets.dart';
import '../../editor/presentation/editor_screen.dart';
import '../application/autoedit_runner.dart';
import '../domain/autoedit_plan.dart';
import '../domain/autoedit_style.dart';

/// AS QUATRO TELAS DO AUTOEDIT, num fluxo so.
///
/// Escolher video, escolher estilo, ver o trabalho, ajustar e abrir. Sem
/// volta obrigatoria: da para sair da tela e voltar, e cancelar em
/// qualquer ponto deixa o que ja foi feito.
enum _Etapa { escolher, estilo, trabalhando, pronto }

class AutoEditScreen extends ConsumerStatefulWidget {
  const AutoEditScreen({super.key});

  @override
  ConsumerState<AutoEditScreen> createState() => _AutoEditScreenState();
}

class _AutoEditScreenState extends ConsumerState<AutoEditScreen> {
  _Etapa _etapa = _Etapa.escolher;
  String? _video;
  AutoEditStyle _estilo = AutoEditStyles.limpo;
  AutoEditRun? _run;
  AutoEditPlan? _plano;

  Future<void> _escolherVideo() async {
    final r = await FilePicker.platform.pickFiles(type: FileType.video);
    final caminho = r?.files.single.path;
    if (caminho == null || !mounted) return;
    setState(() {
      _video = caminho;
      _etapa = _Etapa.estilo;
    });
  }

  Future<void> _rodar(AutoEditStyle estilo) async {
    final video = _video;
    if (video == null) return;
    setState(() {
      _estilo = estilo;
      _etapa = _Etapa.trabalhando;
      _run = null;
    });
    final runner = ref.read(autoEditRunnerProvider);
    final resultado = await runner.analisar(
      videoPath: video,
      estilo: estilo,
      onProgresso: (r) {
        if (mounted) setState(() => _run = r);
      },
    );
    if (!mounted) return;
    setState(() {
      _run = resultado;
      _plano = resultado.plano;
      if (resultado.plano != null) _etapa = _Etapa.pronto;
    });
  }

  /// REPLANEJA sem reprocessar: os silencios e as falas ja estao medidos,
  /// e mexer no Ritmo ou no Zoom e so refazer a conta.
  void _replaneja({double? ritmo, AutoEditZoom? zoom}) {
    final base = _plano;
    if (base == null) return;
    final novo = _estilo.copyWith(ritmo: ritmo, zoom: zoom);
    setState(() {
      _estilo = novo;
      _plano = planejar(
        estilo: novo,
        // O plano carrega os silencios BRUTOS: replanejar a partir dos
        // cortes ja feitos so deixaria diminuir o ritmo, nunca aumentar.
        silencios: base.silencios,
        falas: base.falas,
      );
    });
  }

  Future<void> _abrirNoEditor() async {
    final video = _video;
    final plano = _plano;
    if (video == null || plano == null) return;

    final controller = ref.read(editorControllerProvider.notifier);
    // PROJETO COMUM, pelo caminho comum: o AutoEdit nao tem formato
    // proprio de arquivo nem camada especial. Abrir e editar depois e
    // igual a qualquer projeto feito a mao.
    controller.openProject(VideoProject(
      name: 'AutoEdit',
      createdAt: DateTime.now(),
      layers: const [],
    ));
    final id = await controller.importVideoAwaitingDuration(
      Duration.zero,
      video,
      'Video',
    );
    if (!mounted) return;
    ref.read(autoEditRunnerProvider).aplicar(plano, id);

    await Navigator.of(context).pushReplacement(
      MaterialPageRoute<void>(builder: (_) => const EditorScreen()),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AmColors.bg,
      appBar: AppBar(
        backgroundColor: AmColors.bg,
        title: const Text('AutoEdit'),
        leading: IconButton(
          icon: const Icon(CupertinoIcons.chevron_back),
          onPressed: () {
            ref.read(autoEditRunnerProvider).cancelar();
            Navigator.of(context).pop();
          },
        ),
      ),
      body: SafeArea(
        child: switch (_etapa) {
          _Etapa.escolher => _telaEscolher(),
          _Etapa.estilo => _telaEstilo(),
          _Etapa.trabalhando => _telaTrabalhando(),
          _Etapa.pronto => _telaPronto(),
        },
      ),
    );
  }

  // ------------------------------------------------------- 1 escolher

  Widget _telaEscolher() => Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Um video falado entra, um projeto editavel sai.',
              style: TextStyle(
                fontSize: 17,
                height: 1.35,
                color: AmColors.text,
              ),
            ),
            const SizedBox(height: 8),
            // A VANTAGEM REAL sobre os concorrentes de nuvem, dita na
            // tela e nao em nota de rodape.
            Row(
              children: [
                const Icon(CupertinoIcons.lock_shield,
                    size: 15, color: AmColors.accent),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(
                    'Tudo acontece no aparelho. O video nao e enviado para '
                    'lugar nenhum.',
                    style: TextStyle(
                      fontSize: 12,
                      height: 1.3,
                      color: AmColors.accent.withValues(alpha: 0.9),
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 24),
            SizedBox(
              height: 50,
              child: FilledButton.icon(
                icon: const Icon(CupertinoIcons.videocam, size: 19),
                label: const Text('Escolher video'),
                onPressed: _escolherVideo,
              ),
            ),
          ],
        ),
      );

  // --------------------------------------------------------- 2 estilo

  Widget _telaEstilo() => ListView(
        padding: const EdgeInsets.all(16),
        children: [
          const Text(
            'Escolha o estilo',
            style: TextStyle(
              fontSize: 18,
              fontWeight: FontWeight.w700,
              color: AmColors.text,
            ),
          ),
          const SizedBox(height: 4),
          const Text(
            'Cada um e um conjunto de ajustes prontos. Da para mudar tudo '
            'depois, no editor.',
            style: TextStyle(fontSize: 12, color: AmColors.muted, height: 1.3),
          ),
          const SizedBox(height: 14),
          for (final e in AutoEditStyles.todos) ...[
            _CartaoEstilo(estilo: e, onTap: () => _rodar(e)),
            const SizedBox(height: 8),
          ],
        ],
      );

  // ---------------------------------------------------- 3 trabalhando

  Widget _telaTrabalhando() {
    final run = _run;
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (run?.erro != null) ...[
            Text(
              run!.erro!,
              style: const TextStyle(fontSize: 14, color: AmColors.pink),
            ),
            const SizedBox(height: 16),
            OutlinedButton(
              onPressed: () => setState(() => _etapa = _Etapa.estilo),
              child: const Text('Escolher outro estilo'),
            ),
          ] else
            for (final p in run?.passos ?? const <AutoEditStep>[])
              _LinhaPasso(passo: p),
          const Spacer(),
          Center(
            child: TextButton(
              onPressed: () {
                ref.read(autoEditRunnerProvider).cancelar();
                setState(() => _etapa = _Etapa.estilo);
              },
              child: const Text('Cancelar'),
            ),
          ),
        ],
      ),
    );
  }

  // --------------------------------------------------------- 4 pronto

  Widget _telaPronto() {
    final plano = _plano!;
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const Text(
          'Pronto',
          style: TextStyle(
            fontSize: 20,
            fontWeight: FontWeight.w700,
            color: AmColors.text,
          ),
        ),
        const SizedBox(height: 4),
        Text(
          plano.cortes.isEmpty
              ? '${plano.falas.length} falas legendadas.'
              : '${plano.falas.length} falas · '
                  '${plano.economia.inSeconds} s de silencio cortados.',
          style: const TextStyle(fontSize: 13, color: AmColors.muted),
        ),
        const SizedBox(height: 20),

        // OS QUATRO CONTROLES. Cada um refaz o plano na hora — a conta e
        // barata; reprocessar o video nao seria.
        const _Rotulo('RITMO'),
        AmTickRuler(
          value: _estilo.ritmo * 100,
          min: 0,
          max: 100,
          unitsPerPixel: 0.35,
          height: 44,
          onChanged: (v) => _replaneja(ritmo: v / 100),
        ),
        Text(
          _estilo.ritmo <= 0
              ? 'Nenhum corte'
              : 'Corta ${(_estilo.ritmo * 100).round()}% do silencio',
          style: const TextStyle(fontSize: 11, color: AmColors.muted),
        ),
        const SizedBox(height: 18),

        const _Rotulo('ZOOM'),
        Row(
          children: [
            for (final z in AutoEditZoom.values) ...[
              Expanded(
                child: GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: () => _replaneja(zoom: z),
                  child: Container(
                    height: 38,
                    alignment: Alignment.center,
                    decoration: BoxDecoration(
                      color: _estilo.zoom == z
                          ? AmColors.accentDim
                          : AmColors.chip,
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: Text(
                      z.rotulo,
                      style: TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                        color: _estilo.zoom == z
                            ? AmColors.accent
                            : AmColors.text,
                      ),
                    ),
                  ),
                ),
              ),
              if (z != AutoEditZoom.values.last) const SizedBox(width: 6),
            ],
          ],
        ),
        const SizedBox(height: 26),

        SizedBox(
          height: 50,
          child: FilledButton.icon(
            icon: const Icon(CupertinoIcons.pencil, size: 18),
            label: const Text('Abrir no editor'),
            onPressed: _abrirNoEditor,
          ),
        ),
        const SizedBox(height: 10),
        const Text(
          'Os cortes, as legendas e os keyframes de zoom entram como '
          'camadas comuns. Tudo editavel, nada de caixa-preta.',
          style: TextStyle(fontSize: 11, color: AmColors.muted, height: 1.35),
        ),
      ],
    );
  }
}

class _Rotulo extends StatelessWidget {
  const _Rotulo(this.texto);

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(bottom: 6),
        child: Text(
          texto,
          style: const TextStyle(
            fontSize: 10,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.8,
            color: AmColors.muted,
          ),
        ),
      );
}

class _CartaoEstilo extends StatelessWidget {
  const _CartaoEstilo({required this.estilo, required this.onTap});

  final AutoEditStyle estilo;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.all(14),
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(14),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                estilo.nome,
                style: const TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
              const SizedBox(height: 3),
              Text(
                estilo.descricao,
                style: const TextStyle(
                  fontSize: 12,
                  height: 1.3,
                  color: AmColors.muted,
                ),
              ),
            ],
          ),
        ),
      );
}

class _LinhaPasso extends StatelessWidget {
  const _LinhaPasso({required this.passo});

  final AutoEditStep passo;

  @override
  Widget build(BuildContext context) {
    final (icone, cor) = switch (passo.estado) {
      AutoEditStepState.feito => (CupertinoIcons.check_mark, AmColors.accent),
      AutoEditStepState.correndo => (CupertinoIcons.circle_fill, AmColors.text),
      AutoEditStepState.pulado => (CupertinoIcons.minus, AmColors.muted),
      AutoEditStepState.esperando => (CupertinoIcons.circle, AmColors.muted),
    };
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7),
      child: Row(
        children: [
          Icon(icone, size: 15, color: cor),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              passo.id.rotulo,
              style: TextStyle(
                fontSize: 14,
                color: passo.estado == AutoEditStepState.esperando
                    ? AmColors.muted
                    : AmColors.text,
              ),
            ),
          ),
          if (passo.detalhe != null)
            Text(
              passo.detalhe!,
              style: const TextStyle(fontSize: 12, color: AmColors.muted),
            ),
        ],
      ),
    );
  }
}
