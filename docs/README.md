# Documentação técnica — ps2-native

Este índice separa o material de engenharia da apresentação estável do projeto.

- **[STATUS.md](../STATUS.md)** — estado corrente, resultados demonstrados, limitações e prioridades. É o registro que muda conforme a evolução do código.
- **[NEXT-TASK.md](NEXT-TASK.md)** — tarefa de engenharia ativa/seguinte; atualizar aqui em vez de reescrever regras permanentes.
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — contratos de hardware, AOT, memória, interfaces e decisões arquiteturais.
- **[REFERENCE-AUDIT.md](REFERENCE-AUDIT.md)** — fontes técnicas, comparação de abordagens e limites das referências.
- **[LICENSE-DECISION.md](LICENSE-DECISION.md)** — licenciamento e proteção contra incorporação de código incompatível.
- **[LAUNCHER.md](LAUNCHER.md)** — launcher desktop opcional: camadas, identificação de conteúdo, persistência, diagnóstico, dependências Qt e validação.

## Relatórios de marcos

Os relatórios abaixo registram evidências históricas e hipóteses **como estavam naquela etapa**. Não são uma documentação de API atualizada automaticamente; para estado corrente, leia `STATUS.md`.

| Etapas | Relatórios |
|---|---|
| Fundação e AOT | [M0](M0-STATUS.md), [M1.1](M1.1-STATUS.md), [M1.2](M1.2-STATUS.md), [M1.3](M1.3-STATUS.md) |
| Firmware/EE inicial | [M2.0](M2.0-STATUS.md), [M2.1](M2.1-STATUS.md), [M2.2](M2.2-STATUS.md) |
| Controlador de memória | [M2.3](M2.3-STATUS.md), [M2.4](M2.4-STATUS.md), [M2.5](M2.5-STATUS.md) |
| IOP, NVM e dispositivos | [M2.6](M2.6-STATUS.md), [M2.7](M2.7-STATUS.md), [M2.8](M2.8-STATUS.md) |

## Política editorial permanente

- Os READMEs em inglês e português explicam a **identidade, objetivos, capacidades amplas, instalação/uso e limites públicos**. Não enumerar cada commit, milestone ou contagem de instruções neles.
- `AGENTS.md` e `CLAUDE.md` registram somente **instruções e decisões duradouras**. Não gerar um novo contexto cumulativo a cada tarefa.
- `STATUS.md` e relatórios específicos recebem resultados de engenharia, data, medição e próximo bloqueio. `NEXT-TASK.md` define a tarefa seguinte.
- Qualquer afirmação de boot, compatibilidade, fidelidade, renderização, desempenho ou suporte multiplataforma deve indicar qual evidência realmente a sustenta.
- Não colocar BIOS, NVM, firmware, dumps, traces de conteúdo proprietário ou traduções derivadas de jogos aqui.
