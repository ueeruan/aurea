# Particle World — build 2114

Android e iOS passam a criar Particle World, com três movimentos (explosivo, jato e vórtice), dez controles e duas cores. O painel antigo de 70 parâmetros foi retirado da interface. Os dados e caminhos de leitura antigos permanecem para não destruir projetos salvos.

O novo shader usa emissão uniforme no volume de uma esfera, distribuição isotrópica de velocidade, cone de ângulo sólido uniforme e integração analítica conjunta de resistência do ar e gravidade. O vórtice calcula também a velocidade tangencial para orientar os rastros. O modelo calcula cada instante sem acumular estado entre frames. A emissão é limitada a 12 mil slots por camada. Vulkan/Android e Metal/iOS usam a mesma fonte GLSL.

Os padrões foram extraídos do CC Particle World instalado no After Effects 2020, em um projeto separado (`tools/extract_particle_world.jsx`): vida de 1 segundo, emissor de raio 0,025, física explosiva, partícula em linha, opacidade máxima 75% e cores amarelo/vermelho. A conversão de unidades e a sequência aleatória são nativas. **Não é uma cópia binária do plugin nem há comprovação de imagem idêntica ao CC Particle World.** Câmera, composição de cores e forma de linha podem divergir da referência.

Controles visíveis: raio do emissor, partículas por segundo, duração, velocidade, gravidade, resistência, forma, tamanho inicial, tamanho final e opacidade. Posição é controlada pela transformação da camada. Duas cores definem nascimento e morte. Parâmetros numéricos continuam com keyframes e desfazer. Selecionar outro preset substitui também os keyframes de partículas, dentro de uma operação reversível.

Validação: testes GPU dos três movimentos, seek reverso e salvar/reabrir; regressão dos sistemas de partículas existentes; compilação Android e compilação iOS/Metal. Fluidez em aparelhos físicos e equivalência visual com After Effects exigem aceitação adicional.
