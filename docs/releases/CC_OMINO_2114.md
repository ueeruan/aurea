# CC e Omino Diffusion — 2114

## Uso no app

1. Crie uma camada de ajuste acima dos clipes que receberão o CC.
2. Em Presets → Efeitos, procure CC. Detail, Cinema e Clean são pontos de partida.
3. Em Efeitos, personalize cada etapa: Nitidez, Máscara de nitidez, Exposição,
   Brilho e contraste e Saturação. Curvas, Níveis e outros efeitos podem ser
   adicionados à mesma pilha. Desativar a camada permite comparar com o original.
4. Em Presets → Efeitos → Salvar desta camada, guarde a pilha completa para
   reaplicar em outro projeto. A aplicação é desfeita como uma operação.

Os três presets são compartilhados byte a byte entre Android e iOS. São
implementações editáveis do fluxo da referência https://youtu.be/jyI_zRGLPXU,
não presets extraídos do vídeo nem uma cópia do Magic Bullet Looks/Colorista.
A análise automática identificou nitidez 50 e unsharp amount 15/radius 30;
esses valores são apenas pontos de partida no motor Aurea. Contraste,
exposição e saturação das três variantes são escolhas próprias. Não há
equivalência visual idêntica com os plugins do tutorial comprovada.

## Omino Diffusion

Efeito `aurea.stylize.omino_diffusion` disponível no catálogo das duas plataformas,
com intensidade, difusão, direção, alcance, amostras, largura das faixas,
paleta, atenuação e quatro cores personalizadas. Preset Omino • Diffusion incluído.

Implementação independente a partir da descrição pública do algoritmo:

- Autor: https://omino.com/pixelblog/2007/12/18/diffusion/
- Suite original: https://omino.com/store/ominoAeSuite_2_2_15/index.html
- Amostragem direcional: https://guide.alightmotion.com/effects/omino-diffusion

Uma passagem GPU percorre amostras numa direção, quantiza a cor e propaga o
resíduo para a amostra seguinte. Faixas e paletas são configuráveis. A mistura
preserva o alfa original e a avaliação não depende do histórico de reprodução.
São 24 amostras por padrão, limitadas a 64; custo cresce com resolução/amostras.
O limite móvel e a discretização diferem do plugin original e do Alight Motion.
Não há promessa de correspondência pixel a pixel ou desempenho sustentado em
aparelhos reais sem medição. Nenhum binário/source proprietário foi incorporado.

## Verificação

Testes GPU cobrem intensidade zero, paleta RGB, preservação de alfa, direção,
valores extremos e avaliação determinística. Teste de presets cobre parâmetros,
cores, ordem dos efeitos, keyframes e desfazer após salvar/reaplicar. Compilação
Kotlin e verificações estáticas iOS fazem parte da validação; compilação Metal
e Swift nativa ocorre no workflow do IPA.
