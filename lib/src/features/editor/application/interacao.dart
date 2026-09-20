import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';

/// O DEDO ESTA MEXENDO EM ALGUMA COISA AGORA?
///
/// ============================ POR QUE EXISTE ==========================
///
/// O palco ja sabia baixar a qualidade ENQUANTO TOCA (`tocandoAgora`), mas
/// arrastar um slider, mover uma camada ou esfregar a timeline com o relogio
/// parado rodava em qualidade cheia — interagir saia mais caro que tocar, e
/// e durante a edicao que o celular esquenta.
///
/// Este e o sinal unico de "ha um gesto em curso". Quem produz mutacao
/// continua chama [marcar]; quem desenha escuta [agora] e pode trocar
/// qualidade por resposta. Ao soltar, o sinal cai sozinho depois de
/// [_folga] e sai UM quadro final em qualidade cheia.
///
/// A EXPORTACAO NUNCA LE ISTO: ela tem o proprio caminho, sempre completo.
class Interacao {
  Interacao._();

  static const Duration _folga = Duration(milliseconds: 260);

  /// Verdadeiro do primeiro [marcar] ate [_folga] depois do ultimo.
  static final ValueNotifier<bool> agora = ValueNotifier<bool>(false);

  /// NOS TESTES O SINAL NASCE DESLIGADO. Toda mutacao do projeto e todo
  /// seek com o relogio parado chamam [marcar], e o Timer da folga que fica
  /// pendente derruba qualquer `testWidgets` que termine antes de 260 ms
  /// ("A Timer is still pending"). Os testes que prendem o rascunho da
  /// interacao ligam isto de proposito e chamam [zerar] no fim.
  static bool ligada = !Platform.environment.containsKey('FLUTTER_TEST');

  static Timer? _fim;

  /// Chamar a cada passo do gesto. Barato: um Timer rearmado.
  static void marcar() {
    if (!ligada) return;
    _fim?.cancel();
    _fim = Timer(_folga, _soltar);
    if (!agora.value) agora.value = true;
  }

  /// Fim explicito do gesto (soltar o dedo): cai sem esperar a folga.
  static void soltar() {
    _fim?.cancel();
    _fim = null;
    _soltar();
  }

  static void _soltar() {
    _fim = null;
    if (agora.value) agora.value = false;
  }

  /// So para teste: devolve o sinal ao repouso sem deixar Timer pendente.
  @visibleForTesting
  static void zerar() => soltar();
}
