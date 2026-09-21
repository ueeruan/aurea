import 'effect.dart';

/// MOTION TILE (Mosaico de movimento do After Effects), de volta ao catalogo
/// (beta 89). O passe ja existia (`motion_tile_pass.dart`,
/// `shaders/motion_tile.frag`) e ficou fora quando o catalogo foi refeito.
/// Conferido contra quatro renders do AE 26 (centro fora do meio, ladrilhos
/// nao quadrados, espelho, fase 90/120/180, deslocamento horizontal): mesma
/// grade, mesmo espelho, mesma fase; erro medio de 3 a 4 em 255, so de
/// reamostragem.
const efeitosMotionTile = <EffectType, EffectSpec>{
  EffectType.motionTile: EffectSpec(
    id: 'motion_tile',
    name: 'Motion Tile',
    category: 'Stylize',
    synonyms: [
      'motion tile',
      'mosaico',
      'mosaico de movimento',
      'repetir',
      'ladrilho',
      'tile',
      'grade',
    ],
    params: {
      'tile_center': EffectParam(
        'Centro X',
        .5,
        -1,
        2,
        kind: ParamKind.point,
        relative: true,
      ),
      'tile_center_y': EffectParam(
        'Centro Y',
        .5,
        -1,
        2,
        kind: ParamKind.point,
        relative: true,
      ),
      // O MOSAICO SOBE ATE 300%, E NAO PARA EM 100%.
      //
      // O RELATO: "a imagem comprime, diminui de tamanho". O teto de 100%
      // era a causa direta — com ele, o unico caminho que a pessoa tinha
      // nesse controle era ENCOLHER a imagem, e nao havia como voltar
      // para cima. No After Effects o mesmo controle passa de 100% e o
      // ladrilho fica MAIOR que a camada.
      //
      // 300% E O QUE O SHADER JA FAZIA: `ParametrosDoMotionTile` trava o
      // fator em `.clamp(.01, 3)`. A ficha so estava escondendo dois
      // tercos do que o efeito ja sabia desenhar.
      'tile_width': EffectParam(
        'Largura do mosaico',
        100,
        1,
        300,
        unit: '%',
        decimals: 1,
      ),
      'tile_height': EffectParam(
        'Altura do mosaico',
        100,
        1,
        300,
        unit: '%',
        decimals: 1,
      ),
      'output_width': EffectParam(
        'Largura da saída',
        100,
        1,
        600,
        unit: '%',
        decimals: 1,
      ),
      'output_height': EffectParam(
        'Altura da saída',
        100,
        1,
        600,
        unit: '%',
        decimals: 1,
      ),
      'mirror_edges': EffectParam(
        'Bordas espelhadas',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      // AS TRES FORMAS DE TRATAR A EMENDA ENTRE LADRILHOS.
      //
      // Repetir (padrao) e o que o After Effects faz. Espelhar inverte cada
      // celula vizinha, o que esconde a costura quando a imagem tem
      // gradiente. ESTICAR (clamp) nao repete nada: a ultima coluna e a
      // ultima linha de ladrilhos puxam a cor da borda da fonte ate o fim.
      //
      // ESTICAR E O QUE SALVA O CASO EM QUE O LADRILHO DENUNCIA O TRUQUE —
      // um ceu, uma parede de cor lisa, um fundo com degrade. Repetir ali
      // vira uma grade visivel; esticar vira continuacao. E, como as outras
      // duas, nunca deixa buraco: a regiao ladrilhada cobre o quadro
      // inteiro do mesmo jeito.
      //
      // Ele VENCE o espelho: com os dois ligados, estica.
      'clamp_edges': EffectParam(
        'Esticar bordas',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'phase': EffectParam(
        'Fase',
        0,
        -36000,
        36000,
        unit: '°',
        decimals: 1,
        dragStep: .5,
      ),
      'horizontal_phase_shift': EffectParam(
        'Deslocamento de fase horizontal',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['tile_width', 'tile_height', 'phase'],
    presets: [
      EffectPronto('Grade', {'tile_width': 33.3, 'tile_height': 33.3}),
      EffectPronto('Tijolos', {
        'tile_width': 50,
        'tile_height': 25,
        'phase': 180,
        'horizontal_phase_shift': 1,
      }),
      EffectPronto('Espelho', {
        'tile_width': 50,
        'tile_height': 50,
        'mirror_edges': 1,
      }),
      EffectPronto('Parede de telas', {
        'tile_width': 25,
        'tile_height': 25,
        'output_width': 200,
        'output_height': 200,
      }),
    ],
  ),
};
