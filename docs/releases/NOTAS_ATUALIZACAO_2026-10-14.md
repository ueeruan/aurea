# AUREA 0.1.9 (build 2185) — notas de atualização (14/10/2026)

Base: 0.1.8 BETA 3 (build 2184). Branch `claude/fix-export-vivo-2185`.
Versão 0.1.9, build 2185 (Android versionCode e iOS CURRENT_PROJECT_VERSION).

## Correções

- **Export preso em "Preparando" em celulares com GPU PowerVR** (relato do vivo V2026,
  Helio P35, Android 12). O problema não estava no encoder: os ANRs desse aparelho mostram o
  driver gráfico levando dezenas de segundos para preparar cada shader, e o export esperava
  por isso antes de começar — nem um PNG saía.
  - Os shaders passam a ser preparados só quando um efeito precisa deles.
  - O export reaproveita os shaders e pipelines que a prévia já preparou, em vez de
    prepará-los de novo a cada exportação.
  - A prévia para de desenhar assim que o export começa, sem disputar a GPU com ele.
- **Encoder de software:** já existia e continua igual (OpenH264 e Kvazaar dentro do app,
  mais o do próprio Android como último recurso).
- **Diagnóstico de fechamentos** agora lista os codificadores de vídeo do aparelho
  (hardware/software) e se o app roda em 32 ou 64 bits. Só no Android: o iOS não tem
  esse relatório.

## Novidades

- **Extrusão 3D com reflexo de ambiente**, como nos modelos 3D: novos controles
  *Reflexo do ambiente*, *Metal* e *Rugosidade*, usando o HDRI/estúdio da cena.
  O efeito novo já vem com o reflexo ligado; projetos salvos antes abrem sem reflexo,
  idênticos ao que eram.
- **3D Shatter de verdade:** com a camada na cena 3D, as peças viram objetos da cena —
  usam a câmera 3D, passam na frente e atrás dos modelos, recebem as luzes e caem para o
  chão do mundo (mesmo com a camada girada). Adicionar o efeito coloca a camada no 3D
  automaticamente (desfazer tira os dois juntos). Camada fora da cena 3D continua com o
  desenho de antes.
- **Seleção de camadas com ações no canto superior direito** (Android e iOS): ao segurar
  uma camada aparece a barra com *Agrupar*, *Agrupar e mascarar* (a camada de cima recorta
  as outras), *Agrupar e excluir* (a camada de cima abre um furo) e *Apagar*. O `›` mostra
  as outras ações (escolher uma, busca, vincular, desagrupar, duplicar, play). Mascarar e
  excluir exigem pelo menos duas camadas visuais; fora disso o botão fica apagado e o
  toque explica o motivo.
- **Segurar uma camada no palco** a coloca (ou tira) da seleção e liga o modo de escolher
  várias.
- **Timeline no modo de escolher várias:** arrastar o dedo na vertical pela coluna das
  pílulas soma cada camada por onde passa; as camadas escolhidas mostram um ✓ no lugar do
  olho, e tocar nele tira a camada da seleção. Segurar a pílula continua travando a
  camada.

## Validação (cada nível separado)

- **Motor (host, Windows/Vulkan):**
  - A suíte completa rodou antes das mudanças de seleção: 2082 testes. As falhas são as
    mesmas 28 que o BETA 3 sem estas mudanças já tem. Também falhou
    `PreRenderCache.CorruptFrameHeaderPayloadAndManifestNeverReturnPixelsOrComplete`, nas
    duas rodadas completas deste branch e não na da base. Ele passa sozinho, e esta branch
    não muda esse código; a causa provável é uma disputa de tempo no próprio teste, ainda
    não confirmada.
  - Testes novos passam: reflexo da extrusão; projeto antigo idêntico; Shatter como
    geometria da cena e gravidade do mundo; export reaproveitando o que a prévia já
    preparou; regra de agrupar e mascarar.
  - A regra nova da seleção foi testada só no grupo `EditorReference`; a suíte completa
    não foi rodada de novo depois dela.
- **Android:** Kotlin compila; 420 testes JVM passam. Os arquivos C++ alterados compilam
  também para ARM 32 bits (o build do tester).
- **iOS:** checagens estáticas do CI passam. A compilação nativa e os testes de interface
  só rodam no CI do GitHub e **não foram executados**.
- **Em aparelho:** nada foi validado em celular ainda (nem o vivo, nem iPhone).

## Próximos passos (da análise de UI de referência)

- Máscara de recorte por camada ("Criar/Liberar máscara de recorte" no menu da camada,
  com indicador na timeline).
- Seções recolhíveis nos parâmetros de efeito e alça no palco para parâmetros de ponto.
- Home: "Selecionar" no menu do projeto.
- Compilar um pipeline novo pela primeira vez, nesse driver PowerVR, ainda segura a
  thread de render por alguns segundos.
