/// A PALETA DO EDITOR — ESTE ARQUIVO NAO DECLARA MAIS NADA.
///
/// Ele declarava uma SEGUNDA classe `AmColors`, com valores proprios para
/// `bg`, `topBar`, `panel`, `panelHigh` e `chip`. Quem importava por este
/// caminho pintava o fundo em #12151A; quem importava por
/// `core/ui/am_colors.dart` pintava em #08080C. O mesmo nome, dois tons.
///
/// O dono da paleta e `lib/src/core/ui/am_colors.dart`, e este arquivo virou
/// uma porta para ele — os quarenta e poucos `import 'am_colors.dart'` da
/// pasta `am/` continuam funcionando sem mudar uma linha.
library;

export '../../../../core/ui/am_colors.dart';
