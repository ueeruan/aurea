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
      'tile_width': EffectParam(
        'Largura do mosaico',
        100,
        1,
        100,
        unit: '%',
        decimals: 1,
      ),
      'tile_height': EffectParam(
        'Altura do mosaico',
        100,
        1,
        100,
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
