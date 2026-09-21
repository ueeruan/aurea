import '../shell/contrato.dart';
import 'ambiente.dart';
import 'animacao3d.dart';
import 'animar.dart';
import 'audio.dart';
import 'borda_sombra.dart';
import 'camera.dart';
import 'cena3d.dart';
import 'clonar.dart';
import 'cor.dart';
import 'efeitos.dart';
import 'estilo.dart';
import 'fonte.dart';
import 'forma.dart';
import 'grupo.dart';
import 'legendas.dart';
import 'luz.dart';
import 'mascara.dart';
import 'material.dart';
import 'particulas.dart';
import 'pontos.dart';
import 'propriedades.dart';
import 'rastrear.dart';
import 'tempo.dart';
import 'texto.dart';
import 'texto3d.dart';
import 'transformar.dart';
import 'velocidade.dart';

/// O MAPA PainelId -> PAINEL. Um arquivo por painel: quem reescreve um
/// painel troca o arquivo dele e, se o construtor mudar de nome, a linha
/// dele aqui. `test/ui/editor_shell_test.dart` abre todos.
final Map<PainelId, ConstrutorDePainel> paineisRegistrados = {
  PainelId.transformar: (_, id) => PainelTransformar(layerId: id),
  PainelId.efeitos: (_, id) => PainelEfeitos(layerId: id),
  PainelId.cor: (_, id) => PainelCor(layerId: id),
  PainelId.tempo: (_, id) => PainelTempo(layerId: id),
  PainelId.audio: (_, id) => PainelAudio(layerId: id),
  PainelId.mascara: (_, id) => PainelMascara(layerId: id),
  PainelId.texto: (_, id) => PainelTexto(layerId: id),
  PainelId.fonte: (_, id) => PainelFonte(layerId: id),
  PainelId.estilo: (_, id) => PainelEstilo(layerId: id),
  PainelId.animar: (_, id) => PainelAnimar(layerId: id),
  PainelId.texto3d: (_, id) => PainelTexto3D(layerId: id),
  PainelId.material: (_, id) => PainelMaterial(layerId: id),
  PainelId.luz: (_, id) => PainelLuz(layerId: id),
  PainelId.ambiente: (_, id) => PainelAmbiente(layerId: id),
  PainelId.animacao3d: (_, id) => PainelAnimacao3D(layerId: id),
  PainelId.propriedades: (_, id) => PainelPropriedades(layerId: id),
  PainelId.camera: (_, id) => PainelCamera(layerId: id),
  PainelId.velocidade: (_, id) => PainelVelocidade(layerId: id),
  PainelId.bordaSombra: (_, id) => PainelBordaSombra(layerId: id),
  PainelId.forma: (_, id) => PainelForma(layerId: id),
  PainelId.pontos: (_, id) => PainelPontos(layerId: id),
  PainelId.clonar: (_, id) => PainelClonar(layerId: id),
  PainelId.legendas: (_, id) => PainelLegendas(layerId: id),
  PainelId.particulas: (_, id) => PainelParticulas(layerId: id),
  PainelId.rastrear: (_, id) => PainelRastrear(layerId: id),
  PainelId.cena3d: (_, id) => PainelCena3D(layerId: id),
  PainelId.grupo: (_, id) => PainelGrupo(layerId: id),
};
