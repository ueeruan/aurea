import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

/// UMA AREA DE TOQUE CUJO FORMATO E CALCULADO NA HORA DO TOQUE.
///
/// O clipe, as alcas e os losangos NAO sao widgets posicionados: eles andam
/// com o tempo, e widget que anda com o tempo e reconstrucao a cada tique
/// do relogio. Sao desenhados por um pintor (que so repinta), e o toque
/// pergunta a [acerta] — com a vista DE AGORA — se o dedo caiu em cima de
/// algo. Fora disso a area e transparente ao toque: o arrasto no vazio
/// segue para a timeline (scrub, rolagem, pinca), sem disputar a arena.
class AreaDeToqueCalculada extends SingleChildRenderObjectWidget {
  const AreaDeToqueCalculada({
    super.key,
    required this.acerta,
    required Widget super.child,
  });

  /// O dedo em [local] (coordenada desta area) pega alguma coisa?
  final bool Function(Offset local) acerta;

  @override
  RenderObject createRenderObject(BuildContext context) =>
      RenderAreaDeToqueCalculada(acerta);

  @override
  void updateRenderObject(
    BuildContext context,
    RenderAreaDeToqueCalculada renderObject,
  ) {
    renderObject.acerta = acerta;
  }
}

class RenderAreaDeToqueCalculada extends RenderProxyBox {
  RenderAreaDeToqueCalculada(this.acerta);

  bool Function(Offset local) acerta;

  @override
  bool hitTest(BoxHitTestResult result, {required Offset position}) {
    if (!size.contains(position) || !acerta(position)) return false;
    hitTestChildren(result, position: position);
    result.add(BoxHitTestEntry(this, position));
    return true;
  }
}
