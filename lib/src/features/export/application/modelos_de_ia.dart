import 'dart:io';
import 'dart:typed_data';

/// COPIA UM MODELO DE IA dos assets para a pasta de apoio, uma vez: o C++
/// le arquivo, nao asset. Arquivo com tamanho diferente do asset e refeito
/// (atualizacao do app ou copia interrompida), sempre por um temporario
/// renomeado no fim. Devolve a pasta.
Future<String> prepararModeloDeIa({
  required String subpasta,
  required Map<String, String> arquivos,
  required Future<ByteData> Function(String asset) carregarAsset,
  required Future<Directory> Function() pastaDeApoio,
}) async {
  final dir = Directory('${(await pastaDeApoio()).path}/ai/$subpasta');
  await dir.create(recursive: true);
  for (final e in arquivos.entries) {
    final destino = File('${dir.path}/${e.key}');
    final dados = await carregarAsset(e.value);
    if (await destino.exists() &&
        await destino.length() == dados.lengthInBytes) {
      continue;
    }
    final temporario = File('${destino.path}.tmp');
    await temporario.writeAsBytes(
      dados.buffer.asUint8List(dados.offsetInBytes, dados.lengthInBytes),
      flush: true,
    );
    await temporario.rename(destino.path);
  }
  return dir.path;
}
