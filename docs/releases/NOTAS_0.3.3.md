# Aurea 0.3.3 BETA (versionCode 2200) — a interface nova

Android: AAB (arm64-v8a + armeabi-v7a). iOS: IPA sem assinatura (CI do GitHub).

Agradecimento especial a **hanszx_**, que colaborou no desenho da nova interface.

## Google Play (pt-BR)

```
Novidades da 0.3.3 BETA:
• Interface nova no editor inteiro: timeline, painéis, efeitos, cor e exportação
• Nova barra de adicionar, com o "+" que abre todas as opções
• Uma camada por linha na timeline, com seleção múltipla ao segurar
• Cor e preenchimento com degradê, conta-gotas e paleta própria
• Borda e sombra, 5 modos de mesclagem novos e inclinação
• Texto ao vivo, navegador de fontes, desenho livre e edição de pontos
• Ícones mais nítidos e Home renovada
Na 0.3.4: o novo sistema de plugins, com painéis, abas e scripts!
Obrigado a hanszx_ pela colaboração na interface!
```

## Google Play (en-US)

```
New in 0.3.3 BETA:
• A new interface across the editor: timeline, panels, effects, color and export
• A new add bar, with a "+" that opens every option
• One layer per row on the timeline, with multi-select by holding
• Color & fill with gradients, eyedropper and your own palette
• Border & shadow, 5 new blend modes and skew
• Live text, font browser, freehand drawing and point editing
• Crisper icons and a refreshed Home
Coming in 0.3.4: the new plugin system, with panels, tabs and scripts!
Thanks to hanszx_ for collaborating on the interface!
```

## Versão completa

### Editor
- **Barras de cima** com 3 estados: projeto (sair, título, ⚙ Ajustes, exportar), camada
  (voltar, nome, 3D, desfoque de movimento, vínculo, apagar, ⋮) e seleção múltipla
  (agrupar, agrupar e mascarar, agrupar e excluir, apagar, alinhar e distribuir).
- **Transporte** com desfazer, refazer, anterior, play, próximo, marcador, Copiar/Colar e o
  painel de modos da prévia (pixels reais, grade, destaque da camada, zoom de 1% a 6400%).
- **Copiar/Colar** num menu só, com **Colar estilo** por parte (cor, borda e sombra,
  mesclagem, transformação, texto, volume, efeitos, velocidade) num único desfazer.
- **Menus presos ao botão** no lugar das folhas que subiam da base.
- **Subir e descer a timeline** arrastando a divisa, como antes; toque duplo volta ao automático.

### Barra de adicionar
- Pílula com **Forma, Mídia, Áudio, Texto e Elemento**; a categoria aberta fica destacada.
- O **"+"** abre a grade com Forma, Mídia, Áudio, Texto, Preset, Elemento, 3D e Desenho.
- Painel de adicionar encaixado acima da barra: formas em páginas, faixas de mídia e áudio
  com recentes, Elemento em grade (Câmera, Grupo vazio, Projeto, Vetor) e presets em faixa.
- **Navegador de mídia** em tela cheia: várias escolhas numeradas, duração da imagem, corte do
  vídeo antes de importar, e duas formas de colocar ("mesmo início" ou "em sequência"), tudo
  num desfazer. **Navegador de áudio** com pré-escuta.

### Timeline
- **Uma camada por linha**, a de cima primeiro. Projetos antigos abrem igual.
- Cabeçalho com olho, miniatura e cor da etiqueta; **segurar o cabeçalho** entra na seleção
  múltipla e **arrastar** soma camadas.
- Clipes com degradê por tipo, miniaturas, forma de onda e keyframes do parâmetro em edição
  em destaque.
- **Levantar e mover** o clipe com auto-rolagem; **reordenar** pela linha; **modo foco** com
  alças de corte e **alças de velocidade** (esticar muda a velocidade).
- **Marcadores de batida** como antes, com **nome em cada um**; as cores dos marcadores agora
  aparecem certas.

### Painel da camada
- Botões por tipo de camada numa grade fixa e uma fileira de tempo que muda com o cabeçote
  (cortar início, dividir, cortar fim ou mover e esticar).
- **Velocidade em todos os tipos de camada**; grupos com velocidade embutida.
- **Volume** direto no botão Áudio e painel Som com reforço, balanço e suavizar.
- Moldura comum: voltar, ◆ (segurar apaga todos os keyframes), curva e ⋯.
- **Editor de curva** novo, com presets por grupo, copiar e colar curva.

### Aparência
- **Cor e preenchimento**: nenhum, cor ou **degradê** (linear, radial, angular) com alças no
  palco; a cor do preenchimento agora é animável.
- **Seletor de cor** embutido: amostras, anel de matiz com ímã, alfa, código copiável,
  **conta-gotas ao vivo** na prévia, tela cheia com Roda e RGB e paleta reordenável.
- **Borda e sombra**: traço, várias bordas (dentro, centro, fora) e sombra com deslocamento.
- **Mesclagem e opacidade** numa página só, com **5 modos novos**: cor mais escura, cor mais
  clara, luz linear, luz pontual e sobreposição suave.
- **Mover e transformar**: trackpads de mover e de pivô com ímã nas outras camadas, roda de
  rotação, escala em px com cadeado e **inclinação** (agora aplicada também no export).
- **Opções da câmera**: ângulo de visão, zoom, lentes prontas e desfoque de foco.

### Texto, desenho e pontos
- **Modo texto**: o texto muda a cada letra, com barra de estilo (alinhamento, fonte, tamanho,
  cor) sobre o teclado.
- **Navegador de fontes** em tela cheia com favoritas, recentes, mais usadas e filtros por
  categoria e escrita; mini navegador dentro do painel.
- **Camada de desenho**: caneta, pincel com pressão, preencher e borracha.
- **Editar pontos** em três colunas: tipos de ponto, adicionar e remover, furos e travas das
  alças. As regras de ponto agora ficam no motor (iguais no Android e no iOS).
- Toque duplo na camada abre o editor dela.

### Efeitos
- Lista mais compacta, controles que abrem a partir da linha, réguas com arremesso e encaixe,
  ativação de parâmetro e **arrasto em qualquer lugar da prévia** para ajustar.
- **Duplicar efeito**, **Colar configurações** e copiar todos os efeitos.
- Navegador com recomendados, recentes e favoritos, categorias, busca e detalhe com variações.

### Vem na 0.3.4: o novo sistema de plugins
- Plugins vão poder criar **painéis e abas novas** na interface, rodar **scripts** com uma API
  de projeto, camadas, keyframes e efeitos no estilo dos scripts do After Effects, e usar
  **APIs externas** que a pessoa autorizar na instalação. Os plugins de efeito atuais
  continuam funcionando como hoje.

### Home
- Barra de cima nova, dock com Início, Perfil, criar, Projetos e Presets, e **gaveta de
  ajustes**.
- Projetos em cartões com duração, proporção, resolução, tamanho e fps; seleção ao segurar,
  menu rápido e ordenação com cabeçalho de data.
- Folha **Novo projeto** refeita.

### Exportação
- Página **Exportar e compartilhar** em lista, com estimativa de tamanho e espaço livre,
  ajustes em sub-páginas, qualidade de 20 a 100%, fps Baixo/Reduzido/Cheio e "Arquivo do
  projeto". Progresso com tempo restante e página de resultado com Salvar e Compartilhar.
  Retomada, segundo plano e fundo continuam.

### Visual
- Paleta AUREA no app inteiro e **ícones mais grossos**.

## Formato do projeto
- Timeline na versão 51 (decorações de borda e sombra, degradê do preenchimento) e documento
  vetorial na versão 3 (furos, pressão, borracha). Projetos antigos abrem igual; um projeto
  salvo na 0.3.3 não abre em versões anteriores.

## Conhecido nesta BETA
- Câmera paralela e névoa por câmera ainda não existem (aviso no painel da câmera).
- Mídia como preenchimento ainda não existe.
