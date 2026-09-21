import 'dart:math' as math;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/blob_track_service.dart';
import '../../../application/camera_track_service.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/camera_solver3d.dart';
import '../../../domain/cena_do_rastreio.dart';
import '../../../domain/cut_ops.dart';
import '../../../domain/effect.dart';
import '../../../domain/layer.dart';
import '../../../domain/plano_do_rastreio.dart';
import '../../../domain/model_import3d.dart' show ModelImportException;
import '../toolbar/importacao_3d.dart' show concluirImportacao3D;
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_3d.dart';

// ===========================================================================
// RASTREAR — Camera Tracker (motor 2.0) e BlobTracker, no painel da casa
// ===========================================================================
//
// As duas perguntas que a pessoa realmente faz: "como ponho um objeto 3D
// nesse chao?" e "como faco esse nome seguir a moto?". Sao ferramentas
// diferentes atras da mesma ferramenta da barra.
//
// OS PONTOS APARECEM NO PALCO, e nao numa previa propria. O estudio antigo
// abria uma tela inteira com os quadros do video decodificados de novo
// para desenhar os pontos por cima — uma segunda previa, com o video
// errado quando havia velocidade ou corte. Aqui, com o painel aberto, a
// nuvem e projetada pela pose do INSTANTE do cabecote sobre o video do
// proprio palco (ver [PontosDoRastreioNoPalco]), e tocar num ponto o
// escolhe.

/// OS PONTOS ESCOLHIDOS no palco: o video a que pertencem e os ids da
/// nuvem. Ids de outro video nao valem (trocar de camada nao herda a mao).
final pontosDoRastreioProvider =
    StateProvider<({String layerId, Set<int> ids})>(
      (ref) => (layerId: '', ids: const <int>{}),
    );

Set<int> _escolhidosDe(({String layerId, Set<int> ids}) e, String layerId) =>
    e.layerId == layerId ? e.ids : const <int>{};

/// A cor do ponto pela qualidade — a MESMA no palco e na ficha de escolha.
Color corDoPonto(QualidadeDoPonto q) => switch (q) {
  QualidadeDoPonto.excelente => AureaCores.destaque,
  QualidadeDoPonto.bom => AureaCores.keyframe,
  QualidadeDoPonto.fraco => AureaCores.textoSecundario,
  QualidadeDoPonto.ruim => AureaCores.perigo,
};

/// O QUADRO DA ANALISE que o cabecote [t] mostra no clipe.
///
/// Com o instante da fonte gravado na solucao, o mapeamento passa pela
/// velocidade, pelo reverso e pelo Time Remap do clipe — o mesmo que a
/// camera rastreada usa (`cameraDoRastreio`). Sem ele (analise antiga),
/// o clipe e tomado a 1x.
double quadroDoRastreio(VideoLayer camada, SolucaoCamera3D s, Duration t) {
  final local = camada.localTime(t);
  final inicio = s.inicioDaFonteUs;
  if (inicio != null) {
    final fonte = videoAbsoluteSourceTimeAt(camada, local).inMicroseconds;
    return (fonte - inicio) * s.fps / 1000000;
  }
  return local.inMicroseconds * s.fps / 1000000;
}

/// ONDE CADA PONTO DA NUVEM CAI NA COMPOSICAO, no quadro [quadro].
///
/// A projecao e a do estudio (`projetar` com a pose do quadro, em pixels
/// do quadro analisado); dali o ponto vai para a caixa do clipe no palco:
/// [centro] e a posicao da camada, [caixa] o tamanho dela ja com a escala,
/// [giroGraus] a rotacao. Ponto atras da camera ou longe do quadro fica
/// de fora.
Map<int, Offset> pontosDoRastreioNaComposicao(
  SolucaoCamera3D s,
  double quadro, {
  required Offset centro,
  required Size caixa,
  double giroGraus = 0,
}) {
  if (s.poses.isEmpty || s.largura <= 0 || s.altura <= 0) return const {};
  final pose = poseNoQuadro(s, quadro);
  final f = s.focalPx;
  final cx = s.largura / 2, cy = s.altura / 2;
  final giro = giroGraus * math.pi / 180;
  final cosG = math.cos(giro), senG = math.sin(giro);
  final out = <int, Offset>{};
  for (final e in s.nuvem.entries) {
    final p = projetar(pose.rotacao, pose.translacao, e.value);
    if (p == null) continue;
    final x = cx + p[0] * f, y = cy + p[1] * f;
    if (x < -20 || y < -20 || x > s.largura + 20 || y > s.altura + 20) {
      continue;
    }
    final u = (x / s.largura - 0.5) * caixa.width;
    final v = (y / s.altura - 0.5) * caixa.height;
    out[e.key] = centro + Offset(u * cosG - v * senG, u * senG + v * cosG);
  }
  return out;
}

// ===========================================================================
// o painel
// ===========================================================================

/// RASTREAR — quatro abas:
///
///   Analisar       como a tomada foi feita, analisar, a prova da analise,
///                  criar a cena 3D em cima do video
///   Pontos         escolher pontos no palco (toque) ou por qualidade;
///                  chao, origem, escala real, apagar, resolver de novo
///   Criar          texto, forma, nulo, placa, imagem e modelo 3D sobre a
///                  superficie escolhida (ou ancorado num ponto)
///   Seguir objeto  o BlobTracker: achar o que se mexe e grudar uma camada
class PainelRastrear extends ConsumerStatefulWidget {
  const PainelRastrear({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelRastrear> createState() => _PainelRastrearState();
}

class _PainelRastrearState extends ConsumerState<PainelRastrear> {
  static const _titulo = 'Rastrear';
  static const _abas = ['Analisar', 'Pontos', 'Criar', 'Seguir objeto'];

  int _aba = 0;
  TipoDeTomada _tomada = TipoDeTomada.auto;
  String? _erro;
  bool _resolvendo = false;
  bool _procurando = false;
  int? _blob;
  bool _comEscala = false;

  CameraTrackService get _t => CameraTrackService.instance;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  void initState() {
    super.initState();
    // O QUE JA FOI RESOLVIDO ANTES vem do disco: abrir o painel de um clipe
    // rastreado ontem nao pode pedir para rastrear de novo.
    _t.load(widget.layerId);
  }

  Set<int> get _escolhidos =>
      _escolhidosDe(ref.read(pontosDoRastreioProvider), widget.layerId);

  void _escolher(Set<int> ids) =>
      ref.read(pontosDoRastreioProvider.notifier).state = (
        layerId: widget.layerId,
        ids: ids,
      );

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, widget.layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.rastrear.name}';
    if (camada is! VideoLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Rastrear precisa de um vídeo.',
        portas: const [],
      );
    }
    final escolhidos = _escolhidosDe(
      ref.watch(pontosDoRastreioProvider),
      widget.layerId,
    );
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      corpo: ListenableBuilder(
        listenable: Listenable.merge([
          _t.revision,
          _t.etapa,
          _t.progress,
          BlobTrackService.instance.revision,
        ]),
        builder: (context, _) {
          final solucao = _t.dataFor(widget.layerId);
          final s = solucao == null || solucao.isEmpty ? null : solucao;
          return ListView(
            key: ValueKey('rastreio-aba-$_aba'),
            padding: paddingDoPainel,
            children: switch (_aba) {
              0 => _analisar(camada, s),
              1 => _pontos(s, escolhidos),
              2 => _criar(camada, s, escolhidos),
              _ => _seguir(camada),
            },
          );
        },
      ),
    );
  }

  // ------------------------------------------------------------ Analisar

  Future<void> _rodarAnalise() async {
    setState(() => _erro = null);
    EscopoDoEditor.of(context).playback.pause();
    try {
      await _c.rastrearCamera3D(widget.layerId, tipoDeTomada: _tomada);
    } on RastreioException catch (e) {
      if (mounted) setState(() => _erro = e.mensagem);
    } catch (_) {
      if (mounted) setState(() => _erro = 'Não consegui ler esse vídeo.');
    }
  }

  void _criarCena(SolucaoCamera3D s) {
    final id = _c.criarCenaDoRastreio(widget.layerId, s);
    if (id == null) {
      AureaSnack.show(context, 'Não consegui criar a cena.');
      return;
    }
    AureaSnack.show(
      context,
      'Cena 3D criada com a câmera rastreada',
      actionLabel: 'Desfazer',
      onAction: _c.undo,
    );
  }

  List<Widget> _analisar(VideoLayer camada, SolucaoCamera3D? s) {
    final rodando = _t.isRunning(widget.layerId);
    final projeto = ref.read(editorControllerProvider);
    return [
      if (rodando)
        AureaPropertyRow.personalizada(
          key: const ValueKey('rastreio-andamento'),
          rotulo: 'Analisando',
          filho: Row(
            children: [
              const CupertinoActivityIndicator(radius: 7),
              const SizedBox(width: AureaDims.e8),
              Expanded(
                // A ETAPA vem do motor, ja em palavras.
                child: Text(
                  _t.etapa.value.isEmpty ? '…' : _t.etapa.value,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: AureaEstilos.corpo,
                ),
              ),
              Text(
                '${(_t.progress.value.clamp(0.0, 1.0) * 100).round()}%',
                style: AureaEstilos.valor,
              ),
            ],
          ),
        )
      else ...[
        if (s == null)
          LinhaDeFichas<TipoDeTomada>(
            rotulo: 'Tomada',
            chave: 'rastreio-tomada',
            valores: const [TipoDeTomada.auto, TipoDeTomada.tripe],
            rotuloDe: (t) => t.emPalavras,
            escolhido: _tomada,
            chaveDe: (t) => 'rastreio-tomada-${t.name}',
            aoEscolher: (t) => setState(() => _tomada = t),
          ),
        GradeDeAcoes(
          acoes: [
            AcaoDoPainel(
              chave: 'rastreio-analisar',
              icone: CupertinoIcons.viewfinder,
              rotulo: s == null ? 'Analisar' : 'Analisar de novo',
              ativo: s == null,
              aoTocar: _rodarAnalise,
            ),
            if (s != null)
              AcaoDoPainel(
                chave: 'rastreio-criar-cena',
                icone: CupertinoIcons.cube_box,
                rotulo: 'Criar a cena',
                aoTocar: () => _criarCena(s),
              ),
          ],
        ),
      ],
      if (_erro != null) AureaAvisoDoPainel(texto: _erro!),
      if (!rodando && s == null && _erro == null)
        const AureaAvisoDoPainel(
          texto:
              'Descobre por onde a câmera andou e monta uma cena 3D em cima '
              'do vídeo: o que você puser nela fica parado no lugar do '
              'mundo real.',
        ),
      if (!rodando && s != null) ...[
        AureaPropertyRow.personalizada(
          rotulo: 'Qualidade',
          chave: 'rastreio-qualidade',
          filho: Text(
            '${'★' * s.estrelas}${'☆' * (5 - s.estrelas)}  '
            '${translate(context, 'Rastreio ${s.qualidade.toLowerCase()}')}',
            key: const ValueKey('rastreio-resultado'),
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: AureaEstilos.valor,
          ),
        ),
        AureaPropertyRow.personalizada(
          rotulo: 'Nuvem',
          chave: 'rastreio-nuvem',
          filho: AppTextMoldado(
            '{0} pontos · {1} posições · lente {2} mm',
            [
              s.nuvem.length,
              s.poses.length,
              (36 * s.focalPx / math.max(1, s.largura)).round(),
            ],
            maxLines: 1,
            style: AureaEstilos.valor,
          ),
        ),
        if (s.motor != null)
          AureaPropertyRow.personalizada(
            rotulo: 'Motor',
            chave: 'rastreio-motor',
            filho: AppTextMoldado(
              'Motor 2.0 · {0} quadros · {1} s',
              [
                s.quadros,
                s.analiseMs == null
                    ? '—'
                    : (s.analiseMs! / 1000).toStringAsFixed(
                        s.analiseMs! >= 10000 ? 0 : 1,
                      ),
              ],
              key: const ValueKey('rastreio-assinatura'),
              maxLines: 1,
              style: AureaEstilos.valor,
            ),
          )
        else
          const AureaAvisoDoPainel(
            key: ValueKey('rastreio-analise-antiga'),
            texto:
                'Essa análise veio de uma versão antiga do app e não é '
                'confiável. Analise de novo com o motor atual.',
          ),
        if (s.erroPixels >= 4)
          const AureaAvisoDoPainel(
            texto:
                'A cena vai escorregar. Um plano com mais textura e com a '
                'câmera andando de lado costuma resolver.',
          ),
        if ((s.largura / s.altura - projeto.aspectRatio).abs() > 0.03)
          const AureaAvisoDoPainel(
            texto:
                'O vídeo e a composição têm proporções diferentes: a cena 3D '
                'bate na horizontal e escorrega na vertical. Ajuste a '
                'composição à proporção do vídeo antes de montar em cima.',
          ),
      ],
    ];
  }

  // -------------------------------------------------------------- Pontos

  Future<void> _guardar(SolucaoCamera3D nova) async {
    _escolher({
      for (final id in _escolhidos)
        if (nova.nuvem.containsKey(id)) id,
    });
    await _t.guardar(widget.layerId, nova);
  }

  /// Chao, origem e escala mexem no MUNDO inteiro; objetos ja montados
  /// ficam onde estavam.
  void _avisarMundo(String feito) => AureaSnack.show(
    context,
    '$feito ${translate(context, 'Objetos já montados na cena não acompanham.')}',
    duration: const Duration(seconds: 5),
  );

  Future<void> _definirChao(SolucaoCamera3D s, {required bool auto}) async {
    final ids = auto ? maiorPlano(s.nuvem)?.ids : _escolhidos.toList();
    if (ids == null || ids.length < 3) {
      AureaSnack.show(
        context,
        auto
            ? 'Não achei uma superfície dominante na nuvem.'
            : 'Escolha pelo menos três pontos do chão.',
      );
      return;
    }
    await _guardar(definirChao(s, ids));
    if (mounted) _avisarMundo(translate(context, 'Chão definido.'));
  }

  Future<void> _definirOrigem(SolucaoCamera3D s) async {
    final ponto = s.nuvem[_escolhidos.single];
    if (ponto == null) return;
    await _guardar(definirOrigem(s, ponto));
    if (mounted) _avisarMundo(translate(context, 'Origem definida.'));
  }

  Future<void> _definirEscala(SolucaoCamera3D s) async {
    final ids = _escolhidos.toList();
    final metros = await showNumberInput(
      context,
      value: 1,
      unit: 'm',
      min: 0.001,
      decimals: 2,
      title: translate(context, 'Distância real entre os dois pontos'),
    );
    if (metros == null || !mounted) return;
    final fator = fatorDeEscalaReal(s, ids[0], ids[1], metros);
    if (fator == null) {
      AureaSnack.show(context, 'Esses dois pontos estão juntos demais.');
      return;
    }
    await _guardar(escalarMundo(s, fator));
    if (mounted) {
      _avisarMundo(translate(context, 'Escala definida: 100 unidades = 1 m.'));
    }
  }

  Future<void> _resolverDeNovo(SolucaoCamera3D s) async {
    if (!_t.podeResolverDeNovo(widget.layerId)) {
      AureaSnack.show(
        context,
        'Os rastros dessa análise não estão mais na memória. Analise de novo.',
      );
      return;
    }
    setState(() => _resolvendo = true);
    try {
      final nova = await _t.resolverDeNovo(
        widget.layerId,
        pontosApagados: <int>{},
        tipoDeTomada: s.tipoDeTomada,
      );
      if (nova != null) await _guardar(nova);
    } on RastreioException catch (e) {
      if (mounted) AureaSnack.show(context, e.mensagem);
    } finally {
      if (mounted) setState(() => _resolvendo = false);
    }
  }

  List<Widget> _pontos(SolucaoCamera3D? s, Set<int> escolhidos) {
    if (s == null) {
      return const [
        AureaAvisoDoPainel(
          texto: 'Analise o vídeo primeiro: os pontos aparecem sobre ele.',
        ),
      ];
    }
    final n = escolhidos.length;
    return [
      // A LEGENDA QUE ESCOLHE: tocar em "Ruim" escolhe todos os ruins de
      // uma vez — apagar os ruins e resolver de novo vira dois toques.
      LinhaDeFichas<QualidadeDoPonto>(
        rotulo: 'Escolher',
        chave: 'rastreio-qualidade-escolha',
        valores: QualidadeDoPonto.values,
        traduzir: false,
        rotuloDe: (q) =>
            '${translate(context, q.emPalavras)} · '
            '${s.pontosDaQualidade({q}).length}',
        escolhido: null,
        chaveDe: (q) => 'rastreio-qualidade-${q.name}',
        aoEscolher: (q) {
          final ids = s.pontosDaQualidade({q});
          final todos = ids.isNotEmpty && escolhidos.containsAll(ids);
          _escolher(
            todos
                ? escolhidos.difference(ids.toSet())
                : {...escolhidos, ...ids},
          );
        },
      ),
      AureaPropertyRow.personalizada(
        rotulo: 'Na mão',
        chave: 'rastreio-escolhidos',
        filho: AppTextMoldado(
          n == 1 ? '{0} ponto · toque no vídeo' : '{0} pontos · toque no vídeo',
          [n],
          maxLines: 1,
          style: AureaEstilos.valor,
        ),
      ),
      GradeDeAcoes(
        acoes: [
          AcaoDoPainel(
            chave: 'rastreio-chao-auto',
            icone: CupertinoIcons.square_grid_3x2,
            rotulo: 'Chão automático',
            aoTocar: () => _definirChao(s, auto: true),
          ),
          AcaoDoPainel(
            chave: 'rastreio-chao',
            icone: CupertinoIcons.squares_below_rectangle,
            rotulo: 'Chão escolhido',
            aoTocar: n >= 3 ? () => _definirChao(s, auto: false) : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-origem',
            icone: CupertinoIcons.smallcircle_circle,
            rotulo: 'Origem aqui',
            aoTocar: n == 1 ? () => _definirOrigem(s) : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-escala',
            icone: CupertinoIcons.arrow_left_right,
            rotulo: 'Escala real',
            aoTocar: n == 2 ? () => _definirEscala(s) : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-apagar',
            icone: CupertinoIcons.delete,
            rotulo: 'Apagar',
            aoTocar: n > 0
                ? () => _guardar(semPontos(s, {...escolhidos}))
                : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-apagar-ruins',
            icone: CupertinoIcons.wand_stars,
            rotulo: 'Apagar ruins',
            aoTocar: () {
              final ruins = s.pontosDaQualidade({
                QualidadeDoPonto.ruim,
                QualidadeDoPonto.fraco,
              });
              if (ruins.isEmpty) {
                AureaSnack.show(context, 'Não há pontos ruins para apagar.');
                return;
              }
              _guardar(semPontos(s, ruins.toSet()));
            },
          ),
          AcaoDoPainel(
            chave: 'rastreio-resolver',
            icone: CupertinoIcons.arrow_2_circlepath,
            rotulo: _resolvendo ? 'Resolvendo…' : 'Resolver de novo',
            aoTocar: _resolvendo ? null : () => _resolverDeNovo(s),
          ),
          AcaoDoPainel(
            chave: 'rastreio-soltar',
            icone: CupertinoIcons.xmark_circle,
            rotulo: 'Soltar',
            aoTocar: n > 0 ? () => _escolher(const {}) : null,
          ),
        ],
      ),
    ];
  }

  // --------------------------------------------------------------- Criar

  /// A CENA 3D onde as coisas entram. Cria se ainda nao existe: a pessoa
  /// pediu para pousar um texto no chao, nao para administrar camadas.
  String? _cenaDoClipe(VideoLayer camada, SolucaoCamera3D s) {
    final projeto = ref.read(editorControllerProvider);
    final nome = 'Cena 3D · ${camada.name}';
    for (final l in projeto.layers) {
      if (l is Scene3DLayer && l.name == nome) return l.id;
    }
    final id = _c.criarCenaDoRastreio(widget.layerId, s);
    _voltarAoVideo();
    return id;
  }

  /// CRIAR CAMADA SELECIONA A CAMADA NOVA — e trocar a selecao fecha o
  /// painel aberto, no meio da montagem. A selecao volta para o video e o
  /// painel e reaberto no mesmo passo (a arvore nem chega a ver o fechar):
  /// a pessoa continua pousando coisas no mesmo chao.
  void _voltarAoVideo() {
    final escopo = EscopoDoEditor.maybeOf(context);
    ref.read(selectedLayerProvider.notifier).state = widget.layerId;
    escopo?.abrirPainel(PainelId.rastrear);
  }

  void _feito(String texto) => AureaSnack.show(
    context,
    texto,
    actionLabel: 'Desfazer',
    onAction: _c.undo,
    duration: const Duration(seconds: 5),
  );

  /// O QUE ENTRA NA SUPERFICIE (ou no ponto, quando so um esta escolhido).
  Future<void> _por(
    VideoLayer camada,
    SolucaoCamera3D s,
    PlanoDoRastreio? plano,
    ObjetoNoPlano tipo,
  ) async {
    final escolhidos = _escolhidos;
    if (plano == null) {
      // UM PONTO SO: vira ancora (o "criar nulo" do After Effects).
      if (tipo == ObjetoNoPlano.nulo && escolhidos.length == 1) {
        final cena = _cenaDoClipe(camada, s);
        if (cena == null) return;
        _c.criarNoDoPonto(cena, s, escolhidos.single);
        _feito(translate(context, 'Âncora criada no ponto.'));
      }
      return;
    }
    final cena = _cenaDoClipe(camada, s);
    if (cena == null) {
      AureaSnack.show(context, 'Não consegui criar a cena 3D.');
      return;
    }
    String? textura;
    if (tipo == ObjetoNoPlano.texto) {
      // O TEXTO E UMA CAMADA DE TEXTO usada como textura da placa: ele
      // continua editavel com as ferramentas de texto de sempre.
      final antes = {
        for (final l in ref.read(editorControllerProvider).layers) l.id,
      };
      _c.addTextLayer(camada.startTime, text: 'Seu texto');
      for (final l in ref.read(editorControllerProvider).layers) {
        if (!antes.contains(l.id) && l is TextLayer) textura = l.id;
      }
      _voltarAoVideo();
    }
    _c.updateScene3D(
      cena,
      (c) => c.copyWith(
        nodes: [
          ...c.nodes,
          noNoPlano(plano, tipo, textureLayerId: textura),
        ],
      ),
    );
    if (mounted) _feito('${translate(context, tipo.emPalavras)} ✓');
  }

  /// UMA IMAGEM DA GALERIA, deitada na superficie: a placa com a imagem
  /// de textura (a imagem continua camada, editavel como qualquer outra).
  Future<void> _porImagem(
    VideoLayer camada,
    SolucaoCamera3D s,
    PlanoDoRastreio plano,
  ) async {
    final antes = {
      for (final l in ref.read(editorControllerProvider).layers) l.id,
    };
    await _c.importImageFromGallery(camada.startTime);
    if (!mounted) return;
    String? imagem;
    for (final l in ref.read(editorControllerProvider).layers) {
      if (!antes.contains(l.id) && l is ImageLayer) imagem = l.id;
    }
    if (imagem == null) return;
    final cena = _cenaDoClipe(camada, s);
    if (cena == null) return;
    _c.updateScene3D(
      cena,
      (c) => c.copyWith(
        nodes: [
          ...c.nodes,
          noNoPlano(
            plano,
            ObjetoNoPlano.solido,
            nome: translate(context, 'Imagem'),
            textureLayerId: imagem,
          ),
        ],
      ),
    );
    _voltarAoVideo();
    _feito(translate(context, 'Imagem na superfície.'));
  }

  /// UM MODELO 3D PRESO A SUPERFICIE: uma ancora no plano e a importacao 3D
  /// de sempre (aviso de modelo pesado, mapas, credito), com o modelo
  /// pendurado na ancora.
  Future<void> _porModelo(
    VideoLayer camada,
    SolucaoCamera3D s,
    PlanoDoRastreio plano,
  ) async {
    final escolha = await FilePicker.platform.pickFiles(
      allowMultiple: true,
      type: FileType.any,
      withData: false,
    );
    final caminhos =
        escolha?.files.map((f) => f.path).whereType<String>().toList() ??
        const <String>[];
    if (caminhos.isEmpty || !mounted) return;
    final cena = _cenaDoClipe(camada, s);
    if (cena == null) return;
    final ancora = noNoPlano(
      plano,
      ObjetoNoPlano.nulo,
      nome: translate(context, 'Âncora · superfície'),
    );
    _c.updateScene3D(cena, (c) => c.copyWith(nodes: [...c.nodes, ancora]));
    try {
      final noId = await concluirImportacao3D(
        context,
        ref,
        caminhos,
        playhead: EscopoDoEditor.of(context).playback.time.value,
        sceneId: cena,
      );
      if (noId == null || !mounted) return;
      _c.setSceneNodeParent(cena, noId, ancora.id);
      _voltarAoVideo();
      _feito(translate(context, 'Modelo preso à superfície.'));
    } on ModelImportException catch (e) {
      if (mounted) AureaSnack.show(context, e.message);
    } catch (_) {
      if (mounted) {
        AureaSnack.show(
          context,
          'Não consegui ler esse modelo. Tente GLB, glTF, OBJ ou FBX.',
        );
      }
    }
  }

  List<Widget> _criar(
    VideoLayer camada,
    SolucaoCamera3D? s,
    Set<int> escolhidos,
  ) {
    if (s == null) {
      return const [
        AureaAvisoDoPainel(
          texto:
              'Analise o vídeo primeiro: é na nuvem de pontos que as '
              'coisas pousam.',
        ),
      ];
    }
    final plano = escolhidos.length >= 3
        ? planoDosPontos(s.nuvem, escolhidos.toList())
        : null;
    final umPonto = escolhidos.length == 1;
    final superficie = plano != null;
    return [
      AureaAvisoDoPainel(
        texto: superficie
            ? '${translate(context, plano.tipo.emPalavras)}: '
                  '${translate(context, 'o que você criar deita nesta superfície.')}'
            : umPonto
            ? 'Um ponto escolhido: crie um Nulo para ancorar coisas nele.'
            : 'Escolha três ou mais pontos de uma superfície (toque nos '
                  'pontos sobre o vídeo, ou na aba Pontos).',
      ),
      GradeDeAcoes(
        acoes: [
          AcaoDoPainel(
            chave: 'rastreio-criar-texto',
            icone: CupertinoIcons.textformat,
            rotulo: 'Texto',
            aoTocar: superficie
                ? () => _por(camada, s, plano, ObjetoNoPlano.texto)
                : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-criar-forma',
            icone: CupertinoIcons.cube,
            rotulo: 'Forma',
            aoTocar: superficie
                ? () => _por(camada, s, plano, ObjetoNoPlano.forma)
                : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-criar-nulo',
            icone: CupertinoIcons.scope,
            rotulo: 'Nulo',
            aoTocar: superficie || umPonto
                ? () => _por(camada, s, plano, ObjetoNoPlano.nulo)
                : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-criar-placa',
            icone: CupertinoIcons.square,
            rotulo: 'Placa',
            aoTocar: superficie
                ? () => _por(camada, s, plano, ObjetoNoPlano.solido)
                : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-por-imagem',
            icone: CupertinoIcons.photo,
            rotulo: 'Imagem',
            aoTocar: superficie ? () => _porImagem(camada, s, plano) : null,
          ),
          AcaoDoPainel(
            chave: 'rastreio-por-3d',
            icone: CupertinoIcons.cube_box,
            rotulo: 'Modelo 3D',
            aoTocar: superficie ? () => _porModelo(camada, s, plano) : null,
          ),
        ],
      ),
    ];
  }

  // ------------------------------------------------------- Seguir objeto

  String _efeitoDeRegioes({bool criar = false}) {
    String procurar() {
      final l = ref.read(editorControllerProvider).layerById(widget.layerId);
      for (final e in l?.effects ?? const <EffectInstance>[]) {
        if (e.type == EffectType.blobTracker) return e.id;
      }
      return '';
    }

    final achado = procurar();
    if (achado.isNotEmpty || !criar) return achado;
    _c.addEffect(widget.layerId, EffectType.blobTracker);
    return procurar();
  }

  Future<void> _procurarObjetos() async {
    setState(() => _procurando = true);
    EscopoDoEditor.of(context).playback.pause();
    final efeito = _efeitoDeRegioes(criar: true);
    final n = efeito.isEmpty
        ? null
        : await _c.analyzeBlobsFor(widget.layerId, efeito);
    if (!mounted) return;
    setState(() => _procurando = false);
    if (n == null) AureaSnack.show(context, 'Não consegui ler esse vídeo');
  }

  void _grudar(String alvoId, String efeitoId) {
    final n = _c.grudarNoBlob(
      alvoId: alvoId,
      videoId: widget.layerId,
      effectId: efeitoId,
      blobId: _blob!,
      comEscala: _comEscala,
    );
    if (n == null || n == 0) {
      AureaSnack.show(context, 'Esse objeto não aparece no tempo dessa camada');
      return;
    }
    _feito(translate(context, 'Grudado no objeto.'));
  }

  List<Widget> _seguir(VideoLayer camada) {
    final efeito = _efeitoDeRegioes();
    final dados = efeito.isEmpty
        ? null
        : BlobTrackService.instance.dataFor(efeito);
    // So os que atravessam o trecho: os de tres quadros sao ruido.
    final ids = dados == null || dados.isEmpty
        ? const <int>[]
        : [
            for (final id in dados.idsPorDuracao)
              if (dados.duracaoDe(id) >= 4) id,
          ].take(12).toList();
    final outras = [
      for (final l in ref.read(editorControllerProvider).layers)
        if (l.id != widget.layerId) l,
    ];
    return [
      GradeDeAcoes(
        acoes: [
          AcaoDoPainel(
            chave: 'rastreio-objetos',
            icone: CupertinoIcons.person_crop_rectangle,
            rotulo: _procurando
                ? 'Procurando…'
                : (ids.isEmpty ? 'Procurar objetos' : 'Procurar de novo'),
            ativo: ids.isEmpty,
            aoTocar: _procurando ? null : _procurarObjetos,
          ),
        ],
      ),
      if (dados != null && !dados.isEmpty && ids.isEmpty)
        const AureaAvisoDoPainel(
          texto:
              'Não achei nada que se mexa nesse trecho. Aumente a '
              'sensibilidade no efeito, ou escolha um trecho com mais '
              'movimento.',
        ),
      if (ids.isNotEmpty)
        LinhaDeFichas<int>(
          rotulo: 'Objeto',
          chave: 'rastreio-blob',
          valores: ids,
          traduzir: false,
          rotuloDe: (id) =>
              '${translate(context, 'Objeto')} $id · ${dados!.duracaoDe(id)} q',
          escolhido: _blob,
          chaveDe: (id) => 'blob-$id',
          aoEscolher: (id) => setState(() => _blob = id),
        ),
      if (ids.isEmpty && (dados == null || dados.isEmpty))
        const AureaAvisoDoPainel(
          texto:
              'Acha o que se mexe no vídeo e gruda uma camada nele: o nome '
              'que acompanha a pessoa, a seta que segue o carro.',
        ),
      if (_blob != null && ids.contains(_blob)) ...[
        linhaDeInterruptor(
          rotulo: 'Crescer junto',
          chave: 'rastreio-com-escala',
          valor: _comEscala,
          aoMudar: (v) => setState(() => _comEscala = v),
        ),
        if (outras.isEmpty)
          const AureaAvisoDoPainel(
            texto:
                'Adicione um texto ou uma forma antes: é ela que vai seguir '
                'o objeto.',
          )
        else ...[
          const AureaAvisoDoPainel(texto: 'Qual camada gruda nesse objeto?'),
          for (final l in outras)
            AureaLayerRow(
              key: ValueKey('rastreio-grudar-${l.id}'),
              nome: l.name,
              icone: CupertinoIcons.link,
              aoTocar: () => _grudar(l.id, efeito),
            ),
        ],
      ],
    ];
  }
}

// ===========================================================================
// os pontos no palco
// ===========================================================================

/// OS PONTOS DO RASTREIO SOBRE O PALCO, enquanto o painel Rastrear esta
/// aberto num video ja analisado.
///
/// Vive dentro do `Stack` da composicao (montado pelo
/// `GizmoDaCenaOverlay`), em pixels de composicao — e por isso acompanha
/// o video, o zoom e o pan do palco sozinho. A nuvem e projetada pela
/// pose do instante do cabecote: com o video andando, os pontos andam
/// com ele, e um ponto que escorrega do objeto que marcava e o rastreio
/// dizendo que errou ali.
///
/// O DEDO SO E ROUBADO EM CIMA DE UM PONTO: fora deles o toque desce ao
/// palco (selecionar outra camada, arrastar, pinca).
class PontosDoRastreioNoPalco extends ConsumerWidget {
  const PontosDoRastreioNoPalco({
    super.key,
    required this.tempo,
    required this.escala,
  });

  final ValueListenable<Duration> tempo;

  /// O fator composicao -> tela (o mesmo do palco).
  final double escala;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    if (ref.watch(painelAbertoProvider) != PainelId.rastrear) {
      return const SizedBox.shrink();
    }
    final id = ref.watch(selectedLayerProvider);
    if (id == null) return const SizedBox.shrink();
    final servico = CameraTrackService.instance;
    return Positioned.fill(
      child: ListenableBuilder(
        listenable: Listenable.merge([tempo, servico.revision]),
        builder: (context, _) {
          // SO A CAMADA DO VIDEO: mutacao de outra camada nao redesenha a
          // nuvem.
          final camada = ref.watch(
            editorControllerProvider.select((p) => p.layerById(id)),
          );
          final s = servico.dataFor(id);
          final t = tempo.value;
          if (camada is! VideoLayer ||
              s == null ||
              s.isEmpty ||
              !camada.activeAt(t)) {
            return const SizedBox.shrink();
          }
          final c = ref.read(editorControllerProvider.notifier);
          final local = camada.localTime(t);
          final pontos = pontosDoRastreioNaComposicao(
            s,
            quadroDoRastreio(camada, s, t),
            centro: camada.position.valueAt(local),
            caixa: c.layerBoxRect(camada, t).size,
            giroGraus: camada.rotation.valueAt(local),
          );
          final escolhidos = _escolhidosDe(
            ref.watch(pontosDoRastreioProvider),
            id,
          );
          final e = escala <= 0 ? 1.0 : escala;
          int? noDedo(Offset p) {
            int? achado;
            var perto = 26 / e;
            for (final par in pontos.entries) {
              final d = (par.value - p).distance;
              if (d < perto) {
                perto = d;
                achado = par.key;
              }
            }
            return achado;
          }

          return _AreaDosPontos(
            pega: (p) => noDedo(p) != null,
            child: GestureDetector(
              key: const ValueKey('rastreio-pontos-no-palco'),
              behavior: HitTestBehavior.opaque,
              onTapUp: (d) {
                final ponto = noDedo(d.localPosition);
                if (ponto == null) return;
                final agora = {...escolhidos};
                if (!agora.remove(ponto)) agora.add(ponto);
                ref.read(pontosDoRastreioProvider.notifier).state = (
                  layerId: id,
                  ids: agora,
                );
              },
              child: CustomPaint(
                painter: _PintorDosPontos(
                  solucao: s,
                  pontos: pontos,
                  escolhidos: escolhidos,
                  escala: e,
                  anel: AureaCores.texto,
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}

/// Os pontos: cor pela qualidade, anel nos escolhidos — tamanhos em pixels
/// de TELA (divididos pela escala do palco), para caberem no dedo em
/// qualquer zoom.
class _PintorDosPontos extends CustomPainter {
  _PintorDosPontos({
    required this.solucao,
    required this.pontos,
    required this.escolhidos,
    required this.escala,
    required this.anel,
  });

  final SolucaoCamera3D solucao;
  final Map<int, Offset> pontos;
  final Set<int> escolhidos;
  final double escala;
  final Color anel;

  @override
  void paint(Canvas canvas, Size size) {
    final ponto = Paint()..style = PaintingStyle.fill;
    final contorno = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2 / escala
      ..color = anel;
    for (final e in pontos.entries) {
      ponto.color = corDoPonto(solucao.qualidadeDoPonto(e.key));
      canvas.drawCircle(e.value, 3.5 / escala, ponto);
      if (escolhidos.contains(e.key)) {
        canvas.drawCircle(e.value, 8 / escala, contorno);
      }
    }
  }

  @override
  bool shouldRepaint(_PintorDosPontos old) =>
      !mapEquals(old.pontos, pontos) ||
      !setEquals(old.escolhidos, escolhidos) ||
      old.escala != escala ||
      old.solucao != solucao ||
      old.anel != anel;
}

/// A AREA QUE SO EXISTE EM CIMA DE UM PONTO (a mesma regra do gizmo).
class _AreaDosPontos extends SingleChildRenderObjectWidget {
  const _AreaDosPontos({required this.pega, super.child});

  final bool Function(Offset) pega;

  @override
  RenderObject createRenderObject(BuildContext context) =>
      _RenderAreaDosPontos(pega);

  @override
  void updateRenderObject(BuildContext context, _RenderAreaDosPontos r) {
    r.pega = pega;
  }
}

class _RenderAreaDosPontos extends RenderProxyBox {
  _RenderAreaDosPontos(this.pega);

  bool Function(Offset) pega;

  @override
  bool hitTest(BoxHitTestResult result, {required Offset position}) {
    if (!pega(position)) return false;
    return super.hitTest(result, position: position);
  }
}
