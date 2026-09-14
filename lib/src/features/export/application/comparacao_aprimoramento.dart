// ANTES E DEPOIS DO APRIMORAMENTO NUM QUADRO DO EDITOR.
//
// A comparacao passa pelo MESMO caminho da exportacao: o "antes" e o quadro
// que o arquivo teria sem a IA (a fonte encaixada na composicao pelo
// FFmpeg) e o "depois" e o quadro lido na resolucao da fonte e aprimorado
// pelo mesmo isolate de trabalho. O que aparece aqui e o que sai no video.
import 'dart:io';

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/domain/aprimoramento_ia.dart';
import '../domain/video_color.dart';
import 'aprimoramento_export.dart';
import 'export_engine.dart' show lerInfoDoVideo;

class ComparacaoDoAprimoramento {
  const ComparacaoDoAprimoramento({
    required this.plano,
    this.antes,
    this.depois,
  });

  final PlanoDeAprimoramento plano;

  /// PNG do quadro sem IA (nulo quando o plano nao aplica).
  final String? antes;

  /// PNG do quadro com IA.
  final String? depois;
}

/// Gera a comparacao do instante [tempoDaFonte] do arquivo [fonte], para
/// uma composicao [largura] x [altura] e [forca]. Lanca StateError com o
/// motivo se a extracao ou a IA falharem.
Future<ComparacaoDoAprimoramento> compararAprimoramento({
  required String fonte,
  required Duration tempoDaFonte,
  required int largura,
  required int altura,
  required double forca,
  PerfilDoAprimoramento perfil = PerfilDoAprimoramento.videoReal,
  double reducaoDeRuido = reducaoDeRuidoPadrao,
  AprimoradorDeQuadros? aprimorador,
}) async {
  final ia = aprimorador ?? AprimoradorIa.doAparelho();
  final info = await lerInfoDoVideo(fonte);
  final plano = planoDeAprimoramento(
    ligado: true,
    motorDisponivel: ia != null && ia.disponivel,
    larguraDaFonte: info.cor.largura,
    alturaDaFonte: info.cor.altura,
    rotacao: info.rotacao,
    larguraDaComposicao: largura,
    alturaDaComposicao: altura,
  );
  if (!plano.aplica) return ComparacaoDoAprimoramento(plano: plano);

  final pasta = await (await getTemporaryDirectory()).createTemp(
    'aurea-comparar-',
  );
  final antes = '${pasta.path}/antes.png';
  final depois = '${pasta.path}/depois.png';
  final segundos = (tempoDaFonte.inMicroseconds / 1000000.0).toStringAsFixed(6);

  Future<void> umQuadro(List<String> receitas, String saida) async {
    for (final vf in receitas) {
      final sessao = await FFmpegKit.executeWithArguments([
        '-y',
        '-ss',
        segundos,
        '-i',
        fonte,
        '-vf',
        vf,
        '-frames:v',
        '1',
        '-compression_level',
        '1',
        '-pred',
        'none',
        saida,
      ]);
      if (ReturnCode.isSuccess(await sessao.getReturnCode()) &&
          File(saida).existsSync()) {
        return;
      }
    }
    throw StateError('não consegui ler este quadro do vídeo');
  }

  await umQuadro(
    receitasDeExtracao(info.cor, fps: 30, largura: largura, altura: altura),
    antes,
  );
  await umQuadro(
    receitasDeExtracao(
      info.cor,
      fps: 30,
      largura: largura,
      altura: altura,
      areaMaximaParaIa: areaMaximaDaEntradaDaIa,
    ),
    depois,
  );
  await ia!.aprimorar(
    arquivos: [depois],
    forca: forca,
    larguraDaComposicao: largura,
    alturaDaComposicao: altura,
    perfil: perfil,
    reducaoDeRuido: reducaoDeRuido,
  );
  return ComparacaoDoAprimoramento(plano: plano, antes: antes, depois: depois);
}
