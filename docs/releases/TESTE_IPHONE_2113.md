# Teste de desempenho no iPhone — build 2113

No editor, toque no velocímetro da barra superior (sem camada selecionada).
O relatório também fica disponível em Início → Ajustes → Desempenho no iPhone.

1. **Importar vídeo e medir**: escolha um arquivo no iPhone. O teste registra o
   tempo de cópia/importação e continua medindo enquanto você edita, por até 5 minutos.
2. **Gravar enquanto eu edito**: use os vídeos, efeitos, gestos e exportação que
   apresentam lentidão. O botão **Travou aqui** marca sua observação.
3. **Forçar prévia e timeline**: usa o projeto aberto por 3 minutos: 60 s de
   reprodução automática, 60 s em qualidade total e 60 s com quatro saltos por
   segundo. Não cria/apaga camadas. Restaura qualidade e posição ao terminar.

Use **Parar** e depois **Compartilhar último relatório**. Envie o arquivo `.jsonl`
nesta conversa. Nenhum vídeo é enviado automaticamente. O relatório não inclui
nomes de arquivos, títulos de projetos, caminhos ou conteúdo dos vídeos.

Medições: CPU, GPU quando há timers disponíveis, decodificação, orçamento de frame,
quadros perdidos, percentis do ritmo de apresentação, memória do motor e footprint
do processo medido pelo iOS, áudio, temperatura, qualidade, posição e fase do teste.
Intervalos de CADisplayLink medem a resposta da interface; não são FPS do vídeo.
O relatório não atribui uma causa definitiva ao travamento: os dados permitem
correlacionar a fase lenta com essas medições.

Cada segundo é persistido numa fila separada, com watchdog para ausência de resposta
da thread principal por pelo menos 1 segundo. Depois de fechamento inesperado, a
tela oferece o último arquivo parcial; ausência de `session_end` indica interrupção.
Isso não substitui um crash log/relatório de jetsam do iOS. O teste interrompe em
segundo plano, troca de projeto, alerta de memória ou estado térmico crítico.
O arquivo tem limite de 8 MiB. O próprio monitoramento tem custo pequeno, mas não zero.
