import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../application/font_service.dart';
import '../../../application/interacao.dart';
import '../../../domain/element3d.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/modelo_do_texto3d.dart';
import '../../../domain/texto3d.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_3d.dart';

/// Como a faixa de letras do cartao Caracteres e escolhida.
enum SelecaoDeLetras { todas, uma, intervalo }

String rotuloDaSelecao(SelecaoDeLetras s) => switch (s) {
  SelecaoDeLetras.todas => 'Todas as letras',
  SelecaoDeLetras.uma => 'Um caractere',
  SelecaoDeLetras.intervalo => 'Intervalo',
};

String nomeDoChanfro(TipoDeChanfro c) => switch (c) {
  TipoDeChanfro.nenhum => 'Sem chanfro',
  TipoDeChanfro.angular => 'Angular',
  TipoDeChanfro.redondo => 'Redondo',
};

String nomeDaQualidade(QualidadeDoTexto3D q) => switch (q) {
  QualidadeDoTexto3D.baixa => 'Baixa',
  QualidadeDoTexto3D.media => 'Média',
  QualidadeDoTexto3D.alta => 'Alta',
};

/// TEXTO 3D — a palavra, a fonte, o volume, o metal, a luz e as letras
/// soltas, num painel da casa.
///
/// SO EXISTE PARA CAMADA QUE E TEXTO 3D. Em qualquer outra (texto comum,
/// modelo 3D, video) o painel diz isso e nao oferece controle nenhum: uma
/// regua de profundidade num video seria uma promessa sem resposta.
///
/// Cinco abas, na ordem da prioridade do dono — o que da volume vem antes
/// do acabamento, e as letras soltas por ultimo:
///
///   Texto       a palavra, a fonte (e importar), espacamento, qualidade
///   Extrusao    profundidade, chanfro e o tamanho dele
///   Material    predefinicao, cor base, metal, rugosidade, brilho proprio
///   Luz         reflexo, iluminacao (o estudio refletido) e ambiente
///   Caracteres  todas / uma / intervalo, e as nove medidas com losango
///
/// O que refaz a malha passa pela [FilaDoTexto3D] (o numero anda na hora,
/// a malha 140 ms depois, o arrasto e um desfazer so). Mover letras NAO
/// refaz malha: e matriz por letra, direto no controlador.
class PainelTexto3D extends ConsumerStatefulWidget {
  const PainelTexto3D({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelTexto3D> createState() => _PainelTexto3DState();
}

class _PainelTexto3DState extends ConsumerState<PainelTexto3D> {
  static const _titulo = 'Texto 3D';
  static const _abas = ['Texto', 'Extrusão', 'Material', 'Luz', 'Caracteres'];

  /// Abre na Extrusao: e o que faz a letra ter volume, o primeiro ajuste
  /// que se procura num Texto 3D recem-criado.
  int _aba = 1;

  late final FilaDoTexto3D _fila;
  final _campo = TextEditingController();
  final _foco = FocusNode();
  bool _importandoFonte = false;

  SelecaoDeLetras _selecao = SelecaoDeLetras.todas;
  int _um = 0;
  int _de = 0;
  int _ate = 0;

  /// O aviso "tem keyframes, use o losango" sai UMA vez por arrasto, e nao
  /// a cada pixel.
  bool _avisouNoGesto = false;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  void initState() {
    super.initState();
    _fila = FilaDoTexto3D(
      controlador: ref.read(editorControllerProvider.notifier),
      lerProjeto: () => ref.read(editorControllerProvider),
      aoMudar: () {
        if (mounted) setState(() {});
      },
    );
    FontService.instance.revision.addListener(_fontesMudaram);
  }

  void _fontesMudaram() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    FontService.instance.revision.removeListener(_fontesMudaram);
    _fila.descartar();
    _campo.dispose();
    _foco.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, widget.layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.texto3d.name}';
    final dados = texto3DEmTela(camada, _fila);
    final noId = noDoTexto3D(camada);
    if (dados == null || noId == null || camada is! Scene3DLayer) {
      return AureaPanel(
        titulo: _titulo,
        chave: chave,
        aoFechar: escopo.fecharPainel,
        filhos: const [
          AureaAvisoDoPainel(
            texto:
                'Esta camada não é um Texto 3D. Num texto, use a '
                'ferramenta Texto 3D da barra para convertê-lo.',
          ),
        ],
      );
    }
    final params = dados.params;
    final estilo = dados.estilo;
    if (!_foco.hasFocus && _campo.text != params.texto) {
      _campo.text = params.texto;
    }

    final aviso = _fila.aviso;
    final topo = <Widget>[
      if (aviso != null)
        Padding(
          key: const ValueKey('texto3d-aviso'),
          padding: const EdgeInsets.only(bottom: AureaDims.e6),
          // O motivo vem do controlador (fonte CFF, fonte ilegivel): e
          // frase pronta, nao rotulo.
          child: Text(
            aviso,
            style: AureaEstilos.propriedade.copyWith(color: AureaCores.perigo),
          ),
        ),
    ];

    Widget lista(List<Widget> filhos) => ListView(
      key: ValueKey('texto3d-aba-$_aba'),
      padding: paddingDoPainel,
      children: [...topo, ...filhos],
    );

    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      acoes: [
        if (_fila.montando)
          const Padding(
            key: ValueKey('texto3d-montando'),
            padding: EdgeInsets.only(right: AureaDims.e8),
            child: CupertinoActivityIndicator(radius: 7),
          ),
      ],
      corpo: switch (_aba) {
        0 => lista(_texto(camada, noId, params, estilo)),
        1 => lista(_extrusao(camada, noId, params, estilo)),
        2 => lista(
          linhasDoMaterialDoTexto3D(
            context,
            c: _c,
            camada: camada,
            noId: noId,
            params: params,
            estilo: estilo,
            fila: _fila,
          ),
        ),
        3 => lista(_luz(camada)),
        _ => NoCabecote(
          construir: (context, t) =>
              lista(_caracteres(camada, noId, params, t)),
        ),
      },
    );
  }

  // --------------------------------------------------------------- linhas

  /// UMA MEDIDA QUE REFAZ A MALHA: o numero anda com o dedo, a malha vem
  /// depois pela fila, e o arrasto inteiro e um passo de desfazer.
  AureaPropertyRow _daMalha({
    required String rotulo,
    required String chave,
    required double valor,
    required double min,
    required double max,
    required Texto3D Function(double v) com,
    required Scene3DLayer camada,
    required String noId,
    required EstiloDoTexto3D estilo,
    int casas = 0,
    String unidade = '',
  }) => AureaPropertyRow(
    rotulo: rotulo,
    chave: chave,
    valor: valor.clamp(min, max).toDouble(),
    min: min,
    max: max,
    casas: casas,
    unidade: unidade,
    aoComecarGesto: _c.beginGesture,
    aoTerminarGesto: _fila.terminarGesto,
    aoMudar: (v) {
      Interacao.marcar();
      _fila.agendar(camada.id, noId, com(v.clamp(min, max).toDouble()), estilo);
    },
  );

  Future<void> _aplicar(
    Scene3DLayer camada,
    String noId,
    Texto3D novo,
    EstiloDoTexto3D estilo,
  ) => _fila.aplicar(camada.id, noId, novo, estilo);

  // ------------------------------------------------------------------ Texto

  List<Widget> _texto(
    Scene3DLayer camada,
    String noId,
    Texto3D params,
    EstiloDoTexto3D estilo,
  ) {
    final familias = {
      ...FontService.instance.families,
      params.familia,
    }.toList();
    return [
      Padding(
        padding: const EdgeInsets.only(bottom: AureaDims.e6),
        child: CupertinoTextField(
          key: const ValueKey('texto3d-campo'),
          controller: _campo,
          focusNode: _foco,
          placeholder: translate(context, 'Escreva o texto'),
          style: AureaEstilos.corpo,
          textCapitalization: TextCapitalization.characters,
          padding: const EdgeInsets.symmetric(
            horizontal: AureaDims.e10,
            vertical: AureaDims.e8,
          ),
          decoration: BoxDecoration(
            color: AureaCores.campo,
            borderRadius: BorderRadius.circular(AureaDims.raioMd),
          ),
          // NO FIM, E NAO A CADA TECLA: refazer a malha de dez letras custa
          // caro, e o campo perderia o foco a cada reconstrucao.
          onSubmitted: (v) {
            final limpo = v.trim();
            if (limpo.isEmpty || limpo == params.texto) return;
            _aplicar(camada, noId, params.copyWith(texto: limpo), estilo);
          },
        ),
      ),
      AureaPropertyRow.personalizada(
        rotulo: 'Fonte',
        chave: 'texto3d-fonte',
        filho: AureaDropdown<String>(
          key: const ValueKey('texto3d-fonte-escolha'),
          valor: params.familia,
          opcoes: familias,
          rotuloDe: (f) => f,
          // O NOME DA FONTE e conteudo, nao rotulo.
          traduzir: false,
          titulo: 'Fonte',
          aoMudar: (f) =>
              _aplicar(camada, noId, params.copyWith(familia: f), estilo),
        ),
      ),
      LinhaDePorta(
        key: const ValueKey('texto3d-importar-fonte'),
        rotulo: _importandoFonte
            ? 'Importando fonte...'
            : 'Importar fonte (.ttf / .otf)',
        icone: CupertinoIcons.plus_circle,
        aoTocar: () => _importarFonte(camada, noId, params, estilo),
      ),
      _daMalha(
        rotulo: 'Espaçamento',
        chave: 'texto3d-espacamento',
        valor: params.espacamento,
        min: -0.2,
        max: 0.6,
        casas: 2,
        com: (v) => params.copyWith(espacamento: v),
        camada: camada,
        noId: noId,
        estilo: estilo,
      ),
      LinhaDeFichas<QualidadeDoTexto3D>(
        rotulo: 'Qualidade',
        chave: 'texto3d-qualidade',
        valores: QualidadeDoTexto3D.values,
        rotuloDe: nomeDaQualidade,
        escolhido: params.qualidade,
        chaveDe: (q) => 'texto3d-qualidade-${q.name}',
        aoEscolher: (q) =>
            _aplicar(camada, noId, params.copyWith(qualidade: q), estilo),
      ),
    ];
  }

  /// TRAZ UMA FONTE .ttf/.otf E JA A APLICA AO TEXTO. O app vem com uma
  /// familia so: sem isto nao ha por onde trocar a fonte de um Texto 3D.
  Future<void> _importarFonte(
    Scene3DLayer camada,
    String noId,
    Texto3D params,
    EstiloDoTexto3D estilo,
  ) async {
    if (_importandoFonte) return;
    setState(() => _importandoFonte = true);
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['ttf', 'otf'],
        allowMultiple: true,
      );
      if (r == null) return;
      final result = await FontService.instance.importMany(
        r.files.map((f) => f.path).whereType<String>(),
      );
      if (!mounted) return;
      if (result.imported.isNotEmpty) {
        await _aplicar(
          camada,
          noId,
          params.copyWith(familia: result.imported.first),
          estilo,
        );
      }
    } catch (_) {
      // Fonte ilegivel: o texto fica com a fonte que tinha.
    } finally {
      if (mounted) setState(() => _importandoFonte = false);
    }
  }

  // --------------------------------------------------------------- Extrusao

  List<Widget> _extrusao(
    Scene3DLayer camada,
    String noId,
    Texto3D params,
    EstiloDoTexto3D estilo,
  ) => [
    // PROFUNDIDADE e o nome do que isto faz — "espessura" e a medida
    // interna da extrusao, e ninguem procura por ela.
    _daMalha(
      rotulo: 'Profundidade',
      chave: 'texto3d-profundidade',
      valor: params.espessura,
      min: 4,
      max: 120,
      com: (v) => params.copyWith(espessura: v),
      camada: camada,
      noId: noId,
      estilo: estilo,
    ),
    LinhaDeFichas<TipoDeChanfro>(
      rotulo: 'Chanfro',
      chave: 'texto3d-chanfro',
      valores: TipoDeChanfro.values,
      rotuloDe: nomeDoChanfro,
      escolhido: params.chanfro,
      chaveDe: (c) => 'texto3d-chanfro-${c.name}',
      aoEscolher: (c) =>
          _aplicar(camada, noId, params.copyWith(chanfro: c), estilo),
    ),
    if (params.chanfro != TipoDeChanfro.nenhum)
      _daMalha(
        rotulo: 'Tamanho do chanfro',
        chave: 'texto3d-chanfro-tamanho',
        valor: params.larguraDoChanfro,
        min: 0.5,
        max: 12,
        casas: 1,
        com: (v) => params.copyWith(larguraDoChanfro: v),
        camada: camada,
        noId: noId,
        estilo: estilo,
      ),
  ];

  // -------------------------------------------------------------------- Luz

  /// REFLEXO, ILUMINACAO E AMBIENTE moram na CENA (ela e quem tem o que
  /// refletir): vao direto, sem refazer malha e sem espera.
  List<Widget> _luz(Scene3DLayer camada) {
    final cena = camada.scene;
    return [
      linhaSemLosango(
        _c,
        rotulo: 'Reflexo',
        chave: 'texto3d-reflexo',
        valor: cena.envReflect * 100,
        min: 0,
        max: 100,
        unidade: '%',
        aoMudar: (v) => _c.ajustarReflexoDoTexto3D(camada.id, v / 100),
      ),
      AureaPropertyRow.personalizada(
        rotulo: 'Iluminação',
        chave: 'texto3d-iluminacao',
        filho: AureaDropdown<EnvironmentKind>(
          key: const ValueKey('texto3d-iluminacao-escolha'),
          valor: cena.environment,
          opcoes: EnvironmentKind.values,
          rotuloDe: environmentLabel,
          titulo: 'Iluminação',
          aoMudar: (k) => _c.trocarIluminacaoDoTexto3D(camada.id, k),
        ),
      ),
      linhaSemLosango(
        _c,
        rotulo: 'Ambiente',
        chave: 'texto3d-ambiente',
        valor: cena.ambient * 100,
        min: 0,
        max: 100,
        unidade: '%',
        aoMudar: (v) => _c.ajustarAmbienteDoTexto3D(camada.id, v / 100),
      ),
    ];
  }

  // ------------------------------------------------------------- Caracteres

  /// AS UNIDADES DE TEXTO (graphemes), na contagem que o motor usa para a
  /// vez de cada letra — a contagem em que a faixa e gravada. Espaco conta:
  /// mover "A B" pelo indice 2 tem de pegar o "B".
  static List<String> _letras(Texto3D t) =>
      t.texto.replaceAll('\r', '').characters.toList();

  /// A FAIXA ESCOLHIDA, como e gravada: fim negativo = ate a ultima letra,
  /// que e como "Todas as letras" se escreve.
  (int, int) _faixa(int quantas) {
    final ultima = quantas <= 0 ? 0 : quantas - 1;
    return switch (_selecao) {
      SelecaoDeLetras.todas => (0, -1),
      SelecaoDeLetras.uma => (_um.clamp(0, ultima), _um.clamp(0, ultima)),
      SelecaoDeLetras.intervalo => () {
        final a = _de.clamp(0, ultima), b = _ate.clamp(0, ultima);
        return a <= b ? (a, b) : (b, a);
      }(),
    };
  }

  /// OS AJUSTES GRAVADOS no no — nunca os da fila: uma malha em construcao
  /// carrega a lista de antes, e mover letra nao pode esperar por ela.
  List<AjusteDeCaracteres> _gravados(Scene3DLayer camada, String noId) =>
      camada.scene.nodeById(noId)?.texto3d?.ajustes ?? const [];

  AjusteDeCaracteres _ajuste(
    Scene3DLayer camada,
    String noId,
    (int, int) faixa,
  ) {
    final (i, f) = faixa;
    for (final a in _gravados(camada, noId)) {
      if (a.inicio == i && a.fim == f) return a;
    }
    return AjusteDeCaracteres(inicio: i, fim: f);
  }

  void _gravarAjuste(
    Scene3DLayer camada,
    String noId,
    (int, int) faixa,
    AjusteDeCaracteres novo,
  ) {
    final (i, f) = faixa;
    final resto = [
      for (final a in _gravados(camada, noId))
        if (a.inicio != i || a.fim != f) a,
    ];
    // NAO PASSA PELO EXTRUSOR: mover uma letra e matriz, nao malha.
    _c.ajustarCaracteresDoTexto3D(
      camada.id,
      noId,
      novo.inerte ? resto : [...resto, novo],
    );
  }

  List<Widget> _caracteres(
    Scene3DLayer camada,
    String noId,
    Texto3D params,
    Duration t,
  ) {
    final letras = _letras(params);
    final ultima = (letras.length - 1).clamp(0, 1 << 20).toDouble();
    final faixa = _faixa(letras.length);
    final (i0, f0) = faixa;
    final local = camada.localTime(t);
    final aj = _ajuste(camada, noId, faixa);
    final playback = EscopoDoEditor.of(context).playback;

    Widget medida(MedidaDoCaractere m) {
      final trilha = aj.trilha(m);
      final (min, max, casas, unidade) = faixaDaMedida(m);
      final kf = losangoDasMarcas(
        marcasUs: marcasDaTrilha(trilha),
        camada: camada,
        t: t,
        playback: playback,
        aoAlternar: () => _gravarAjuste(
          camada,
          noId,
          faixa,
          aj.com(
            m,
            trilha.hasKeyframeAt(local)
                ? trilha.withoutKeyframe(local)
                : trilha.comMarcaInserida(local),
          ),
        ),
      );
      return AureaPropertyRow(
        rotulo: rotuloDaMedida(m),
        chave: 'texto3d-ajuste-${m.name}',
        valor: trilha.valueAt(local).clamp(min, max).toDouble(),
        min: min,
        max: max,
        casas: casas,
        unidade: unidade,
        keyframe: kf.estado,
        aoAnterior: kf.anterior,
        aoProximo: kf.proximo,
        aoResetar: () => _gravarAjuste(
          camada,
          noId,
          faixa,
          aj.com(m, AnimatedDouble(padraoDaMedida(m))),
        ),
        aoComecarGesto: () {
          _avisouNoGesto = false;
          _c.beginGesture();
        },
        aoTerminarGesto: _c.endGesture,
        aoMudar: (v) {
          Interacao.marcar();
          // O AJUSTE E RELIDO A CADA PASSO: o `aj` do build ja ficou velho
          // depois do primeiro pixel do arrasto.
          final atual = _ajuste(camada, noId, faixa);
          final tr = atual.trilha(m);
          if (!tr.aceitaEdicaoEm(local)) {
            // A REGRA DA CASA (docs/keyframe-explicito.md): editar um
            // valor animado fora de uma marca nao cria keyframe sozinho.
            if (!_avisouNoGesto) {
              _avisouNoGesto = true;
              AureaSnack.show(
                context,
                'Esta opção tem keyframes: toque no losango para marcar '
                'este instante',
              );
            }
            return;
          }
          _gravarAjuste(
            camada,
            noId,
            faixa,
            atual.com(m, tr.edited(local, v.clamp(min, max).toDouble())),
          );
        },
      );
    }

    return [
      LinhaDeFichas<SelecaoDeLetras>(
        rotulo: 'Seleção',
        chave: 'texto3d-selecao',
        valores: SelecaoDeLetras.values,
        rotuloDe: rotuloDaSelecao,
        escolhido: _selecao,
        chaveDe: (s) => 'texto3d-selecao-${s.name}',
        aoEscolher: (s) => setState(() {
          _selecao = s;
          if (s == SelecaoDeLetras.intervalo && _de == _ate) {
            _ate = ultima.toInt();
          }
        }),
      ),
      // AS LETRAS COMO FICHAS: tocar no "C" escolhe o "C". Procurar o
      // indice 2 de "ABCDE" num numero e trabalho; a letra esta ali.
      if (_selecao != SelecaoDeLetras.todas)
        AureaPropertyRow.personalizada(
          rotulo: 'Letras',
          chave: 'texto3d-letras',
          filho: SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              children: [
                for (var i = 0; i < letras.length; i++)
                  Padding(
                    padding: const EdgeInsets.only(right: AureaDims.e4),
                    child: AureaChip(
                      key: ValueKey('texto3d-letra-$i'),
                      rotulo: letras[i].trim().isEmpty ? '␣' : letras[i],
                      // A LETRA e conteudo do usuario.
                      traduzir: false,
                      ativo: i >= i0 && (f0 < 0 || i <= f0),
                      aoTocar: () => setState(() {
                        if (_selecao == SelecaoDeLetras.uma) {
                          _um = i;
                        } else if (i < _de) {
                          _de = i;
                        } else {
                          _ate = i;
                        }
                      }),
                    ),
                  ),
              ],
            ),
          ),
        ),
      if (_selecao == SelecaoDeLetras.intervalo) ...[
        AureaPropertyRow(
          rotulo: 'De',
          chave: 'texto3d-de',
          valor: _de.toDouble().clamp(0, ultima),
          min: 0,
          max: ultima <= 0 ? 1 : ultima,
          casas: 0,
          aoMudar: (v) => setState(() => _de = v.round()),
        ),
        AureaPropertyRow(
          rotulo: 'Até',
          chave: 'texto3d-ate',
          valor: _ate.toDouble().clamp(0, ultima),
          min: 0,
          max: ultima <= 0 ? 1 : ultima,
          casas: 0,
          aoMudar: (v) => setState(() => _ate = v.round()),
        ),
      ],
      for (final m in MedidaDoCaractere.values) medida(m),
    ];
  }
}

/// Faixa, casas e unidade de cada medida. Posicao anda em unidades da cena
/// (o texto nasce com 100 de corpo), giro em graus, escala em multiplos,
/// e espacamento e offset em FRACAO DO CORPO — assim valem o mesmo num
/// texto grande e num pequeno.
(double, double, int, String) faixaDaMedida(MedidaDoCaractere m) => switch (m) {
  MedidaDoCaractere.x ||
  MedidaDoCaractere.y ||
  MedidaDoCaractere.z => (-400, 400, 1, ''),
  MedidaDoCaractere.girX ||
  MedidaDoCaractere.girY ||
  MedidaDoCaractere.girZ => (-180, 180, 1, '°'),
  MedidaDoCaractere.escala => (0, 4, 2, '×'),
  MedidaDoCaractere.espacamento => (-1, 2, 2, ''),
  MedidaDoCaractere.offset => (-4, 4, 2, ''),
};
