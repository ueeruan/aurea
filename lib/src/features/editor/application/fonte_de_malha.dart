import 'dart:ui' show Offset;

import '../domain/element3d.dart';
import '../domain/model_asset3d.dart';
import '../domain/scene3d.dart';
import 'registro_de_travadas.dart';
import 'perfil3d.dart';

/// A MALHA DE UM NO, PRONTA PARA VIRAR BUFFER DE GPU.
///
/// Vertices, normais, UVs e o material de cada face — o que o montador
/// de geometria precisa. E, junto, o suficiente para reconhecer que
/// nada mudou desde o quadro anterior.
class MalhaDoNo {
  MalhaDoNo({
    required this.malha,
    required this.normais,
    required this.uvs,
    required this.materiais,
    required this.assinatura,
    this.dinamica = false,
    this.quadroDoModelo,
    this.materialDoNo,
    this.usouMateriaisDoModelo = false,
  });

  final Element3DMesh malha;
  final List<Vec3?>? normais;
  final List<Offset?>? uvs;
  final List<Material3D> materiais;

  /// Muda quando o no precisa ser RECONSTRUIDO na GPU (outra malha,
  /// outro material, outro modelo).
  final String assinatura;

  /// A malha muda a cada quadro (modelo animado): os buffers da GPU
  /// nascem atualizaveis.
  final bool dinamica;

  /// O quadro do modelo de onde esta malha saiu (nulo em primitiva). E o
  /// que deixa reconhecer, pela identidade, que nada mudou.
  final ModelFrame3D? quadroDoModelo;
  final Material3D? materialDoNo;
  final bool usouMateriaisDoModelo;
}

/// DE ONDE VEM A MALHA DE CADA NO, COM MEMORIA.
///
/// Montar a malha de um no aloca duas listas do tamanho do modelo: uma
/// normal por vertice e um material por face. A bancada mediu, numa cena
/// de vinte objetos medios, QUINZE MIL materiais e OITO MIL normais por
/// quadro — jogados fora no quadro seguinte, so para concluir que nada
/// tinha mudado. Isso nao aparece na media do tempo de quadro; aparece
/// como engasgo, quando o coletor de lixo passa.
///
/// Esta classe guarda a ultima malha de cada no e so refaz quando o que
/// a define muda: outra malha escolhida, outro material, outro quadro do
/// modelo. Um objeto parado passa a custar uma comparacao de identidade.
class CacheDeMalhas {
  final Map<String, MalhaDoNo> _porNo = {};

  int get tamanho => _porNo.length;

  /// A malha do no neste instante. [lodDaReceita] e o LOD que a receita
  /// de qualidade escolheu, usado quando o no esta em automatico.
  MalhaDoNo? doNo(
    SceneNode node,
    Duration t, {
    required Element3DMesh? Function(SceneNode node) lodDaReceita,
    required String Function(Material3D m) assinaturaDoMaterial,
    Duration? fimDaCamada,
  }) {
    final asset = node.modelAsset;
    if (asset != null) {
      final motion = node.modelMotion;
      final animado =
          asset.temAnimacaoDeTexto ||
          motion.keys.isNotEmpty ||
          (motion.clip >= 0 && motion.clip < asset.clips.length);
      // A AVALIACAO DO MODELO e o passo mais caro do quadro quando o
      // cache dela nao pega: ela refaz a pose sobre TODOS os vertices,
      // em Dart, no fio da interface.
      final frame = RegistroDeTravadas.marcando(
        'avaliando o modelo importado',
        () => Perfil3D.fase(
          'modelo.avaliar',
          () => asset.evaluate(t, motion, fimDaCamada: fimDaCamada),
        ),
      );
      // O MESMO QUADRO DO MODELO: nada a refazer. Modelo parado devolve
      // sempre o mesmo objeto (o `evaluate` guarda o ultimo).
      final guardada = _porNo[node.id];
      if (guardada != null &&
          identical(guardada.quadroDoModelo, frame) &&
          guardada.materialDoNo == node.material &&
          guardada.usouMateriaisDoModelo == node.useModelMaterials) {
        Perfil3D.contar('malha.reaproveitada');
        return guardada;
      }
      final List<Material3D> materiais;
      if (node.useModelMaterials) {
        materiais = frame.materials;
      } else {
        Perfil3D.contar('alocacao.materiais', frame.mesh.faces.length);
        materiais = List<Material3D>.filled(
          frame.mesh.faces.length,
          node.material,
        );
      }
      Perfil3D.contar('malha.refeita');
      return _porNo[node.id] = MalhaDoNo(
        quadroDoModelo: frame,
        materialDoNo: node.material,
        usouMateriaisDoModelo: node.useModelMaterials,
        malha: frame.mesh,
        normais: frame.normals,
        uvs: frame.uvs,
        materiais: materiais,
        dinamica: animado,
        // A ASSINATURA DECIDE SE O NO E RECONSTRUIDO NA GPU — e
        // reconstruir um modelo importado e destruir e resubir buffers
        // de dezenas de milhares de vertices, no fio da interface.
        //
        // Ela usava `identityHashCode(motion)`. Como o no e imutavel,
        // toda reconstrucao traz um `ModelMotion3D` NOVO com o mesmo
        // conteudo — objeto novo, hash de identidade novo, assinatura
        // nova, no inteiro refeito. E o mesmo defeito que ja estava no
        // cache do `evaluate` e que foi corrigido dando igualdade por
        // VALOR aquela classe; aqui ele tinha sobrado.
        //
        // Agora a assinatura pergunta pelo VALOR. Mudar de animacao
        // continua reconstruindo; reconstruir o widget, nao.
        assinatura:
            'm${identityHashCode(asset)}:${motion.hashCode}:'
            '${node.instances.isNotEmpty}:'
            '${node.useModelMaterials ? 'a' : assinaturaDoMaterial(node.material)}',
      );
    }
    // O LOD: a escolha do no, e no automatico a da receita. A assinatura
    // leva a identidade da malha, entao trocar de LOD refaz o no.
    final escolhida = switch (node.lod) {
      MeshLod3D.low => node.lowMesh ?? node.mediumMesh ?? node.mesh,
      MeshLod3D.medium => node.mediumMesh ?? node.mesh,
      MeshLod3D.high => node.mesh,
      MeshLod3D.auto => lodDaReceita(node),
    };
    final mesh = escolhida ?? element3DMesh(node.kind);
    // A MESMA MALHA E O MESMO MATERIAL: devolve o que ja estava pronto.
    final guardada = _porNo[node.id];
    if (guardada != null &&
        identical(guardada.malha, mesh) &&
        guardada.materialDoNo == node.material &&
        guardada.quadroDoModelo == null) {
      Perfil3D.contar('malha.reaproveitada');
      return guardada;
    }
    Perfil3D.contar('alocacao.normais', mesh.normals?.length ?? 0);
    Perfil3D.contar('alocacao.materiais', mesh.faces.length);
    Perfil3D.contar('malha.refeita');
    return _porNo[node.id] = MalhaDoNo(
      materialDoNo: node.material,
      malha: mesh,
      normais: mesh.normals
          ?.map((n) => Vec3(n[0], n[1], n[2]))
          .toList(growable: false),
      uvs: null,
      materiais: List<Material3D>.filled(mesh.faces.length, node.material),
      assinatura:
          'p${node.kind.index}:${identityHashCode(mesh)}:'
          '${node.instances.isNotEmpty}:${assinaturaDoMaterial(node.material)}',
    );
  }

  /// Esquece os nos que sairam da cena.
  void manterApenas(Set<String> ids) =>
      _porNo.removeWhere((id, _) => !ids.contains(id));

  void limpar() => _porNo.clear();
}
