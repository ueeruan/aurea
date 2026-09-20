import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/model_import_service.dart';
import '../../application/motor3d_nativo.dart';
import '../../application/texture_cache.dart';
import '../../domain/analise_do_modelo.dart';
import '../../domain/model_asset3d.dart';
import '../../domain/model_import3d.dart';
import '../../domain/scene3d.dart';
import '../../domain/texturas_importadas.dart';

/// Em que ponto a importacao esta. Quem mostra progresso (a tela do
/// Sketchfab, que ainda tem o download na frente) escuta um
/// `ValueNotifier` disto; [baixando] e de quem baixa, as outras saem de
/// [concluirImportacao3D].
enum EtapaDaImportacao3D {
  baixando('Baixando…'),
  lendo('Preparando o modelo…'),
  texturas('Preparando as texturas…'),
  importando('Importando…'),
  finalizando('Finalizando…');

  const EtapaDaImportacao3D(this.rotulo);

  /// Em pt-BR; quem mostra passa por `AppText`.
  final String rotulo;
}

/// O que o dono respondeu ao aviso de modelo pesado.
enum EscolhaDoPeso { otimizar, original }

/// A PORTA UNICA DA IMPORTACAO 3D: de "tenho os arquivos em disco" ate a
/// camada de cena pronta na timeline.
///
/// Existe fora da folha de adicionar porque ha mais de uma origem para os
/// mesmos arquivos — o seletor do aparelho e o download do Sketchfab — e o
/// que vem depois tem de ser IGUAL nas duas: o aviso de modelo pesado, os
/// cinco mapas preparados antes de a camada nascer, a conferencia do motor
/// e o credito preso ao no. Duas copias deste trecho ja existiram
/// (`add_layer_sheet` e `ModelImportButton`), e so uma preparava os cinco
/// mapas.
///
/// Com [sceneId], o modelo entra como mais um objeto NAQUELA cena; sem
/// ele, nasce uma camada de cena nova no [playhead].
///
/// Devolve o id do no criado, ou `null` quando o dono cancelou no aviso, a
/// tela saiu ou o projeto mudou no meio. Falha de leitura sai como
/// [ModelImportException], com a mensagem pronta para mostrar.
Future<String?> concluirImportacao3D(
  BuildContext context,
  WidgetRef ref,
  List<String> paths, {
  required Duration playhead,
  String? sceneId,
  ModelCredit3D? credito,
  ValueNotifier<EtapaDaImportacao3D>? etapa,
}) async {
  if (paths.isEmpty) return null;
  final projectId = ref.read(editorControllerProvider).id;
  bool segue() =>
      context.mounted && ref.read(editorControllerProvider).id == projectId;

  etapa?.value = EtapaDaImportacao3D.lendo;
  ModelAsset3D model;
  try {
    model = await readModel3DFiles(
      paths,
      permitirModeloGrande: true,
      politica: PoliticaDePeso.perguntar,
    );
  } on ModeloPesadoException catch (pesado) {
    // (o `context.mounted` explicito e para o analisador: ele nao enxerga
    // a conferencia dentro de `segue`.)
    if (!context.mounted || !segue()) return null;
    final escolha = await perguntarModeloPesado(
      context,
      pesado.analise,
      licenca: credito?.license,
    );
    if (escolha == null || !segue()) return null;
    // RELER E O PRECO DE PERGUNTAR, pago so pelo modelo pesado: a malha
    // gigante nao atravessa a fronteira do isolate para esperar o dono, e a
    // reducao roda la dentro, no meio da solda.
    model = await readModel3DFiles(
      paths,
      permitirModeloGrande: true,
      politica: escolha == EscolhaDoPeso.otimizar
          ? PoliticaDePeso.otimizar
          : PoliticaDePeso.original,
    );
  }
  if (!segue()) return null;

  etapa?.value = EtapaDaImportacao3D.texturas;
  final materiais = model.data['materials'];
  for (final m in materiais is List ? materiais : const []) {
    if (m is! Map) continue;
    // OS CINCO MAPAS SAO PREPARADOS ANTES DE A CAMADA NASCER.
    //
    // O `prepare` deixa a imagem no cache do pintor e o `prepareRgba`
    // deixa os pixels prontos para a placa. Sem o segundo, o modelo
    // nasceria sem mapa nenhum e so se vestiria um quadro depois — o dono
    // veria um cinza aparecer e virar textura, que parece defeito.
    //
    // SO A TEXTURA DE COR DERRUBA A IMPORTACAO. Um relevo ilegivel e um
    // modelo sem relevo, e nao um modelo que nao entra.
    for (final chave in mapasDoMaterial) {
      final caminho = m[chave];
      if (caminho is! String || caminho.isEmpty) continue;
      final abriu = await TextureCache.instance.prepare(caminho);
      if (abriu) await TextureCache.instance.prepareRgba(caminho);
      if (!abriu && chave == 'image') {
        modelFail('Uma textura não pôde ser aberta. Use PNG, JPEG ou WebP.');
      }
      if (!segue()) return null;
    }
  }

  etapa?.value = EtapaDaImportacao3D.importando;
  if (etapa != null) {
    // A conferencia abaixo e SINCRONA e sobe a malha para a GPU: sem ceder
    // um quadro antes, o rotulo "Importando" nunca chegaria a ser pintado.
    await WidgetsBinding.instance.endOfFrame;
    if (!segue()) return null;
  }
  // A IMPORTACAO CONFERE ANTES DE DIZER QUE DEU CERTO. Um modelo pode ser
  // lido e mesmo assim nao virar desenho (o avaliador nao tira malha, ou o
  // acervo recusa a geometria) — e fechar a folha como sucesso com a camada
  // vazia e o "importei e nao apareceu nada".
  final falha = Motor3DNativo.instance.conferirModelo(model);
  if (falha != null) {
    throw ModelImportException(
      falha == '3D_GPU_BUFFER_FAILED'
          ? '$falha: o motor 3D não aceitou a geometria deste modelo '
                '(memória ou malha degenerada).'
          : '$falha: o modelo não tem geometria que o motor 3D desenhe.',
    );
  }

  etapa?.value = EtapaDaImportacao3D.finalizando;
  final controller = ref.read(editorControllerProvider.notifier);
  final nodeId = sceneId == null
      ? controller.addImportedModel3D(playhead, model, credito: credito)
      : controller.addModel3D(sceneId, model, credito: credito);
  if (nodeId.isEmpty) {
    throw const ModelImportException(
      '3D_SCENE_ATTACH_FAILED: não foi '
      'possível criar a cena 3D.',
    );
  }
  return nodeId;
}

/// Mostra o aviso de modelo pesado. `null` = cancelou.
Future<EscolhaDoPeso?> perguntarModeloPesado(
  BuildContext context,
  AnaliseDoModelo analise, {
  String? licenca,
}) => showCupertinoDialog<EscolhaDoPeso>(
  context: context,
  builder: (_) => AvisoDeModeloPesado(analise: analise, licenca: licenca),
);

/// O AVISO "MODELO PESADO".
///
/// Substitui o aviso fixo que aparecia em TODA importacao (ate de um cubo)
/// dizendo que o app "nao vai reduzir o arquivo". Agora ele so aparece
/// quando a ficha do modelo passa de [limitesDeModeloPesado], diz o que
/// pesa, e oferece a reducao com o antes -> depois estimado.
class AvisoDeModeloPesado extends StatelessWidget {
  const AvisoDeModeloPesado({super.key, required this.analise, this.licenca});

  final AnaliseDoModelo analise;

  /// A licenca do credito, quando o modelo veio com uma. So serve para o
  /// lembrete de "sem obras derivadas".
  final String? licenca;

  /// Licenca que proibe derivados: reduzir a malha e, em tese, derivar. O
  /// app informa e deixa a decisao com o dono.
  bool get _semDerivados {
    final l = (licenca ?? '').toLowerCase();
    return l.contains('noderiv') ||
        l.contains('no deriv') ||
        RegExp(r'(^|[-\s])nd($|[-\s])').hasMatch(l);
  }

  @override
  Widget build(BuildContext context) {
    final depois = analise.estimativaOtimizada();
    final malhaMuda = depois.triangulos < analise.triangulos;
    final texturaMuda = depois.maiorTextura < analise.maiorTextura;
    final memoriaMuda = depois.memoriaBytes < analise.memoriaBytes;
    const linha = TextStyle(fontSize: 13, height: 1.35);
    return CupertinoAlertDialog(
      title: const AppText('Modelo pesado'),
      content: Column(
        key: const ValueKey('modelo-pesado-ficha'),
        mainAxisSize: MainAxisSize.min,
        children: [
          const SizedBox(height: 4),
          const AppText(
            'Este modelo pode travar ou fechar o app neste aparelho. A '
            'otimização reduz a malha e as texturas; materiais, ossos e '
            'animações ficam como estão.',
            style: linha,
          ),
          const SizedBox(height: 10),
          if (malhaMuda)
            AppTextMoldado('Triângulos: {0} → {1}', [
              contagemLegivel(analise.triangulos),
              contagemLegivel(depois.triangulos),
            ], style: linha)
          else
            AppTextMoldado('Triângulos: {0}', [
              contagemLegivel(analise.triangulos),
            ], style: linha),
          if (analise.texturas > 0 && texturaMuda)
            AppTextMoldado('Texturas ({0}): {1} px → {2} px', [
              analise.texturas,
              analise.maiorTextura,
              depois.maiorTextura,
            ], style: linha)
          else if (analise.texturas > 0)
            AppTextMoldado('Texturas ({0}): até {1} px', [
              analise.texturas,
              analise.maiorTextura,
            ], style: linha),
          if (memoriaMuda)
            AppTextMoldado('Memória estimada: {0} → {1}', [
              memoriaLegivel(analise.memoriaBytes),
              memoriaLegivel(depois.memoriaBytes),
            ], style: linha)
          else
            AppTextMoldado('Memória estimada: {0}', [
              memoriaLegivel(analise.memoriaBytes),
            ], style: linha),
          AppTextMoldado('Materiais: {0} · Animações: {1} · Ossos: {2}', [
            analise.materiais,
            analise.animacoes,
            analise.ossos,
          ], style: linha),
          if (_semDerivados) ...[
            const SizedBox(height: 8),
            const AppText(
              'A licença deste modelo não permite obras derivadas: otimizar '
              'pode contar como derivado.',
              style: linha,
            ),
          ],
        ],
      ),
      actions: [
        CupertinoDialogAction(
          key: const ValueKey('modelo-pesado-otimizar'),
          isDefaultAction: true,
          onPressed: () => Navigator.of(context).pop(EscolhaDoPeso.otimizar),
          child: const AppText('Otimizar automaticamente'),
        ),
        CupertinoDialogAction(
          key: const ValueKey('modelo-pesado-original'),
          onPressed: () => Navigator.of(context).pop(EscolhaDoPeso.original),
          child: const AppText('Importar original'),
        ),
        CupertinoDialogAction(
          key: const ValueKey('modelo-pesado-cancelar'),
          onPressed: () => Navigator.of(context).pop(),
          child: const AppText('Cancelar'),
        ),
      ],
    );
  }
}
