/// PRESETS DE MOVIMENTO — nomes do que a folha Animar oferece.
///
/// Cada um vira KEYFRAMES REAIS nas trilhas de sempre (opacidade,
/// escala, posicao): nada procedural escondido, tudo editavel depois,
/// curva a curva. As receitas moram no controller
/// (`aplicarPresetDeMovimento` e `morphRapidoDeForma`).
library;

/// Entradas e saidas de camada.
enum PresetDeMovimento {
  aparecer,
  sumir,
  subir,
  pop,
  soco,
  zoomSuave;

  String get emPalavras => switch (this) {
    PresetDeMovimento.aparecer => 'Aparecer',
    PresetDeMovimento.sumir => 'Sumir',
    PresetDeMovimento.subir => 'Surgir de baixo',
    PresetDeMovimento.pop => 'Pop',
    PresetDeMovimento.soco => 'Soco',
    PresetDeMovimento.zoomSuave => 'Zoom suave',
  };

  String get explicacao => switch (this) {
    PresetDeMovimento.aparecer => 'Fade de entrada no timing da Apple.',
    PresetDeMovimento.sumir => 'Fade de saída, começando aqui.',
    PresetDeMovimento.subir => 'Sobe 48 px enquanto aparece.',
    PresetDeMovimento.pop => 'Entra pequeno e assenta com overshoot.',
    PresetDeMovimento.soco => 'Incha 12% e volta — o punch de batida.',
    PresetDeMovimento.zoomSuave =>
      'Aproxima 8% devagar, do cabeçote ao fim da camada.',
  };
}

/// Morphs rapidos da forma parametrica (retangulo animando tamanho e
/// arredondamento — o canto nao deforma).
enum FormaRapida {
  circulo,
  pilula,
  card;

  String get emPalavras => switch (this) {
    FormaRapida.circulo => 'Virar círculo',
    FormaRapida.pilula => 'Virar pílula',
    FormaRapida.card => 'Virar card',
  };
}
