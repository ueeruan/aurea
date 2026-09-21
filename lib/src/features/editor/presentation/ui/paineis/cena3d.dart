import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/element3d.dart';
import '../../../domain/layer.dart';
import '../../../domain/scene3d.dart';
import '../../am/cameras_sheet.dart' show showCamerasSheet;
import '../../am/layer_menu.dart' show showElement3DSheet;
import '../../widgets/gizmo_da_cena_overlay.dart'
    show noDaCenaSelecionadoProvider;
import '../shell/contrato.dart';
import 'ambiente.dart';
import 'animacao3d.dart';
import 'comum.dart';
import 'comum_3d.dart';
import 'luz.dart';
import 'material.dart';

String rotuloDoDetalhe(MeshLod3D l) => switch (l) {
  MeshLod3D.auto => 'Automático',
  MeshLod3D.high => 'Alto',
  MeshLod3D.medium => 'Médio',
  MeshLod3D.low => 'Baixo',
};

/// CENA — o INSPECTOR do objeto 3D: o que o gizmo esta segurando, com as
/// seis abas do assunto.
///
///   Transformar   posicao XYZ, rotacao XYZ, escala uniforme e XYZ — cada
///                 uma com o losango da casa, no cabecote vivo
///   Material      o material do objeto (ou o metal da letra)
///   Iluminacao    as luzes da cena
///   Ambiente      o estudio, o reflexo, a luz do ar, ceu e chao
///   Animacao      o clipe do arquivo, ou os presets do Texto 3D
///   Propriedades  nome, visivel, cadeado, etiqueta, pai, detalhe,
///                 enquadrar, duplicar, apagar, cameras e cortes
///
/// O OBJETO EDITADO E O MESMO DO GIZMO ([noDaCenaSelecionadoProvider]):
/// escolher aqui move o gizmo, tocar outro objeto no palco troca o que o
/// painel mostra. Nao ha viewport nem previa: o palco e a previa.
class PainelCena3D extends ConsumerStatefulWidget {
  const PainelCena3D({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelCena3D> createState() => _PainelCena3DState();
}

class _PainelCena3DState extends ConsumerState<PainelCena3D> {
  static const _titulo = 'Cena';
  static const _abas = [
    'Transformar',
    'Material',
    'Iluminação',
    'Ambiente',
    'Animação',
    'Propriedades',
  ];

  int _aba = 0;
  final _nome = TextEditingController();
  final _foco = FocusNode();

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  void dispose() {
    _nome.dispose();
    _foco.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final id = widget.layerId;
    final camada = camadaVisivel(ref, id);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.cena3d.name}';
    if (camada is Element3DLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        portas: [
          LinhaDePorta(
            rotulo: 'Forma, material e acabamento do elemento',
            icone: CupertinoIcons.cube_box,
            aoTocar: () {
              escopo.playback.pause();
              showElement3DSheet(context, ref, id);
            },
          ),
        ],
      );
    }
    if (camada is! Scene3DLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é uma cena 3D.',
        portas: const [],
      );
    }
    final no = noEmFoco(camada.scene, ref.watch(noDaCenaSelecionadoProvider));
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      corpo: switch (_aba) {
        0 => NoCabecote(construir: (context, t) => _transformar(camada, no, t)),
        1 => CorpoDoMaterial3D(layerId: id),
        2 => CorpoDaLuz3D(layerId: id),
        3 => CorpoDoAmbiente3D(layerId: id),
        4 => CorpoDaAnimacao3D(layerId: id),
        _ => _propriedades(camada, no),
      },
    );
  }

  /// A cena ainda nao tem objeto: o caminho para o primeiro.
  List<Widget> _semObjeto(Scene3DLayer camada, String texto) => [
    AureaAvisoDoPainel(texto: texto),
    GradeDeAcoes(
      acoes: [
        AcaoDoPainel(
          chave: 'cena3d-adicionar-objeto',
          icone: CupertinoIcons.cube,
          rotulo: 'Adicionar cubo',
          aoTocar: () => _c.addSceneNode(camada.id, Element3DKind.cube),
        ),
      ],
    ),
  ];

  // ------------------------------------------------------------ Transformar

  Widget _transformar(Scene3DLayer camada, SceneNode? no, Duration t) {
    final playback = EscopoDoEditor.of(context).playback;
    AureaPropertyRow linha(
      PropDoNo p,
      String rotulo, {
      required double min,
      required double max,
      required double sensibilidade,
      String unidade = '',
      double fator = 1,
      double padrao = 0,
      int casas = 1,
    }) => linhaDoNo(
      _c,
      camada: camada,
      no: no!,
      prop: p,
      rotulo: rotulo,
      t: t,
      playback: playback,
      min: min,
      max: max,
      casas: casas,
      unidade: unidade,
      fator: fator,
      padrao: padrao,
      sensibilidade: sensibilidade,
    );

    return ListView(
      key: const ValueKey('cena3d-transformar'),
      padding: paddingDoPainel,
      children: [
        EscolhaDoObjeto3D(cena: camada.scene, escolhido: no),
        if (no == null)
          ..._semObjeto(camada, 'Esta cena não tem objeto para transformar.')
        else ...[
          AureaSection(
            titulo: 'Posição',
            chave: 'cena3d-posicao',
            filhos: [
              for (final (p, r) in const [
                (PropDoNo.x, 'Posição X'),
                (PropDoNo.y, 'Posição Y'),
                (PropDoNo.z, 'Posição Z'),
              ])
                linha(p, r, min: -20000, max: 20000, sensibilidade: 2),
            ],
          ),
          AureaSection(
            titulo: 'Rotação',
            chave: 'cena3d-rotacao',
            filhos: [
              for (final (p, r) in const [
                (PropDoNo.giroX, 'Rotação X'),
                (PropDoNo.giroY, 'Rotação Y'),
                (PropDoNo.giroZ, 'Rotação Z'),
              ])
                linha(
                  p,
                  r,
                  min: -1440,
                  max: 1440,
                  sensibilidade: .8,
                  unidade: '°',
                ),
            ],
          ),
          AureaSection(
            titulo: 'Escala',
            chave: 'cena3d-escala',
            filhos: [
              // A UNIFORME PRIMEIRO: e a que quase sempre se quer, e a unica
              // que desce pela cadeia de pais. As de eixo esticam so este
              // objeto.
              for (final (p, r) in const [
                (PropDoNo.escala, 'Uniforme'),
                (PropDoNo.escalaX, 'Escala X'),
                (PropDoNo.escalaY, 'Escala Y'),
                (PropDoNo.escalaZ, 'Escala Z'),
              ])
                linha(
                  p,
                  r,
                  min: 1,
                  max: 2000,
                  sensibilidade: 1,
                  unidade: '%',
                  fator: 100,
                  padrao: 100,
                  casas: 0,
                ),
            ],
          ),
          GradeDeAcoes(
            acoes: [
              AcaoDoPainel(
                chave: 'cena3d-enquadrar',
                icone: CupertinoIcons.viewfinder,
                rotulo: 'Enquadrar',
                aoTocar: () => _c.frameSceneNode(camada.id, no.id),
              ),
              AcaoDoPainel(
                chave: 'cena3d-apontar',
                icone: CupertinoIcons.scope,
                rotulo: 'Apontar câmera',
                aoTocar: () => _c.focusCameraOnNode(camada.id, no.id),
              ),
            ],
          ),
        ],
      ],
    );
  }

  // ----------------------------------------------------------- Propriedades

  Widget _propriedades(Scene3DLayer camada, SceneNode? no) {
    final escopo = EscopoDoEditor.of(context);
    final cameras = LinhaDePorta(
      key: const ValueKey('cena3d-cameras'),
      rotulo: 'Câmeras e cortes',
      icone: CupertinoIcons.videocam,
      aoTocar: () {
        escopo.playback.pause();
        showCamerasSheet(context, ref, camada.id, escopo.playback);
      },
    );
    if (no == null) {
      return ListView(
        key: const ValueKey('cena3d-propriedades'),
        padding: paddingDoPainel,
        children: [..._semObjeto(camada, 'Esta cena não tem objeto.'), cameras],
      );
    }
    if (!_foco.hasFocus && _nome.text != no.name) _nome.text = no.name;
    final cena = camada.scene;
    final pais = [
      for (final outro in cena.nodes)
        if (outro.id != no.id) outro,
    ];
    final modelo = no.modelAsset;
    return ListView(
      key: const ValueKey('cena3d-propriedades'),
      padding: paddingDoPainel,
      children: [
        EscolhaDoObjeto3D(cena: cena, escolhido: no),
        Padding(
          padding: const EdgeInsets.only(bottom: AureaDims.e6),
          child: CupertinoTextField(
            key: const ValueKey('cena3d-nome'),
            controller: _nome,
            focusNode: _foco,
            placeholder: translate(context, 'Nome do objeto'),
            style: AureaEstilos.corpo,
            padding: const EdgeInsets.symmetric(
              horizontal: AureaDims.e10,
              vertical: AureaDims.e8,
            ),
            decoration: BoxDecoration(
              color: AureaCores.campo,
              borderRadius: BorderRadius.circular(AureaDims.raioMd),
            ),
            onSubmitted: (v) {
              if (v.trim().isNotEmpty) {
                _c.renameSceneNode(camada.id, no.id, v.trim());
              }
            },
          ),
        ),
        linhaDeInterruptor(
          rotulo: 'Visível',
          chave: 'cena3d-visivel',
          valor: no.visible,
          aoMudar: (v) => _c.setSceneNodeVisible(camada.id, no.id, v),
        ),
        linhaDeInterruptor(
          rotulo: 'Bloqueado',
          chave: 'cena3d-bloqueado',
          valor: no.locked,
          aoMudar: (v) => _c.setSceneNodeLocked(camada.id, no.id, v),
        ),
        linhaDeCor(
          context,
          rotulo: 'Etiqueta',
          chave: 'cena3d-etiqueta',
          cor: no.colorTag,
          aoMudar: (cor) => _c.setSceneNodeColorTag(camada.id, no.id, cor),
        ),
        if (pais.isNotEmpty)
          AureaPropertyRow.personalizada(
            rotulo: 'Pai na cena',
            chave: 'cena3d-pai',
            filho: AureaDropdown<String>(
              key: const ValueKey('cena3d-pai-escolha'),
              valor: no.parentId ?? '',
              opcoes: ['', for (final p in pais) p.id],
              // O NOME DO OBJETO e conteudo; "Sem pai" e rotulo.
              traduzir: false,
              rotuloDe: (pid) => pid.isEmpty
                  ? translate(context, 'Sem pai')
                  : (cena.nodeById(pid)?.name ?? pid),
              titulo: 'Pai na cena',
              aoMudar: (pid) => _c.setSceneNodeParent(
                camada.id,
                no.id,
                pid.isEmpty ? null : pid,
              ),
            ),
          ),
        if (modelo != null) ...[
          LinhaDeFichas<MeshLod3D>(
            rotulo: 'Detalhe',
            chave: 'cena3d-lod',
            valores: MeshLod3D.values,
            rotuloDe: rotuloDoDetalhe,
            escolhido: no.lod,
            chaveDe: (l) => 'cena3d-lod-${l.name}',
            aoEscolher: (l) => _c.setSceneNodeLod(camada.id, no.id, l),
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Triângulos',
            chave: 'cena3d-triangulos',
            filho: Text(
              _milhar(modelo.triangleCount),
              style: AureaEstilos.valor,
            ),
          ),
          if (!no.credit.isEmpty)
            AureaPropertyRow.personalizada(
              rotulo: 'Crédito',
              chave: 'cena3d-credito',
              filho: Text(
                no.credit.author ??
                    no.credit.source ??
                    no.credit.license ??
                    '—',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: AureaEstilos.valor,
              ),
            ),
        ] else ...[
          linhaSemLosango(
            _c,
            rotulo: 'Tamanho',
            chave: 'cena3d-tamanho',
            valor: no.size,
            min: 1,
            max: 2000,
            aoMudar: (v) => _c.setSceneNodeSize(camada.id, no.id, v),
          ),
          linhaSemLosango(
            _c,
            rotulo: 'Subdivisões',
            chave: 'cena3d-subdivisoes',
            valor: no.subdivisions.toDouble(),
            min: 0,
            max: 4,
            aoMudar: (v) =>
                _c.setSceneNodeSubdivisions(camada.id, no.id, v.round()),
          ),
        ],
        GradeDeAcoes(
          acoes: [
            AcaoDoPainel(
              chave: 'cena3d-duplicar',
              icone: CupertinoIcons.plus_square_on_square,
              rotulo: 'Duplicar',
              aoTocar: () {
                final novo = _c.duplicateSceneNode(camada.id, no.id);
                if (novo.isNotEmpty) {
                  ref.read(noDaCenaSelecionadoProvider.notifier).state = novo;
                }
              },
            ),
            AcaoDoPainel(
              chave: 'cena3d-apagar',
              icone: CupertinoIcons.trash,
              rotulo: 'Apagar',
              aoTocar: () {
                _c.removeSceneNode(camada.id, no.id);
                ref.read(noDaCenaSelecionadoProvider.notifier).state = null;
              },
            ),
            AcaoDoPainel(
              chave: 'cena3d-adicionar-objeto',
              icone: CupertinoIcons.cube,
              rotulo: 'Adicionar cubo',
              aoTocar: () => _c.addSceneNode(camada.id, Element3DKind.cube),
            ),
          ],
        ),
        cameras,
      ],
    );
  }

  static String _milhar(int v) {
    final s = '$v';
    final saida = StringBuffer();
    for (var i = 0; i < s.length; i++) {
      if (i > 0 && (s.length - i) % 3 == 0) saida.write('.');
      saida.write(s[i]);
    }
    return saida.toString();
  }
}
