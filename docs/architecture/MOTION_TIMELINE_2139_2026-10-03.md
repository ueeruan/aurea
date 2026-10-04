# AUREA 2139 — efeitos, timeline e preview

## Escopo implementado

O aviso de atualização dentro do app foi retirado a pedido do usuário. A versão
continua sendo distribuída manualmente. Nenhum APK foi publicado no Drive.

### Efeitos no motor compartilhado

Foram acrescentadas 23 implementações próprias, acessíveis pelo catálogo comum
do Android e do iOS. Com Chroma Key avançado e Luma Key já existentes, a lista
solicitada fica disponível:

- Blink, Flicker, Pulse Size, Random Displacement, Random Jitter, Swing e Spin.
- Stretch Axis, Scale Assist, Raster Transform e Offset.
- Chroma Key básico e avançado, Color Luma Key, Luma Key, Solid Matte e Matte Choker.
- Repeat (Basic), Linear Repeat, Grid Repeat, Radial Repeat, Repeat Along Path e Scatter Repeat.
- Squeeze e Fish Eye.

As animações usam o tempo local da camada. As variações aleatórias dependem de
uma semente, permitindo voltar ao mesmo quadro e obter o mesmo resultado.
Os repetidores usam até 64 cópias por efeito e uma única chamada de desenho.
Repeat Along Path usa a camada vetorial imediatamente abaixo como guia: caminho
vetorial, retângulo, elipse ou máscara. Sem guia compatível, preserva a imagem.
Raster Transform reamostra a imagem na sua posição na pilha de efeitos.

Os efeitos foram escritos a partir de comportamento e documentação pública.
Não houve incorporação de código, shader ou asset extraído de outro aplicativo.
Não há homologação de equivalência visual exata com o After Effects. Vários nomes
da lista não identificam um efeito nativo único do AE; Matte Choker e Fish Eye,
por exemplo, ainda precisam de comparação com renders de referência.

### Enquadramento e efeitos personalizados

- Ajustar ao projeto preserva toda a imagem; preencher o projeto cobre o quadro.
  O cálculo de escala, rotação e compensação da âncora fica em C++.
  Transformações de uma camada pai não entram nesse cálculo de enquadramento.
- Criar efeito tem modos normal e avançado nas duas interfaces. O normal salva
  a combinação da pilha; o avançado abre os parâmetros adicionais e salva o efeito
  escolhido. Ambos usam o formato de preset existente, inclusive em imagens.

### Timeline e interface

- Aparar à esquerda/direita aceita clipes previamente estendidos além da duração
  do arquivo. Encurtar esse intervalo não exige que a borda que foi mantida volte
  para dentro da mídia. Extensões continuam sujeitas aos limites da fonte.
- O arraste da borda inicial respeita o comportamento magnético da própria camada,
  além do modo magnético global. Isso evita acumular o deslocamento a cada evento.
- Clipes bloqueados não iniciam o arraste de corte no Android.
- Os botões de corte receberam rótulos e retorno quando o motor rejeita a operação.
- O filtro permite alternar entre todas as camadas e a seleção.
- O layout reserva espaço para timeline e painéis em telas menores e considera
  o tamanho da fonte. Em paisagem, usa painel lateral a partir de 520 dp/pt.
- A barra de transporte move ações para o menu em larguras pequenas; os alvos
  principais de toque mantêm pelo menos 44 dp/pt. O catálogo usa colunas adaptáveis.
- As contas da timeline protegem FPS inválido, zoom não finito e limites inteiros.

### Buffer do preview

O decoder prepara aproximadamente 180 ms à frente, com teto de oito quadros
adicionais e respeito ao orçamento de memória e aos buffers do codec. O preview
pausado também prepara essa janela, exceto sob pressão térmica severa. Scrub
mantém a prioridade do quadro solicitado.

Foi corrigida uma falha de descarte: com sete posições no cache, a regra anterior
retinha um quadro passado e rejeitava o sexto futuro. O decoder precisava voltar
para buscar novamente um quadro que acabara de decodificar. O novo peso preserva
a janela na direção da reprodução.

Isto é buffering de vídeo decodificado. Não é um cache completo de frames com
todos os efeitos renderizados. Projetos pesados continuam dependendo da GPU.

### Piscadas e travadas do preview no Android

A auditoria encontrou um caminho de falha sob pressão de buffers: o scheduler
tratava `BudgetExceeded` como erro do decoder, invalidava sua posição e fazia
outra busca. O conversor Android também descartava a imagem cujo alvo RGBA
continuava ocupado pela GPU. Isso pode provocar trabalho repetido e perda de
quadros em aparelhos mais lentos.

- O motor compartilhado repete a entrega após uma pausa curta, conservando a
  posição do decoder. Pressão temporária não consome o limite de tentativas
  reservado a falhas reais de mídia.
- O adaptador MediaCodec guarda a imagem pendente, ou sua solicitação ao
  ImageReader, até conseguir entregá-la. PTS, duração e fim do vídeo são mantidos.
  Busca, suspensão e destruição liberam essa imagem.
- A correção comum atende Android e iOS. A retenção do AImage é específica do
  adaptador Android; o decoder iOS não retorna esse erro de pressão de buffers.

O usuário relatou piscadas e lag no Android, sem modelo específico. A correção
trata os defeitos encontrados no código; ainda requer reprodução e validação no
aparelho afetado. O emulador usa conversão por CPU e não valida o caminho GL de
um driver Qualcomm, Mali ou outro fabricante.

A documentação do [ImageReader no Android NDK](https://developer.android.com/ndk/reference/group/media)
descreve o limite de imagens simultaneamente adquiridas e a necessidade de
devolvê-las ao leitor para liberar capacidade.

### Contorno do texto

- Todos os contornos são desenhados antes dos preenchimentos. Assim, o contorno
  espesso da próxima letra não apaga parte da anterior em texto com kerning ou
  espaçamento pequeno.
  A ordem corresponde à opção “All Fills Over All Strokes” descrita pela
  [Adobe](https://helpx.adobe.com/lt/after-effects/desktop/add-text/formatting-characters-and-paragraphs/formatting-characters-character-panel.html).
- O limite do contorno considera o tamanho real de cada glifo. Trechos pequenos
  e texto reduzido para caber na caixa não saturam toda a célula do atlas,
  causando retângulos coloridos.
- A margem da camada inclui o contorno dos animadores de texto, evitando corte
  nas bordas. A implementação está no renderer C++ compartilhado.

O print recebido mostra o relato, mas não permite identificar a fonte e o
projeto original. Foram preparados casos de regressão para os defeitos acima;
a comparação visual com esse projeto continua pendente.

## Compilação e verificações

- C++ do host: compilado com MSVC e shaders SPIR-V.
- Android: APKs release arm64-v8a e armeabi-v7a gerados, versão 2139,
  pacote `com.aurea.aurea`, assinaturas v2/v3 verificadas com o certificado existente.
- iOS: implementação Swift/ObjC++ atualizada. Auditorias estáticas de API, projeto,
  recursos, tipos dos parâmetros e regra de layout passaram. Os quatro novos
  shaders foram traduzidos para MSL e tiveram os bindings conferidos. O app
  para iPhone compilou com Xcode; os 140 shaders passaram pelo compilador/linker
  Metal da Apple. O IPA sem assinatura foi baixado e validado localmente:
  versão 2139, bundle `com.aurea.aurea`, executável ARM64 e integridade do ZIP.
- O host local é Windows; a compilação nativa iOS usa o workflow macOS
  `build-ipa.yml` do repositório. O snapshot
  `b502eebcb7b66d485b71922f77499886061b4979` foi preparado sem alterar a branch
  ou o índice de trabalho do usuário. Após autorização explícita, foi enviado
  para `codex/build-2139-effects-preview`. O pacote foi gerado no
  [run 37146291206](https://github.com/ueeruan/aurea/actions/runs/37146291206).
  O app do simulador compilou com Xcode e as quatro capturas solicitadas passaram.

Resultados por filtro (alguns filtros podem selecionar o mesmo teste):

| Área | Testes | Verificações | Falhas |
| --- | ---: | ---: | ---: |
| Novos efeitos, enquadramento e buffer | 11 | 282 | 0 |
| Edição de clipes | 8 | 1398 | 0 |
| Modo de edição | 3 | 38 | 0 |
| Edição magnética | 11 | 148 | 0 |
| Organização temporal | 7 | 387 | 0 |
| Timeline | 24 | 1705 | 0 |
| Corrupção, salvamento e estabilidade | 27 | 4519 | 0 |
| Cache de quadros decodificados | 16 | 91 | 0 |
| Fontes de vídeo | 24 | 944 | 0 |
| Presets | 20 | 1021 | 0 |
| Gerenciamento dos decoders | 2 | 26 | 0 |
| Reprodução | 24 | 680 | 0 |
| Regressões de contorno do texto | 2 | 19 | 0 |
| Centralização do texto com contorno | 1 | 18 | 0 |
| Transformações de texto | 8 | 118 | 0 |
| Texto na GPU e material 3D | 3 | 51 | 0 |
| Fundo e sombra do texto | 1 | 9 | 0 |

Os testes dos efeitos fizeram leitura real da GPU Vulkan no host: repetição,
recorte, lentes, identidade quando desativados e consistência entre preview e
exportação. O teste dos presets passou a medir a entrada antes de sua saída
programada e também verifica a ausência do texto após a saída.

No teste sintético do buffer, a reprodução para a frente entregou 55/60 quadros
no prazo (91,7%), com uma busca. Reverso e scrub ficaram entre 35% e 42% nesse
cenário; o resultado não comprova reprodução sem travadas em todo aparelho.

## Execução nativa e aparelhos

- Android: 347 testes JVM passaram. Quatro testes de interface passaram no emulador
  API 35, com largura de 320 dp, em tela alta e em área de 480 dp de altura com
  fonte a 130%. Importam vídeo, estendem a camada, alternam o filtro, cortam à
  esquerda/direita pelos botões e conferem desfazer.
- Os outros dois testes verificam pixels do contorno espesso e amostram a prévia
  real durante reprodução e quatro buscas alternadas no vídeo. São casos
  controlados, não uma medição de todos os frames nem uma validação em aparelho físico.
- As capturas foram revistas visualmente. O rótulo do filtro foi encurtado para
  evitar corte de texto com fonte ampliada.
- iOS: app nativo executado no simulador iPhone 16. As telas `layer-dock`,
  `transform`, `effects` e `text-2d` foram capturadas e revistas. Os testes de
  gestos e desfazer passaram no workflow macOS: 48 testes, zero falhas, em
  1267 segundos. O workflow terminou com sucesso. Relatório local:
  `build/ios-2139-gestures.log`.
- Não houve execução em Android físico nem iPhone físico nesta sessão.
  Essas etapas permanecem necessárias antes de afirmar estabilidade nos aparelhos.

## Referências de comportamento

- [Adobe — Keying effects](https://helpx.adobe.com/after-effects/desktop/animate-in-after-effects/work-with-keying-effects/keying-effects.html).
- [Adobe — Distort effects](https://helpx.adobe.com/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/distort-effects.html).
- [Adobe — Matte effects](https://helpx.adobe.com/br/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/matte-effects.html).
- [Adobe — animação de shape layers](https://www.adobe.com/learn/after-effects/web/add-animation-to-shape-layers?ntd=1).
- Pesquisa no YouTube: [Matte Choker, Jake In Motion](https://www.youtube.com/watch?v=rGmc9rrf5pc) e
  [Optics Compensation, AEJuice](https://www.youtube.com/watch?v=rSj5cadvAjQ).
  Foram consultados os resultados e descrições disponíveis; não foi realizada
  comparação quadro a quadro com os vídeos.

Logs e capturas desta validação estão em `build/`: `final-*.log`,
`audit-*.log`, `stability-audit.log`, `motion-extras-*.log` e
`aurea-timeline-{short,tall}.png`.
