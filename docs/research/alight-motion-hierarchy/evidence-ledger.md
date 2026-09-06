# Evidência e lacunas - 05/09/2026

Fontes acessadas em 05/09/2026. Artigos de ajuda são evidência do fluxo
documentado, não observação da versão instalada. Vídeos incorporados falharam
com erro JavaScript. Nenhum teste comparativo com usuários foi realizado.

| Questão / afirmação | Evidência | Confiança / contraponto | Decisão / próxima checagem |
|---|---|---|---|
| Adicionar, ajustar, animar é a gramática introdutória | S1 | Alta; menus variam entre versões | Contexto, não quatro telas obrigatórias |
| Camada, pilha e efeito têm comandos de escopos distintos | S2 | Alta, documentação textual | Manter alvo legível nos painéis |
| Keyframes/easing pertencem à propriedade e ao intervalo | S3 | Alta; não valida implementação AUREA | Testar propriedade ativa e tempo local |
| Grupo processa composição; parenting relaciona transforms | S4, S5 | Alta; não são equivalentes | Preservar recursos existentes, não unificá-los |
| Elements separam reutilização e cópia local | S6 | Alta; efeitos de edição vinculada requerem aviso | Fora desta implementação; recomendação futura |
| Preview tem gestos dependentes de seleção e enquadramento recuperável | S7 | Alta, guia atualizado 2025 | Não remapear gestos existentes nesta etapa |
| Alight sempre volta propriedade/camada/grupo/projeto | Não comprovado | Lacuna; vídeos não acessíveis | Não atribuir ao Alight; AUREA terá regra própria testada |
| Revelação progressiva ajuda, mas profundidade excessiva prejudica | S8 | Diretriz alta; aplicação média | Mostrar grade diretamente e conservar preview/timeline |
| Retorno deve preservar controle/contexto | S9, S10 | Alta como diretriz; sem medição local | Topo e Voltar do sistema devem seguir a mesma regra |
| Reconhecimento e gesto com alternativa visível | S11, S12, S13 | Alta, aplicação específica inferida | Rótulos de keyframe e botões anterior/próximo |
| Manipulação direta e alvos amplos | S14, S15 | Conceito original / guia Android; não certificação | Reusar atualização de preview e hit areas 48 lógicos |
| AUREA já tem categorias por tipo, preview persistente e curvas | Código am_sections, panel_chrome, curve_panel | Alta, inspeção local | Não recriar motor nem remover ferramentas |
| AUREA exige selecionar, reabrir menu, escolher categoria | am_timeline e editor_screen | Alta, contagem estrutural de caminho | Dock reduz a 2 toques no caminho usual, verificar widget |
| Topo já limpa seleção, mas rota não intercepta Voltar do sistema | editor_screen _TopBar / ausência PopScope | Alta; corrige precisão da hipótese inicial | Unificar e testar, não afirmar bug em todo botão Voltar |
| Recentes são globais por título, sem alvo | am_widgets / param_sheet_shell | Alta, inspeção | Invalidar ao mudar contexto e fechar folha antiga |

## Fontes

- S1. Support / Alight Motion. *Alight Motion Quick Start Guide*. Atualizado 16/06/2023. https://support.alightmotion.com/hc/en-us/articles/10536777320337-Alight-Motion-Quick-Start-Guide
- S2. Support / Alight Motion. *How do I copy and paste effects?*. 14/03/2023. https://support.alightmotion.com/hc/en-us/articles/13725250940689-How-do-I-copy-and-paste-effects
- S3. Support / Alight Motion. *Animation Easing Curves*. Atualizado 14/06/2023. https://support.alightmotion.com/hc/en-us/articles/10536934703889-Animation-Easing-Curves
- S4. Support / Alight Motion. *Displacement Map*. Atualizado 11/11/2022, Ripple / Energy Burst passos 5-8. https://support.alightmotion.com/hc/en-us/articles/10537076760593-Displacement-Map
- S5. Support / Alight Motion. *Layer Parenting and Null Objects*. Atualizado 16/06/2023. https://support.alightmotion.com/hc/en-us/articles/10536997444369-Layer-Parenting-and-Null-Objects
- S6. Support / Alight Motion. *Elements: The Complete Guide*. Atualizado 16/06/2023. https://support.alightmotion.com/hc/en-us/articles/10536791122449-Elements-The-Complete-Guide
- S7. Support / Alight Motion. *Preview Pan and Zoom*. Atualizado 10/06/2025. https://support.alightmotion.com/hc/en-us/articles/10536990235409-Preview-Pan-and-Zoom
- S8. Jakob Nielsen / Nielsen Norman Group. *Progressive Disclosure*. 03/12/2006. https://www.nngroup.com/articles/progressive-disclosure/
- S9. Maria Rosala / Nielsen Norman Group. *User Control and Freedom (Usability Heuristic #3)*. 29/11/2020. https://www.nngroup.com/articles/user-control-and-freedom/
- S10. Kate Kaplan / Nielsen Norman Group. *8 Design Guidelines for Complex Applications*. 08/11/2020. https://www.nngroup.com/articles/complex-application-design/
- S11. Raluca Budiu / Nielsen Norman Group. *Memory Recognition and Recall in User Interfaces*. 15/01/2024. https://www.nngroup.com/articles/recognition-and-recall/
- S12. Apple Developer. *Discoverable design*. WWDC21, 2021. https://developer.apple.com/videos/play/wwdc2021/10126/
- S13. W3C WAI / AG WG. *Understanding SC 2.5.7: Dragging Movements*. Atualizado 10/08/2026. https://www.w3.org/WAI/WCAG22/Understanding/dragging-movements.html
- S14. Ben Shneiderman / IEEE Computer 16(8), 57-69. *Direct Manipulation: A Step Beyond Programming Languages*. Agosto/1983. https://www.cs.umd.edu/~ben/papers/Shneiderman1983Direct.pdf DOI 10.1109/MC.1983.1654471
- S15. Google / Android Developers. *Make apps more accessible (Views)*. Atualizado 21/04/2026. https://developer.android.com/guide/topics/ui/accessibility/views/apps-views?hl=en

## Fechamento do aprofundamento

As duas trilhas independentes convergiram em escopo, estado e retorno. Leitura
principal adicional confirmou S1, S3, S8 e S9. Não é necessário inferir a ordem
atual completa de botões para implementar os princípios. Máscaras foram tratadas
como evidência parcial histórica e não usadas como especificação visual atual.
Sem analytics, a prioridade das categorias segue contrato existente do AUREA,
não uma frequência de uso inventada. Validação seguinte: widget e regressão.

## Proveniência interna e validação final

Checagem principal: turn26view0 (S1), turn26view1 (S3), turn26view2 (S8),
turn26view3 (S9). Trilhas de pesquisa: alight_workflows e editing_usability,
com registros recebidos e reconciliados antes da implementação final. Exemplos
de proveniência auxiliar: turn16view0/1 (S8/S11), turn18view2/3/5/6
(S9/S10/S12), turn22view4/5 (S13), turn24view0/1/3/4 (S14/S15).
Esses identificadores são internos e não aparecem no PDF.

Resultado: suíte completa 1.121/1.121; sete testes novos de hierarquia;
análise estática sem problemas; capturas 375 x 667 e 430 x 932 inspecionadas.
PDF final: todas as cinco páginas inspecionadas; 25 anotações de link
(quebras de linha geram múltiplas anotações), 15 URLs distintas.
