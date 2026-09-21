import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../../../core/utils/time_format.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/camera_cuts.dart';
import '../../../domain/layer.dart';
import 'comum_3d.dart';
import 'comum_de_objetos.dart';

/// VARIAS CAMERAS, COM CORTE — as cameras de uma camada de cena 3D.
///
/// Uma camera so obriga a animar a mesma camera de um enquadramento ao
/// outro — e ai todo corte vira um voo. Cinema nao voa entre planos:
/// corta. Aqui cada camera guarda um enquadramento, e a lista de tomadas
/// diz qual esta no ar em cada instante.
///
/// Nao duplica o painel Camera (`camera.dart`): aquele e a camera DA
/// COMPOSICAO (lente, foco, neblina); esta folha e das cameras DE DENTRO
/// da cena 3D, com os cortes entre elas e o nulo que a camera segue.
///
/// Folha NAO modal: o cabecote anda com a folha aberta (o corte e marcado
/// onde ele estiver), e o palco mostra qual camera esta no ar.
Future<void> showCamerasSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  if (!context.mounted) return;
  await mostrarAureaFolha<void>(
    context,
    modal: false,
    // A folha poe 10 de respiro em cima: o total fica nos 326 do painel
    // grande.
    altura: AureaDims.painelGrande - AureaDims.e10,
    construtor: (_) => _FolhaDasCameras(layerId: layerId, playback: playback),
  );
}

class _FolhaDasCameras extends ConsumerStatefulWidget {
  const _FolhaDasCameras({required this.layerId, required this.playback});

  final String layerId;
  final PlaybackController playback;

  @override
  ConsumerState<_FolhaDasCameras> createState() => _FolhaDasCamerasState();
}

class _FolhaDasCamerasState extends ConsumerState<_FolhaDasCameras> {
  static const _titulo = 'Câmeras';
  static const _chave = 'folha-cameras';

  /// A transicao do PROXIMO corte, em segundos. E escolha da folha, nao do
  /// projeto: so vira dado quando um corte e marcado com ela.
  double _transicao = 0;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  void _fechar() => Navigator.of(context).maybePop();

  @override
  Widget build(BuildContext context) {
    // O PROJETO INTEIRO: a lista de nulos da composicao tambem mora aqui,
    // e um nulo novo (ou apagado) tem de aparecer sem reabrir a folha.
    final projeto = ref.watch(editorControllerProvider);
    final camada = projeto.layerById(widget.layerId);
    if (camada is! Scene3DLayer) {
      return AureaPanel(
        titulo: _titulo,
        chave: _chave,
        aoFechar: _fechar,
        filhos: [
          AureaAvisoDoPainel(
            texto: camada == null
                ? 'Esta camada não existe mais.'
                : 'Esta camada não é uma cena 3D.',
          ),
        ],
      );
    }
    final nulos = projeto.layers.whereType<NullLayer>().toList();
    return AureaPanel(
      titulo: _titulo,
      chave: _chave,
      aoFechar: _fechar,
      // O RELOGIO DA FOLHA: a camera no ar e o instante do corte mudam com
      // o cabecote, e o `t` lido no build ficaria velho depois de um scrub
      // (o corte cairia no instante em que a folha abriu).
      corpo: ValueListenableBuilder<Duration>(
        valueListenable: widget.playback.time,
        builder: (context, t, _) => ListView(
          key: const ValueKey('cameras-lista'),
          padding: paddingDoPainel,
          children: [
            ..._cameras(camada, camada.localTime(t)),
            ..._seguirNulo(camada, nulos),
            ..._tomadas(camada),
          ],
        ),
      ),
    );
  }

  // ---------------------------------------------------------------- cameras

  List<Widget> _cameras(Scene3DLayer camada, Duration local) {
    final cameras = camada.allCameras;
    final noAr = shotAt(camada.shots, local);
    return [
      AppTextMoldado(
        'Cabeçote em {0}. Toque numa câmera para cortar para ela aqui.',
        [formatTime(local)],
        style: AureaEstilos.propriedade,
      ),
      const SizedBox(height: AureaDims.e6),
      for (var i = 0; i < cameras.length; i++)
        _LinhaDaCamera(
          key: ValueKey('cameras-camera-${cameras[i].id}'),
          chave: 'cameras-camera-${cameras[i].id}',
          nome: cameras[i].name,
          principal: i == 0,
          noAr: noAr?.cameraId == cameras[i].id,
          // CORTAR E UMA MUTACAO SO: um passo de desfazer por corte.
          aoCortar: () => _c.setCameraShot(
            camada.id,
            local,
            cameras[i].id,
            transition: Duration(milliseconds: (_transicao * 1000).round()),
          ),
          // A PRINCIPAL NAO SE APAGA: a cena ficaria sem camera nenhuma.
          aoApagar: i == 0
              ? null
              : () => _c.removeScene3DCamera(camada.id, cameras[i].id),
        ),
      // A transicao nao e dado do projeto (so entra no proximo corte): o
      // arrasto nao abre passo de desfazer.
      AureaPropertyRow(
        rotulo: 'Transição',
        chave: 'cameras-transicao',
        valor: _transicao,
        min: 0,
        max: 3,
        casas: 1,
        unidade: 's',
        aoResetar: () => setState(() => _transicao = 0),
        aoMudar: (v) => setState(() => _transicao = v),
      ),
      const AureaAvisoDoPainel(
        texto:
            'Zero é corte seco. Maior que zero derrete de uma câmera na '
            'outra.',
      ),
      LinhaDeAcao(
        key: const ValueKey('cameras-nova'),
        rotulo: 'Nova câmera (enquadramento atual)',
        icone: CupertinoIcons.plus_circle,
        aoTocar: () => _c.addScene3DCamera(camada.id),
      ),
    ];
  }

  // ------------------------------------------------------------ seguir nulo

  /// A PONTE COM A COMPOSICAO: todo rig de camera e "camera parenteada a
  /// um nulo". Sem isto, orbita, tripe, dolly, camera na mao e dolly zoom
  /// estao todos quebrados.
  List<Widget> _seguirNulo(Scene3DLayer camada, List<NullLayer> nulos) {
    final pai = camada.cameraParentLayerId;
    return [
      AureaSection(
        titulo: 'Seguir um nulo da composição',
        chave: 'cameras-seguir',
        recolhivel: false,
        filhos: [
          if (nulos.isEmpty)
            const AureaAvisoDoPainel(
              texto:
                  'Não há objeto nulo no projeto. Crie um e a câmera pode '
                  'segui-lo — girar o nulo orbita a cena.',
            )
          else ...[
            AureaPropertyRow.personalizada(
              rotulo: 'Nulo',
              chave: 'cameras-nulo',
              // PILULAS, NAO MENU: um toque escolhe (como na folha antiga).
              // "Nenhum" e rotulo; o nome do nulo e conteudo.
              filho: SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: Row(
                  children: [
                    AureaChip(
                      key: const ValueKey('cameras-nulo-nenhum'),
                      rotulo: 'Nenhum',
                      ativo: pai == null,
                      aoTocar: () =>
                          _c.setSceneCameraCompParent(camada.id, null),
                    ),
                    for (final n in nulos) ...[
                      const SizedBox(width: AureaDims.e6),
                      AureaChip(
                        key: ValueKey('cameras-nulo-${n.id}'),
                        rotulo: n.name,
                        traduzir: false,
                        ativo: pai == n.id,
                        aoTocar: () =>
                            _c.setSceneCameraCompParent(camada.id, n.id),
                      ),
                    ],
                  ],
                ),
              ),
            ),
            const AureaAvisoDoPainel(
              texto:
                  'A câmera herda posição e rotação do nulo — nunca a '
                  'escala. Câmera não tem escala.',
            ),
          ],
        ],
      ),
    ];
  }

  // ---------------------------------------------------------------- tomadas

  List<Widget> _tomadas(Scene3DLayer camada) {
    final tomadas = sortedShots(camada.shots);
    if (tomadas.isEmpty) return const [];
    final cameras = camada.allCameras;
    return [
      AureaSection(
        titulo: 'Tomadas',
        chave: 'cameras-tomadas',
        recolhivel: false,
        filhos: [
          for (final t in tomadas)
            _LinhaDaTomada(
              key: ValueKey('cameras-tomada-${t.time.inMicroseconds}'),
              chave: 'cameras-tomada-${t.time.inMicroseconds}',
              tempo: formatTime(t.time),
              camera: cameras
                  .where((c) => c.id == t.cameraId)
                  .map((c) => c.name)
                  .firstOrNull,
              transicaoEmSegundos: t.isCut
                  ? null
                  : t.transition.inMilliseconds / 1000,
              aoIr: () => widget.playback.seek(camada.startTime + t.time),
              aoApagar: () => _c.removeCameraShot(camada.id, t.time),
            ),
          LinhaDeAcao(
            key: const ValueKey('cameras-limpar'),
            rotulo: 'Limpar tomadas',
            icone: CupertinoIcons.trash,
            destrutiva: true,
            aoTocar: () => _c.clearCameraShots(camada.id),
          ),
        ],
      ),
    ];
  }
}

/// UMA CAMERA DA CENA: o toque corta para ela no cabecote; a lixeira
/// (so nas extras) apaga a camera e as tomadas que apontavam para ela.
class _LinhaDaCamera extends StatelessWidget {
  const _LinhaDaCamera({
    super.key,
    required this.chave,
    required this.nome,
    required this.principal,
    required this.noAr,
    required this.aoCortar,
    required this.aoApagar,
  });

  final String chave;
  final String nome;
  final bool principal;
  final bool noAr;
  final VoidCallback aoCortar;
  final VoidCallback? aoApagar;

  @override
  Widget build(BuildContext context) {
    final cor = noAr ? AureaCores.destaque : AureaCores.texto;
    return Tocavel(
      onTap: aoCortar,
      encolhe: 1,
      child: SizedBox(
        height: AureaDims.itemDeLista + AureaDims.e6,
        child: Row(
          children: [
            Icon(
              noAr ? CupertinoIcons.videocam_fill : CupertinoIcons.videocam,
              size: AureaDims.iconeMd,
              color: noAr ? AureaCores.destaque : AureaCores.textoSecundario,
            ),
            const SizedBox(width: AureaDims.e10),
            Expanded(
              // O NOME E CONTEUDO; "(principal)" e rotulo — por isso o molde.
              child: Text(
                principal ? moldar(context, '{0} (principal)', [nome]) : nome,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: AureaEstilos.corpo.copyWith(color: cor),
              ),
            ),
            if (aoApagar != null)
              Tocavel(
                key: ValueKey('$chave-apagar'),
                onTap: aoApagar,
                child: SizedBox(
                  width: AureaDims.toqueMinimo,
                  height: AureaDims.itemDeLista,
                  child: Icon(
                    CupertinoIcons.trash,
                    size: AureaDims.iconeSm,
                    color: AureaCores.textoSecundario,
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

/// UMA TOMADA: instante, camera e transicao. O toque leva o cabecote ate
/// ela; o x tira a tomada.
class _LinhaDaTomada extends StatelessWidget {
  const _LinhaDaTomada({
    super.key,
    required this.chave,
    required this.tempo,
    required this.camera,
    required this.transicaoEmSegundos,
    required this.aoIr,
    required this.aoApagar,
  });

  final String chave;
  final String tempo;

  /// Nulo: a camera daquela tomada foi apagada.
  final String? camera;

  /// Nulo: corte seco.
  final double? transicaoEmSegundos;
  final VoidCallback aoIr;
  final VoidCallback aoApagar;

  @override
  Widget build(BuildContext context) {
    final secundario = AureaEstilos.propriedade;
    final segundos = transicaoEmSegundos;
    return Tocavel(
      onTap: aoIr,
      encolhe: 1,
      child: SizedBox(
        height: AureaDims.itemDeLista,
        child: Row(
          children: [
            SizedBox(
              width: 74,
              child: Text(
                tempo,
                style: AureaEstilos.valor.copyWith(
                  fontSize: 12,
                  color: AureaCores.destaque,
                ),
              ),
            ),
            Expanded(
              child: camera == null
                  ? AppText(
                      'câmera apagada',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: secundario,
                    )
                  : Text(
                      camera!,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: AureaEstilos.corpo.copyWith(fontSize: 12),
                    ),
            ),
            if (segundos == null)
              AppText('corte', style: secundario)
            else
              Text('${segundos.toStringAsFixed(1)} s', style: secundario),
            Tocavel(
              key: ValueKey('$chave-apagar'),
              onTap: aoApagar,
              child: SizedBox(
                width: AureaDims.toqueMinimo,
                height: AureaDims.itemDeLista,
                child: Icon(
                  CupertinoIcons.xmark,
                  size: AureaDims.iconeSm - 2,
                  color: AureaCores.textoSecundario,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
