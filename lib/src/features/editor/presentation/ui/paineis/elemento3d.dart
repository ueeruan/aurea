import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../../media/application/media_import_service.dart';
import '../../../application/editor_controller.dart';
import '../../../application/mesh_cache.dart';
import '../../../domain/acabamento3d.dart';
import '../../../domain/element3d.dart';
import '../../../domain/layer.dart';
import '../../../domain/mesh_import.dart';
import 'comum_3d.dart';
import 'pecas_centrais.dart';

/// A FOLHA DO ELEMENTO 3D: forma, material e acabamento de uma camada
/// [Element3DLayer].
///
/// A rotacao NAO mora aqui: vem do transform normal da camada (X/Y/Z,
/// keyframes e curvas de sempre) — e da cadeia de nulos quando ela esta
/// vinculada a um pai. Duplicar a rotacao aqui criaria dois lugares para
/// o mesmo numero.
///
/// Folha NAO modal: o palco continua tocavel, e o que muda aparece nele
/// (o palco e a unica previa do app).
Future<void> showElement3DSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  if (!context.mounted) return;
  await mostrarAureaFolha<void>(
    context,
    modal: false,
    // A folha poe 10 de respiro em cima: o total fica nos 326 do painel
    // grande, a mesma altura do editor de curva.
    altura: AureaDims.painelGrande - AureaDims.e10,
    construtor: (_) => _FolhaDoElemento3D(layerId: layerId),
  );
}

/// Degrades prontos do material brilhante (roxo/azul/rosa das
/// referencias, por do sol, oceano, ouro, prata, neon).
///
/// SAO CONTEUDO, NAO TEMA: e a tinta que vai para o solido. Por isso ficam
/// em hexadecimal fixo — trocar a paleta do app nao pode repintar o
/// projeto de ninguem.
final List<List<Color>> _degradesDoBrilhante = [
  [Color(0xFF7A3FF2), Color(0xFF2F7BFF), Color(0xFFFF4FD8)],
  [Color(0xFFFF7A18), Color(0xFFFF2D95), Color(0xFF7A3FF2)],
  [Color(0xFF2F7BFF), Color(0xFF6FAED9), Color(0xFFA9D3EC)],
  [Color(0xFF7A4A00), Color(0xFFFFD36A), Color(0xFFFFF4C2)],
  [Color(0xFF3A3F4A), Color(0xFFC9D1DC), Color(0xFFFFFFFF)],
  [Color(0xFF6FAED9), Color(0xFF35C4E7), Color(0xFFFF4FD8)],
];

/// As quatro cores de um toque (azul claro e azul da marca, vermelho,
/// branco). Conteudo tambem, pelo mesmo motivo dos degrades.
final List<Color> _coresProntas = [
  Color(0xFFA9D3EC),
  Color(0xFF6FAED9),
  Color(0xFFFF3B52),
  Color(0xFFFFFFFF),
];

bool _mesmasCores(List<Color> a, List<Color> b) {
  if (a.length != b.length) return false;
  for (var i = 0; i < a.length; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

/// O NOME DO MODELO E O ESTADO DA LEITURA, ja na lingua da tela. O nome
/// do arquivo e conteudo; o resto e rotulo — por isso o molde.
String _descricaoDoModelo(BuildContext context, Element3DLayer layer) {
  final caminho = layer.meshPath;
  if (caminho == null) return translate(context, 'Sólido nativo');
  final nome = caminho.split(RegExp(r'[\\/]')).last;
  final r = MeshCache.instance.resultFor(caminho);
  if (r != null) return moldar(context, '{0} · {1} faces', [nome, r.faceCount]);
  final erro = MeshCache.instance.errorFor(caminho);
  if (erro != null) return moldar(context, '{0} · não carregou', [nome]);
  return moldar(context, '{0} · carregando…', [nome]);
}

/// Escolhe um OBJ/FBX, le fora da UI, avisa se for pesado e aplica.
///
/// NAO E O FLUXO DE `concluirImportacao3D`: aquele cria um objeto numa
/// camada de CENA 3D (modelo com materiais, texturas e o motor nativo).
/// Aqui o modelo so troca a MALHA do elemento — o solido continua com a
/// cor, o acabamento e o ambiente da camada, lidos pelo [MeshCache].
Future<void> _escolherModelo3D(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  final r = await FilePicker.platform.pickFiles(type: FileType.any);
  final caminho = r?.files.single.path;
  if (caminho == null) return;
  final ext = caminho.split('.').last.toLowerCase();
  if (!context.mounted) return;
  if (ext != 'obj' && ext != 'fbx') {
    await _avisoModelo(
      context,
      'Formato não suportado',
      'Escolha um arquivo .obj ou .fbx (ASCII).',
    );
    return;
  }
  MeshImportResult resultado;
  try {
    resultado = await MeshCache.instance.load(caminho);
  } on MeshImportException catch (e) {
    if (!context.mounted) return;
    // A mensagem vem do leitor (conteudo tecnico): vai crua, sem catalogo.
    await _avisoModelo(
      context,
      'Não deu para importar',
      e.message,
      traduzirTexto: false,
    );
    return;
  }
  if (!context.mounted) return;
  if (resultado.heavy) {
    final segue = await showCupertinoDialog<bool>(
      context: context,
      builder: (c) => CupertinoAlertDialog(
        title: const AppText('Modelo pesado'),
        content: Padding(
          padding: const EdgeInsets.only(top: AureaDims.e8),
          child: resultado.truncated
              ? AppTextMoldado(
                  '{0} faces (o app usa as primeiras {1}). Modelos assim '
                  'podem travar em celulares fracos; em aparelhos potentes '
                  'rodam bem. Importar mesmo assim?',
                  [resultado.faceCount, kMeshFacesMax],
                )
              : AppTextMoldado(
                  '{0} faces. Modelos assim podem travar em celulares '
                  'fracos; em aparelhos potentes rodam bem. Importar mesmo '
                  'assim?',
                  [resultado.faceCount],
                ),
        ),
        actions: [
          CupertinoDialogAction(
            key: const ValueKey('elemento3d-modelo-cancelar'),
            onPressed: () => Navigator.of(c).pop(false),
            child: const AppText('Cancelar'),
          ),
          CupertinoDialogAction(
            key: const ValueKey('elemento3d-modelo-importar'),
            isDefaultAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const AppText('Importar'),
          ),
        ],
      ),
    );
    if (segue != true) return;
  }
  // UMA MUTACAO SO: o modelo entra num passo de desfazer.
  controller.updateElement3D(
    layerId,
    (e) => e.copyElement3D(meshPath: caminho),
  );
}

Future<void> _avisoModelo(
  BuildContext context,
  String titulo,
  String texto, {
  bool traduzirTexto = true,
}) async {
  await showCupertinoDialog<void>(
    context: context,
    builder: (c) => CupertinoAlertDialog(
      title: AppText(titulo),
      content: Padding(
        padding: const EdgeInsets.only(top: AureaDims.e8),
        child: traduzirTexto ? AppText(texto) : Text(texto),
      ),
      actions: [
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.of(c).pop(),
          child: const AppText('OK'),
        ),
      ],
    ),
  );
}

/// O CORPO DA FOLHA, em duas abas:
///
///   Forma     tipo do solido, tamanho, arestas, modelo importado e a
///             imagem que veste as faces
///   Material  cor, reflexo e ambiente, acabamento (os metais do Texto
///             3D), degrade do brilhante antigo e brilho
///
/// Observa SO a camada (nao o projeto inteiro): a folha fica aberta
/// enquanto o dedo arrasta, e o desfazer tambem tem de redesenhá-la.
class _FolhaDoElemento3D extends ConsumerStatefulWidget {
  const _FolhaDoElemento3D({required this.layerId});

  final String layerId;

  @override
  ConsumerState<_FolhaDoElemento3D> createState() => _FolhaDoElemento3DState();
}

class _FolhaDoElemento3DState extends ConsumerState<_FolhaDoElemento3D> {
  static const _titulo = 'Elemento 3D';
  static const _chave = 'folha-elemento3d';
  static const _abas = ['Forma', 'Material'];

  int _aba = 0;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  void _mudar(Element3DLayer Function(Element3DLayer) f) =>
      _c.updateElement3D(widget.layerId, f);

  void _fechar() => Navigator.of(context).maybePop();

  @override
  Widget build(BuildContext context) {
    final camada = ref.watch(
      editorControllerProvider.select((p) => p.layerById(widget.layerId)),
    );
    if (camada is! Element3DLayer) {
      return AureaPanel(
        titulo: _titulo,
        chave: _chave,
        aoFechar: _fechar,
        filhos: [
          AureaAvisoDoPainel(
            texto: camada == null
                ? 'Esta camada não existe mais.'
                : 'Esta camada não é um elemento 3D.',
          ),
        ],
      );
    }
    return AureaPanel(
      titulo: _titulo,
      chave: _chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: _fechar,
      corpo: ListView(
        key: ValueKey('elemento3d-aba-$_aba'),
        padding: paddingDoPainel,
        children: _aba == 0 ? _forma(camada) : _material(camada),
      ),
    );
  }

  // ------------------------------------------------------------------ Forma

  List<Widget> _forma(Element3DLayer e) => [
    LinhaDeFichas<Element3DKind>(
      rotulo: 'Tipo',
      chave: 'elemento3d-tipo',
      valores: Element3DKind.values,
      rotuloDe: element3DLabel,
      escolhido: e.kind,
      chaveDe: (k) => 'elemento3d-tipo-${k.name}',
      aoEscolher: (k) => _mudar((x) => x.copyElement3D(kind: k)),
    ),
    linhaSemLosango(
      _c,
      rotulo: 'Tamanho',
      chave: 'elemento3d-tamanho',
      valor: e.size,
      min: 20,
      max: 600,
      aoMudar: (v) => _mudar((x) => x.copyElement3D(size: v)),
    ),
    linhaDeInterruptor(
      rotulo: 'Arestas',
      chave: 'elemento3d-arestas',
      valor: e.edges,
      aoMudar: (v) => _mudar((x) => x.copyElement3D(edges: v)),
    ),
    // MODELO IMPORTADO: OBJ ou FBX (ASCII) no lugar do solido. A leitura
    // e assincrona: a linha escuta o cache para trocar "carregando" pelo
    // numero de faces sem esperar outro toque.
    ValueListenableBuilder<int>(
      valueListenable: MeshCache.instance.revision,
      builder: (context, _, _) => _linhaDeArquivo(
        rotulo: 'Modelo',
        chave: 'elemento3d-modelo',
        descricao: _descricaoDoModelo(context, e),
        icone: CupertinoIcons.cube_box,
        acao: 'OBJ / FBX',
        aoEscolher: () => _escolherModelo3D(context, ref, widget.layerId),
        aoLimpar: e.meshPath == null
            ? null
            : () => _mudar((x) => x.copyElement3D(clearMesh: true)),
      ),
    ),
    // IMAGEM NO SOLIDO: uma foto ou logo vestindo as faces.
    _linhaDeArquivo(
      rotulo: 'Imagem',
      chave: 'elemento3d-imagem',
      descricao: e.imagePath == null
          ? translate(context, 'Nenhuma')
          : e.imagePath!.split(RegExp(r'[\\/]')).last,
      icone: CupertinoIcons.photo,
      acao: 'Escolher',
      aoEscolher: _escolherImagem,
      aoLimpar: e.imagePath == null
          ? null
          : () => _mudar((x) => x.copyElement3D(clearImage: true)),
    ),
    const AureaAvisoDoPainel(
      texto:
          'Gire com a rotação X/Y/Z normal da camada — ou vincule a um nulo '
          '3D e gire o nulo.',
    ),
  ];

  /// COPIA PARA O APP ANTES DE GUARDAR: o seletor devolve um arquivo em
  /// CACHE, que o Android apaga quando quer — e o solido abria sem a
  /// imagem. (`pickImageFile` ja copia; no Android o seletor reabre na
  /// ultima pasta.)
  Future<void> _escolherImagem() async {
    final String? caminho;
    try {
      caminho = await ref.read(mediaImportServiceProvider).pickImageFile();
    } catch (_) {
      if (mounted) {
        showReasonToast(
          context,
          translate(context, 'Não consegui abrir essa imagem.'),
        );
      }
      return;
    }
    if (caminho == null || !mounted) return;
    _mudar((x) => x.copyElement3D(imagePath: caminho));
  }

  /// A LINHA DE UM ARQUIVO (modelo, imagem): o nome do que esta em uso, a
  /// pilula que escolhe outro e o x que tira.
  Widget _linhaDeArquivo({
    required String rotulo,
    required String chave,
    required String descricao,
    required IconData icone,
    required String acao,
    required VoidCallback aoEscolher,
    required VoidCallback? aoLimpar,
  }) => AureaPropertyRow.personalizada(
    rotulo: rotulo,
    chave: chave,
    filho: Row(
      children: [
        Expanded(
          // Ja chega traduzido (e com o nome do arquivo, que e conteudo).
          child: Text(
            descricao,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: AureaEstilos.corpo.copyWith(fontSize: 12),
          ),
        ),
        const SizedBox(width: AureaDims.e6),
        AureaChip(
          key: ValueKey('$chave-escolher'),
          rotulo: acao,
          icone: icone,
          aoTocar: aoEscolher,
        ),
        if (aoLimpar != null)
          Tocavel(
            key: ValueKey('$chave-limpar'),
            onTap: aoLimpar,
            child: SizedBox(
              width: AureaDims.toqueMinimo,
              height: AureaDims.linhaDePropriedade,
              child: Icon(
                CupertinoIcons.xmark_circle_fill,
                size: AureaDims.iconeMd,
                color: AureaCores.textoSecundario,
              ),
            ),
          ),
      ],
    ),
  );

  // --------------------------------------------------------------- Material

  List<Widget> _material(Element3DLayer e) => [
    _linhaDaCor(e),
    // REFLEXO DO AMBIENTE + qual ambiente. E o que faz o solido deixar de
    // parecer plastico fosco.
    linhaSemLosango(
      _c,
      rotulo: 'Reflexo',
      chave: 'elemento3d-reflexo',
      valor: e.reflect * 100,
      min: 0,
      max: 100,
      unidade: '%',
      aoMudar: (v) => _mudar((x) => x.copyElement3D(reflect: v / 100)),
    ),
    LinhaDeFichas<EnvironmentKind>(
      rotulo: 'Ambiente',
      chave: 'elemento3d-ambiente',
      valores: EnvironmentKind.values,
      rotuloDe: environmentLabel,
      escolhido: e.environment,
      chaveDe: (k) => 'elemento3d-ambiente-${k.name}',
      aoEscolher: (k) => _mudar((x) => x.copyElement3D(environment: k)),
    ),
    // ACABAMENTO: O MESMO SISTEMA DE MATERIAIS DO TEXTO 3D. Os cinco botoes
    // antigos (solido, brilhante, vidro, metal, fosco) nao eram materiais;
    // estes sao os metais do Texto 3D, com metalico e rugosidade de
    // verdade e o mesmo mapa de ambiente — o cubo de cromo e a letra de
    // cromo saem da mesma conta.
    LinhaDeFichas<AcabamentoDoElemento3D>(
      rotulo: 'Acabamento',
      chave: 'elemento3d-acabamento',
      valores: AcabamentoDoElemento3D.values,
      rotuloDe: nomeDoAcabamento,
      escolhido: e.acabamento,
      chaveDe: (a) => 'elemento3d-acabamento-${a.name}',
      aoEscolher: (a) => _mudar((x) => x.copyElement3D(acabamento: a)),
    ),
    // O DEGRADE SO EXISTE NO MATERIAL BRILHANTE ANTIGO (material 1): um
    // projeto de antes do acabamento continua editavel, e nenhum novo
    // ganha uma linha que nao faz nada.
    if (e.material == 1) _linhaDoDegrade(e),
    linhaSemLosango(
      _c,
      rotulo: 'Brilho',
      chave: 'elemento3d-brilho',
      valor: e.shininess * 100,
      min: 0,
      max: 100,
      unidade: '%',
      aoMudar: (v) => _mudar((x) => x.copyElement3D(shininess: v / 100)),
    ),
  ];

  /// A COR: a amostra abre o seletor da casa (espectro, hex e alfa) e as
  /// quatro prontas trocam num toque. O seletor e UM gesto — a cor muda
  /// viva e tudo vira um passo de desfazer.
  Widget _linhaDaCor(Element3DLayer e) => AureaPropertyRow.personalizada(
    rotulo: 'Cor',
    chave: 'elemento3d-cor',
    filho: Row(
      children: [
        Tocavel(
          key: const ValueKey('cor-elemento3d-cor'),
          onTap: () => escolherCor(
            context,
            ref,
            inicial: e.color,
            aplicar: (cor) => _mudar((x) => x.copyElement3D(color: cor)),
          ),
          child: SizedBox(
            height: AureaDims.linhaDePropriedade,
            child: Center(
              child: Container(
                width: 34,
                height: 22,
                decoration: BoxDecoration(
                  color: e.color,
                  borderRadius: BorderRadius.circular(AureaDims.raioMd),
                ),
              ),
            ),
          ),
        ),
        const SizedBox(width: AureaDims.e10),
        for (var i = 0; i < _coresProntas.length; i++)
          _Amostra(
            key: ValueKey('elemento3d-cor-pronta-$i'),
            ativa: e.color == _coresProntas[i],
            largura: 22,
            raio: AureaDims.raioPilula,
            fundo: BoxDecoration(color: _coresProntas[i]),
            aoTocar: () =>
                _mudar((x) => x.copyElement3D(color: _coresProntas[i])),
          ),
      ],
    ),
  );

  Widget _linhaDoDegrade(Element3DLayer e) => AureaPropertyRow.personalizada(
    rotulo: 'Degradê',
    chave: 'elemento3d-degrade',
    filho: SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      child: Row(
        children: [
          for (var i = 0; i < _degradesDoBrilhante.length; i++)
            _Amostra(
              key: ValueKey('elemento3d-degrade-$i'),
              ativa: _mesmasCores(e.gradient, _degradesDoBrilhante[i]),
              largura: 40,
              raio: AureaDims.raioMd,
              fundo: BoxDecoration(
                gradient: LinearGradient(colors: _degradesDoBrilhante[i]),
              ),
              aoTocar: () => _mudar(
                (x) => x.copyElement3D(gradient: _degradesDoBrilhante[i]),
              ),
            ),
        ],
      ),
    ),
  );
}

/// UMA AMOSTRA DE UM TOQUE (cor ou degrade pronto).
///
/// A escolhida ganha o fundo do destaque apagado em volta — a mesma
/// linguagem da pilula acesa. Nada de contorno: a amostra e conteudo, e
/// um aro branco sumiria justo na amostra branca.
class _Amostra extends StatelessWidget {
  const _Amostra({
    super.key,
    required this.ativa,
    required this.largura,
    required this.raio,
    required this.fundo,
    required this.aoTocar,
  });

  final bool ativa;
  final double largura;
  final double raio;

  /// A cor ou o degrade (sem a forma: o raio vem de [raio]).
  final BoxDecoration fundo;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: aoTocar,
    child: Container(
      margin: const EdgeInsets.only(right: AureaDims.e4),
      padding: const EdgeInsets.all(AureaDims.e4),
      decoration: BoxDecoration(
        color: ativa ? AureaCores.destaqueApagado : null,
        borderRadius: BorderRadius.circular(raio),
      ),
      child: Container(
        width: largura,
        height: 22,
        decoration: fundo.copyWith(borderRadius: BorderRadius.circular(raio)),
      ),
    ),
  );
}
