# Aurea App - RMK Dnyx

Referencia fornecida: Download (2).mp4.
SHA-256: e73e94b965a1609a8305abb59d5d413f892deeb56574f68b92ffcc79d18d0d76.
576 x 576, 309 quadros a 2997/100 fps, 10,310310 s de imagem.

## Entrega e autoria

Projeto novo em Modelos > Aurea App · RMK Dnyx. Abrir cria uma copia com
identidade nova nos projetos locais. Fotos, reacoes e audio sao copiados
do pacote do app para a pasta persistente do aparelho antes da abertura.
A fonte Roboto (Aurea Motion Sans no editor) acompanha o app e sua licenca.
O modelo anterior continua disponivel.

A composicao usa camadas de texto, curvas, formas, fotos e audio. Nenhuma
camada de video da referencia, sequencia de screenshots ou quadro inteiro
da fonte e usada para simular a recriacao. As fotografias e reacoes foram
isoladas dos quadros da referencia; partes ocultas nao podem ser recuperadas
do MP4. A foto principal foi extraida de um quadro sem reacoes sobrepostas.
O TikTok e o handle nao foram incluidos. A assinatura final e texto
editavel: Aurea App - RMK Dnyx.

## Mapa e verificacao

- 0–65: exposicao azul, pixels medidos, orbita, tipografia e selecao.
- 66–95: foto, reacoes e cursor de arraste.
- 96–139: entrada da caixa, digitacao e foto virando miniatura quadrada.
- 140–195: aproximacao ao botao, movimento de cursor e clique.
- 196–237: titulos e abertura da galeria.
- 238–272: transicao de pixels e simbolo.
- 273–308: assinatura pedida, aproximacao, desfoque e halo.

tool/dnyx_study.py registra tempo, cor e variacao de TODOS os 309 quadros.
Os retangulos coloridos iniciais usam bounds medidos; as demais formas e
trajetorias foram reconstruidas e ajustadas pela comparacao visual.
test/render_dnyx_remix_test.dart renderiza a mesma CompositionView do app.
tool/compare_dnyx.py compara indices iguais, sem realinhar a referencia.
O erro RGB nao e percentual de similaridade, nem prova de identidade.

A passagem 05 comparou 309 quadros, com erro medio RGB 9,27/255; fora
das regioes de marca/assinatura alteradas, 7,83/255. Ainda ha diferencas
de geometria, fonte, reacoes, movimento e iluminacao. A passagem final
ajusta tambem a foto quadrada que aparece dentro da frase de abertura.

Passagem final 06: 309 quadros comparados, erro RGB 9,26/255 ou 7,82/255
fora das regioes deliberadamente alteradas. MP4 verificado em 576 x 576,
2997/100 fps e 309 quadros, com audio. SHA-256 do MP4 final:
716a130afb7ae0cf833b316a7dc7901f48c91b1f8d576c7f8a71451adf7cc7a6.
Saidas em build/render/dnyx/06. Nao e uma copia pixel-identica.

O MP4 entregue usa a taxa original 29,97 fps. Os keyframes do projeto
guardam os tempos originais; o seletor/exportador nativo do app permanece
em 30 fps (taxas inteiras). A exportacao feita pelo usuario pode portanto
ter amostragem ligeiramente diferente do MP4 entregue (cerca de 0,1%).
Nao declarar igualdade pixel a pixel ou validacao em iPhone fisico.

## Recursos reutilizaveis

Cursor seta e Cursor mao na biblioteca de formas: caminhos Bezier normais,
com cor, contorno e pontos editaveis. Fonte empacotada disponivel na lista
de fontes, sem depender das fontes do sistema iOS; nao e removivel pela UI.
Gradientes, mascaras arredondadas, digitacao, glow e desfoque usam recursos
nativos existentes. O blur das fotografias compensa a escala anterior ao
efeito, preservando a largura visual medida no quadro de saida.

## Build

Versao preparada: 1.1.0+34, workflow de IPA sem assinatura do repositorio
ruanpablo9928-sys/aurea, o mesmo destino utilizado no IPA anterior.
O envio foi bloqueado pelo revisor de seguranca: a autorizacao anterior
nao cobre explicitamente o novo payload de codigo, fotos, audio e fonte.
Nenhum commit/tag/envio foi executado nessa tentativa. Falta autorizacao
especifica do usuario para publicar esses arquivos e compilar o novo IPA.
As alteracoes locais anteriores no modelo Pindown, lock e caches Android
nao fazem parte desta entrega e foram preservadas.
