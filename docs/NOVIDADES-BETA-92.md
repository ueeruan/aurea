# O que mudou no Aurea — beta 92 (1.1.7)

## Atualizar sem sair do app

- Quando sair versão nova, aparece uma faixa no alto da Início. Toque em **Atualizar**: o app baixa, confere o arquivo e abre o instalador. Não precisa procurar APK, nem entrar em grupo, nem pedir link.
- Se preferir deixar para depois, o **X** cala o aviso por um dia. Se a versão for obrigatória, o X não aparece.
- O Android pede uma permissão para instalar aplicativo de fora da loja. O app leva você direto à tela do ajuste em vez de só dizer que faltou.
- Isso é só no Android. No iPhone o caminho continua sendo o TestFlight.

## O brilho parou de travar

- **Deep Glow**, **Brilho**, **S_GlowAura**, **S_GlowDarks** e os outros da aba Glow e Luz pediam centenas de leituras de textura por pixel, em todo quadro. Era isso que fazia o app parecer travado ao adicionar um brilho.
- Agora a prévia paga uma fração disso, e a exportação continua no máximo — **o resultado salvo não mudou**.
- Enquanto o vídeo toca, o brilho sai mais simples de propósito. O selo "Rascunho" na tela avisa; ao pausar, a qualidade cheia volta.

## Texto árabe ligado

- Com animação por letra, cada letra árabe saía separada e sem ligação com a seguinte. A forma de cada letra depende das vizinhas, e o desenho por letra sozinha perdia isso.
- Agora a letra continua se movendo sozinha, mas a ligação com a palavra é mantida.
- O texto em árabe também passou a ser alinhado e quebrado da direita para a esquerda, que é como ele se lê.

## Camada bloqueada de verdade

- O cadeado ganhou **botão no menu da camada** e uma **faixa "Camada bloqueada"** com o botão Desbloquear, no painel e no palco.
- Bloqueada não anda, não apara, não reordena, não recebe keyframe e não é apagada por engano.
- As alças de escala e rotação somem: alça que promete um gesto recusado é pior do que alça nenhuma.
- O cadeado continua funcionando depois de salvar e reabrir o projeto.

## Editor de pontos e transições

- **Editar pontos** voltou a desenhar e editar o mesmo caminho. Antes, sair do painel por outro caminho deixava o editor de nós ligado sobre o palco — o toque inseria um ponto no contorno antigo em vez de selecionar a camada.
- Apagar ponto funciona em contorno aberto, e virar canto/curva usa o vizinho certo nas pontas.
- **As transições saíram do app.** Elas estavam quebradas e foram removidas por inteiro, a seu pedido. Projetos antigos que tinham transição **continuam abrindo** — a transição se perde, o resto do projeto fica.

---

## O que ainda não está pronto

Nada aqui foi medido em celular além da instalação. O ganho do brilho vem da
contagem de leituras de textura, não de um cronômetro no aparelho — quem
sentir diferença (ou não sentir), me diga em que aparelho.
