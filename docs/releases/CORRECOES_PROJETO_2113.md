# Duração e resolução — 2113

Relato: projeto preso em cinco segundos e conteúdo saindo do quadro ao trocar
a resolução no Android/iOS.

O comando compartilhado `LayerSetTimeRange` alterava o fim da camada sem ampliar
a composição e o limite do controlador de reprodução. Agora amplia os dois até
o último fim de camada, inclusive após ripple. Encurtar uma camada não reduz
automaticamente o projeto. O histórico restaura camada e duração juntos.

As configurações nativas das duas plataformas agora oferecem **Duração**, em
segundos, convertida para frames na taxa atual. A duração de uma fonte de vídeo
continua sendo o limite para revelar conteúdo que existe no arquivo; aumentar
o projeto não inventa frames de vídeo.

O comando de tamanho mudava somente largura/altura da composição. Agora adapta
as transformações das camadas raiz e seus keyframes ao novo quadro, mantendo a
proporção. Filhos herdam essa transformação uma única vez. Quando a proporção
muda, o conteúdo é centralizado sem esticá-lo. Fontes, duração e tempos dos
keyframes permanecem iguais. O setter bruto usado para carregar arquivos não
redimensiona as camadas. Desfazer restaura tamanho e transformações.

Limites: expressões que calculam coordenadas absolutas continuam sendo
expressões do usuário; não são reescritas. A aceitação visual de cenas 3D,
partículas e efeitos dependentes de coordenadas permanece necessária.
Fechamento inesperado em Samsung/iPhone não foi reproduzido neste computador;
a correção de enquadramento não comprova correção de crash ou falta de memória.
O teste de desempenho do iPhone gera o relatório para essa investigação.

Regressões automatizadas acrescentadas: extensão de 5 para 30 segundos com e
sem ripple e desfazer; redimensionamento com hierarquia e keyframes; comando
de resolução e desfazer; mudanças repetidas de tamanho com captura e exportação
de vídeo na GPU do host.
