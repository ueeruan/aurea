/// O IMPACTO DO AMV em tres forcas. A receita mora no controller
/// (`aplicarImpactoAmv`): pulso de escala + tremor com envelope + glow +
/// flash + RGB split, tudo como efeitos e keyframes NORMAIS na pilha.
enum ImpactoAmv {
  suave,
  medio,
  forte;

  String get emPalavras => switch (this) {
    ImpactoAmv.suave => 'Impacto suave',
    ImpactoAmv.medio => 'Impacto médio',
    ImpactoAmv.forte => 'Impacto forte',
  };

  String get explicacao => switch (this) {
    ImpactoAmv.suave => 'Pulso e glow discretos, sem RGB split.',
    ImpactoAmv.medio => 'Pulso, tremor, flash e um RGB split curto.',
    ImpactoAmv.forte => 'A batida cheia: tudo acima, com mais força.',
  };
}
