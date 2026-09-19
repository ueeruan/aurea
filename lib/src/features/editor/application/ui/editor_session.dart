import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../domain/video_project.dart' show LayerProp;

/// AS FERRAMENTAS DO PAINEL TRANSFORMAR (uma por sub-aba).
enum TransformTool { position, rotation, scale, skew, pivot, opacity }

LayerProp propOfTool(TransformTool tool) => switch (tool) {
  TransformTool.position => LayerProp.position,
  TransformTool.rotation => LayerProp.rotation,
  TransformTool.scale => LayerProp.scale,
  TransformTool.skew => LayerProp.skew,
  TransformTool.pivot => LayerProp.pivot,
  TransformTool.opacity => LayerProp.opacity,
};

/// AS CINCO FACES DO PAINEL TRANSFORMAR, na ordem do trilho direito.
///
/// O PIVO E A QUINTA. Ele voltou a ter superficie propria: o ponto de
/// giro se ajusta ARRASTANDO, no lugar de digitar dois numeros e torcer
/// para o ponto cair onde o olho queria. Sem uma face para ele, o item
/// "Editar pivo" do menu trocava o titulo do painel e nao abria nada —
/// um controle que mentia.
enum ModoDeTransformacao { mover, girar, escalar, inclinar, pivo }

/// A FACE DE CADA FERRAMENTA. Uma tabela, e nao tres interruptores
/// iguais espalhados: o corpo do painel, o titulo do cabecalho e o
/// losango do trilho precisam concordar, e concordam porque leem daqui.
///
/// A OPACIDADE NAO TEM FACE. Ela mora em "Mistura e opacidade" desde o
/// redesign; enquanto o menu Transformar oferecia "Opacidade", a escolha
/// trocava o titulo para "Transformar · Opacidade" e deixava o corpo na
/// face anterior — o controle parecia quebrado, e era.
ModoDeTransformacao? modoDoTool(TransformTool tool) => switch (tool) {
  TransformTool.position => ModoDeTransformacao.mover,
  TransformTool.rotation => ModoDeTransformacao.girar,
  TransformTool.scale => ModoDeTransformacao.escalar,
  TransformTool.skew => ModoDeTransformacao.inclinar,
  TransformTool.pivot => ModoDeTransformacao.pivo,
  TransformTool.opacity => null,
};

TransformTool toolDoModo(ModoDeTransformacao modo) => switch (modo) {
  ModoDeTransformacao.mover => TransformTool.position,
  ModoDeTransformacao.girar => TransformTool.rotation,
  ModoDeTransformacao.escalar => TransformTool.scale,
  ModoDeTransformacao.inclinar => TransformTool.skew,
  ModoDeTransformacao.pivo => TransformTool.pivot,
};

LayerProp propDoModo(ModoDeTransformacao modo) => propOfTool(toolDoModo(modo));

/// A face escolhida no trilho direito. Vive aqui, e nao no widget, porque
/// o titulo do painel e o losango do trilho sao de fora do widget.
final modoDeTransformacaoProvider =
    StateProvider<ModoDeTransformacao>((_) => ModoDeTransformacao.mover);

/// AS SUB-ABAS DO PAINEL EDITAR FORMA.
enum ShapeTool { size, corners, points, angle, rotation, stroke, draw, nodes }

/// AS TRES AREAS DA EDICAO DE TEXTO.
///
/// O animador manual e os presets eram portas SEPARADAS, abertas de
/// fora (um botao no painel de texto e outro no menu da camada): quem
/// queria animar saia do texto, animava as cegas e voltava para corrigir
/// uma virgula. Sao as mesmas tres coisas que se faz com um texto, entao
/// viram tres abas do mesmo painel — e o texto fica visivel enquanto se
/// anima.
enum TextSection {
  /// Conteudo, fonte, tamanho, cor: o que o texto DIZ.
  edit,

  /// O animador manual inteiro (catalogo por posicao + controles).
  animation,

  /// Pilhas de animadores prontas ([textPresets]).
  presets,
}

/// O QUE O PAINEL CONTEXTUAL (zona E) ESTA MOSTRANDO.
///
/// `none` = a acao segue a selecao (E1 sem selecao, E2 com selecao).
/// Os demais sao as categorias abertas (E3/E4/E5) e os dois espacos de
/// edicao direta (pontos e curva).
enum EditorPanel {
  none,
  add,
  transform,
  blending,
  colorFill,
  effects,
  curve,
  editText,
  editShape,
  editPoints,
}

/// O ESTADO DE SESSAO DO EDITOR — o que era `setState` privado da tela.
///
/// Mora num provider para que a barra de cima, o transporte, a timeline
/// e o painel contextual leiam o mesmo estado sem parametro nenhum, e
/// para que um teste consiga abrir uma categoria sem tocar em pixel.
class EditorSession {
  const EditorSession({
    this.panel = EditorPanel.none,
    this.tool = TransformTool.position,
    this.shapeTool = ShapeTool.size,
    this.curveProp = LayerProp.position,
    this.curveReturn = EditorPanel.transform,
    this.pointsReturn = EditorPanel.editShape,
    this.pointsItemId,
    this.textSection = TextSection.edit,
    this.previewExpanded = false,
    this.timelineExpanded = false,
    this.inPoint,
    this.outPoint,
  });

  /// ENTRADA e SAIDA (edicao de 3 pontos, Pro): o trecho que Levantar e
  /// Extrair usam. Nulos = sem marca.
  final Duration? inPoint;
  final Duration? outPoint;

  final EditorPanel panel;
  final TransformTool tool;
  final ShapeTool shapeTool;
  final LayerProp curveProp;

  /// Para onde "voltar" leva ao sair da curva / dos pontos.
  final EditorPanel curveReturn;
  final EditorPanel pointsReturn;
  final String? pointsItemId;

  /// Qual das tres areas do painel de texto esta aberta.
  final TextSection textSection;

  /// Preview em tela cheia (esconde o resto).
  final bool previewExpanded;

  /// Timeline em tela cheia (preview vira janela pequena).
  final bool timelineExpanded;

  /// AS ALTURAS NAO SE ARRASTAM MAIS.
  ///
  /// O painel tinha tres niveis e uma alca que ia de um ao outro, e o
  /// preview tinha outra alca. Na mao dos testadores isso virou o
  /// contrario do que prometia: cada toque perto da borda mudava o
  /// tamanho de tudo, a pessoa perdia o lugar onde estava e nao achava
  /// mais o botao que tinha acabado de ver. Um editor de video nao pede
  /// que se escolha o tamanho do painel — ele pede que o painel esteja
  /// SEMPRE no mesmo lugar, como no Alight Motion.
  ///
  /// O preview continua se ajustando sozinho, mas pela PROPORCAO DA
  /// COMPOSICAO (a tela resolve isso), e nunca pelo dedo.
  static const double alturaDoPreview = 0.50;
  static const double alturaDaFolha = 0.40;

  /// A ABA DE ANIMACAO E A FERRAMENTA, NAO UMA FICHA.
  ///
  /// O painel de texto comum e uma lista de ajustes: 46% chega e sobra.
  /// A animacao tem a grade do catalogo MAIS os controles da animacao
  /// escolhida empilhados — em 46% a grade comia a tela e os controles
  /// ficavam atras de uma rolagem de duas telas.
  static const double alturaDaAnimacaoDeTexto = 0.60;

  /// QUEM CEDE E A TIMELINE.
  ///
  /// A FRACAO ACIMA NAO BASTA. O painel nao pode passar de
  /// `workspace - preview - piso da timeline`, e com o piso de 90 px de
  /// um painel comum a conta fecha exatamente na altura do painel ANTIGO
  /// — a aba de animacao abriria do mesmo tamanho, que e justamente o
  /// que nao se quer. O preview nao entra na conta: ele nunca cede a um
  /// painel (regra cobrada em teste), e a composicao continua do mesmo
  /// tamanho no palco.
  ///
  /// A timeline fica com uma tira: a regua, o cabo do cabecote e a
  /// faixa da camada escolhida ainda cabem, e o que se esta animando e
  /// o texto, nao a montagem. Sair da aba devolve os 90 px.
  static const double pisoDaTimelineAoAnimar = 44.0;

  bool get panelOpen => panel != EditorPanel.none && panel != EditorPanel.add;
  bool get adding => panel == EditorPanel.add;

  /// A area de texto esta aberta E e a de animacao (o painel pede mais
  /// altura, e o cabecalho muda de nome).
  bool get animandoTexto =>
      panel == EditorPanel.editText && textSection == TextSection.animation;

  EditorSession copyWith({
    EditorPanel? panel,
    TransformTool? tool,
    ShapeTool? shapeTool,
    LayerProp? curveProp,
    EditorPanel? curveReturn,
    EditorPanel? pointsReturn,
    String? pointsItemId,
    TextSection? textSection,
    bool clearPointsItem = false,
    bool? previewExpanded,
    bool? timelineExpanded,
    Duration? inPoint,
    Duration? outPoint,
    bool clearInOut = false,
  }) {
    final novoPainel = panel ?? this.panel;
    return EditorSession(
      panel: novoPainel,
      tool: tool ?? this.tool,
      shapeTool: shapeTool ?? this.shapeTool,
      curveProp: curveProp ?? this.curveProp,
      curveReturn: curveReturn ?? this.curveReturn,
      pointsReturn: pointsReturn ?? this.pointsReturn,
      // O CAMINHO EM EDICAO SO EXISTE COM O PAINEL DE PONTOS ABERTO.
      //
      // Sem esta regra o item ficava guardado depois de SAIR do painel por
      // outro caminho que nao o Voltar — tocar noutra coisa da tela, abrir
      // outra ferramenta. O editor de nos do palco se acha pela sessao, e
      // com o item velho de pe ele voltava aceso apontando para um
      // contorno que ninguem estava editando: o toque no palco INSERIA
      // ponto em vez de selecionar a camada.
      pointsItemId: clearPointsItem || novoPainel != EditorPanel.editPoints
          ? null
          : (pointsItemId ?? this.pointsItemId),
      textSection: textSection ?? this.textSection,
      previewExpanded: previewExpanded ?? this.previewExpanded,
      timelineExpanded: timelineExpanded ?? this.timelineExpanded,
      inPoint: clearInOut ? null : (inPoint ?? this.inPoint),
      outPoint: clearInOut ? null : (outPoint ?? this.outPoint),
    );
  }

  /// O trecho entre Entrada e Saida, quando as duas existem e fazem sentido.
  (Duration, Duration)? get inOut {
    final a = inPoint;
    final b = outPoint;
    if (a == null || b == null || b <= a) return null;
    return (a, b);
  }
}

class EditorSessionNotifier extends AutoDisposeNotifier<EditorSession> {
  @override
  EditorSession build() => const EditorSession();

  /// Ao abrir outro projeto, a sessao volta ao zero.
  void reset() => state = const EditorSession();

  void openPanel(EditorPanel panel) {
    state = state.copyWith(panel: panel);
  }

  void closePanel() => state = state.copyWith(panel: EditorPanel.none);

  void openAdd() => state = state.copyWith(panel: EditorPanel.add);

  void closeAdd() {
    if (state.panel == EditorPanel.add) {
      state = state.copyWith(panel: EditorPanel.none);
    }
  }

  void setTool(TransformTool tool) {
    state = state.copyWith(tool: tool);
    _seguirFerramenta(tool);
  }

  /// QUEM ESCOLHE A FERRAMENTA ESCOLHE A FACE. O trilho direito e a
  /// unica coisa que muda a face DEPOIS que o painel esta aberto; toda
  /// outra porta (o aviso "este keyframe e de Pivo", a ficha, o menu)
  /// entra por aqui, e por isso nao existe mais o caso de o titulo
  /// dizer uma propriedade e o corpo mostrar outra.
  void _seguirFerramenta(TransformTool tool) {
    final modo = modoDoTool(tool);
    if (modo != null) ref.read(modoDeTransformacaoProvider.notifier).state = modo;
  }

  void setShapeTool(ShapeTool tool) => state = state.copyWith(shapeTool: tool);

  void openTransform([TransformTool? tool]) {
    final escolhida = tool ?? state.tool;
    state = state.copyWith(panel: EditorPanel.transform, tool: escolhida);
    _seguirFerramenta(escolhida);
  }

  void openShape(ShapeTool tool) =>
      state = state.copyWith(panel: EditorPanel.editShape, shapeTool: tool);

  /// ABRE A EDICAO DE TEXTO, opcionalmente ja numa das tres areas. E o
  /// unico caminho para o animador manual: nao existe mais painel
  /// proprio dele, e sim a aba de dentro do texto.
  void openText([TextSection? section]) => state = state.copyWith(
    panel: EditorPanel.editText,
    textSection: section ?? state.textSection,
  );

  void setTextSection(TextSection section) =>
      state = state.copyWith(textSection: section);

  void openCurve(LayerProp prop) => state = state.copyWith(
    curveProp: prop,
    curveReturn: state.panel == EditorPanel.curve
        ? state.curveReturn
        : state.panel,
    panel: EditorPanel.curve,
  );

  void backFromCurve() => state = state.copyWith(panel: state.curveReturn);

  void openEditPoints(String itemId, {required EditorPanel returnTo}) =>
      state = state.copyWith(
        pointsItemId: itemId,
        pointsReturn: returnTo,
        panel: EditorPanel.editPoints,
      );

  void backFromEditPoints() =>
      state = state.copyWith(panel: state.pointsReturn, clearPointsItem: true);

  void togglePreviewExpanded() =>
      state = state.copyWith(previewExpanded: !state.previewExpanded);

  void setPreviewExpanded(bool value) =>
      state = state.copyWith(previewExpanded: value);

  void toggleTimelineExpanded() =>
      state = state.copyWith(timelineExpanded: !state.timelineExpanded);

  void setInPoint(Duration? t) => state = t == null
      ? state.copyWith(clearInOut: state.outPoint == null)
      : state.copyWith(inPoint: t);

  void setOutPoint(Duration? t) => state = t == null
      ? state.copyWith(clearInOut: state.inPoint == null)
      : state.copyWith(outPoint: t);

  void clearInOut() => state = state.copyWith(clearInOut: true);
}

/// AUTO-DISPOSE: sem ninguem escutando (o editor fechou), a sessao some
/// e a proxima abertura nasce limpa — sem precisar de reset em initState
/// (que o Riverpod proibe por ser durante o build).
final editorSessionProvider =
    NotifierProvider.autoDispose<EditorSessionNotifier, EditorSession>(
      EditorSessionNotifier.new,
    );
