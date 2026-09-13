import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:video_player/video_player.dart';

import '../../../core/theme/app_theme.dart';
import '../domain/tutorial.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// A TELA DO TUTORIAL: o video em cima, os passos embaixo.
///
/// O video ja traz a legenda desenhada; a lista de passos e o indice —
/// o passo que esta tocando fica aceso, e tocar num passo pula para ele.
/// Se o aparelho nao conseguir abrir o video, a tela ainda serve: mostra
/// o poster e os passos escritos.
class TutorialScreen extends StatefulWidget {
  const TutorialScreen({
    super.key,
    this.id = 'cena3d',
    this.carregar,
    this.criarPlayer,
  });

  final String id;

  /// Para os testes: de onde vem o tutorial e como se cria o player.
  final Future<Tutorial> Function(String id)? carregar;
  final VideoPlayerController Function(String asset)? criarPlayer;

  @override
  State<TutorialScreen> createState() => _TutorialScreenState();
}

class _TutorialScreenState extends State<TutorialScreen> {
  Tutorial? _t;
  VideoPlayerController? _p;
  String? _erro;
  int _cenaAtual = 1;
  bool _pronto = false;

  @override
  void initState() {
    super.initState();
    _iniciar();
  }

  Future<void> _iniciar() async {
    Tutorial t;
    try {
      t = await (widget.carregar ?? Tutorial.carregar)(widget.id);
    } catch (_) {
      if (mounted) setState(() => _erro = 'Não encontrei este tutorial.');
      return;
    }
    if (!mounted) return;
    setState(() => _t = t);
    final p = (widget.criarPlayer ?? VideoPlayerController.asset)(t.video);
    _p = p;
    try {
      await p.initialize();
      await p.setLooping(true);
      p.addListener(_acompanhar);
      await p.play();
      if (mounted) setState(() => _pronto = true);
    } catch (_) {
      if (mounted) {
        setState(
          () => _erro =
              'Não deu para abrir o vídeo neste aparelho. '
              'Os passos estão escritos abaixo.',
        );
      }
    }
  }

  void _acompanhar() {
    final p = _p;
    final t = _t;
    if (p == null || t == null) return;
    final n = t.cenaEm(p.value.position.inMilliseconds / 1000)?.n ?? 1;
    if (n != _cenaAtual && mounted) setState(() => _cenaAtual = n);
  }

  Future<void> _irPara(CenaDoTutorial c) async {
    setState(() => _cenaAtual = c.n);
    final p = _p;
    if (p == null || !_pronto) return;
    await p.seekTo(Duration(milliseconds: (c.inicio * 1000).round()));
    if (!p.value.isPlaying) await p.play();
  }

  @override
  void dispose() {
    _p?.removeListener(_acompanhar);
    _p?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final t = _t;
    final p = _p;
    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: Column(
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(8, 6, 20, 6),
              child: Row(
                children: [
                  GestureDetector(
                    key: const ValueKey('tutorial-voltar'),
                    behavior: HitTestBehavior.opaque,
                    onTap: () => Navigator.of(context).maybePop(),
                    child: const SizedBox(
                      width: 44,
                      height: 44,
                      child: Icon(CupertinoIcons.chevron_left, size: 22),
                    ),
                  ),
                  Expanded(
                    child: AppText(
                      t == null ? 'Tutorial' : 'Tutorial · ${t.titulo}',
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                  ),
                  if (t != null)
                    AppText(
                      _mmss(t.duracao),
                      style: TextStyle(fontSize: 12.5, color: AppColors.muted),
                    ),
                ],
              ),
            ),
            Expanded(
              flex: 5,
              child: t == null
                  ? Center(
                      child: _erro == null
                          ? const CupertinoActivityIndicator()
                          : AppText(
                              _erro!,
                              style: TextStyle(color: AppColors.muted),
                            ),
                    )
                  : Center(
                      child: AspectRatio(
                        aspectRatio: t.largura / t.altura,
                        child: ClipRRect(
                          borderRadius: BorderRadius.circular(14),
                          child: GestureDetector(
                            key: const ValueKey('tutorial-video'),
                            behavior: HitTestBehavior.opaque,
                            onTap: () {
                              if (p == null || !_pronto) return;
                              p.value.isPlaying ? p.pause() : p.play();
                              setState(() {});
                            },
                            child: Stack(
                              fit: StackFit.expand,
                              children: [
                                if (_pronto && p != null)
                                  VideoPlayer(p)
                                else
                                  Image.asset(
                                    t.poster,
                                    fit: BoxFit.cover,
                                    errorBuilder: (_, _, _) => ColoredBox(
                                      color: AppColors.surfaceHigh,
                                    ),
                                  ),
                                if (_pronto && p != null && !p.value.isPlaying)
                                  Center(
                                    child: Icon(
                                      CupertinoIcons.play_circle_fill,
                                      size: 64,
                                      color: Colors.white.withValues(
                                        alpha: .85,
                                      ),
                                    ),
                                  ),
                                if (_erro != null)
                                  Positioned(
                                    left: 12,
                                    right: 12,
                                    bottom: 12,
                                    child: AppText(
                                      _erro!,
                                      key: const ValueKey('tutorial-erro'),
                                      style: TextStyle(
                                        fontSize: 12.5,
                                        color: AppColors.onDark,
                                      ),
                                    ),
                                  ),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ),
            ),
            if (t != null)
              Expanded(
                flex: 3,
                child: ListView(
                  padding: const EdgeInsets.fromLTRB(20, 10, 20, 24),
                  children: [
                    for (final c in t.cenas)
                      GestureDetector(
                        key: ValueKey('tutorial-cena-${c.n}'),
                        behavior: HitTestBehavior.opaque,
                        onTap: () => _irPara(c),
                        child: Padding(
                          padding: const EdgeInsets.symmetric(vertical: 7),
                          child: Row(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Container(
                                width: 24,
                                height: 24,
                                alignment: Alignment.center,
                                decoration: BoxDecoration(
                                  color: c.n == _cenaAtual
                                      ? AppColors.lime
                                      : AppColors.surfaceHigh,
                                  shape: BoxShape.circle,
                                ),
                                child: AppText(
                                  '${c.n}',
                                  style: TextStyle(
                                    fontSize: 12,
                                    fontWeight: FontWeight.w700,
                                    color: c.n == _cenaAtual
                                        ? const Color(0xFF0B0E12)
                                        : AppColors.muted,
                                  ),
                                ),
                              ),
                              const SizedBox(width: 12),
                              Expanded(
                                child: AppText(c.texto,
                                  style: TextStyle(
                                    fontSize: 14,
                                    height: 1.35,
                                    fontWeight: c.n == _cenaAtual
                                        ? FontWeight.w600
                                        : FontWeight.w400,
                                    color: c.n == _cenaAtual
                                        ? AppColors.onDark
                                        : AppColors.muted,
                                  ),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                  ],
                ),
              ),
          ],
        ),
      ),
    );
  }

  static String _mmss(double s) {
    final total = s.round();
    return '${total ~/ 60}:${(total % 60).toString().padLeft(2, '0')}';
  }
}
