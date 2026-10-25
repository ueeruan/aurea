# Aurea 0.3.1 (versionCode 2197)

## Google Play (pt-BR)

```
Novidades da 0.3.1:
• Copiar fundo: ajuste só uma parte do vídeo usando uma forma
• Plugins 3D que usam a câmera 3D e nulos 3D
• Aurora e Constelação seguem a câmera 3D como as partículas
• Modelo 3D com efeito respeita a profundidade da cena
• Cena 3D mais fácil; .jsx e .ffx do After Effects corrigidos
• Gráfico de keyframes volta a abrir em todos os efeitos
• Exportação mais estável e escolha do codificador
• Instalar plugin não fecha mais o app; prévia preta corrigida
```

## Google Play (en-US)

```
New in 0.3.1:
• Copy Background: adjust only part of the video with a shape
• 3D plugins that use the 3D camera and 3D nulls
• Aurora and Constellation follow the 3D camera like particles
• 3D models with effects respect the scene depth
• Easier 3D Scene; After Effects .jsx and .ffx import fixed
• The keyframe graph opens again for every effect
• More stable export and an encoder choice
• Installing a plugin no longer closes the app; black preview fixed
```

## Versão completa

### Novo: Copiar fundo
- Efeito novo em Utilidades. Coloque numa forma, imagem, vídeo, texto ou grupo: os
  efeitos colocados **depois** dele agem nas camadas de baixo, só dentro da forma.
  Serve para desfocar, colorir ou corrigir só uma parte do vídeo.
- **Janela**: alfa da camada, alfa invertido, luma ou luma invertida.
- **Misturar com a camada**: traz de volta a cor da própria camada por cima.
- Efeitos colocados **antes** dele moldam a janela (um desfoque antes suaviza a borda).
- Respeita a opacidade e o modo de mesclagem da camada.

### Novo: plugins 3D
- Novo tipo de plugin `"kind": "shader3d"`. Ele recebe a câmera da cena e até 4
  camadas ligadas: câmera 3D, nulo 3D ou luz.
- Parâmetro novo `"type": "layer"` com `"accept"`. A tela só oferece as camadas
  aceitas: só nulos, só câmeras etc.
- Funções prontas no shader: raio da câmera, posição e eixos da camada ligada e
  projeção para a tela.
- Exemplo `target-3d` e documentação em `docs/plugins` (seção "Plugins 3D").
- O gerenciador de plugins mostra "Shader 3D (câmera e nulos)".

### Keyframes e gráfico
- Marcar keyframes num efeito e tocar no keyframe volta a abrir o editor de gráfico.
  Antes isso falhava em vários efeitos no Android.
- As curvas de cor (Curvas e Curvas de matiz da Correção de cor) voltam a ser editáveis.

### Cena 3D
- Luz numa fileira só: Neutro, Escuro, Produto, Céu, Cromado ou Imagem HDR. Fica
  marcada só a opção em uso.
- Chão com três opções: Sem chão, Chão visível ou Só sombra. Escolher a luz não muda
  mais o chão.
- Nova seção **Avançado** com qualidade, tone map (6 tipos), exposição e bloom
  (força e limiar).
- Material e Luz abrem num painel de baixo, deixando a cena visível.
- Os botões da barra rolam quando não cabem.
- "Flare 3D" aparece no menu de adicionar.
- Cada toque desfaz em um passo só.
- Leitura de tela mais completa: "selecionado" e áreas de toque maiores.

### Aurora e Constelação 3D
- Numa camada 3D, a câmera não move mais a camada com o efeito. A camada fica
  parada na tela e o efeito é que vive no espaço 3D, como as partículas.
- Aurora: o céu gira com a câmera.
- Constelação: a rede de pontos fica no mundo e muda com a câmera, de perto, de
  longe ou de lado.

### Modelo 3D com efeito
- Um modelo 3D com efeito (Brilho, Inverter etc.) respeita a profundidade dos
  outros objetos 3D. O que está mais perto da câmera aparece na frente, seja
  qual for a ordem das camadas. Antes ele ficava sempre na frente ou atrás de tudo.

### Câmera 3D
- Câmeras novas afetam só as camadas 3D, como no After Effects.
- Para a câmera afetar também as camadas abaixo, ligue "Afetar camadas abaixo" na lente.

### Importar do After Effects (.jsx e .ffx)
- Importar um preset `.ffx` sem camada selecionada cria a camada, como no After
  Effects: um texto para preset de texto ou um sólido para os demais.
- A opção de importar também aparece no menu da camada e no menu da Edição.
- Scripts `.jsx` com painel (ScriptUI) rodam sem travar. Os botões do painel são
  ignorados e o relatório avisa.
- Scripts que usam `prototype`, `JSON.stringify`/`JSON.parse` e bibliotecas
  comuns funcionam.
- Arquivos em UTF-16 e Latin-1 (salvos no Windows) são lidos certo.
- `.jsxbin` é reconhecido e explicado.
- O relatório mostra "Importado em parte" quando o script para no meio e
  "Nada foi importado" quando ele roda sem mudar nada.
- Arquivos de até 64 MB.

### Exportação
- Não trava mais sempre no mesmo quadro. Quando um quadro do vídeo não pode ser lido,
  o app tenta de novo, usa o quadro vizinho do mesmo trecho e avisa na tela.
- Clipes 1080p60 e vídeos cortados voltam a exportar.
- Exportar → Avançado → **Codificador** tem três opções:
  - Automático;
  - Compatível: H.264 básico do aparelho;
  - Software: feito pelo processador, mais lento.
- Menos fechamentos em celulares de até 4 GB e no iPhone 11/XR.
- Se o app fechar no meio de uma exportação, a próxima começa num modo mais leve
  e avisa.
- Sequências de imagens voltam a exportar.
- Se uma exportação a 200 qps falhar, o app tenta de novo a 60 qps, mantendo a resolução.
- O motion blur exportado fica igual ao da prévia, sem efeito fantasma em projetos antigos.

### Correções
- Instalar um plugin não fecha mais o app no Android.
- Prévia e clipes pretos e play que parava foram corrigidos.
- Filtros e Ajustar não seguram mais a prévia durante o play.
- Em aparelhos com Vulkan problemático (como o Huawei P30 lite), o app usa OpenGL ES.
- Várias câmeras no mesmo projeto: vale a câmera visível mais de cima. Ocultá-la
  volta para a de baixo.
- O app não fecha mais quando um objeto passa pela câmera.
- A imagem não troca mais com a de outro grupo.
- Grupo copiado e colado fica independente do original.
- PNG transparente sem linhas nem bordas coloridas, na prévia e na miniatura.

### Observação
- No iOS, o app que não abre no dia seguinte é a validade do certificado de
  instalação (sideload), não um erro do app.
