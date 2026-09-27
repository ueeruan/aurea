# Home, efeitos e Environment Texture — 26/09/2026

Implementação local sobre `codex/build-2112-timeline`. Este documento descreve as mudanças desta revisão; não declara uma nova versão distribuída.

## Interface

- Home nativa Android/iOS: criação no círculo central inferior, Projetos à esquerda, Presets à direita, menu Configurações/Sobre/Licenças e importação de mídia nas extremidades.
- Áreas de toque do dock de pelo menos 48 dp no Android; teste com largura de 320 dp e fonte a 150%. O dock ocupa espaço próprio e não cobre a lista.
- A comunidade usa o serviço existente de presets de legendas, com busca, downloads e erros/repetição. Não há um novo serviço geral de presets de efeitos. No iOS, componentes baixados ficam nas bibliotecas correspondentes de texto, efeitos, animação e legendas.
- Skill instalada e consultada: `C:/Users/Ruan/.codex/skills/ui-ux-pro-max/SKILL.md`, de `nextlevelbuilder/ui-ux-pro-max-skill`.
- Captura real: `build/prompt03/home-redesign.png`.
- Texto e texto 3D têm ações separadas de **Editar texto** e **Opções de texto**. Adicionar texto abre o editor de conteúdo e o teclado; o conteúdo inicial vem selecionado. A edição usa uma janela própria, com Concluir e Cancelar, sem comprimir a timeline. Concluir altera apenas as palavras da camada original; fonte, geometria, materiais e animações permanecem nos painéis anteriores. A seleção de trechos para rich text continua nas opções, sem abrir teclado.

## Efeitos e texto 3D

- Shake: trajetória contínua e determinística em segundos; controles de intensidade, velocidade, rotação, zoom, estilos Normal/Twitchy/Jumpy, fase, bordas e motion blur opcional. Um passe de GPU, até oito amostras quando o desfoque está ativo.
- Light Sweep: centro XY, largura em pixels, direção, três perfis de feixe, realce das bordas de alfa, espessura e recepção Add/Composite/Cutout. Não se move sozinho sem animação do usuário.
- Glitchify: controles adicionais de quantidade/velocidade, transformação, separação e escala vertical de canais, centro, módulos liga/desliga, transição, dither, repetição de bordas, ordenação vertical e pixel streak. Tempo consistente em 24/30/60 fps e resultado determinístico ao voltar na timeline.
- Texto 3D: Rotação abre os controles das letras, com opção separada para o objeto completo. X/Y/Z usam o pivô de cada letra. Há atraso em quadros e variação por letra; sem keyframes, o texto permanece parado.
- Purple crystals: preset de fragmentos roxos com facetas e halo, distribuídos em um volume 3D. A forma usa billboards, não malhas PBR individuais. Corrigida a coordenada local para a rotação das partículas não cancelar sua orientação.
- Render real do preset: `build/prompt03/purple-crystals.png`.
- Projetos antigos recebem os novos parâmetros que faltam nos efeitos ao abrir, mantendo os valores, curvas e keyframes já salvos. Isso permite editar os controles adicionados nesta revisão sem remover/recriar o efeito.

O arquivo Glitchify foi aberto com a senha fornecida e inspecionado para identificar módulos/controles. Nenhum binário proprietário foi incorporado ao aplicativo ou executado. As implementações são nativas do Aurea; igualdade visual com AE/Glitchify não foi validada por render comparativo. A simulação completa dos codecs JPEG/WebP do Glitchify original não foi implementada. IDs de efeito e índices antigos foram preservados, mas o novo Shake e a nova largura do Light Sweep mudam a aparência de projetos antigos e exigem revisão visual.

Referências: [S_Shake](https://borisfx.com/documentation/sapphire/ae/shake/), [manual Cycore, CC Light Sweep](https://www.cycorefx.com/downloads/cfx_hd_std/CycoreFX%20HD%201.8.1%20Manual.pdf), [Glitchify](https://elementsupply.co/products/glitchify).

## Environment Texture

- A câmera passa a oferecer o painel de ambiente. HDR/HDRI com conteúdo Radiance e panoramas JPG/PNG são copiados para o armazenamento do aplicativo e associados à composição.
- Opção de fundo 360°, rotação e intensidade. A orientação da câmera muda a direção vista; sua translação não desloca o panorama, pois ele representa um ambiente distante.
- O fundo é uma única camada inferior, inclusive em cenas contendo apenas uma câmera. Não se repete em cada grupo 3D. A mesma imagem participa da iluminação/reflexos existentes.
- Salvamento, reabertura e desfazer usam o estado de ambiente já serializado do projeto. O ambiente é da composição, compartilhado pelas câmeras, sem uma textura diferente por câmera.
- Limite atual de importação: 8 megapixels. Radiance/JPG/PNG são decodificados pelo conteúdo; `.exr` não está incluído. O fundo usa cubemap de até 512 px por face; a convolução da iluminação continua em 128 px. Arquivos acima do limite retornam erro antes da alocação de pixels float.

## Piscada durante o arraste

O compositor podia apresentar um quadro sem uma camada de vídeo cujo decoder ainda não havia entregue uma imagem. Durante seek parado, mantém agora a imagem anterior por até 250 ms enquanto aguarda esse recurso. O limite impede que uma mídia ausente congele indefinidamente as outras edições; playback e export mantêm seus caminhos próprios.

O teste de imagem em plano 3D com câmera e rotação animada passou em idas e voltas pelo tempo. O desaparecimento específico relatado no print não foi reproduzido com o projeto original; não está declarado resolvido integralmente.

## Validação

- Android: compilação do pacote isolado `uiTest`, 124 testes unitários e 7 testes no emulador Android 12/API 31, zero falhas. Os dois testes de texto verificam abertura real do teclado, edição/reabertura, cancelamento e preservação de tamanho, profundidade e material. Log: `build/prompt03/text-content-android.log`. O pacote principal e seus projetos não foram usados nesses testes.
- Motor: 19 testes focados, incluindo Vulkan real do computador, para os três efeitos, texto 3D, Particle World, panoramas, HDRI, câmera, seeks, limites de camada e migração dos controles antigos. Logs em `build/prompt03/test-*.log`.
- iOS: verificações estáticas de chamadas Swift/bridge, tipos dos parâmetros e recursos compartilhados aprovadas. Não houve compilação Xcode nem execução nativa no iPhone nesta revisão.
- Nenhum APK de distribuição/IPA novo foi gerado ou publicado nesta revisão. O A51 5G real e a equivalência visual dos plug-ins permanecem fora da evidência obtida aqui.
