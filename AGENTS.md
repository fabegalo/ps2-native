# Trabalho dos agentes — regras permanentes

Leia `CLAUDE.md`, `STATUS.md`, `docs/NEXT-TASK.md` e `docs/ARCHITECTURE.md` antes de alterar o ps2-native. As regras abaixo se aplicam a **todos os agentes**. O estado de cada marco pertence ao `STATUS.md` e aos relatórios `docs/M*-STATUS.md`, **não** a este arquivo.

## Performance-first, correctness-preserving

Performance é objetivo central, mas nenhuma otimização pode sacrificar correção, compatibilidade, comportamento R5900/IOP, timing, áudio, física ou fidelidade gráfica. Medir antes/depois sob condições equivalentes; registrar também ausência de ganho. Priorizar soluções genéricas compartilhadas, sem hacks por título, revisão ou plataforma. Instrumentação de diagnóstico deve ser opcional e ter overhead controlado. O modo GS fiel é referência de correção; o futuro modo Native não pode alterar lógica do jogo.

## Fidelidade ao PlayStation 2

Reproduzir o sistema original — boot, Browser, configurações, Memory Cards, discos e erros — sem substituir suas telas por um launcher falso. Sem sucesso falso, stubs que retornem zero ou respostas MMIO arbitrárias. Comportamento não comprovado é hipótese identificada, isolada e testada; comportamento desconhecido falha com causa. Interpretador é explícito quando o AOT não tem prova estática suficiente.

## Isolamento permanente de firmware e dados

Cada BIOS/revisão/região só utiliza módulos, IRX, ROM1/ROM2, recursos, NVM e dependências **compatíveis e verificadas**. Conferir procedência, ROMVER, modelo e SHA-256. Não misturar arquivos por nome semelhante, não compartilhar caches de firmware original entre perfis e não usar fallback silencioso. A infraestrutura de hardware em C++20 é compartilhada. Preservar dados reais do usuário, mesmo quando dados sintéticos de teste puderem ser descartados.

## Documentação estável — regra permanente

- `README.md` e `README.pt-BR.md` são **vitrines estáveis do projeto**, não changelogs, relatórios de tarefa ou lista de milestones. Mantê-los equivalentes em conteúdo.
- `AGENTS.md` e `CLAUDE.md` são **contratos permanentes**. Não atualizar por marco, commit, número de testes, novo dispositivo, PC de bloqueio ou status diário.
- Atualizar `STATUS.md` com avanços efetivamente demonstrados, limites e evidência; detalhes técnicos no relatório específico em `docs/`; próximo trabalho apenas em `docs/NEXT-TASK.md`.
- Modificar os quatro arquivos estáveis **somente** quando mudar uma decisão arquitetural duradoura, requisito de produto, instrução de uso pública ou política do mantenedor. Mudança documental irrelevante não justifica novos commits nem repetição de testes.
- Não mover o texto antigo de cada README para `STATUS.md` como cópia integral; histórico detalhado continua nos relatórios existentes.

## Licença, commits e execução

Código original BSD-3-Clause. PCSX2 e PS2Recomp GPL são referências de comportamento, nunca código a copiar, derivar ou vincular; qualquer licença incompatível requer decisão do mantenedor. Não comitar firmware Sony, jogos, arquivos NVM, dados privados, traces ou código traduzido de conteúdo proprietário.

Trabalhar diretamente na `main`, salvo instrução contrária. Conventional Commits, sem PR, ZIP, Release ou coautoria atribuída a assistentes. Não reescrever histórico publicado. Não modificar o x360-native.

Priorizar testes focados; não executar suítes longas ou testes visuais sem necessidade e autorização. Computer Use exige autorização explícita. Não deixar processos em segundo plano sem acompanhamento e prazo. Se uma etapa precisar de autorização não concedida, informar o impedimento sem permanecer aguardando. Concluir a tarefa antes de ampliar o escopo; conferir CI dos seis alvos quando houver mudança de código. Informar ao terminar apenas commits, o que mudou, testes/CI e limites restantes.
