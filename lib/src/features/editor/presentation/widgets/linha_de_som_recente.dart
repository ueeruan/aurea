import 'dart:async';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:video_player/video_player.dart';

import '../../../../core/l10n/app_language.dart';
import '../../../../core/ui/tocavel.dart';
import '../../../media/application/sons_recentes.dart';
import '../am/am_colors.dart';

/// UM SOM RECENTE NA ABA DE ÁUDIO: o anel toca a prévia (e mostra onde
/// está), o + põe a trilha no cabeçote.
class LinhaDeSomRecente extends StatefulWidget {
  const LinhaDeSomRecente({
    super.key,
    required this.som,
    required this.onAdicionar,
  });

  final SomRecente som;
  final VoidCallback onAdicionar;

  @override
  State<LinhaDeSomRecente> createState() => _LinhaDeSomRecenteState();
}

class _LinhaDeSomRecenteState extends State<LinhaDeSomRecente> {
  VideoPlayerController? _tocador;
  Timer? _relogio;
  var _tocando = false;
  double _avanco = 0;

  @override
  void dispose() {
    _relogio?.cancel();
    _tocador?.dispose();
    super.dispose();
  }

  Future<void> _alternar() async {
    if (_tocando) {
      await _tocador?.pause();
      _relogio?.cancel();
      if (mounted) setState(() => _tocando = false);
      return;
    }
    try {
      // O tocador nasce no primeiro play — dez linhas paradas nao podem
      // custar dez decodificadores abertos.
      final tocador = _tocador ??= VideoPlayerController.file(
        File(widget.som.caminho),
      );
      if (!tocador.value.isInitialized) await tocador.initialize();
      await tocador.seekTo(Duration.zero);
      await tocador.play();
      _relogio?.cancel();
      _relogio = Timer.periodic(const Duration(milliseconds: 120), (_) {
        final v = _tocador?.value;
        if (!mounted || v == null) return;
        final total = v.duration.inMilliseconds;
        setState(() {
          _avanco = total <= 0
              ? 0
              : (v.position.inMilliseconds / total).clamp(0.0, 1.0);
          _tocando = v.isPlaying;
        });
        if (!v.isPlaying) _relogio?.cancel();
      });
      if (mounted) setState(() => _tocando = true);
    } catch (_) {
      // Sem tocador neste ambiente (ou arquivo ruim): o + continua
      // funcionando; so a previa fica muda.
      if (mounted) setState(() => _tocando = false);
    }
  }

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 3),
    child: Row(
      children: [
        // O ANEL DA PRÉVIA: o progresso da audição dá a volta no play.
        Tocavel(
          key: ValueKey('som-previa-${widget.som.caminho}'),
          onTap: _alternar,
          child: SizedBox(
            width: 38,
            height: 38,
            child: Stack(
              alignment: Alignment.center,
              children: [
                SizedBox(
                  width: 34,
                  height: 34,
                  child: CircularProgressIndicator(
                    value: _tocando ? _avanco : 0,
                    strokeWidth: 2.4,
                    backgroundColor: AmColors.chip,
                    valueColor: const AlwaysStoppedAnimation<Color>(
                      AmColors.accent,
                    ),
                  ),
                ),
                Icon(
                  _tocando
                      ? CupertinoIcons.pause_fill
                      : CupertinoIcons.play_fill,
                  size: 14,
                  color: AmColors.text,
                ),
              ],
            ),
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: AppText(
            widget.som.nome,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(fontSize: 13, color: AmColors.text),
          ),
        ),
        Tocavel(
          key: ValueKey('som-adicionar-${widget.som.caminho}'),
          onTap: () {
            _tocador?.pause();
            widget.onAdicionar();
          },
          child: Container(
            width: 34,
            height: 34,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: AmColors.chip,
              borderRadius: BorderRadius.circular(10),
            ),
            child: const Icon(
              CupertinoIcons.plus,
              size: 16,
              color: AmColors.accent,
            ),
          ),
        ),
      ],
    ),
  );
}
