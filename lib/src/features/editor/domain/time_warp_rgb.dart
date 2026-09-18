import 'effect.dart';
import 'layer.dart';
import 'time_slice.dart';

/// TIME WARP RGB — CADA CANAL NUM INSTANTE DIFERENTE.
///
/// ==========================================================================
/// OS PARAMETROS FORAM LIDOS DO PLUGIN; O EFEITO NAO PODE SER MEDIDO
/// ==========================================================================
///
/// O `S_TimeWarpRGB` EXISTE no AE do dono, e os parametros dele foram
/// lidos de la (nao ha o que adivinhar):
///
///   * Red Shift Frames   =  1
///   * Green Shift Frames =  0
///   * Blue Shift Frames  = -1
///   * Clamp Chroma       =  1
///
/// O QUE NAO DEU PARA MEDIR, e fica escrito: com o efeito LIGADO o
/// render do AE sai **byte a byte igual** ao render com ele desligado. A
/// bancada tinha um quadrado vermelho andando 14 px por quadro e um
/// deslocamento de +2/-2 quadros — o que daria 28 px de separacao entre
/// os canais, impossivel de nao ver. Os quatro PNGs sairam com o mesmo
/// md5. O Sapphire nao renderiza por `saveFrameToPng` nesta maquina (ele
/// quer o proprio caminho de GPU), entao a unica coisa que se pode dizer
/// do efeito original e o que os parametros dizem.
///
/// POR ISSO A CONVENCAO DO SINAL E NOSSA, e esta escrita: deslocamento
/// POSITIVO mostra um quadro MAIS TARDE. E o que os valores de fabrica
/// sugerem (vermelho +1, azul -1, o classico "vermelho adiantado"), mas
/// nao foi conferido contra o plugin.
///
/// O `Clamp Chroma` tambem ficou de fora: sem medir o que ele faz, um
/// interruptor com o nome dele seria uma promessa vazia. A soma dos tres
/// canais ja corta em 0..1 por construcao.
///
/// ==========================================================================
/// COMO ELE E FEITO AQUI
/// ==========================================================================
///
/// Tres montagens da MESMA camada, em instantes diferentes, cada uma
/// reduzida a um canal por `ColorFilter.matrix`, somadas com
/// `BlendMode.plus`. O custo e o de tres quadros — a mesma familia de
/// custo do Time Slice, que ja respeita o teto por quadro.
///
/// O ALFA SOMA: com a camada opaca (o caso deste efeito) a conta e
/// exata; com alfa parcial os tres alfas se somam e saturam em 1, entao
/// uma camada meio transparente sai mais opaca do que entrou. Consertar
/// isso pediria uma quarta montagem so para devolver o alfa, e nao valeu
/// o quadro extra — esta escrito aqui para quem tropecar nisso.
const efeitosTimeWarpRgb = <EffectType, EffectSpec>{
  EffectType.timeWarpRgb: EffectSpec(
    id: 's_timewarp_rgb',
    name: 'RGB no tempo',
    category: 'Time',
    synonyms: [
      'rgb no tempo',
      'time warp rgb',
      'timewarp',
      'time warp',
      'separacao rgb',
      'aberração temporal',
      'canal no tempo',
    ],
    params: {
      'mix': EffectParam('Mistura', 100, 0, 100, unit: '%', decimals: 1),
      // OS NOMES E OS VALORES DE FABRICA SAO OS DO PLUGIN.
      'desloc_r': EffectParam(
        'Deslocamento do vermelho',
        1,
        -24,
        24,
        unit: 'q',
        decimals: 0,
        dragStep: .2,
      ),
      'desloc_g': EffectParam(
        'Deslocamento do verde',
        0,
        -24,
        24,
        unit: 'q',
        decimals: 0,
        dragStep: .2,
      ),
      'desloc_b': EffectParam(
        'Deslocamento do azul',
        -1,
        -24,
        24,
        unit: 'q',
        decimals: 0,
        dragStep: .2,
      ),
    },
    montar: ['desloc_r', 'desloc_g', 'desloc_b'],
    presets: [
      // O de fabrica do plugin.
      EffectPronto('Padrão', {'desloc_r': 1, 'desloc_g': 0, 'desloc_b': -1}),
      EffectPronto('Aberto', {'desloc_r': 4, 'desloc_g': 0, 'desloc_b': -4}),
      EffectPronto('Largo', {'desloc_r': 10, 'desloc_g': 0, 'desloc_b': -10}),
      EffectPronto('Só o vermelho', {'desloc_r': 6, 'desloc_g': 0, 'desloc_b': 0}),
      EffectPronto('Só o azul', {'desloc_r': 0, 'desloc_g': 0, 'desloc_b': -6}),
      EffectPronto('Verde na frente', {
        'desloc_r': -3,
        'desloc_g': 4,
        'desloc_b': -6,
      }),
      EffectPronto('Fantasma', {
        'desloc_r': 3,
        'desloc_g': 1,
        'desloc_b': -3,
        'mix': 70,
      }),
    ],
  ),
};

/// OS DESLOCAMENTOS EM QUADROS, na ordem R, G, B.
///
/// Positivo mostra um quadro MAIS TARDE — a convencao e nossa, e o porque
/// esta na ficha.
({int r, int g, int b}) deslocamentosDoTimeWarp(
  EffectInstance efeito,
  Duration local,
) => (
  r: efeito.paramAt('desloc_r', local).round().clamp(-240, 240),
  g: efeito.paramAt('desloc_g', local).round().clamp(-240, 240),
  b: efeito.paramAt('desloc_b', local).round().clamp(-240, 240),
);

/// SEM DESLOCAMENTO NAO HA EFEITO: os tres canais seriam o mesmo quadro.
///
/// (Com os TRES deslocamentos IGUAIS e nao nulos o resultado tambem e o
/// quadro original, mas so para camada opaca e pagando tres montagens —
/// nao vale a guarda, porque a pessoa ve o mesmo na tela nos dois casos.)
bool timeWarpEhIdentidade(({int r, int g, int b}) d) =>
    d.r == 0 && d.g == 0 && d.b == 0;

/// OS INSTANTES DA COMPOSICAO QUE A CAMADA PRECISA MOSTRAR, um por canal.
///
/// E o que a exportacao decodifica antes de desenhar: sem isso, os tres
/// canais mostrariam o mesmo quadro do video — foi assim que o Time Slice
/// precisou do mesmo tratamento. A conta do deslocamento e a MESMA dele
/// ([localDeslocado]), para nao existirem duas regras de "prender nas
/// pontas".
Set<Duration> instantesDoTimeWarp({
  required Layer layer,
  required Duration local,
  required ({int r, int g, int b}) deslocamentos,
  required int fps,
}) {
  final out = <Duration>{};
  for (final q in [
    deslocamentos.r,
    deslocamentos.g,
    deslocamentos.b,
  ]) {
    final l = q == 0 ? local : localDeslocado(layer, local, q, fps);
    out.add(layer.startTime + l);
  }
  return out;
}

/// A MATRIZ QUE DEIXA PASSAR SO O CANAL [canal] (0 = R, 1 = G, 2 = B).
///
/// O ALFA ATRAVESSA: isolar canal e conta de COR, quem manda no alfa e a
/// camada. A matriz e a do `ColorFilter.matrix` do motor, que trabalha em
/// RGB NAO PREMULTIPLICADO — por isso o zero de um canal nao arrasta o
/// alfa junto.
List<double> matrizDoCanal(int canal) => switch (canal) {
  0 => const [
    1, 0, 0, 0, 0, //
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 1, 0,
  ],
  1 => const [
    0, 0, 0, 0, 0, //
    0, 1, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 1, 0,
  ],
  _ => const [
    0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 0, 1, 0,
  ],
};
