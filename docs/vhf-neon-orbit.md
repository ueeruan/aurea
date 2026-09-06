# VHF · Neon Orbit — projeto nativo AUREA

O modelo aparece em **Inicio → Modelos → VHF · Neon Orbit**. Cada abertura
cria um projeto independente. O audio empacotado e copiado para Documentos do
aplicativo; nenhuma midia depende de Downloads ou de uma conexao de rede.

## Referencia e reconstrucao

Referencia fornecida: `Vhfdigital_pindown.io_1788585393.mp4`, 720 × 1280,
234 quadros, 24209/1000 fps, duracao de aproximadamente 9,665827 segundos.
Os 234 quadros foram decodificados, indexados por timestamp e medidos. A
composicao nativa contem 12 cenas de formas Bezier, trajetorias, deformacoes,
cores animadas, arcos, reflexos, gradientes e halos independentes.

Nao ha VideoLayer nem ImageLayer substituindo a animacao. O unico recurso
extraido e incluido no modelo e a trilha sonora. A miniatura mostra um quadro
da nova renderizacao, nao do video original.

Esta e uma **reconstrucao vetorial aproximada, nao uma copia pixel a pixel**.
Volume, materiais, reflexos, geometria e algumas trajetorias ainda diferem da
referencia. As superficies com aparencia 3D foram reconstruidas como vetores
2D animados, nao como os modelos 3D originais. Nao anunciar como identico.

Os keyframes guardam os timestamps da referencia. O editor/exportador atual
usa fps inteiro: este projeto usa 24 fps. O MP4 de verificacao renderizado pelos
testes usa os 234 timestamps originais e e empacotado a 24,209 fps. Portanto a
amostragem da exportacao pelo app pode diferir ligeiramente dessa demonstracao.

## Recurso novo do editor

`ShapeGradientFill.colorFrames` anima cada parada de cor. As cores interpolam
com o easing do segmento, sao serializadas no projeto e aparecem na timeline.
Em **Editar forma → Gradiente**, ligar **Animar cores**, posicionar o playhead e
editar uma cor grava o keyframe daquele instante; o diamante adiciona/remove
um keyframe. Gradientes antigos sem esse campo continuam estaticos.

## Verificacao reproduzivel

- `tool/vhf_study.py`: timestamps, extracao de todos os quadros e diferencas.
- `tool/vhf_measure.py`: medidas de paletas e contornos das esferas.
- `test/vhf_motion_test.dart`: estrutura, geometria nativa, persistencia,
  gradientes animados e preparacao offline dos recursos.
- `test/render_vhf_motion_test.dart`: renderiza pelo compositor do app com
  `AUREA_VHF_RENDER=1`; `AUREA_VHF_PASS` escolhe a pasta de resultados.
- `tool/compare_vhf.py`: compara todos os pixels dos quadros renderizados com
  a referencia e grava o erro absoluto medio na escala 0–255. Nao e um indice
  percentual de semelhanca.

O IPA 35 tambem inclui o conserto da navegacao: abertura de Cena 3D, Grid e
outras ferramentas nao executa mais dois fechamentos consecutivos do menu.
O controle Gradiente esta ligado ao ShapePanel utilizado pelo editor atual,
com teste de abertura, animacao de cores no playhead e fechamento do painel.
Testes de widget usam dimensoes de iPhone; nao substituem um teste em aparelho
fisico. O IPA gerado pelo workflow e sem assinatura, para assinar ao instalar.
