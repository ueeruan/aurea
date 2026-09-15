/// OS LOOKS DE CINEMA da acao "Look": uma camada de ajuste no topo com
/// grade de cor, grao, bloom, halation e vinheta — todos efeitos
/// normais, editaveis peca a peca. A receita mora no controller
/// (`adicionarLook`).
enum LookDeCinema {
  cine,
  quente,
  frio;

  String get emPalavras => switch (this) {
    LookDeCinema.cine => 'Cine (teal & orange)',
    LookDeCinema.quente => 'Quente (golden hour)',
    LookDeCinema.frio => 'Frio (noite azul)',
  };

  String get explicacao => switch (this) {
    LookDeCinema.cine =>
      'Sombras frias, altas quentes, grão e halation discretos.',
    LookDeCinema.quente => 'Meios-tons dourados, bloom e halation altos.',
    LookDeCinema.frio => 'Sombras azuladas, halation quase zero.',
  };
}
