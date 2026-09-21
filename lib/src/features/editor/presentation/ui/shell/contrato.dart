import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../application/playback_controller.dart';
import '../../../application/video_layer_manager.dart';
import '../../../domain/layer.dart';
import '../paineis/registro.dart';

// ===========================================================================
// OS CONTRATOS DA UI NOVA DO EDITOR
// ===========================================================================
//
// Este arquivo e o que DESACOPLA as frentes: a casca abre painel por
// [PainelId], a barra contextual sai de [ferramentasDa], e cada painel mora
// num arquivo proprio em `paineis/`, registrado em `paineis/registro.dart`.
// Quem reescreve um painel troca UM arquivo; quem reescreve a timeline nao
// encosta em painel nenhum.

/// OS PAINEIS DA CAMADA.
///
/// ACRESCENTAR SO NO FIM. A posicao nao vai para disco, mas testes e
/// frentes paralelas usam o `name`, e reordenar por gosto so gera conflito.
enum PainelId {
  /// Posicao, escala, rotacao, opacidade, inclinacao e pivo.
  transformar,

  /// A pilha de efeitos da camada.
  efeitos,

  /// Cor: preenchimento (forma, texto) ou correcao de cor (video, imagem).
  cor,

  /// Video: velocidade, rampas, reverso, congelar.
  tempo,

  /// Volume e fades.
  audio,

  /// Mistura, opacidade e mascaras.
  mascara,

  /// Conteudo do texto (texto comum ou texto 3D).
  texto,
  fonte,

  /// Negrito, alinhamento, borda e sombra do texto.
  estilo,

  /// Animadores de texto.
  animar,

  /// Letra, profundidade, chanfro e metal do texto 3D.
  texto3d,

  /// Material dos objetos 3D.
  material,
  luz,
  ambiente,

  /// Animacoes prontas do 3D.
  animacao3d,

  /// A ficha da camada: nome, visivel, cadeado, solo, timida, vinculo.
  propriedades,

  /// A lente da camera da composicao.
  camera,

  /// Audio: velocidade do clipe de som.
  velocidade,

  // ---- acrescentados pela fundacao: as portas das secoes que ja existiam.

  /// Borda e sombra (toda camada visual).
  bordaSombra,

  /// Editar forma (tamanho, cantos, traco).
  forma,

  /// Editar pontos (nos do caminho da forma ou da mascara).
  pontos,

  /// Grade de clones do nulo.
  clonar,

  /// Falas e estilo da legenda.
  legendas,
  particulas,

  /// Rastrear a camera do video (Cena 3D rastreada) e blobs.
  rastrear,

  /// Objetos, cameras e cortes da cena 3D.
  cena3d,

  /// Entrar, desagrupar e tempo proprio do grupo.
  grupo,
}

/// O PAINEL ABERTO (nulo = nenhum). Um so por vez, sempre da camada
/// selecionada: trocar a selecao fecha o painel.
final painelAbertoProvider = StateProvider<PainelId?>((ref) => null);

/// UMA FERRAMENTA DA BARRA CONTEXTUAL: ou ABRE um painel ([abre]), ou faz
/// uma acao direta ([acao]).
class Ferramenta {
  const Ferramenta({
    required this.id,
    required this.icone,
    required this.rotulo,
    this.abre,
    this.acao,
  });

  /// Estavel: e a chave de teste (`ferramenta-<id>`). Para ferramenta que
  /// abre painel, e o `name` do [PainelId].
  final String id;
  final IconData icone;

  /// Texto de UI (vai por `AppText`).
  final String rotulo;
  final PainelId? abre;
  final VoidCallback? acao;
}

/// As acoes diretas que [ferramentasDa] pode pedir.
abstract final class AcaoDaFerramenta {
  /// O menu da camada (duplicar, apagar, copiar, agrupar, dados da midia).
  static const mais = 'mais';

  /// Cortar a camada no cabecote.
  static const dividir = 'dividir';

  /// Texto comum -> Texto 3D.
  static const ativar3d = 'ativar3d';
}

/// Quem executa uma acao direta: a casca, que tem contexto e relogio.
typedef AoAcionarFerramenta = void Function(String idDaAcao, String layerId);

bool _temTexto3D(Layer l) =>
    l is Scene3DLayer && l.scene.nodes.any((n) => n.texto3d != null);

/// A BARRA CONTEXTUAL DA CAMADA (nivel 1 do app de referencia).
///
/// Sem camada: vazia — Adicionar, Play e Desfazer/Refazer moram na casca.
/// Com camada: o que o TIPO dela usa, na ordem em que se procura.
///
/// DERIVADA DE `domain/am_sections.dart` (`secoesDe`), e nenhuma porta
/// ficou para tras: cada [AmSecao] de cada tipo tem uma ferramenta aqui, e
/// as acoes da doca antiga (dividir, velocidade do audio, grupo, vinculo,
/// o menu da camada) tambem. `test/ui/editor_shell_test.dart` cobra.
///
/// [aoAcionar] liga as acoes diretas; sem ele elas vem com [acao] nula
/// (serve ao teste do contrato, que so olha a lista).
List<Ferramenta> ferramentasDa(Layer? camada, {AoAcionarFerramenta? aoAcionar}) {
  if (camada == null) return const [];
  Ferramenta painel(PainelId id, IconData icone, String rotulo) =>
      Ferramenta(id: id.name, icone: icone, rotulo: rotulo, abre: id);
  Ferramenta acao(String id, IconData icone, String rotulo) => Ferramenta(
    id: id,
    icone: icone,
    rotulo: rotulo,
    acao: aoAcionar == null ? null : () => aoAcionar(id, camada.id),
  );

  final transformar = painel(
    PainelId.transformar,
    CupertinoIcons.move,
    'Transformar',
  );
  final efeitos = painel(PainelId.efeitos, CupertinoIcons.sparkles, 'Efeitos');
  final mascara = painel(
    PainelId.mascara,
    CupertinoIcons.circle_lefthalf_fill,
    'Máscara',
  );
  final borda = painel(
    PainelId.bordaSombra,
    CupertinoIcons.square_on_square,
    'Borda e sombra',
  );
  final propriedades = painel(
    PainelId.propriedades,
    CupertinoIcons.info_circle,
    'Camada',
  );
  final dividir = acao(
    AcaoDaFerramenta.dividir,
    CupertinoIcons.scissors,
    'Dividir',
  );
  final mais = acao(AcaoDaFerramenta.mais, CupertinoIcons.ellipsis, 'Mais');

  return switch (camada) {
    VideoLayer() => [
      transformar,
      efeitos,
      painel(PainelId.cor, CupertinoIcons.color_filter, 'Cor'),
      painel(PainelId.tempo, CupertinoIcons.timer, 'Tempo'),
      painel(PainelId.audio, CupertinoIcons.speaker_2, 'Áudio'),
      mascara,
      borda,
      painel(PainelId.rastrear, CupertinoIcons.viewfinder, 'Rastrear'),
      dividir,
      propriedades,
      mais,
    ],
    ImageLayer() => [
      transformar,
      efeitos,
      painel(PainelId.cor, CupertinoIcons.color_filter, 'Cor'),
      mascara,
      borda,
      dividir,
      propriedades,
      mais,
    ],
    TextLayer() => [
      painel(PainelId.texto, CupertinoIcons.textformat, 'Texto'),
      painel(PainelId.fonte, CupertinoIcons.textformat_alt, 'Fonte'),
      painel(PainelId.estilo, CupertinoIcons.bold_italic_underline, 'Estilo'),
      painel(PainelId.animar, CupertinoIcons.wand_stars, 'Animar'),
      efeitos,
      transformar,
      painel(PainelId.cor, CupertinoIcons.paintbrush, 'Cor'),
      mascara,
      borda,
      acao(AcaoDaFerramenta.ativar3d, CupertinoIcons.cube, 'Texto 3D'),
      dividir,
      propriedades,
      mais,
    ],
    Scene3DLayer() when _temTexto3D(camada) => [
      painel(PainelId.texto, CupertinoIcons.textformat, 'Texto'),
      painel(PainelId.texto3d, CupertinoIcons.cube, 'Texto 3D'),
      transformar,
      efeitos,
      painel(PainelId.material, CupertinoIcons.circle_grid_hex, 'Material'),
      painel(PainelId.luz, CupertinoIcons.lightbulb, 'Luz'),
      painel(PainelId.ambiente, CupertinoIcons.cloud_sun, 'Ambiente'),
      painel(PainelId.animacao3d, CupertinoIcons.play_circle, 'Animação'),
      painel(PainelId.cena3d, CupertinoIcons.cube_box, 'Cena'),
      dividir,
      propriedades,
      mais,
    ],
    Scene3DLayer() => [
      transformar,
      painel(PainelId.material, CupertinoIcons.circle_grid_hex, 'Material'),
      painel(PainelId.luz, CupertinoIcons.lightbulb, 'Luz'),
      painel(PainelId.ambiente, CupertinoIcons.cloud_sun, 'Ambiente'),
      painel(PainelId.animacao3d, CupertinoIcons.play_circle, 'Animação'),
      painel(PainelId.cena3d, CupertinoIcons.cube_box, 'Cena'),
      efeitos,
      dividir,
      propriedades,
      mais,
    ],
    Element3DLayer() => [
      transformar,
      painel(PainelId.material, CupertinoIcons.circle_grid_hex, 'Material'),
      efeitos,
      mascara,
      borda,
      dividir,
      propriedades,
      mais,
    ],
    ShapeLayer() => [
      transformar,
      painel(
        PainelId.forma,
        CupertinoIcons.slider_horizontal_below_rectangle,
        'Forma',
      ),
      painel(PainelId.cor, CupertinoIcons.paintbrush, 'Cor'),
      borda,
      mascara,
      efeitos,
      dividir,
      propriedades,
      mais,
    ],
    AudioLayer() => [
      painel(PainelId.audio, CupertinoIcons.speaker_2, 'Áudio'),
      painel(PainelId.velocidade, CupertinoIcons.speedometer, 'Velocidade'),
      efeitos,
      dividir,
      propriedades,
      mais,
    ],
    CameraLayer() => [
      transformar,
      painel(PainelId.camera, CupertinoIcons.videocam, 'Câmera'),
      dividir,
      propriedades,
      mais,
    ],
    NullLayer() => [
      transformar,
      painel(PainelId.clonar, CupertinoIcons.circle_grid_3x3, 'Clonar'),
      dividir,
      propriedades,
      mais,
    ],
    GroupLayer() => [
      painel(PainelId.grupo, CupertinoIcons.folder, 'Grupo'),
      transformar,
      efeitos,
      mascara,
      borda,
      dividir,
      propriedades,
      mais,
    ],
    AdjustmentLayer() => [
      efeitos,
      transformar,
      mascara,
      borda,
      dividir,
      propriedades,
      mais,
    ],
    CaptionLayer() => [
      painel(PainelId.legendas, CupertinoIcons.captions_bubble, 'Legendas'),
      transformar,
      efeitos,
      mascara,
      borda,
      dividir,
      propriedades,
      mais,
    ],
    ParticulasLayer() => [
      painel(PainelId.particulas, CupertinoIcons.sparkles, 'Partículas'),
      transformar,
      efeitos,
      mascara,
      borda,
      dividir,
      propriedades,
      mais,
    ],
  };
}

/// As acoes da SELECAO MULTIPLA (a barra do lote do editor antigo).
abstract final class AcaoDoLote {
  static const agrupar = 'lote-agrupar';
  static const alinhar = 'lote-alinhar';
  static const cascata = 'lote-cascata';
  static const vincular = 'lote-vincular';
  static const apagar = 'lote-apagar';
  static const soltar = 'lote-soltar';
}

/// A BARRA COM VARIAS CAMADAS ESCOLHIDAS: agrupar, alinhar, cascata,
/// vincular, apagar e soltar a selecao. [aoAcionar] recebe o id da acao
/// ([AcaoDoLote]); sem ele as acoes vem nulas.
List<Ferramenta> ferramentasDoLote({void Function(String idDaAcao)? aoAcionar}) {
  Ferramenta f(String id, IconData icone, String rotulo) => Ferramenta(
    id: id,
    icone: icone,
    rotulo: rotulo,
    acao: aoAcionar == null ? null : () => aoAcionar(id),
  );
  return [
    f(AcaoDoLote.agrupar, CupertinoIcons.folder_badge_plus, 'Agrupar'),
    f(AcaoDoLote.alinhar, CupertinoIcons.rectangle_grid_1x2, 'Alinhar'),
    f(AcaoDoLote.cascata, CupertinoIcons.chart_bar_alt_fill, 'Cascata'),
    f(AcaoDoLote.vincular, CupertinoIcons.link, 'Vincular'),
    f(AcaoDoLote.apagar, CupertinoIcons.trash, 'Apagar'),
    f(AcaoDoLote.soltar, CupertinoIcons.xmark_circle, 'Soltar'),
  ];
}

/// O CONSTRUTOR DE UM PAINEL: recebe o id da camada que ele edita.
typedef ConstrutorDePainel =
    Widget Function(BuildContext context, String layerId);

/// O CONSTRUTOR DO PAINEL [id], vindo de `paineis/registro.dart`.
///
/// Todo [PainelId] tem um construtor la — o teste da casca abre um por um.
ConstrutorDePainel registroDePaineis(PainelId id) => paineisRegistrados[id]!;

/// O QUE O PAINEL PRECISA DA CASCA E NAO VEM POR PROVIDER.
///
/// O relogio ([PlaybackController]) e o gerente de video nao sao
/// providers: quem os cria e a tela do editor, com o ciclo de vida dela.
/// Os paineis leem daqui o `playback.time` (o `t` das edicoes) e pedem
/// para abrir ou fechar painel.
class EscopoDoEditor extends InheritedWidget {
  const EscopoDoEditor({
    super.key,
    required this.playback,
    required this.videos,
    required this.abrirPainel,
    required this.fecharPainel,
    required super.child,
  });

  final PlaybackController playback;
  final VideoLayerManager videos;
  final void Function(PainelId id) abrirPainel;
  final VoidCallback fecharPainel;

  static EscopoDoEditor of(BuildContext context) {
    final e = context.dependOnInheritedWidgetOfExactType<EscopoDoEditor>();
    assert(e != null, 'Painel montado fora da casca do editor.');
    return e!;
  }

  static EscopoDoEditor? maybeOf(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<EscopoDoEditor>();

  @override
  bool updateShouldNotify(EscopoDoEditor old) =>
      old.playback != playback || old.videos != videos;
}
