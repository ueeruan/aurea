import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../am/color_picker_sheet.dart' show showColorPicker;
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';

/// PARTICULAS — a receita (um toque) e os controles do motor em cinco
/// abas curtas: Principal (o que muda o resultado em qualquer receita),
/// Emissor, Movimento, Aparencia e Faiscas.
///
/// Os parametros do motor nao tem trilha (nao animam): sem losango. Toda
/// edicao troca a receita por uma COPIA (`clonar`) — sem ela o desfazer
/// guardaria o mesmo objeto que a camada nova e voltar nao voltaria nada.
class PainelParticulas extends ConsumerStatefulWidget {
  const PainelParticulas({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelParticulas> createState() => _PainelParticulasState();
}

class _PainelParticulasState extends ConsumerState<PainelParticulas> {
  static const _titulo = 'Partículas';
  static const _abas = [
    'Principal',
    'Emissor',
    'Movimento',
    'Aparência',
    'Faíscas',
  ];

  int _aba = 0;

  String get _id => widget.layerId;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  void _up(void Function(ParametrosDeParticulas p) mexer) =>
      _c.updateParticulas(_id, (p) {
        final copia = p.clonar();
        mexer(copia);
        return copia;
      });

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, _id);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.particulas.name}';
    if (camada is! ParticulasLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de partículas.',
      );
    }
    final ParametrosDeParticulas q = camada.parametros;
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      corpo: ListView(
        key: ValueKey('particulas-aba-$_aba'),
        padding: const EdgeInsets.fromLTRB(
          AureaDims.margemDoPainel,
          AureaDims.e4,
          AureaDims.margemDoPainel,
          AureaDims.topoDoPainel,
        ),
        children: switch (_aba) {
          0 => _principal(q),
          1 => _emissor(q),
          2 => _movimento(q),
          3 => _aparencia(q),
          _ => _faiscas(q),
        },
      ),
    );
  }

  Widget _n(
    String rotulo,
    double valor,
    double min,
    double max,
    ValueChanged<double> aoMudar, {
    int casas = 0,
    String unidade = '',
    String? chave,
  }) => linhaNumerica(
    ref,
    rotulo: rotulo,
    chave: chave,
    valor: valor,
    min: min,
    max: max,
    casas: casas,
    unidade: unidade,
    aoMudar: (v) => aoMudar(v.clamp(min, max).toDouble()),
  );

  Widget _escolha<T>(
    String rotulo,
    T atual,
    List<T> opcoes,
    String Function(T) rotuloDe,
    ValueChanged<T> aoMudar,
  ) => AureaPropertyRow.personalizada(
    rotulo: rotulo,
    filho: AureaDropdown<T>(
      valor: atual,
      opcoes: opcoes,
      rotuloDe: rotuloDe,
      titulo: rotulo,
      aoMudar: aoMudar,
    ),
  );

  Widget _cor(String rotulo, int argb, ValueChanged<int> aoMudar) =>
      AureaPropertyRow.cor(
        rotulo: rotulo,
        cor: Color(argb),
        aoTocar: () async {
          EscopoDoEditor.of(context).playback.pause();
          final nova = await showColorPicker(context, initial: Color(argb));
          if (nova != null) aoMudar(nova.toARGB32());
        },
      );

  Widget _desenho(ParametrosDeParticulas q) => _escolha<FormaDaParticula>(
    'Desenho',
    q.forma,
    FormaDaParticula.values,
    (f) => switch (f) {
      FormaDaParticula.esfera => 'Esfera',
      FormaDaParticula.estrela => 'Estrela',
      FormaDaParticula.risco => 'Risco',
      FormaDaParticula.nuvem => 'Nuvem',
      FormaDaParticula.quadrado => 'Quadrado',
      FormaDaParticula.anel => 'Anel',
    },
    (f) => _up((p) => p.forma = f),
  );

  // ---------------------------------------------------------- principal

  List<Widget> _principal(ParametrosDeParticulas q) {
    final receitas = MotorDeParticulasRender.nomesDosPresets;
    return [
      if (receitas.isNotEmpty) ...[
        Wrap(
          spacing: AureaDims.e6,
          runSpacing: AureaDims.e6,
          children: [
            for (var i = 0; i < receitas.length; i++)
              AureaChip(
                key: ValueKey('particulas-receita-$i'),
                rotulo: receitas[i],
                aoTocar: () {
                  final nova = MotorDeParticulasRender.aplicarPreset(i, q);
                  if (nova != null) _c.updateParticulas(_id, (_) => nova);
                },
              ),
          ],
        ),
        const SizedBox(height: AureaDims.e6),
      ],
      if (q.taxaDeNascimento > 0)
        _n(
          'Emissão/s',
          q.taxaDeNascimento,
          1,
          600,
          (v) => _up((p) => p.taxaDeNascimento = v),
        ),
      _n(
        q.taxaDeNascimento > 0 ? 'Limite' : 'Quantidade',
        q.maximo.toDouble(),
        1,
        6000,
        (v) => _up((p) => p.maximo = v.round()),
        chave: 'particulas-quantidade',
      ),
      _n(
        'Vida',
        q.vidaS,
        .2,
        30,
        (v) => _up((p) => p.vidaS = v),
        casas: 1,
        unidade: 's',
      ),
      _n(
        'Velocidade',
        q.velocidade,
        0,
        3000,
        (v) => _up((p) => p.velocidade = v),
      ),
      _n(
        'Gravidade',
        q.gravidade,
        -3000,
        3000,
        (v) => _up((p) => p.gravidade = v),
      ),
      _n(
        'Turbulência',
        q.turbulencia,
        0,
        900,
        (v) => _up((p) => p.turbulencia = v),
      ),
      _desenho(q),
      _n(
        'Tamanho',
        q.tamanho,
        .5,
        400,
        (v) => _up((p) => p.tamanho = v),
        casas: 1,
      ),
      _n(
        'Opacidade',
        q.opacidade * 100,
        0,
        100,
        (v) => _up((p) => p.opacidade = v / 100),
        unidade: '%',
      ),
      _cor('Cor', q.corInicio, (c) => _up((p) => p.corInicio = c)),
    ];
  }

  // ------------------------------------------------------------ emissor

  List<Widget> _emissor(ParametrosDeParticulas q) => [
    _escolha<EmissorDeParticulas>(
      'Forma',
      q.emissor,
      EmissorDeParticulas.values,
      (e) => switch (e) {
        EmissorDeParticulas.caixa => 'Caixa',
        EmissorDeParticulas.ponto => 'Ponto',
        EmissorDeParticulas.esfera => 'Esfera',
        EmissorDeParticulas.linha => 'Linha',
        EmissorDeParticulas.anel => 'Anel',
      },
      (e) => _up((p) => p.emissor = e),
    ),
    _escolha<ModoDeEmissao>(
      'Saída',
      q.modoDeEmissao,
      ModoDeEmissao.values,
      (m) => switch (m) {
        ModoDeEmissao.cone => 'Cone',
        ModoDeEmissao.esfera => 'Todas',
        ModoDeEmissao.radial => 'Para fora',
      },
      (m) => _up((p) => p.modoDeEmissao = m),
    ),
    _n('Área X', q.largura, 0, 4000, (v) => _up((p) => p.largura = v)),
    _n('Área Y', q.altura, 0, 4000, (v) => _up((p) => p.altura = v)),
    _n(
      'Fundo (Z)',
      q.profundidade,
      0,
      4000,
      (v) => _up((p) => p.profundidade = v),
    ),
    _n('Raio', q.raio, 0, 2000, (v) => _up((p) => p.raio = v)),
    _n(
      'Nascimentos/s',
      q.taxaDeNascimento,
      0,
      600,
      (v) => _up((p) => p.taxaDeNascimento = v),
    ),
    _n(
      'Vida aleatória',
      q.vidaVariacao * 100,
      0,
      100,
      (v) => _up((p) => p.vidaVariacao = v / 100),
      unidade: '%',
    ),
    _n(
      'Semente',
      q.semente.toDouble(),
      0,
      9999,
      (v) => _up((p) => p.semente = v.round()),
    ),
  ];

  // ---------------------------------------------------------- movimento

  List<Widget> _movimento(ParametrosDeParticulas q) => [
    _n(
      'Velocidade',
      q.velocidade,
      0,
      3000,
      (v) => _up((p) => p.velocidade = v),
    ),
    _n(
      'Direção',
      q.direcaoGraus,
      -180,
      180,
      (v) => _up((p) => p.direcaoGraus = v),
      unidade: '°',
    ),
    _n(
      'Abertura',
      q.aberturaGraus,
      0,
      360,
      (v) => _up((p) => p.aberturaGraus = v),
      unidade: '°',
    ),
    _n(
      'Gravidade',
      q.gravidade,
      -3000,
      3000,
      (v) => _up((p) => p.gravidade = v),
    ),
    _n('Vento X', q.ventoX, -2000, 2000, (v) => _up((p) => p.ventoX = v)),
    _n('Vento Y', q.ventoY, -2000, 2000, (v) => _up((p) => p.ventoY = v)),
    _n('Vento Z', q.ventoZ, -2000, 2000, (v) => _up((p) => p.ventoZ = v)),
    _n(
      'Freio do ar',
      q.arrasto,
      0,
      12,
      (v) => _up((p) => p.arrasto = v),
      casas: 2,
    ),
    _n(
      'Turbulência',
      q.turbulencia,
      0,
      900,
      (v) => _up((p) => p.turbulencia = v),
    ),
    _n(
      'Detalhe',
      q.turbulenciaEscala,
      20,
      1600,
      (v) => _up((p) => p.turbulenciaEscala = v),
    ),
    _n(
      'Evolução',
      q.turbulenciaVelocidade,
      0,
      6,
      (v) => _up((p) => p.turbulenciaVelocidade = v),
      casas: 2,
    ),
    // ATRACAO E REPULSAO: zero desliga (e o padrao, para projeto antigo
    // abrir igual).
    _n('Atração', q.atracao, -8, 8, (v) => _up((p) => p.atracao = v), casas: 2),
    if (q.atracao != 0) ...[
      _n(
        'Centro X',
        q.atracaoX,
        -2000,
        2000,
        (v) => _up((p) => p.atracaoX = v),
      ),
      _n(
        'Centro Y',
        q.atracaoY,
        -2000,
        2000,
        (v) => _up((p) => p.atracaoY = v),
      ),
      _n(
        'Centro Z',
        q.atracaoZ,
        -2000,
        2000,
        (v) => _up((p) => p.atracaoZ = v),
      ),
    ],
  ];

  // ---------------------------------------------------------- aparencia

  List<Widget> _aparencia(ParametrosDeParticulas q) => [
    _desenho(q),
    _n(
      'Tamanho',
      q.tamanho,
      .5,
      400,
      (v) => _up((p) => p.tamanho = v),
      casas: 1,
    ),
    _n(
      'Tamanho aleatório',
      q.tamanhoVariacao * 100,
      0,
      100,
      (v) => _up((p) => p.tamanhoVariacao = v / 100),
      unidade: '%',
    ),
    _escolha<TamanhoNaVida>(
      'Tamanho na vida',
      q.tamanhoNaVida,
      TamanhoNaVida.values,
      (t) => switch (t) {
        TamanhoNaVida.fixo => 'Fixo',
        TamanhoNaVida.cresce => 'Cresce',
        TamanhoNaVida.encolhe => 'Encolhe',
        TamanhoNaVida.sobeEDesce => 'Sobe e desce',
      },
      (t) => _up((p) => p.tamanhoNaVida = t),
    ),
    _n(
      'Opacidade',
      q.opacidade * 100,
      0,
      100,
      (v) => _up((p) => p.opacidade = v / 100),
      unidade: '%',
    ),
    _n(
      'Opacidade aleatória',
      q.opacidadeVariacao * 100,
      0,
      100,
      (v) => _up((p) => p.opacidadeVariacao = v / 100),
      unidade: '%',
    ),
    _escolha<OpacidadeNaVida>(
      'Opacidade na vida',
      q.opacidadeNaVida,
      OpacidadeNaVida.values,
      (o) => switch (o) {
        OpacidadeNaVida.entraESai => 'Entra e sai',
        OpacidadeNaVida.some => 'Some',
        OpacidadeNaVida.aparece => 'Aparece',
        OpacidadeNaVida.fixa => 'Fixa',
      },
      (o) => _up((p) => p.opacidadeNaVida = o),
    ),
    _n(
      'Brilho',
      q.brilho * 100,
      0,
      100,
      (v) => _up((p) => p.brilho = v / 100),
      unidade: '%',
    ),
    _n(
      'Rastro',
      q.rastro * 100,
      0,
      100,
      (v) => _up((p) => p.rastro = v / 100),
      unidade: '%',
    ),
    _n(
      'Giro',
      q.giroGrausS,
      -720,
      720,
      (v) => _up((p) => p.giroGrausS = v),
      unidade: '°/s',
    ),
    _cor('Cor', q.corInicio, (c) => _up((p) => p.corInicio = c)),
    linhaDeLigar(
      rotulo: 'Cor final',
      chave: 'particulas-cor-final',
      valor: q.temCorFim,
      aoMudar: (v) => _up((p) => p.temCorFim = v),
    ),
    if (q.temCorFim)
      _cor('Cor no fim', q.corFim, (c) => _up((p) => p.corFim = c)),
    linhaDeLigar(
      rotulo: 'Cintilar',
      chave: 'particulas-cintilar',
      valor: q.cintilar,
      aoMudar: (v) => _up((p) => p.cintilar = v),
    ),
  ];

  // ------------------------------------------------------------ faiscas

  List<Widget> _faiscas(ParametrosDeParticulas q) => [
    // EM ZERO O SISTEMA INTEIRO DORME, e o resto nem aparece.
    _n(
      'Por partícula',
      q.faiscas.toDouble(),
      0,
      24,
      (v) => _up((p) => p.faiscas = v.round()),
    ),
    if (q.faiscas > 0) ...[
      _n(
        'Vida',
        q.faiscaVidaS,
        .08,
        6,
        (v) => _up((p) => p.faiscaVidaS = v),
        casas: 2,
        unidade: 's',
      ),
      _n(
        'Herda do pai',
        q.faiscaHeranca,
        0,
        1,
        (v) => _up((p) => p.faiscaHeranca = v),
        casas: 2,
      ),
      _n(
        'Força',
        q.faiscaVelocidade,
        0,
        900,
        (v) => _up((p) => p.faiscaVelocidade = v),
      ),
      _n(
        'Tamanho',
        q.faiscaTamanho,
        .05,
        3,
        (v) => _up((p) => p.faiscaTamanho = v),
        casas: 2,
      ),
      _n(
        'Começa em',
        q.faiscaInicio,
        0,
        .95,
        (v) => _up((p) => p.faiscaInicio = v),
        casas: 2,
      ),
    ] else
      const AureaAvisoDoPainel(
        texto: 'Cada partícula solta faíscas ao longo da vida.',
      ),
  ];
}
