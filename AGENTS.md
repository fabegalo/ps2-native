# Trabalho dos agentes

## Performance-first, correctness-preserving — regra permanente

Performance é objetivo central do ps2-native. Toda otimização deve preservar
correção, compatibilidade, fidelidade gráfica, semântica R5900 (delay slots,
registradores de 128 bits, memória, exceções) e temporização do console.
Medir antes/depois com cenários e condições equivalentes; registrar também
resultados sem ganho. Evitar overhead desnecessário no runtime; instrumentação
opcional, desligada normalmente. Melhorias compartilhadas devem beneficiar jogos
genericamente, sem hacks por título. O modo Native nunca altera física, lógica,
temporização ou renderização para ganhar FPS; o modo GS fiel é a referência.

Leia e siga `CLAUDE.md`, especialmente as seções **Trabalho dos agentes — regras
permanentes**, **Licenças** e **Commits e publicação**, além do status do marco
mais recente em `docs/` e de `docs/NEXT-TASK.md`, antes de atuar neste
repositório. Essas regras se aplicam a todos os agentes: Computer Use e sessões
visuais/longas exigem autorização explícita; não repetir validações aprovadas
sem mudança relevante; priorizar testes focados e evidências existentes.
Processos em segundo plano exigem acompanhamento e duração limitada. Interação
não autorizada exige interrupção e comunicação imediata do bloqueio; não esperar
horas por aprovação. Concluir o escopo atual antes de ampliá-lo ou começar
outro marco. Não modificar o x360-native.

## Sem sucesso falso — regra permanente

Nada de stubs que retornam sucesso, hacks específicos por jogo ou fallback
silencioso. Opcode, relocation, destino indireto, syscall, MMIO ou código
dinâmico não suportado gera erro explícito. Descoberta de código somente por
evidência verificável; relocations de executáveis já vinculados servem como
evidência e nunca são reaplicadas. "Compilou" não é "executa"; nenhum jogo é
declarado compatível sem teste real de gameplay.

## Fidelidade ao console — regra permanente

Ser o mais fiel possível ao PS2 original, idealmente idêntico (boot, Browser,
System Configuration, Memory Card Browser, discos, códigos de erro e serviços
da BIOS da revisão correspondente). O launcher não substitui menus originais.
Divergências de projeto são corrigidas em direção ao original; o não comprovado
fica como hipótese. Saves, Memory Cards e configurações reais são preservados.

## Regra arquitetural permanente — isolamento de BIOS e firmware

Cada revisão/região de BIOS usa exclusivamente seus módulos e recursos
originais (IRX, ROM1/ROM2, fontes, OSD/Browser). Nomes, exports ou tamanhos
iguais não comprovam compatibilidade. Registrar versão, região, procedência e
SHA-256 por perfil; verificar antes do carregamento, sem fallback silencioso,
busca por basename em outras versões ou cache global de componentes originais.
Dependência ausente ou sem identificação gera erro claro. Infraestrutura
genérica pode ser compartilhada; firmware específico de uma revisão, não.

## Licenças e conteúdo proprietário — regra permanente

Código original é BSD-3-Clause. PCSX2 e PS2Recomp (GPL) são apenas referência:
não copiar, derivar, ligar nem traduzir seu código. Integração de componente
com licença incompatível exige decisão do mantenedor registrada em
`docs/LICENSE-DECISION.md`. Nunca adicionar BIOS, firmware, jogos, assets,
saves ou código traduzido de binários proprietários ao repositório.

## Commits e CI

Commits Conventional Commits diretamente na `main`, sem PR, ZIP ou Release,
sem atribuição a assistentes (nada de `Co-Authored-By` ou “Generated with”).
Após o push, acompanhar o GitHub Actions nos seis alvos (Windows x86-64/ARM64,
Linux x86-64/ARM64, macOS Intel/Apple Silicon) e corrigir falhas e regressões
antes de concluir. Ao finalizar, informar apenas commits, funcionalidades
implementadas, testes/CI e limitações restantes.
