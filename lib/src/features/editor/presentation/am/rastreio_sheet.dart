import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/blob_track_service.dart';
import '../../application/camera_track_service.dart';
import '../../application/editor_controller.dart';
import '../../domain/blob_track.dart';
import '../../domain/camera_solver3d.dart';
import '../../domain/effect.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'rastreio3d_screen.dart';

/// RASTREAR — as duas perguntas que a pessoa realmente faz.
///
/// "Como faco esse nome seguir a moto?" e "como ponho um objeto 3D nesse
/// chao?". Sao problemas diferentes e ferramentas diferentes, e ate aqui
/// nenhuma das duas tinha porta de entrada: o rastreio de regioes vivia
/// escondido dentro de um efeito e so desenhava caixas, e o rastreio de
/// camera nao existia.
///
/// A folha e uma so, com as duas, porque quem chega nao sabe o nome de
/// nenhuma delas — sabe o que quer que aconteca.
Future<void> showRastreioSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  await CameraTrackService.instance.load(layerId);
  if (!context.mounted) return;
  await showParamSheet(
    context,
    title: 'Rastrear',
    heightFactor: 0.72,
    builder: (sheetContext) => _Rastreio(ref: ref, layerId: layerId),
  );
}

class _Rastreio extends StatefulWidget {
  const _Rastreio({required this.ref, required this.layerId});

  final WidgetRef ref;
  final String layerId;

  @override
  State<_Rastreio> createState() => _RastreioState();
}

class _RastreioState extends State<_Rastreio> {
  bool _rodandoCamera = false;
  bool _rodandoObjeto = false;
  String? _erroCamera;
  int? _blobEscolhido;
  bool _comEscala = false;

  EditorController get _c => widget.ref.read(editorControllerProvider.notifier);

  /// O efeito de rastreio de regioes desta camada — criado na hora se
  /// ainda nao existir.
  ///
  /// Antes era preciso saber que existe um efeito chamado "Blob Tracker",
  /// adiciona-lo, achar o botao Analisar dentro dele e so entao ver
  /// alguma coisa. Quatro passos para uma pergunta simples.
  String _efeitoDeRegioes() {
    final projeto = widget.ref.read(editorControllerProvider);
    final camada = projeto.layerById(widget.layerId)!;
    for (final e in camada.effects) {
      if (e.type == EffectType.blobTracker) return e.id;
    }
    _c.addEffect(widget.layerId, EffectType.blobTracker);
    final atualizada = widget.ref
        .read(editorControllerProvider)
        .layerById(widget.layerId)!;
    for (final e in atualizada.effects) {
      if (e.type == EffectType.blobTracker) return e.id;
    }
    return '';
  }

  Future<void> _rastrearCamera() async {
    setState(() {
      _rodandoCamera = true;
      _erroCamera = null;
    });
    try {
      await _c.rastrearCamera3D(widget.layerId);
    } on RastreioException catch (e) {
      if (mounted) setState(() => _erroCamera = e.mensagem);
    } catch (_) {
      if (mounted) {
        setState(() => _erroCamera = 'Nao consegui ler esse video.');
      }
    } finally {
      if (mounted) setState(() => _rodandoCamera = false);
    }
  }

  Future<void> _rastrearObjetos() async {
    setState(() => _rodandoObjeto = true);
    final efeito = _efeitoDeRegioes();
    final n = efeito.isEmpty
        ? null
        : await _c.analyzeBlobsFor(widget.layerId, efeito);
    if (!mounted) return;
    setState(() => _rodandoObjeto = false);
    if (n == null) {
      AureaSnack.show(context, 'Nao consegui ler esse video');
    }
  }

  void _grudar(String alvoId, String efeitoId) {
    final n = _c.grudarNoBlob(
      alvoId: alvoId,
      videoId: widget.layerId,
      effectId: efeitoId,
      blobId: _blobEscolhido!,
      comEscala: _comEscala,
    );
    Navigator.of(context).maybePop();
    AureaSnack.show(
      context,
      n == null || n == 0
          ? 'Esse objeto nao aparece no tempo dessa camada'
          : 'Grudado com $n keyframes',
      actionLabel: n == null || n == 0 ? null : 'Desfazer',
      onAction: _c.undo,
    );
  }

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: ListenableBuilder(
        listenable: Listenable.merge([
          CameraTrackService.instance.revision,
          CameraTrackService.instance.etapa,
          BlobTrackService.instance.revision,
        ]),
        builder: (context, _) {
          // O PROJETO E LIDO AQUI DENTRO, e nao no `build` de fora.
          //
          // "Procurar objetos" cria o efeito de rastreio na camada
          // quando ele nao existe. Lido de fora, o efeito recem-criado
          // ficava invisivel para esta folha ate ela ser reaberta — e a
          // pessoa via "0 objetos" logo depois de analisar.
          final projeto = widget.ref.read(editorControllerProvider);
          final camada = projeto.layerById(widget.layerId);
          if (camada == null) return const SizedBox.shrink();

          final solucao = CameraTrackService.instance.dataFor(widget.layerId);
          final efeitoId = () {
            for (final e in camada.effects) {
              if (e.type == EffectType.blobTracker) return e.id;
            }
            return '';
          }();
          final blobs = efeitoId.isEmpty
              ? null
              : BlobTrackService.instance.dataFor(efeitoId);

          return SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 20),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // ------------------------------------------- objeto 2D
                const _Titulo(
                  'Seguir um objeto',
                  'Acha o que se mexe no vídeo e gruda uma camada nele: '
                      'o nome que acompanha a pessoa, a seta que segue o carro.',
                ),
                const SizedBox(height: 10),
                _Botao(
                  chave: 'rastreio-objetos',
                  texto: _rodandoObjeto
                      ? 'Procurando no vídeo...'
                      : (blobs != null && !blobs.isEmpty
                            ? 'Procurar de novo'
                            : 'Procurar objetos'),
                  destaque: blobs == null || blobs.isEmpty,
                  onTap: _rodandoObjeto ? null : _rastrearObjetos,
                ),
                if (blobs != null && !blobs.isEmpty) ...[
                  const SizedBox(height: 10),
                  _ListaDeBlobs(
                    dados: blobs,
                    escolhido: _blobEscolhido,
                    onEscolher: (id) => setState(() => _blobEscolhido = id),
                  ),
                  if (_blobEscolhido != null) ...[
                    const SizedBox(height: 10),
                    _Interruptor(
                      rotulo: 'Crescer e encolher junto',
                      valor: _comEscala,
                      onChanged: (v) => setState(() => _comEscala = v),
                    ),
                    const SizedBox(height: 6),
                    const AppText('Qual camada gruda nesse objeto?',
                      style: TextStyle(fontSize: 12, color: AmColors.muted),
                    ),
                    const SizedBox(height: 6),
                    for (final outra in projeto.layers)
                      if (outra.id != widget.layerId)
                        _LinhaDeCamada(
                          nome: outra.name,
                          onTap: () => _grudar(outra.id, efeitoId),
                        ),
                    if (projeto.layers.length < 2)
                      const _Aviso(
                        'Adicione um texto ou uma forma antes: é ela que '
                        'vai seguir o objeto.',
                      ),
                  ],
                ],

                const SizedBox(height: 22),
                _Divisoria(),
                const SizedBox(height: 18),

                // ------------------------------------------- camera 3D
                const _Titulo(
                  'Rastrear a câmera em 3D',
                  'Descobre por onde a câmera andou e monta uma cena 3D em '
                      'cima do vídeo. O que você puser nela fica parado no '
                      'lugar do mundo real.',
                ),
                const SizedBox(height: 10),
                if (_rodandoCamera)
                  _Andamento(CameraTrackService.instance.etapa.value)
                else
                  _Botao(
                    chave: 'rastreio-camera',
                    texto: solucao == null
                        ? 'Rastrear a câmera'
                        : 'Rastrear de novo',
                    destaque: solucao == null,
                    onTap: _rastrearCamera,
                  ),
                if (_erroCamera != null) ...[
                  const SizedBox(height: 10),
                  _Aviso(_erroCamera!),
                ],
                if (solucao != null && !solucao.isEmpty) ...[
                  const SizedBox(height: 10),
                  _Resultado(solucao),
                  // A cena 3D e desenhada no tamanho da composicao, e o
                  // angulo de visao resolvido vale para o quadro do
                  // video. Proporcoes diferentes = 3D certo na
                  // horizontal e desencontrado na vertical.
                  if ((solucao.largura / solucao.altura - projeto.aspectRatio)
                          .abs() >
                      0.03) ...[
                    const SizedBox(height: 8),
                    const _Aviso(
                      'O vídeo e a composição têm proporções diferentes. '
                      'A cena 3D vai bater na horizontal e escorregar na '
                      'vertical. Ajuste a composição para a proporção do '
                      'vídeo antes de montar em cima.',
                    ),
                  ],
                  const SizedBox(height: 10),
                  // A TELA DOS PONTOS VEM ANTES DE CRIAR A CENA.
                  //
                  // Criar a cena direto funciona, mas entrega uma camada
                  // 3D com a orientacao que o solver escolheu — e ai a
                  // pessoa descobre que o chao esta torto so depois de
                  // montar tudo em cima. Ver os pontos, escolher o chao e
                  // pousar o objeto na superficie e a ordem que evita
                  // refazer.
                  _Botao(
                    chave: 'rastreio-abrir-pontos',
                    texto: 'Ver os pontos e montar a cena',
                    destaque: true,
                    onTap: () {
                      Navigator.of(context).push(
                        MaterialPageRoute<void>(
                          builder: (_) => Rastreio3DScreen(
                            layerId: widget.layerId,
                            solucao: solucao,
                          ),
                        ),
                      );
                    },
                  ),
                  const SizedBox(height: 8),
                  _Botao(
                    chave: 'rastreio-criar-cena',
                    texto: 'Criar a cena 3D em cima do vídeo',
                    onTap: () {
                      _c.criarCenaDoRastreio(widget.layerId, solucao);
                      Navigator.of(context).maybePop();
                      AureaSnack.show(
                        context,
                        'Cena 3D criada com a câmera rastreada',
                        actionLabel: 'Desfazer',
                        onAction: _c.undo,
                      );
                    },
                  ),
                ],
              ],
            ),
          );
        },
      ),
    );
  }
}

class _Titulo extends StatelessWidget {
  const _Titulo(this.titulo, this.explicacao);
  final String titulo;
  final String explicacao;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      AppText(titulo,
        style: const TextStyle(
          fontSize: 15,
          fontWeight: FontWeight.w700,
          color: AmColors.text,
        ),
      ),
      const SizedBox(height: 3),
      AppText(
        explicacao,
        style: const TextStyle(
          fontSize: 11.5,
          height: 1.35,
          color: AmColors.muted,
        ),
      ),
    ],
  );
}

class _Botao extends StatelessWidget {
  const _Botao({
    required this.texto,
    required this.onTap,
    required this.chave,
    this.destaque = false,
  });

  final String texto;
  final VoidCallback? onTap;
  final String chave;
  final bool destaque;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey(chave),
    onTap: onTap,
    child: Container(
      width: double.infinity,
      // 44 pt de alvo: a regra do app inteiro.
      constraints: const BoxConstraints(minHeight: 44),
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: destaque ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(12),
      ),
      child: AppText(texto,
        textAlign: TextAlign.center,
        style: TextStyle(
          fontSize: 13,
          fontWeight: FontWeight.w600,
          color: destaque ? AmColors.accent : AmColors.text,
        ),
      ),
    ),
  );
}

class _Andamento extends StatelessWidget {
  const _Andamento(this.etapa);
  final String etapa;

  @override
  Widget build(BuildContext context) => Container(
    key: const ValueKey('rastreio-andamento'),
    width: double.infinity,
    constraints: const BoxConstraints(minHeight: 44),
    alignment: Alignment.center,
    decoration: BoxDecoration(
      color: AmColors.chip,
      borderRadius: BorderRadius.circular(12),
    ),
    child: Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        const CupertinoActivityIndicator(radius: 8),
        const SizedBox(width: 8),
        AppText(
          etapa.isEmpty ? 'Rastreando...' : etapa,
          style: const TextStyle(fontSize: 13, color: AmColors.text),
        ),
      ],
    ),
  );
}

/// O RESULTADO EM PALAVRAS, e nao so em numeros.
///
/// "0,8 px de erro" nao diz a ninguem se deu certo. O que diz e a
/// palavra na frente, e o que fazer quando ela e ruim.
class _Resultado extends StatelessWidget {
  const _Resultado(this.solucao);
  final SolucaoCamera3D solucao;

  @override
  Widget build(BuildContext context) {
    final ruim = solucao.erroPixels >= 4;
    return Container(
      key: const ValueKey('rastreio-resultado'),
      width: double.infinity,
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          AppText(
            'Rastreio ${solucao.qualidade.toLowerCase()}',
            style: const TextStyle(
              fontSize: 13,
              fontWeight: FontWeight.w700,
              color: AmColors.text,
            ),
          ),
          const SizedBox(height: 3),
          AppText(
            '${solucao.poses.length} posições de câmera, '
            '${solucao.nuvem.length} pontos no espaço, '
            'lente de ${(36 * solucao.focalPx / solucao.largura).round()} mm.',
            style: const TextStyle(
              fontSize: 11.5,
              height: 1.35,
              color: AmColors.muted,
            ),
          ),
          if (ruim) ...[
            const SizedBox(height: 4),
            const AppText('A cena vai escorregar. Um plano com mais textura e com a '
              'câmera andando de lado costuma resolver.',
              style: TextStyle(
                fontSize: 11.5,
                height: 1.35,
                color: AmColors.pink,
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _ListaDeBlobs extends StatelessWidget {
  const _ListaDeBlobs({
    required this.dados,
    required this.escolhido,
    required this.onEscolher,
  });

  final BlobTrackData dados;
  final int? escolhido;
  final ValueChanged<int> onEscolher;

  @override
  Widget build(BuildContext context) {
    // Só os que atravessam o plano: os de tres quadros sao ruido, e
    // oferecer ruido faz a lista parecer quebrada.
    final ids = [
      for (final id in dados.idsPorDuracao)
        if (dados.duracaoDe(id) >= 4) id,
    ].take(12).toList();
    if (ids.isEmpty) {
      return const _Aviso(
        'Não achei nada que se mexa nesse trecho. Aumente a sensibilidade '
        'no efeito, ou escolha um trecho com mais movimento.',
      );
    }
    return Column(
      key: const ValueKey('rastreio-blobs'),
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        AppText(
          '${ids.length} ${ids.length == 1 ? "objeto" : "objetos"} '
          'encontrados. Toque no que você quer seguir.',
          style: const TextStyle(fontSize: 12, color: AmColors.muted),
        ),
        const SizedBox(height: 6),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            for (final id in ids)
              GestureDetector(
                key: ValueKey('blob-$id'),
                onTap: () => onEscolher(id),
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 10,
                  ),
                  decoration: BoxDecoration(
                    color: id == escolhido ? AmColors.accentDim : AmColors.chip,
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: AppText(
                    'Objeto $id · ${dados.duracaoDe(id)} q',
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                      color: id == escolhido ? AmColors.accent : AmColors.text,
                    ),
                  ),
                ),
              ),
          ],
        ),
      ],
    );
  }
}

class _LinhaDeCamada extends StatelessWidget {
  const _LinhaDeCamada({required this.nome, required this.onTap});
  final String nome;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      width: double.infinity,
      constraints: const BoxConstraints(minHeight: 44),
      margin: const EdgeInsets.only(bottom: 6),
      padding: const EdgeInsets.symmetric(horizontal: 12),
      alignment: Alignment.centerLeft,
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        children: [
          Expanded(
            child: AppText(nome,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontSize: 13, color: AmColors.text),
            ),
          ),
          const Icon(CupertinoIcons.link, size: 15, color: AmColors.accent),
        ],
      ),
    ),
  );
}

class _Interruptor extends StatelessWidget {
  const _Interruptor({
    required this.rotulo,
    required this.valor,
    required this.onChanged,
  });

  final String rotulo;
  final bool valor;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      Expanded(
        child: AppText(
          rotulo,
          style: const TextStyle(fontSize: 13, color: AmColors.text),
        ),
      ),
      CupertinoSwitch(
        value: valor,
        activeTrackColor: AmColors.accent,
        onChanged: onChanged,
      ),
    ],
  );
}

class _Aviso extends StatelessWidget {
  const _Aviso(this.texto);
  final String texto;

  @override
  Widget build(BuildContext context) => Container(
    key: const ValueKey('rastreio-aviso'),
    width: double.infinity,
    padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
    decoration: BoxDecoration(
      color: AmColors.chip,
      borderRadius: BorderRadius.circular(12),
    ),
    child: AppText(texto,
      style: const TextStyle(
        fontSize: 11.5,
        height: 1.35,
        color: AmColors.pink,
      ),
    ),
  );
}

class _Divisoria extends StatelessWidget {
  @override
  Widget build(BuildContext context) =>
      Container(height: 1, color: AmColors.hairline);
}
