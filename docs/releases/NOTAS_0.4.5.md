# Aurea Beta 0.4.5 (build 2208) · 01/11/2026

Android e iPhone. Mesmas mudanças nas duas plataformas: regras no motor, telas nas duas.
Correções dos relatos da 0.4.4 e pedidos da comunidade.

## Correções

### Mais graves
- **Prévia que travava tocando** (Frostko, Idkmaybe675 no S23 Ultra):
  - A agulha andava pelo relógio mesmo quando a prévia não entregava quadro, e por isso prévia e
    timeline saíam de sincronia. Agora o motor segura a agulha até o quadro chegar. A espera tem
    teto de 1,5 s, para mídia quebrada não prender o play.
  - Android: um engasgo passageiro do decoder o rebaixava para sempre (GPU → CPU → software).
    Agora ele tenta de novo no mesmo caminho e só troca depois de falhas seguidas.
  - Buffers de vídeo de clipes já fechados ficavam presos 20–40 s (Vulkan) e o cache de texturas
    do Metal nunca era esvaziado tocando. Agora são soltos em poucos quadros.
- **App que abria e fechava** (Renasantos, Mon):
  - Falhas de rede, configuração remota, login e anúncios na abertura não derrubam mais o app.
  - O build da Play não tinha as regras do R8 para os plugins e para o login com Google. As regras
    foram acrescentadas.
  - iPhone: o login com Google sem configuração completa escondia o botão em vez de fechar o app.
  - A causa exata do relato não foi comprovada (sem log do aparelho).
- **Exportação que falhava** (PsycoLegend, kee):
  - Lado ímpar comum (ex.: 1079) era recusado e valores absurdos passavam. Agora o lado é
    alinhado e só o que passa de 16384 é recusado.
  - "Codec não suportado" pulava direto para 720p. Agora tenta H.264 e software no mesmo tamanho
    antes de reduzir.
  - A taxa de bits respeita o teto anunciado pelo encoder do aparelho.
  - A mensagem de erro final diz o que já foi tentado e traz um código para o suporte.
- **iPhone, painel de efeitos** (ToxxicZzz):
  - Com 6 ou 7 efeitos a lista parava de rolar e "Adicionar efeito" ficava inalcançável. Os gestos
    de deslizar, segurar e ≡ de cada efeito disputavam o dedo com a rolagem; agora cedem a ela.
  - Toques de um efeito fechando não vazam mais para os botões ao lado do painel.

### Altos
- **Gráfico que abria e fechava** (vankzso): tocar num keyframe sem par abria o editor de curvas,
  que fechava na hora. Agora ele escolhe um keyframe com curva, ou avisa sem abrir.
- **Grupos** (وشـو؟):
  - "Desagrupar" só aparecia com 2 ou mais camadas escolhidas. Agora aparece com um grupo só,
    também na barra de seleção, e vários grupos desagrupam num único desfazer.
  - Camadas dentro de grupo: comandos pendentes caíam na composição errada ao entrar ou sair do
    grupo; desfazer com o grupo aberto deixava uma composição órfã; o cabeçote pulava ao fechar.
  - Quando desagrupar não é possível, o motivo aparece traduzido (também em árabe).
- **Letras que sumiam depois de exportar** (Grizzlyy): a exportação "consumia" o aviso de que o
  atlas de letras mudou, e a prévia não reenviava as letras. Agora cada renderer acompanha a própria
  versão do atlas.
- **3D:**
  - Sombras voltaram a responder à luz (saito). Desde a 0.4.2, uma camada 2D em 3D na cena
    afastava o chão do modelo; e sair do chão reflexivo deixava o chão escuro.
  - "Afeta camadas abaixo" da Câmera 3D (MIYUKI) continuava existindo, mas ficava escondido no
    fim do painel. Agora está no topo da aba Visão.
- **Recursos que tinham sumido** (Gagapcs), voltaram:
  - conta-gotas do chroma key;
  - máscara em formas, texto, grupo e partículas;
  - ligar várias camadas a um nulo de uma vez (botão Vincular na barra de seleção, um desfazer).

### Médios
- **Campo hex** (Abubakari): o código da cor agora aceita digitação (3, 6 ou 8 dígitos, `#`
  opcional).
- **Borda e Deep Glow** (Gagapcs, 𝙒𝙞𝙩𝙘𝙝𝙚𝙧):
  - A Borda mostrava a beirada da imagem esticada em vez da cor escolhida em fotos e vídeos.
  - Bordas novas nascem por fora da anterior, com cores alternadas.
  - A Intensidade do Deep Glow virou slider de 0 a 10; o teclado ainda aceita até 100.
- **Salvar na galeria** (وشـو؟): o botão "Salvo na galeria ✓" abria o seletor de arquivos. Agora
  só confirma; "Salvar em…" virou uma opção separada.

## Novidades

- **Conta-gotas em todas as cores** (ya2umi, Gagapcs): roda de cores e tela cheia, chroma key,
  cor de fundo do projeto, preenchimento, borda, sombra, texto e efeitos.
  - A mira fica sob o dedo, com lupa.
  - Em cores de efeito, a amostra sai sem o próprio efeito: no chroma key você pega o verde
    original, não o fundo já removido.
- **Remover fundo de vídeo, sem tela verde** (Savo, Ivannn):
  - Efeito "Remover fundo", no menu da camada, na Edição e no catálogo.
  - Analisa o clipe em segundo plano, com progresso e cancelamento.
  - Refino de borda: suavizar, contrair/expandir, borda fina, inverter.
  - Os recortes ficam salvos com o projeto e a exportação usa os mesmos.
  - Usa o modelo de recorte que já vem no app, offline. No iPhone há também o modo "Pessoa", com o
    recorte do sistema (Vision).
- **Efeitos novos e ampliados** (kee, zixo, rinku7195, inann):
  - **Glitch:**
    - O Glitchify ganhou 25 controles, especificados a partir do plugin original: transição por
      canal, separação por componente, escala por canal, corrupção de bytes, fatia comprimida,
      blocos trocados, compressão, pontilhado e ruído.
    - Novos: Hyper Glitch, Block Glitch (estilo DaVinci) e Shift Channels.
    - O Signal já era um Signal Glitch e agora é encontrado por esse nome.
  - **Distorção e tempo:**
    - Novos: Time Displacement, Tile Shift, Hexagon Tile Rotate, Hexagon Tile Shift e Hollow Box
      (caixa 3D oca, como no Alight Motion).
    - Mirror ganhou o espelho nos dois eixos e a cópia sobreposta, como no AM.
    - Zoom Blur ganhou os modos Mais claro/Mais escuro (CC Radial Fast Blur).
    - Twixtor (Flow Retime) e Turbulent Displace já existiam e agora aparecem por esses nomes.
  - **Cor, luz e transições:**
    - Novos: Reflexo na água, Envolver com luz (Light Wrap) e Looks de cinema (13 looks próprios,
      estilo Magic Bullet Looks, sem LUTs de terceiros).
    - Transições novas: Caneta esferográfica (Ballpoint) e Poeira (Dust).
    - CC Glass (Vidro) e Lumetri (Correção de cor) já existiam e agora são encontrados por esses
      nomes.
    - Venetian Blinds ganhou largura da faixa em px.
- **Puppet e esqueleto 3D mais fáceis** (⚡ᴍᴀʀᴋᴢ, pauloph2517, Jess):
  - **Esqueleto 3D:**
    - Juntas maiores e anéis de rotação X/Y/Z no palco, no espaço do osso.
    - Tocar de novo alterna entre juntas sobrepostas.
    - IK simples: arrastar a ponta leva os ossos acima.
  - **Painel Esqueleto:**
    - Árvore de ossos com busca e campos numéricos.
    - Zerar, espelhar e copiar/colar pose.
    - Keyframe do osso ou de todos, e mostrar/ocultar o esqueleto.
  - **Fantoche:** toque mais fácil nos pinos, alternância entre pinos sobrepostos, e girar um
    grupo de pinos em volta de um pino-pivô.

## Validação
- **Motor (host):**
  - Suíte completa: 2583 testes. Os testes novos desta versão passam: correções, Glitch045,
    Distort045, UserFx045, BgRemove, RigSkeleton e Eyedropper.
  - As 23 falhas que restam (GPU, captura e exportação no host) são as mesmas do código da 0.4.4,
    rodado à parte com os mesmos testes. Não têm relação com esta versão.
- **Android:**
  - O Kotlin compila; o APK de teste x86_64 (0.4.5-beta, 2208) foi gerado. Não foram gerados AAB
    nem IPA.
  - Emulador, com as correções (antes dos efeitos novos entrarem):
    - Abertura com dados limpos, em árabe.
    - Prévia em loop por cerca de 2 minutos sem subir memória.
    - Botão Máscara no texto; campo hex aplicando a cor.
  - Emulador, com o APK final: abre com dados limpos (árabe) sem fechar. Os recursos novos não
    foram exercitados na tela: o onboarding exige abrir links externos e entrar na conta.
- **iPhone:** o Swift não foi compilado nesta máquina (Windows). Os checadores do repositório
  (textos, símbolos da ponte, tipos) não acusam problemas.
- **Não testado em aparelho:**
  - os casos dos usuários (S23 Ultra, exportação com erro, abertura que fechava);
  - os gestos novos do painel de efeitos no iPhone;
  - o Remover fundo (desempenho e qualidade, e o Vision no iPhone);
  - os anéis do esqueleto e o conta-gotas nas duas telas.
- **Em aberto:**
  - "Texto e Texto 3D não alternam" (Abubakari): não encontramos a tela do relato, falta o vídeo.
  - A fidelidade exata do Glitchify ao plugin não pôde ser comparada sem o After Effects.
