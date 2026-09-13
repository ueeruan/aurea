import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/am_colors.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/camera3d.dart';
import '../../domain/camera_cuts.dart';
import '../../domain/layer.dart';
import '../estudio/estudio_da_cena.dart';
import 'linha_de_parametro.dart';

/// O recado da ultima acao da cena.
final recadoDaCenaProvider = StateProvider<String?>((ref) => null);

/// A CENA 3D: vista, camera e enquadramento.
///
/// O estudio 3D foi apagado por inteiro com a UI antiga (commit
/// `7fe26b6`) e nunca voltou. A auditoria contou **48 comandos 3D com
/// zero chamadores** — a cena renderiza, e nao ha um botao que mexa
/// nela. Este cartao devolve a parte que nao depende de gesto no palco:
/// de que ANGULO se olha, com que CAMERA, e como enquadrar.
///
/// O que continua fora, e de proposito: mover, girar e escalar NO no,
/// que precisa de selecao de no no palco — e o palco ainda nao tem um
/// unico `GestureDetector`. Prometer aqui seria prometer o palco.
class PainelDaCena extends ConsumerWidget {
  const PainelDaCena({
    super.key,
    required this.camada,
    required this.playback,
  });

  final Layer camada;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    ref.watch(editorControllerProvider);
    final c = ref.read(editorControllerProvider.notifier);
    final cena = camada;
    if (cena is! Scene3DLayer) return const SizedBox.shrink();
    final camera = cena.camera;
    final local = cena.localTime(playback.time.value);
    final recado = ref.watch(recadoDaCenaProvider);

    void dizer(String texto) =>
        ref.read(recadoDaCenaProvider.notifier).state = texto;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // A PORTA DO ESTUDIO, e ela e a primeira coisa do cartao.
        //
        // Este painel de 300 px alcanca oito comandos. Objeto, material,
        // luz, hierarquia, gizmo e animacao pedem um ESPACO — julgar
        // profundidade e enquadramento numa faixa de rodape nao e
        // possivel. Ver `docs/estudio-da-cena.md`.
        _Acao(
          icone: Icons.open_in_full_rounded,
          rotulo: 'Abrir o estudio',
          detalhe: 'Objetos, luzes, materiais e animacao, em tela cheia',
          aoTocar: () => abrirEstudioDaCena(
            context,
            layerId: cena.id,
            playback: playback,
          ),
        ),
        const _Titulo('De onde se olha'),
        // AS VISTAS FIXAS SAO O QUE TORNA O Z COMPREENSIVEL. Sem elas,
        // "esta atras ou e so menor?" nao tem resposta — a perspectiva
        // esconde a diferenca por definicao. O motor tinha as sete e o
        // palco lia `l.view`, que ficava preso em "Camera" para sempre.
        _Grade<SceneView>(
          itens: [
            for (final v in SceneView.values)
              if (v != SceneView.custom1 && v != SceneView.custom2)
                (sceneViewLabel(v), v),
          ],
          atual: cena.view,
          prefixo: 'Vista',
          aoTocar: (v) => c.setScene3DView(cena.id, v),
        ),
        _Interruptor(
          rotulo: camera.orthographic
              ? 'Ortografica (sem fuga)'
              : 'Perspectiva',
          icone: camera.orthographic
              ? Icons.crop_square_rounded
              : Icons.filter_center_focus_rounded,
          ligado: camera.orthographic,
          aoTocar: () =>
              c.setCameraOrthographic(cena.id, !camera.orthographic),
        ),
        LinhaDeParametro(
          rotulo: 'Lente',
          valor: camera.focalLength.valueAt(local),
          casas: 0,
          sufixo: 'mm',
          porPixel: .5,
          escolhida: true,
          aoComecar: c.beginGesture,
          aoMudar: (v) => c.setCameraFocalLength(cena.id, playback.time.value, v),
          aoTerminar: c.endGesture,
          aoDigitar: (v) => c.setCameraFocalLength(cena.id, playback.time.value, v),
        ),
        // O ANGULO E A LENTE SAO A MESMA GRANDEZA, e quem pensa em
        // "grande angular" pensa em graus. Mostrar os dois custa uma
        // linha de leitura e evita a conta de cabeca.
        _Aviso('Angulo de visao: ${camera.fovAt(local).round()}°'),
        const _Titulo('Cameras'),
        for (final cam in cena.allCameras)
          _LinhaDaCamera(
            nome: cam.name,
            // QUEM ESTA NO AR NESTE INSTANTE. Sem corte gravado, e a
            // camera da propria cena; com cortes, e a do ultimo corte
            // antes do cabecote — a mesma conta que o render faz.
            emUso: cam.id == (shotAt(cena.shots, local)?.cameraId ??
                cena.camera.id),
            podeApagar: cam.id != cena.camera.id,
            aoCortar: () {
              c.setCameraShot(cena.id, local, cam.id);
              dizer('Corte para "${cam.name}" no cabecote.');
            },
            aoDuplicar: () => c.duplicateSceneCamera(cena.id, cam.id),
            aoApagar: () => c.removeScene3DCamera(cena.id, cam.id),
          ),
        _Acao(
          icone: Icons.add_a_photo_rounded,
          rotulo: 'Nova camera',
          detalhe: 'Nasce com o enquadramento de agora',
          aoTocar: () => c.addScene3DCamera(cena.id),
        ),
        if (cena.shots.isNotEmpty)
          _Acao(
            icone: Icons.clear_rounded,
            rotulo: 'Tirar os cortes de camera (${cena.shots.length})',
            aoTocar: () {
              c.clearCameraShots(cena.id);
              dizer('Cortes apagados.');
            },
          ),
        const _Titulo('Movimento pronto'),
        // OS RIGS GERAM KEYFRAMES DE VERDADE, e nao um efeito escondido:
        // depois de aplicar, cada marca continua editavel. Sao o maior
        // retorno por menos codigo de toda a frente 3D.
        _Grade<CameraRig>(
          itens: [for (final r in CameraRig.values) (cameraRigLabel(r), r)],
          atual: null,
          prefixo: 'Rig',
          aoTocar: (r) {
            c.applyRigToScene(cena.id, r);
            dizer('${cameraRigLabel(r)} aplicado, com keyframes editaveis.');
          },
        ),
        const _Titulo('Enquadrar'),
        _Acao(
          icone: Icons.fit_screen_rounded,
          rotulo: 'Enquadrar a cena inteira',
          aoTocar: () {
            c.frameSceneAll(cena.id);
            dizer('Camera reposicionada para caber tudo.');
          },
        ),
        if (recado != null) _Aviso(recado),
      ],
    );
  }
}

class _LinhaDaCamera extends StatelessWidget {
  const _LinhaDaCamera({
    required this.nome,
    required this.emUso,
    required this.podeApagar,
    required this.aoCortar,
    required this.aoDuplicar,
    required this.aoApagar,
  });

  final String nome;
  final bool emUso;

  /// A PRIMEIRA CAMERA NAO SE APAGA. Ela e a da cena; sem nenhuma nao
  /// ha do que renderizar, e o motor cairia de volta nela de qualquer
  /// jeito — um botao que nao faz nada e pior que botao nenhum.
  final bool podeApagar;

  final VoidCallback aoCortar;
  final VoidCallback aoDuplicar;
  final VoidCallback aoApagar;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: 40,
    child: Row(
      children: [
        Expanded(
          child: Semantics(
            container: true,
            excludeSemantics: true,
            button: true,
            selected: emUso,
            label: 'Cortar para $nome',
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: aoCortar,
              child: Row(
                children: [
                  Icon(
                    emUso
                        ? Icons.videocam_rounded
                        : Icons.videocam_outlined,
                    size: 18,
                    color: emUso ? AmColors.accent : AmColors.muted,
                  ),
                  const SizedBox(width: 10),
                  Flexible(
                    child: AppText(nome,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: 13,
                        fontWeight: FontWeight.w600,
                        color: emUso ? AmColors.accent : AmColors.text,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
        _Icone(
          icone: Icons.copy_all_rounded,
          rotulo: 'Duplicar $nome',
          aoTocar: aoDuplicar,
        ),
        if (podeApagar)
          _Icone(
            icone: Icons.delete_outline_rounded,
            rotulo: 'Apagar $nome',
            aoTocar: aoApagar,
          ),
      ],
    ),
  );
}

class _Icone extends StatelessWidget {
  const _Icone({
    required this.icone,
    required this.rotulo,
    required this.aoTocar,
  });

  final IconData icone;
  final String rotulo;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: SizedBox(
        width: 34,
        height: 40,
        child: Icon(icone, size: 17, color: AmColors.muted),
      ),
    ),
  );
}

class _Grade<T> extends StatelessWidget {
  const _Grade({
    required this.itens,
    required this.atual,
    required this.prefixo,
    required this.aoTocar,
  });

  final List<(String, T)> itens;

  /// Nulo quando a grade e de ACOES, e nao de estado: um rig nao fica
  /// "escolhido" — ele e aplicado e vira keyframe.
  final T? atual;

  final String prefixo;
  final void Function(T) aoTocar;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 4),
    child: Wrap(
      spacing: 6,
      runSpacing: 6,
      children: [
        for (final (nome, valor) in itens)
          Semantics(
            container: true,
            excludeSemantics: true,
            button: true,
            selected: atual != null && valor == atual,
            label: '$prefixo $nome',
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: () => aoTocar(valor),
              child: Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: 12,
                  vertical: 9,
                ),
                decoration: BoxDecoration(
                  color: valor == atual ? AmColors.chip : null,
                  borderRadius: BorderRadius.circular(8),
                  border: valor == atual
                      ? null
                      : Border.all(color: AmColors.hairline),
                ),
                child: AppText(nome,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: valor == atual ? AmColors.accent : AmColors.text,
                  ),
                ),
              ),
            ),
          ),
      ],
    ),
  );
}

class _Acao extends StatelessWidget {
  const _Acao({
    required this.icone,
    required this.rotulo,
    required this.aoTocar,
    this.detalhe,
  });

  final IconData icone;
  final String rotulo;
  final VoidCallback aoTocar;
  final String? detalhe;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 10),
        child: Row(
          children: [
            Icon(icone, size: 19, color: AmColors.text),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  AppText(
                    rotulo,
                    style: const TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                      color: AmColors.text,
                    ),
                  ),
                  if (detalhe != null)
                    Padding(
                      padding: const EdgeInsets.only(top: 2),
                      child: AppText(
                        detalhe!,
                        style: TextStyle(
                          fontSize: 10,
                          color: AmColors.muted.withValues(alpha: .7),
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

class _Interruptor extends StatelessWidget {
  const _Interruptor({
    required this.rotulo,
    required this.icone,
    required this.ligado,
    required this.aoTocar,
  });

  final String rotulo;
  final IconData icone;
  final bool ligado;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    toggled: ligado,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 10),
        child: Row(
          children: [
            Icon(
              icone,
              size: 19,
              color: ligado ? AmColors.accent : AmColors.text,
            ),
            const SizedBox(width: 12),
            Expanded(
              child: AppText(
                rotulo,
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: ligado ? AmColors.accent : AmColors.text,
                ),
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

class _Titulo extends StatelessWidget {
  const _Titulo(this.texto);

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(4, 12, 0, 6),
    child: AppText(texto,
      style: const TextStyle(
        fontSize: 11,
        fontWeight: FontWeight.w700,
        color: AmColors.muted,
      ),
    ),
  );
}

class _Aviso extends StatelessWidget {
  const _Aviso(this.texto);

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(6, 2, 0, 6),
    child: AppText(texto,
      style: const TextStyle(fontSize: 11, color: AmColors.muted, height: 1.4),
    ),
  );
}
