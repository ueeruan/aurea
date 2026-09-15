import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// O TOQUE QUE RESPONDE — o atomo de fluidez do app inteiro.
///
/// Os botoes do editor eram GestureDetector cru: funcionavam, mas nao
/// davam nenhum sinal debaixo do dedo, e e essa mudez que faz uma
/// interface parecer travada mesmo rodando a 60 quadros. Este atomo
/// encolhe de leve (96,5%) e abaixa um pouco a luz enquanto o dedo esta
/// em cima, no timing das transicoes da Apple: resposta imediata na
/// descida, volta suave na subida, e interrompivel — soltar no meio nao
/// espera animacao nenhuma acabar.
///
/// SEM RIPPLE de proposito (regra de estilo do app: Cupertino, nunca
/// Material). E sem mexer no layout: o filho e medido exatamente como
/// antes, so a pintura escala em torno do centro.
class Tocavel extends StatefulWidget {
  const Tocavel({
    super.key,
    required this.child,
    this.onTap,
    this.onLongPress,
    this.behavior = HitTestBehavior.opaque,
    this.haptico = false,
    this.encolhe = 0.965,
  });

  final Widget child;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;
  final HitTestBehavior behavior;

  /// Clique fisico leve na descida (para acoes principais).
  final bool haptico;

  /// Quanto encolhe apertado (1 = so escurece).
  final double encolhe;

  @override
  State<Tocavel> createState() => _TocavelState();
}

class _TocavelState extends State<Tocavel> {
  bool _apertado = false;

  bool get _ativo => widget.onTap != null || widget.onLongPress != null;

  void _poe(bool v) {
    if (_apertado == v || !_ativo) return;
    setState(() => _apertado = v);
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: widget.behavior,
      onTapDown: !_ativo
          ? null
          : (_) {
              _poe(true);
              if (widget.haptico) HapticFeedback.lightImpact();
            },
      onTapUp: (_) => _poe(false),
      onTapCancel: () => _poe(false),
      onTap: widget.onTap,
      onLongPress: widget.onLongPress == null
          ? null
          : () {
              _poe(false);
              widget.onLongPress!();
            },
      child: AnimatedScale(
        scale: _apertado ? widget.encolhe : 1,
        // Descida instantanea ao olho; subida com o assentamento padrao
        // das transicoes de interface da Apple.
        duration: Duration(milliseconds: _apertado ? 90 : 220),
        curve: Curves.easeOutCubic,
        child: AnimatedOpacity(
          opacity: _apertado ? 0.82 : 1,
          duration: Duration(milliseconds: _apertado ? 60 : 180),
          curve: Curves.easeOut,
          child: widget.child,
        ),
      ),
    );
  }
}
