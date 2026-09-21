import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/gizmo_da_cena3d.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/modelo_do_texto3d.dart';
import '../../../domain/scene3d.dart';
import '../../../domain/text_anim.dart';
import '../../../domain/texto3d.dart';
import '../../../domain/texto3d_animado.dart';
import '../../../domain/video_project.dart';
import '../../am/color_picker_sheet.dart' show showColorPicker;
import '../../widgets/gizmo_da_cena_overlay.dart'
    show noDaCenaSelecionadoProvider;
import 'comum.dart';

// AS PECAS DOS PAINEIS DO 3D (Texto 3D, Material, Luz, Ambiente, Animacao,
// Cena e Camera). Nenhuma e componente visual novo: sao arranjos de
// `AureaPropertyRow`, `AureaChip` e `AureaToolbarButton` que se repetem em
// todos eles — o mesmo controle no mesmo lugar, que e o que o dono pediu
// depois de reprovar duas fichas de 3D "com cara de plugin".
//
// NENHUM PAINEL DAQUI TEM PREVIA PROPRIA. Tudo o que muda aparece no palco,
// o unico preview do aplicativo: o motor 3D tem UM alvo, e uma segunda
// vista recriaria cor, profundidade e MSAA a cada toque.

// --------------------------------------------------------------- leitura

/// O no de texto 3D da camada (nulo quando ela nao e um Texto 3D).
String? noDoTexto3D(Layer? camada) => camada is Scene3DLayer
    ? camada.scene.nodes.where((n) => n.texto3d != null).firstOrNull?.id
    : null;

/// A camada E um Texto 3D (e nao um modelo, uma cena vazia ou um video)?
bool eTexto3D(Layer? camada) => noDoTexto3D(camada) != null;

/// OS OBJETOS QUE O INSPECTOR OFERECE: tudo que tem geometria, inclusive
/// o que esta escondido — senao o objeto que a pessoa acabou de esconder
/// sumiria da lista e nao haveria por onde mostra-lo de volta. (O gizmo,
/// ao contrario, so pega o que esta visivel: `objetosDaCena`.)
List<SceneNode> objetosDoInspector(Scene3D cena) => [
  for (final n in cena.nodes)
    if (!n.isNull) n,
];

/// O OBJETO EM FOCO: o escolhido no palco ou na ficha, quando ainda existe;
/// senao o padrao do gizmo; senao o primeiro que tiver geometria.
SceneNode? noEmFoco(Scene3D cena, String? escolhido) {
  final objetos = objetosDoInspector(cena);
  if (objetos.isEmpty) return null;
  if (escolhido != null) {
    final achado = objetos.where((n) => n.id == escolhido).firstOrNull;
    if (achado != null) return achado;
  }
  final padrao = noPadraoDaCena(cena);
  return objetos.where((n) => n.id == padrao).firstOrNull ?? objetos.first;
}

/// Os tempos (LOCAIS, µs) de uma trilha.
List<int> marcasDaTrilha(AnimatedDouble trilha) => [
  for (final k in trilha.keyframes) k.time.inMicroseconds,
];

/// O padding da lista de um painel com rolagem propria (o mesmo do corpo
/// padrao do `AureaPanel`).
const paddingDoPainel = EdgeInsets.fromLTRB(
  AureaDims.margemDoPainel,
  AureaDims.e4,
  AureaDims.margemDoPainel,
  AureaDims.topoDoPainel,
);

// ---------------------------------------------------------------- linhas

/// UMA ESCOLHA CURTA NUMA LINHA DE PROPRIEDADE: o rotulo de 75 e as fichas
/// lado a lado, rolando na horizontal quando nao cabem (como as fichas de
/// preset do app de referencia). Fica nos 51 da linha, sem quebrar em duas.
class LinhaDeFichas<T> extends StatelessWidget {
  const LinhaDeFichas({
    super.key,
    required this.rotulo,
    required this.valores,
    required this.rotuloDe,
    required this.aoEscolher,
    required this.chaveDe,
    this.escolhido,
    this.traduzir = true,
    this.chave,
  });

  final String rotulo;
  final List<T> valores;
  final String Function(T) rotuloDe;
  final ValueChanged<T> aoEscolher;

  /// A chave de teste de cada ficha.
  final String Function(T) chaveDe;
  final T? escolhido;

  /// Falso quando as opcoes sao conteudo (nome de objeto, de fonte).
  final bool traduzir;
  final String? chave;

  @override
  Widget build(BuildContext context) => AureaPropertyRow.personalizada(
    rotulo: rotulo,
    chave: chave,
    filho: SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      child: Row(
        children: [
          for (final v in valores)
            Padding(
              padding: const EdgeInsets.only(right: AureaDims.e6),
              child: AureaChip(
                key: ValueKey(chaveDe(v)),
                rotulo: rotuloDe(v),
                ativo: v == escolhido,
                traduzir: traduzir,
                aoTocar: () => aoEscolher(v),
              ),
            ),
        ],
      ),
    ),
  );
}

/// Uma acao de painel: um bloco com icone e rotulo.
class AcaoDoPainel {
  const AcaoDoPainel({
    required this.chave,
    required this.icone,
    required this.rotulo,
    required this.aoTocar,
    this.ativo = false,
  });

  final String chave;
  final IconData icone;
  final String rotulo;

  /// Nulo = acao apagada (falta escolher algo antes).
  final VoidCallback? aoTocar;
  final bool ativo;
}

/// A GRADE DE ACOES: blocos de 57 (o `AureaToolbarButton` em bloco), quatro
/// por fileira, no vao do painel. E a grade da folha Adicionar, dentro do
/// painel — nada de botao desenhado a mao.
class GradeDeAcoes extends StatelessWidget {
  const GradeDeAcoes({super.key, required this.acoes, this.porFileira = 4});

  final List<AcaoDoPainel> acoes;
  final int porFileira;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: AureaDims.e6),
    child: LayoutBuilder(
      builder: (context, c) {
        final largura = c.maxWidth.isFinite ? c.maxWidth : 320.0;
        final cada =
            (largura - AureaDims.vaoDoPainel * (porFileira - 1)) / porFileira;
        return Wrap(
          spacing: AureaDims.vaoDoPainel,
          runSpacing: AureaDims.vaoDoPainel,
          children: [
            for (final a in acoes)
              SizedBox(
                width: cada,
                child: AureaToolbarButton(
                  key: ValueKey(a.chave),
                  icone: a.icone,
                  rotulo: a.rotulo,
                  ativo: a.ativo,
                  bloco: true,
                  largura: cada,
                  aoTocar: a.aoTocar,
                ),
              ),
          ],
        );
      },
    ),
  );
}

/// A LINHA DE COR: a amostra da casa, e o toque abre o seletor da casa com
/// a cor chegando VIVA (o palco mostra enquanto o dedo anda).
AureaPropertyRow linhaDeCor(
  BuildContext context, {
  required String rotulo,
  required Color cor,
  required ValueChanged<Color> aoMudar,
  String? chave,
}) => AureaPropertyRow.cor(
  rotulo: rotulo,
  cor: cor,
  chave: chave,
  aoTocar: () async {
    final nova = await showColorPicker(
      context,
      initial: cor,
      withAlpha: false,
      onChanged: aoMudar,
    );
    if (nova != null) aoMudar(nova);
  },
);

/// A LINHA LIGA/DESLIGA.
AureaPropertyRow linhaDeInterruptor({
  required String rotulo,
  required bool valor,
  required ValueChanged<bool> aoMudar,
  String? chave,
}) => AureaPropertyRow.personalizada(
  rotulo: rotulo,
  chave: chave,
  filho: AureaToggle(
    key: chave == null ? null : ValueKey('interruptor-$chave'),
    valor: valor,
    aoMudar: aoMudar,
  ),
);

/// UMA LINHA NUMERICA SEM LOSANGO (o que nao anima: material, ambiente,
/// alcance da luz). Um arrasto = um passo de desfazer, e cada passo avisa
/// o palco que o dedo esta na tela.
AureaPropertyRow linhaSemLosango(
  EditorController c, {
  required String rotulo,
  required double valor,
  required double min,
  required double max,
  required ValueChanged<double> aoMudar,
  String unidade = '',
  int casas = 0,
  String? chave,
  VoidCallback? aoResetar,
}) => AureaPropertyRow(
  rotulo: rotulo,
  chave: chave,
  valor: valor.clamp(min, max).toDouble(),
  min: min,
  max: max,
  unidade: unidade,
  casas: casas,
  aoResetar: aoResetar,
  aoComecarGesto: c.beginGesture,
  aoTerminarGesto: c.endGesture,
  aoMudar: (v) {
    Interacao.marcar();
    aoMudar(v.clamp(min, max).toDouble());
  },
);

// ------------------------------------------------------- objeto da cena

/// UMA TRILHA DO OBJETO DA CENA, com o losango da casa.
///
/// Mexer no valor NAO cria keyframe (`docs/keyframe-explicito.md`); o
/// losango e que poe e tira, sempre no cabecote VIVO [t].
AureaPropertyRow linhaDoNo(
  EditorController c, {
  required Scene3DLayer camada,
  required SceneNode no,
  required PropDoNo prop,
  required String rotulo,
  required Duration t,
  required PlaybackController playback,
  required double min,
  required double max,
  int casas = 1,
  String unidade = '',
  double fator = 1,
  double? padrao,
  double? sensibilidade,
}) {
  final local = camada.localTime(t);
  final kf = losangoDasMarcas(
    marcasUs: [
      for (final d in c.sceneNodeKeyframeTimes(no, prop)) d.inMicroseconds,
    ],
    camada: camada,
    t: t,
    playback: playback,
    aoAlternar: () => c.toggleSceneNodeKeyframe(camada.id, no.id, prop, t),
  );
  return AureaPropertyRow(
    rotulo: rotulo,
    chave: 'cena3d-${prop.name}',
    valor: (c.sceneNodeValueAt(no, prop, local) * fator)
        .clamp(min, max)
        .toDouble(),
    min: min,
    max: max,
    casas: casas,
    unidade: unidade,
    sensibilidade: sensibilidade,
    keyframe: kf.estado,
    aoAnterior: kf.anterior,
    aoProximo: kf.proximo,
    aoResetar: padrao == null
        ? null
        : () => c.editSceneNodeProp(camada.id, no.id, prop, t, padrao / fator),
    aoComecarGesto: c.beginGesture,
    aoTerminarGesto: c.endGesture,
    aoMudar: (v) {
      // GESTO CONTINUO: cada passo avisa o palco que o dedo esta na tela
      // (o preview cede qualidade enquanto isso).
      Interacao.marcar();
      c.editSceneNodeProp(
        camada.id,
        no.id,
        prop,
        t,
        v.clamp(min, max).toDouble() / fator,
      );
    },
  );
}

/// AS FICHAS DOS OBJETOS da cena, quando ha mais de um. Escolher aqui move
/// o gizmo do palco (os dois leem [noDaCenaSelecionadoProvider]), e tocar
/// um objeto no palco troca o que o painel mostra.
class EscolhaDoObjeto3D extends ConsumerWidget {
  const EscolhaDoObjeto3D({
    super.key,
    required this.cena,
    required this.escolhido,
  });

  final Scene3D cena;
  final SceneNode? escolhido;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final objetos = objetosDoInspector(cena);
    if (objetos.length < 2) return const SizedBox.shrink();
    return LinhaDeFichas<SceneNode>(
      rotulo: 'Objeto',
      chave: 'cena3d-objeto',
      valores: objetos,
      // O NOME DO OBJETO E CONTEUDO: nao passa pelo catalogo.
      traduzir: false,
      rotuloDe: (n) => n.name,
      escolhido: escolhido,
      chaveDe: (n) => 'cena3d-objeto-${n.id}',
      aoEscolher: (n) =>
          ref.read(noDaCenaSelecionadoProvider.notifier).state = n.id,
    );
  }
}

// ------------------------------------------------------------- texto 3D

/// A FILA DO TEXTO 3D — o que faz um slider de profundidade ou de metal
/// nao travar o painel nem embaralhar o desfazer.
///
/// Profundidade, chanfro, fonte e metal refazem a MALHA (`editarTexto3D`
/// e assincrono: le a fonte, planifica, triangula). Refazer a cada pixel
/// de arrasto enfileiraria dezenas de construcoes; aqui:
///
///  * o NUMERO muda na hora ([pendente] e o que o painel mostra);
///  * a malha e refeita 140 ms depois do ultimo passo, e nunca duas ao
///    mesmo tempo — a que chega durante uma construcao espera a vez e so
///    a ULTIMA roda;
///  * soltar o dedo aplica o que faltava e SO ENTAO fecha o gesto: o
///    arrasto inteiro vira um passo de desfazer.
///
/// As animacoes do texto moram no modelo, que a malha nova substitui: a
/// fila as devolve depois de cada reconstrucao, senao mudar a profundidade
/// apagaria a entrada que a pessoa acabou de escolher.
class FilaDoTexto3D {
  FilaDoTexto3D({
    required this.controlador,
    required this.lerProjeto,
    required this.aoMudar,
  });

  final EditorController controlador;

  /// O projeto de agora (o `ref.read` do painel).
  final VideoProject Function() lerProjeto;

  /// Pede ao painel para se redesenhar (o `setState` dele).
  final VoidCallback aoMudar;

  /// O que o painel mostra enquanto a malha ainda nao foi refeita.
  Texto3D? pendente;
  EstiloDoTexto3D? estiloPendente;

  /// Construindo a malha agora.
  bool montando = false;

  /// A ultima recusa (fonte que nao abre), para o painel dizer por que.
  String? aviso;

  Timer? _espera;
  _PedidoDoTexto3D? _agendado;
  _PedidoDoTexto3D? _proximo;
  Completer<void>? _vazia;
  bool _descartada = false;

  /// Agenda a reconstrucao (o dedo ainda anda).
  void agendar(String cena, String no, Texto3D params, EstiloDoTexto3D estilo) {
    pendente = params;
    estiloPendente = estilo;
    _agendado = _PedidoDoTexto3D(cena, no, params, estilo);
    aoMudar();
    _espera?.cancel();
    _espera = Timer(const Duration(milliseconds: 140), () {
      final a = _agendado;
      if (a != null) aplicar(a.cena, a.no, a.params, a.estilo);
    });
  }

  /// Aplica ja (toque em ficha, fim de digitacao, fim de arrasto).
  Future<void> aplicar(
    String cena,
    String no,
    Texto3D params,
    EstiloDoTexto3D estilo,
  ) {
    _espera?.cancel();
    _agendado = null;
    pendente = params;
    estiloPendente = estilo;
    _proximo = _PedidoDoTexto3D(cena, no, params, estilo);
    final vazia = _vazia ??= Completer<void>();
    if (!montando) _rodar();
    return vazia.future;
  }

  /// Fecha o gesto DEPOIS da ultima reconstrucao: o arrasto inteiro e um
  /// passo de desfazer so.
  Future<void> terminarGesto() async {
    final a = _agendado;
    if (a != null) {
      await aplicar(a.cena, a.no, a.params, a.estilo);
    } else if (montando) {
      await (_vazia ??= Completer<void>()).future;
    }
    controlador.endGesture();
  }

  Future<void> _rodar() async {
    while (_proximo != null && !_descartada) {
      final job = _proximo!;
      _proximo = null;
      montando = true;
      aviso = null;
      aoMudar();
      final anims = _animsDe(job.cena, job.no);
      final deuCerto = await controlador.editarTexto3D(
        job.cena,
        job.no,
        job.params,
        job.estilo,
      );
      if (_descartada) break;
      if (deuCerto && anims.isNotEmpty) {
        controlador.setTexto3DAnims(job.cena, job.no, anims);
      }
      if (!deuCerto) {
        // A EDICAO FALHOU: o painel VOLTA ao que esta gravado e DIZ por
        // que — mostrar a fonte nova com o texto na velha e o "troquei e
        // nao mudou nada" do relato.
        aviso =
            controlador.ultimoMotivoDoTexto3D ??
            'Não foi possível aplicar essa mudança ao texto 3D.';
      }
    }
    montando = false;
    if (_proximo == null && _agendado == null) {
      pendente = null;
      estiloPendente = null;
    }
    final v = _vazia;
    _vazia = null;
    if (v != null && !v.isCompleted) v.complete();
    if (!_descartada) aoMudar();
  }

  List<TextAnim> _animsDe(String cena, String no) {
    final camada = lerProjeto().layerById(cena);
    if (camada is! Scene3DLayer) return const [];
    final modelo = camada.scene.nodeById(no)?.modelAsset;
    if (modelo == null) return const [];
    return animsDoTexto3D(modelo);
  }

  /// O painel saiu: nada mais redesenha, e o temporizador nao sobra.
  void descartar() {
    _descartada = true;
    _espera?.cancel();
    final v = _vazia;
    _vazia = null;
    if (v != null && !v.isCompleted) v.complete();
  }
}

class _PedidoDoTexto3D {
  const _PedidoDoTexto3D(this.cena, this.no, this.params, this.estilo);

  final String cena;
  final String no;
  final Texto3D params;
  final EstiloDoTexto3D estilo;
}

/// OS PARAMETROS QUE O PAINEL MOSTRA: os da fila (o numero que o dedo esta
/// arrastando) ou os gravados no no.
({Texto3D params, EstiloDoTexto3D estilo})? texto3DEmTela(
  Layer? camada,
  FilaDoTexto3D fila,
) {
  if (camada is! Scene3DLayer) return null;
  final id = noDoTexto3D(camada);
  final no = id == null ? null : camada.scene.nodeById(id);
  final gravado = no?.texto3d;
  if (no == null || gravado == null) return null;
  return (
    params: fila.pendente ?? gravado,
    estilo: fila.estiloPendente ?? no.estiloTexto3d ?? EstiloDoTexto3D.ouro,
  );
}

/// O METAL, A RUGOSIDADE E A COR QUE A PESSOA VE antes de mexer: os da
/// predefinicao escolhida. Sem isto "Metal" abriria em zero e a primeira
/// coisa que o painel mostraria do ouro seria plastico.
double metalDoTexto3D(Texto3D t, EstiloDoTexto3D e) =>
    t.metalico ?? (materiaisDoTexto3D(e).first['metallic'] as num).toDouble();

double rugosidadeDoTexto3D(Texto3D t, EstiloDoTexto3D e) =>
    t.rugosidade ??
    (materiaisDoTexto3D(e).first['roughness'] as num).toDouble();

Color corDoTexto3D(Texto3D t, EstiloDoTexto3D e) {
  final propria = t.cor;
  if (propria != null) return Color(propria);
  final c = materiaisDoTexto3D(e).first['color'] as List;
  int canal(int i) => ((c[i] as num).toDouble() * 255).round().clamp(0, 255);
  return Color.fromARGB(255, canal(0), canal(1), canal(2));
}

/// As linhas do MATERIAL do texto 3D — as mesmas no painel Texto 3D (aba
/// Material) e no painel Material de uma camada de Texto 3D.
List<Widget> linhasDoMaterialDoTexto3D(
  BuildContext context, {
  required EditorController c,
  required Scene3DLayer camada,
  required String noId,
  required Texto3D params,
  required EstiloDoTexto3D estilo,
  required FilaDoTexto3D fila,
}) {
  void agendar(Texto3D novo) => fila.agendar(camada.id, noId, novo, estilo);
  Future<void> ja(Texto3D novo, [EstiloDoTexto3D? e]) =>
      fila.aplicar(camada.id, noId, novo, e ?? estilo);

  AureaPropertyRow numero(
    String rotulo,
    String chave,
    double valor,
    double max,
    Texto3D Function(double v) com,
  ) => AureaPropertyRow(
    rotulo: rotulo,
    chave: chave,
    valor: (valor * 100).clamp(0, max).toDouble(),
    min: 0,
    max: max,
    casas: 0,
    unidade: '%',
    aoComecarGesto: c.beginGesture,
    aoTerminarGesto: fila.terminarGesto,
    aoMudar: (v) {
      Interacao.marcar();
      agendar(com(v.clamp(0, max) / 100));
    },
  );

  return [
    // A PREDEFINICAO MANDA: escolher o metal LIMPA o acabamento a mao.
    // Ouro que continuasse com a rugosidade do cromo anterior seria
    // "escolhi ouro e nao ficou ouro".
    LinhaDeFichas<EstiloDoTexto3D>(
      rotulo: 'Predefinição',
      chave: 'texto3d-estilo',
      valores: EstiloDoTexto3D.values,
      rotuloDe: nomeDoEstiloDoTexto3D,
      escolhido: estilo,
      chaveDe: (e) => 'texto3d-estilo-${e.name}',
      aoEscolher: (e) => ja(params.copyWith(semAcabamentoProprio: true), e),
    ),
    linhaDeCor(
      context,
      rotulo: 'Cor base',
      chave: 'texto3d-cor',
      cor: corDoTexto3D(params, estilo),
      aoMudar: (cor) => agendar(params.copyWith(cor: cor.toARGB32())),
    ),
    numero(
      'Metal',
      'texto3d-metal',
      metalDoTexto3D(params, estilo),
      100,
      (v) => params.copyWith(metalico: v),
    ),
    numero(
      'Rugosidade',
      'texto3d-rugosidade',
      rugosidadeDoTexto3D(params, estilo),
      100,
      (v) => params.copyWith(rugosidade: v),
    ),
    numero(
      'Brilho próprio',
      'texto3d-brilho',
      params.emissivo,
      200,
      (v) => params.copyWith(emissivo: v),
    ),
  ];
}
