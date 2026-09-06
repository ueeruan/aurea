# Plano — hierarquia de edição móvel

Pedido: pesquisa profunda sobre a hierarquia de edição do Alight Motion, por que
ela pode facilitar o uso e implementação dos princípios relevantes no AUREA.

Público: criador/editor do AUREA. Data de referência: 5 de setembro de 2026.
Premissas: foco no editor móvel e nos fluxos existentes; preservar recursos,
projetos, keyframes e o trabalho local anterior. Não copiar marca/arte proprietária,
não alegar superioridade medida sem estudo e não gerar IPA sem pedido específico.

Fontes: ajuda e tutoriais oficiais do Alight Motion; pesquisa/princípios originais
de interação e guias primários de plataforma; inspeção do código e testes AUREA.
Não haverá medição empírica de facilidade com usuários nesta entrega.

O recurso update_plan não está disponível nesta sessão; este arquivo mantém o
plano equivalente, com apenas uma etapa em andamento.

1. [concluída] Descoberta: fontes, mapa de fluxos e auditoria do AUREA.
2. [concluída] Aprofundamento: confrontar evidência, registrar lacunas e escolher mudanças.
3. [concluída] Síntese: relatório-fonte, rastreabilidade e especificação de implementação.
4. [concluída] Implementação e verificação: navegação, contexto, ferramentas e regressões.
5. [concluída] Entrega: relatório PDF revisado e resumo das mudanças/testes/limitações.

Verificação: 1.121 testes aprovados; flutter analyze sem problemas. Sete novos
testes de hierarquia e capturas em 375 x 667 / 430 x 932. Sem build de IPA.

Entrega verificada: output/pdf/AUREA-hierarquia-de-edicao.pdf, 5 páginas,
62.239 bytes; todas as páginas renderizadas e inspecionadas, 15 destinos de
fontes únicos, sem marcadores temporários. git diff --check sem erros.
