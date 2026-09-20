import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';

/// UM REPOSITORIO QUE NAO TOCA NO DISCO.
///
/// Toda tela de edicao montada por inteiro grava sozinha: desde que a
/// ponte `editorController -> projectsController` voltou, cada mutacao
/// agenda uma escrita. Num teste isso vira `MissingPluginException` no
/// `path_provider` — que aparece la no `dispose` do container, longe do
/// que quebrou, e manda procurar no lugar errado.
///
/// Quem quer cobrar a GRAVACAO usa [gravados]; quem so quer montar a
/// tela ignora, e o teste deixa de depender de plugin nenhum.
class RepositorioSemDisco extends ProjectRepository {
  RepositorioSemDisco() : super();

  final List<VideoProject> gravados = [];

  @override
  Future<List<VideoProject>> loadAll() async => const [];

  @override
  Future<void> save(VideoProject project) async => gravados.add(project);

  @override
  Future<void> delete(String id) async {}

  @override
  Future<void> flush() async {}
}
