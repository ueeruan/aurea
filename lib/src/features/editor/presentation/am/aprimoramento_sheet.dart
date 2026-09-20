import 'dart:io';

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show CircularProgressIndicator;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../export/application/aprimoramento_export.dart';
import '../../../export/application/comparacao_aprimoramento.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/aprimoramento_ia.dart';
import '../../domain/cut_ops.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// O motor de IA existe neste aparelho? Hoje so no Android. Os testes
/// trocam por verdadeiro para exercitar a folha.
final motorDeAprimoramentoProvider = Provider<bool>(
  (ref) => AprimoradorIa.doAparelho()?.disponivel ?? false,
);

/// O pedido de uma comparacao: o quadro e todas as escolhas do clipe.
typedef PedidoDeComparacao = ({
  String fonte,
  Duration tempoDaFonte,
  int largura,
  int altura,
  double forca,
  PerfilDoAprimoramento perfil,
  double reducaoDeRuido,
});

/// Quem gera o antes/depois (os testes trocam por um falso).
final comparadorDeAprimoramentoProvider =
    Provider<Future<ComparacaoDoAprimoramento> Function(PedidoDeComparacao)>(
      (ref) =>
          (p) => compararAprimoramento(
            fonte: p.fonte,
            tempoDaFonte: p.tempoDaFonte,
            largura: p.largura,
            altura: p.altura,
            forca: p.forca,
            perfil: p.perfil,
            reducaoDeRuido: p.reducaoDeRuido,
          ),
    );

/// APRIMORAR COM IA: liga o Real-ESRGAN do clipe na exportacao, com a
/// intensidade, e mostra o antes/depois de um quadro pelo mesmo caminho
/// do arquivo final.
Future<void> showAprimoramentoSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  await showParamSheet(
    context,
    title: 'Aprimorar com IA',
    heightFactor: 0.72,
    builder: (_) => _FolhaDoAprimoramento(layerId: layerId, playback: playback),
  );
}

class _FolhaDoAprimoramento extends ConsumerStatefulWidget {
  const _FolhaDoAprimoramento({required this.layerId, required this.playback});

  final String layerId;
  final PlaybackController playback;

  @override
  ConsumerState<_FolhaDoAprimoramento> createState() =>
      _FolhaDoAprimoramentoState();
}

class _FolhaDoAprimoramentoState extends ConsumerState<_FolhaDoAprimoramento> {
  ComparacaoDoAprimoramento? _comparacao;
  String? _erro;
  bool _comparando = false;
  bool _mostrarAntes = false;

  /// Uma comparacao por vez; a de antes de mudar a forca nao vale mais.
  int _geracao = 0;

  void _esquecerComparacao() {
    _geracao++;
    _comparacao = null;
    _erro = null;
    _comparando = false;
  }

  Future<void> _comparar(VideoLayer layer) async {
    final geracao = ++_geracao;
    setState(() {
      _comparando = true;
      _erro = null;
      _comparacao = null;
    });
    final project = ref.read(editorControllerProvider);
    var local = widget.playback.time.value - layer.startTime;
    if (local < Duration.zero) local = Duration.zero;
    if (local > layer.duration) local = layer.duration;
    try {
      final r = await ref.read(comparadorDeAprimoramentoProvider)((
        fonte: layer.sourcePath,
        tempoDaFonte: videoAbsoluteSourceTimeAt(layer, local),
        largura: project.outputWidth,
        altura: project.outputHeight,
        forca: layer.forcaDoAprimoramento,
        perfil: layer.perfilDoAprimoramento,
        reducaoDeRuido: layer.reducaoDeRuido,
      ));
      if (!mounted || geracao != _geracao) return;
      setState(() {
        _comparacao = r;
        _comparando = false;
        _mostrarAntes = false;
      });
    } catch (e) {
      if (!mounted || geracao != _geracao) return;
      setState(() {
        _erro = e is StateError ? e.message : '$e';
        _comparando = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final layer = project.layerById(widget.layerId);
    if (layer is! VideoLayer) return const SizedBox.shrink();
    final motor = ref.watch(motorDeAprimoramentoProvider);
    final forca = layer.forcaDoAprimoramento;

    void definirForca(double f) {
      controller.setClipAprimoramento(widget.layerId, forca: f);
      setState(_esquecerComparacao);
    }

    void definirRuido(double r) {
      controller.setClipAprimoramento(widget.layerId, ruido: r);
      setState(_esquecerComparacao);
    }

    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(18, 14, 18, 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(CupertinoIcons.sparkles, size: 18, color: AmColors.accent),
                SizedBox(width: 8),
                AppText(
                  'Aprimorar com IA',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            const AppText(
              'A IA amplia vídeos de baixa resolução até o tamanho em que '
              'aparecem, limpando ruído e blocos de compressão. Vale no '
              'arquivo exportado; o palco continua mostrando o original.',
              style: TextStyle(fontSize: 12, height: 1.35, color: AmColors.muted),
            ),
            const SizedBox(height: 4),
            const AppText(
              'Vídeos que já têm a resolução da composição não mudam.',
              style: TextStyle(fontSize: 12, height: 1.35, color: AmColors.muted),
            ),
            if (!motor) ...[
              const SizedBox(height: 10),
              AppText(
                'O motor de IA ainda não existe neste aparelho. O clipe exporta sem aprimoramento.',
                key: ValueKey('aprimorar-ia-indisponivel'),
                style: TextStyle(fontSize: 12, height: 1.35, color: AmColors.accent),
              ),
            ],
            const SizedBox(height: 12),
            Row(
              children: [
                const Expanded(
                  child: AppText(
                    'Aprimorar na exportação',
                    style: TextStyle(fontSize: 13, color: AmColors.text),
                  ),
                ),
                CupertinoSwitch(
                  key: const ValueKey('aprimorar-ia-ligar'),
                  value: layer.aprimorar,
                  activeTrackColor: AmColors.accent,
                  // Sem motor so da para DESLIGAR (projeto vindo de outro
                  // aparelho): nunca "IA ativada" onde ela nao existe.
                  onChanged: motor || layer.aprimorar
                      ? (v) {
                          controller.setClipAprimoramento(
                            widget.layerId,
                            ligado: v,
                          );
                          setState(_esquecerComparacao);
                        }
                      : null,
                ),
              ],
            ),
            if (layer.aprimorar) ...[
              const SizedBox(height: 10),
              const AppText(
                'Tipo de vídeo',
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
              const SizedBox(height: 6),
              Wrap(
                spacing: 8,
                children: [
                  for (final perfil in PerfilDoAprimoramento.values)
                    _Chip(
                      key: ValueKey('aprimorar-ia-perfil-${perfil.name}'),
                      label: perfil.emPalavras,
                      selected: layer.perfilDoAprimoramento == perfil,
                      onTap: () {
                        controller.setClipAprimoramento(
                          widget.layerId,
                          perfil: perfil,
                        );
                        setState(_esquecerComparacao);
                      },
                    ),
                ],
              ),
              if (layer.perfilDoAprimoramento ==
                  PerfilDoAprimoramento.videoReal) ...[
                const SizedBox(height: 10),
                Row(
                  children: [
                    const SizedBox(
                      width: 90,
                      child: AppText(
                        'Redução de ruído',
                        style: TextStyle(fontSize: 12, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: AmTickRuler(
                        key: const ValueKey('aprimorar-ia-ruido'),
                        value: layer.reducaoDeRuido * 100,
                        min: 0,
                        max: 100,
                        unitsPerPixel: 100 / 420,
                        height: 40,
                        onChanged: (v) => definirRuido(v / 100),
                      ),
                    ),
                    SizedBox(
                      width: 48,
                      child: Text(
                        '${(layer.reducaoDeRuido * 100).round()}%',
                        key: const ValueKey('aprimorar-ia-ruido-valor'),
                        textAlign: TextAlign.right,
                        style: const TextStyle(fontSize: 12, color: AmColors.text),
                      ),
                    ),
                  ],
                ),
                const AppText(
                  'Menos preserva o grão; mais limpa. A IA mistura dois modelos, não aplica um filtro depois.',
                  style: TextStyle(fontSize: 11, height: 1.35, color: AmColors.muted),
                ),
              ],
              const SizedBox(height: 8),
              Row(
                children: [
                  const SizedBox(
                    width: 90,
                    child: AppText(
                      'Intensidade',
                      style: TextStyle(fontSize: 12, color: AmColors.muted),
                    ),
                  ),
                  Expanded(
                    child: AmTickRuler(
                      value: forca * 100,
                      min: 0,
                      max: 100,
                      unitsPerPixel: 100 / 420,
                      height: 40,
                      onChanged: (v) => definirForca(v / 100),
                    ),
                  ),
                  SizedBox(
                    width: 48,
                    child: Text(
                      '${(forca * 100).round()}%',
                      textAlign: TextAlign.right,
                      style: const TextStyle(fontSize: 12, color: AmColors.text),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 6),
              Wrap(
                spacing: 8,
                children: [
                  for (final (nome, valor) in const [
                    ('Suave', 0.35),
                    ('Médio', 0.65),
                    ('Forte', 1.0),
                  ])
                    _Chip(
                      key: ValueKey('aprimorar-ia-$valor'),
                      label: nome,
                      selected: (forca - valor).abs() < 0.005,
                      onTap: () => definirForca(valor),
                    ),
                ],
              ),
              if (motor) ...[
                const SizedBox(height: 14),
                SizedBox(
                  width: double.infinity,
                  child: CupertinoButton(
                    key: const ValueKey('aprimorar-ia-comparar'),
                    color: AmColors.chip,
                    padding: const EdgeInsets.symmetric(vertical: 10),
                    onPressed: _comparando ? null : () => _comparar(layer),
                    child: const AppText(
                      'Comparar neste quadro',
                      style: TextStyle(fontSize: 13, color: AmColors.text),
                    ),
                  ),
                ),
                const SizedBox(height: 10),
                _resultado(),
              ],
            ],
          ],
        ),
      ),
    );
  }

  Widget _resultado() {
    if (_comparando) {
      return const Row(
        children: [
          SizedBox(
            width: 16,
            height: 16,
            child: CircularProgressIndicator(strokeWidth: 2),
          ),
          SizedBox(width: 10),
          AppText(
            'Preparando a comparação…',
            style: TextStyle(fontSize: 12, color: AmColors.muted),
          ),
        ],
      );
    }
    final erro = _erro;
    if (erro != null) {
      return Text(
        erro,
        key: const ValueKey('aprimorar-ia-erro'),
        style: TextStyle(fontSize: 12, color: AmColors.accent),
      );
    }
    final c = _comparacao;
    if (c == null) return const SizedBox.shrink();
    if (!c.plano.aplica) {
      return Text(
        c.plano.emPalavras,
        key: const ValueKey('aprimorar-ia-motivo'),
        style: const TextStyle(fontSize: 12, color: AmColors.muted),
      );
    }
    final caminho = _mostrarAntes ? c.antes : c.depois;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        CupertinoSlidingSegmentedControl<bool>(
          key: const ValueKey('aprimorar-ia-antes-depois'),
          groupValue: _mostrarAntes,
          onValueChanged: (v) => setState(() => _mostrarAntes = v ?? false),
          children: const {
            true: AppText('Antes', style: TextStyle(fontSize: 12)),
            false: AppText('Depois', style: TextStyle(fontSize: 12)),
          },
        ),
        const SizedBox(height: 8),
        Text(
          c.plano.emPalavras,
          style: const TextStyle(fontSize: 11, color: AmColors.muted),
        ),
        const SizedBox(height: 8),
        if (caminho != null)
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: Image.file(
              File(caminho),
              key: ValueKey('aprimorar-ia-$_mostrarAntes'),
              fit: BoxFit.contain,
              gaplessPlayback: true,
            ),
          ),
      ],
    );
  }
}

class _Chip extends StatelessWidget {
  const _Chip({
    super.key,
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7),
      decoration: BoxDecoration(
        color: selected ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(8),
      ),
      child: AppText(
        label,
        style: TextStyle(
          fontSize: 12,
          color: selected ? AmColors.accent : AmColors.muted,
        ),
      ),
    ),
  );
}
