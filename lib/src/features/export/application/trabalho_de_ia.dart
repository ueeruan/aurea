import 'dart:async';
import 'dart:ffi';
import 'dart:isolate';

import 'package:ffi/ffi.dart';

/// RODA UM TRABALHO DE IA NUM ISOLATE, com progresso, cancelamento e erro
/// com motivo. E o que a camera lenta (RIFE) e o aprimoramento usam na
/// exportacao: a inferencia bloqueia e nunca pode rodar na interface.
///
/// [pedido] monta a mensagem com a porta e o endereco de uma celula nativa
/// de parada — o isolate fica preso no FFI e so a le entre um quadro e
/// outro. [corpo] (funcao de topo) manda pela porta: `int` = quadros
/// feitos, `String` = falha (a primeira vale), `true` = concluido.
///
/// UMA porta para tudo, inclusive a saida do isolate (nulo) e o erro nao
/// tratado ([erro, pilha]): a ordem numa porta e garantida, entao a saida
/// chega por ultimo e nenhuma mensagem se perde.
Future<void> rodarTrabalhoDeIa<P>({
  required void Function(P pedido) corpo,
  required P Function(SendPort porta, int enderecoDeParar) pedido,
  required int total,
  void Function(int feitos, int total)? aoAvancar,
  bool Function()? cancelado,
  String interrompido = 'Processamento interrompido',
}) async {
  final parar = calloc<Int32>();
  final mensagens = ReceivePort();
  final saiu = Completer<void>();
  final vigia = Timer.periodic(const Duration(milliseconds: 100), (_) {
    if (cancelado?.call() ?? false) parar.value = 1;
  });
  String? falha;
  var concluido = false;
  mensagens.listen((m) {
    if (m == null) {
      if (!saiu.isCompleted) saiu.complete();
    } else if (m is int) {
      aoAvancar?.call(m, total);
    } else if (m is String) {
      falha ??= m;
    } else if (m == true) {
      concluido = true;
    } else if (m is List) {
      falha ??= m.isEmpty ? interrompido : '${m.first}';
    }
  });
  try {
    await Isolate.spawn(
      corpo,
      pedido(mensagens.sendPort, parar.address),
      onExit: mensagens.sendPort,
      onError: mensagens.sendPort,
    );
    await saiu.future;
  } finally {
    vigia.cancel();
    mensagens.close();
    // So depois da saida: o isolate lia esta celula ate o fim.
    calloc.free(parar);
  }
  if (falha != null) throw StateError(falha!);
  if (!concluido) throw StateError(interrompido);
}
