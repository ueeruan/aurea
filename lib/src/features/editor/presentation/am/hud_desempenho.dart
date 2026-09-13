import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../../application/perfil3d.dart';
import '../../application/preview_stats.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// O PAINEL DE DESEMPENHO, para builds de desenvolvimento.
///
/// A missao pediu, e com razao: "assim conseguimos descobrir
/// imediatamente o que esta causando problemas". Ele mostra, ao vivo:
///
///   FPS e tempo de quadro (media e o PIOR do ultimo segundo)
///   CPU (construir + medir) e GPU (rasterizar), separados
///   quadros perdidos, triangulos, nos, texturas e memoria
///   as fases mais caras do pipeline, quando o perfilador esta ligado
///
/// O PIOR QUADRO E MAIS IMPORTANTE QUE A MEDIA: uma cena a 60 fps com um
/// engasgo de 90 ms parece pior do que uma a 30 fps constantes, e so a
/// media nao mostra isso.
///
/// Ele nunca aparece em release: [mostrarHudDesempenho] so e verdade em
/// debug/profile, ou com --dart-define=AUREA_HUD=true.
const bool _hudPorDefine = bool.fromEnvironment('AUREA_HUD');
bool get mostrarHudDesempenho => _hudPorDefine || kDebugMode;

class HudDesempenho extends StatefulWidget {
  const HudDesempenho({super.key, this.alinhamento = Alignment.topRight});

  final Alignment alinhamento;

  @override
  State<HudDesempenho> createState() => _HudDesempenhoState();
}

class _HudDesempenhoState extends State<HudDesempenho> {
  /// Os tempos do ultimo segundo: e deles que saem a media e o pior.
  final List<double> _quadros = [];
  double _cpuMs = 0;
  double _gpuMs = 0;
  double _pior = 0;
  double _media = 0;
  int _perdidos = 0;
  bool _detalhe = false;

  @override
  void initState() {
    super.initState();
    SchedulerBinding.instance.addTimingsCallback(_medir);
  }

  @override
  void dispose() {
    SchedulerBinding.instance.removeTimingsCallback(_medir);
    super.dispose();
  }

  void _medir(List<FrameTiming> timings) {
    if (!mounted) return;
    for (final t in timings) {
      final total = t.totalSpan.inMicroseconds / 1000.0;
      _cpuMs = t.buildDuration.inMicroseconds / 1000.0;
      _gpuMs = t.rasterDuration.inMicroseconds / 1000.0;
      _quadros.add(total);
      // Quadro perdido: passou de 20 ms num alvo de 60.
      if (total > 20) _perdidos++;
    }
    if (_quadros.length > 60) {
      _quadros.removeRange(0, _quadros.length - 60);
    }
    if (_quadros.isEmpty) return;
    var soma = 0.0, pior = 0.0;
    for (final q in _quadros) {
      soma += q;
      if (q > pior) pior = q;
    }
    setState(() {
      _media = soma / _quadros.length;
      _pior = pior;
    });
  }

  @override
  Widget build(BuildContext context) {
    if (!mostrarHudDesempenho) return const SizedBox.shrink();
    final cena = PreviewStats.cena3d.value;
    final fps = _media <= 0 ? 0 : (1000 / _media).clamp(0, 120);
    final cor = _pior > 33
        ? const Color(0xFFFF7A7A)
        : (_pior > 20 ? const Color(0xFFFFC978) : const Color(0xFFB8FF3D));
    final linhas = <String>[
      'FPS ${fps.toStringAsFixed(0)}   quadro ${_media.toStringAsFixed(1)} ms',
      'PIOR ${_pior.toStringAsFixed(1)} ms   perdidos $_perdidos',
      'CPU ${_cpuMs.toStringAsFixed(1)}   GPU ${_gpuMs.toStringAsFixed(1)} ms',
      if (cena != null)
        '${cena.motor}  ${(cena.triangulos / 1000).toStringAsFixed(0)}K tri  '
            '${cena.chamadas} chamadas  ${cena.texturas} tex',
      if (PreviewStats.rssMb.value > 0) 'RAM ${PreviewStats.rssMb.value} MB',
    ];
    if (_detalhe && Perfil3D.ligado) {
      final r = Perfil3D.relatorio();
      final fases = r.fases.entries.toList()
        ..sort((a, b) => b.value.ms.compareTo(a.value.ms));
      for (final f in fases.take(5)) {
        linhas.add(
          '  ${f.key}  ${r.msPorQuadroDe(f.key).toStringAsFixed(2)} ms',
        );
      }
    }

    return Align(
      alignment: widget.alinhamento,
      child: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(8),
          child: GestureDetector(
            key: const ValueKey('hud-desempenho'),
            behavior: HitTestBehavior.opaque,
            // Um toque liga o detalhe por fase (e o perfilador junto).
            onTap: () => setState(() {
              _detalhe = !_detalhe;
              Perfil3D.ligado = _detalhe;
              if (!_detalhe) Perfil3D.zerar();
            }),
            onLongPress: () => setState(() {
              _perdidos = 0;
              _quadros.clear();
              Perfil3D.zerar();
            }),
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              decoration: BoxDecoration(
                color: const Color(0xCC0B0E12),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: cor.withValues(alpha: .6)),
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  for (final l in linhas)
                    AppText(
                      l,
                      style: TextStyle(
                        fontSize: 10,
                        height: 1.35,
                        fontFamilyFallback: const ['monospace'],
                        color: l.startsWith('PIOR') ? cor : Colors.white,
                      ),
                    ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
