import 'dart:math' as math;

import 'effect.dart';

/// APARECER E SUMIR: a opacidade da camada no instante [local], dada a
/// [duracao] dela. Entrar e sair sem precisar de keyframe nenhum — o
/// pedido mais comum de quem esta comecando, e o que mais enchia a
/// trilha de marcas nos projetos dos testadores.
///
/// A conta e do TEMPO DA CAMADA: o efeito nao tem o que perguntar. Uma
/// camada mais curta que a soma das duas pontas divide o que tem entre
/// elas, em vez de sumir no meio.
double opacidadeDoAparecerSumir(
  EffectInstance effect,
  Duration local,
  Duration duracao,
) {
  final total = duracao.inMicroseconds / 1e6;
  if (total <= 0) return 1;
  final t = (local.inMicroseconds / 1e6).clamp(0.0, total);
  var entrada = effect.paramAt('entrada', local).clamp(0.0, 10.0);
  var saida = effect.paramAt('saida', local).clamp(0.0, 10.0);
  // Nao da para aparecer e sumir ao mesmo tempo: as duas pontas
  // encolhem juntas ate caberem.
  final soma = entrada + saida;
  if (soma > total && soma > 0) {
    final f = total / soma;
    entrada *= f;
    saida *= f;
  }
  var v = 1.0;
  if (entrada > 0 && t < entrada) v = math.min(v, t / entrada);
  final restante = total - t;
  if (saida > 0 && restante < saida) v = math.min(v, restante / saida);
  v = v.clamp(0.0, 1.0);
  // Curva suave: a mesma aceleracao das transicoes do app, que e o que
  // faz o video nao "pular" no comeco do fade.
  if (effect.paramAt('curva', local).round() == 1) {
    v = v * v * (3 - 2 * v);
  }
  return v;
}
