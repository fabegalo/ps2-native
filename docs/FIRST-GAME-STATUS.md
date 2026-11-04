# Primeiro jogo comercial — execução parcial, sem jogabilidade

Execução local em 03/11/2026. **A meta de jogabilidade não foi atingida.** O ELF original executa;
inicialização normal, primeiro frame, menu, controles e gameplay não foram demonstrados.
Código original BSD-3-Clause; nenhum código de emulador, firmware, IRX ou jogo foi incorporado.

## Seleção e identidade

- `games/` contém Shadow of the Colossus (USA), ISO 9660 de DVD, e Tetris Plus (USA), CD CUE/BIN.
  O segundo tem `BOOT`, executável `PS-X EXE` e serial `SLUS-00338`: é PS1, não um ELF R5900.
  O único candidato ao runtime EE existente é **Shadow of the Colossus, SCUS-97472**.
- ISO: 2.693.955.584 bytes, SHA-256
  `d431a5b7e6c7009713d743c22ac5fd61077da40d5f7923e3250fe072a226eacb`.
- `SYSTEM.CNF` aponta `cdrom0:\SCUS_974.72;1`; ELF32 MIPS ET_EXEC, 242.940 bytes, entrada
  `0x00100010`, dois PT_LOAD, sem símbolos de função/relocations verificáveis; SHA-256
  `d16ad76ba8cdf87695d94b1316d51c1f248959ec0749814c31bb7a2d55d9a2e1`.
- Dependências presentes no mesmo disco: `IOPRP300.IMG`, `GAMECORE.XFF`, `KERNEL.XFF`,
  `MANAGER.XFF`, `STARTUP.XFF`, `SG2IOPM1.IRX`; diretórios MODULES2 (MCMAN/MCSERV) e
  MODULES (DBCMAN, DS1O_D, LIBSD, MC2_D, PL2303, SIO2D, SIO2MAN, USBD).
  XFF exige carregamento dinâmico; o bootstrap pequeno não representa todo o código do jogo.
  Não se infere a ordem de uso dos módulos apenas por estarem no disco.

## Execução e bloqueios

| Rota | Evidência | Limite |
|---|---|---|
| Direct ELF inicial | 86.277 instruções retiradas; syscall `v1=0x3C` (SetupThread), PC `0x0010006C` | Entrega da exceção ainda ausente antes desta mudança |
| Direct ELF corrigido, interpretador | 86.278 instruções; `Cause=0x20`, `EPC=0x0010006C`, `EXL=1`; PC `0x80000180` | Checkpoint na entrada da exceção; nenhum handler/kernel é instalado pelo modo bare-metal |
| Direct ELF corrigido, AOT | 86.277 AOT + 1 interpretada, uma troca, mesmo checkpoint/COP0 | 23 instruções estáticas em 4 blocos; 99,9988% da execução deste prefixo, dominado pelo laço de limpeza, não do jogo completo |
| Boot normal do firmware pareado | 17.690.079 instruções; PC `0x80007D20`, zero escritas de pixels | ID `0x7E` do codificador de vídeo, barramento serial `0x42`, não medido |

O firmware é ROMVER `0200EC20040614`, ROM0 SHA-256 `d6653f4e…c6685b`, com a NVM Dragon
SCPH-70004 verificada (`5c3c7a94…b0c39b`), perfil GH-032-11. Não se usou outra revisão,
NVM sintética ou resposta assumida de MMIO. O boot normal foi repetido com o núcleo atual e
confirmou o mesmo bloqueio físico já identificado em M2.8. A outra placa possível e demais
limites dos dados são descritos em [M2.8](M2.8-STATUS.md).

### Experimento separado com kernel parcial

Para continuar verificando funcionalidades necessárias ao mesmo jogo, um harness **local, ignorado**
carregou os segmentos do ELF na RAM do firmware já interrompido. Preservou ROM0, IOP, TLB,
relógio e bits CU; selecionou explicitamente BEV=0, entrada e SP do ambiente de desenvolvimento.
Isso **não executa ExecPS2, não conclui o boot e não comprova um kernel inicializado**.
Os handlers e os resultados de serviços vêm de instruções Sony já residentes em RAM;
nenhum serviço foi interceptado ou substituído por retorno do host.

Esse experimento permitiu localizar e corrigir, em sequência:

1. Retorno do handler ao modo usuário (`ERET` em `0x80000328`), após SetupThread real.
2. Leitura de `T3_MODE`, PC `0x001102D4`, após 302.574 instruções.
3. CACHE DXWBIN por índice em VA zero, PC `0x80002AC8`, após 306.617.
4. PCPYH (`0x001283FC`, 309.275) e PCPYLD (`0x00128408`, 309.278).
5. `kuseg` com ERL durante seção crítica, PC seguinte `0x001103E4`, após 312.657.

Última repetição: **312.702 instruções retiradas**, parada em `0x001103E8` ao escrever
`T2_MODE=0xF82` (`0x10001010`): interrupções de temporizador/INTC ainda não implementadas.
O primeiro experimento zerava CU junto com o Status e parava indevidamente no MFC0 do jogo;
o resultado final preserva esses bits. Nenhuma dessas contagens promove o estágio de aceitação
além de "ELF executa". Não houve imagem, menu original, input ou jogabilidade.

## Código entregue e regressões

- `DiscImage`: setores 2048 e CD raw 2352 Mode 1/Mode 2 Form 1, diretórios/subdiretórios,
  BOOT2 e ELF pelo loader comum. Limites de extensão/leitura, inteiros both-endian e recusas
  explícitas de PS1, Form 2, multi-extent, imagens truncadas e formatos não modelados.
  Leitura aleatória pelo I/O comum; não carrega/copia a ISO inteira. CUE/compressão continuam fora
  dessa API; o probe do launcher existente permanece independente e inalterado.
- SYSCALL/BREAK entregam a exceção arquitetural, preservando EPC/BD em EXL aninhado,
  cancelando o branch pendente e retornando pelo dispatcher. Não implementam serviços HLE.
- Modo usuário/supervisor aplica permissões de segmento e TLB; EXL/ERL selecionam kernel.
  COP0/CACHE sem permissão falham; ERL mapeia os 2 GiB de kuseg por identidade, sem alias de 29 bits.
- CACHE index não traduz pela TLB; operações hit conservam a tradução. O modelo continua coerente
  sem cache físico; acesso a tags/dados não suportados continua recusado.
- PCPYH/PCPYLD compartilham semântica AOT/interpretador, inclusive alias entre operandos e `$zero`.
- Tn_MODE lê a configuração real do subconjunto suportado. EQUF/OVFF permanecem claros porque
  contar com interrupções habilitadas é recusado; não se inventam flags nem conclusão de IRQ.
- CLI `ps2n-run`: ISO via `--disc`, trace/tail, PC/COP0 e escritas GS. `ps2n-aot --disc`
  mantém a identidade do ELF e a geração idempotente. `ps2n-disc` lista/extrai BOOT2 para AOT offline.

Windows x86-64/MSVC Release: 5 testes focados aprovados inicialmente; **38 CTests aprovados**
na regressão final do núcleo (28,52 s), incluindo a guarda de privilégio CACHE. A CI por commit
é registrada abaixo. O teste de compilação em caminhos longos não
foi repetido localmente, pois o adaptador de paths/build não mudou; permanece na CI dos seis alvos.
O novo ELF próprio compara trace, memória, GPRs, COP0 e execução híbrida/interpretada de
SYSCALL, BREAK, delay slot, likely anulado e instruções MMI; 69 instruções, 14 AOT/55 interpretadas.
Testes separados cobrem EXL aninhado, ERL, permissões, cache index/hit, disco e recusas.

Não houve otimização de performance nesta tarefa. As três amostras do novo programa de exceções
mostraram híbrido 0,0419/0,0622/0,0542 ms contra interpretador 0,0363/0,0357/0,0354 ms:
**sem ganho** nessa carga curta. Não se apresenta FPS ou desempenho de gameplay.
Build incremental preserva os fontes AOT idênticos; a verificação posterior informou `ninja: no work to do`.

## Reprodução local

```sh
ps2n-disc "games/Shadow of the Colossus (USA).iso"
ps2n-aot "games/Shadow of the Colossus (USA).iso" out/game-first/scus.cpp --disc --hybrid
# Configurar o build existente com PS2N_ELF_AOT_SOURCE apontando esse C++ local.
ps2n-run-hybrid "games/Shadow of the Colossus (USA).iso" --disc --budget=1000000 --stop=0x80000180 --trace-out=out/game-first/actual-direct-hybrid.log
```

O endereço de parada é o vetor arquitetural da CPU, não um patch do título. A chegada ao checkpoint
não significa que SetupThread foi atendido. Sem kernel, continuar por RAM zerada não seria
inicialização válida do jogo. O launcher não foi alterado.

Registros locais ignorados: `out/game-first/` contém hashes completos de 18 arquivos de bootstrap,
IRX e XFF em `disc-metadata.json`, inventários, fonte/binário do harness, logs de cada bloqueio,
tradução AOT e relatórios de testes. Esses arquivos não são publicados. A ISO original permanece intacta.

## Referências e dados ainda necessários

Implementação independente baseada nos contratos dos manuais:
[EE Core, exceções e memória](https://docs.alexrp.com/mips/ee.pdf),
[EE Instruction Set, PCPYH/PCPYLD e CACHE](https://docs.alexrp.com/mips/ee_insns.pdf),
[EE User, Tn_MODE](https://manuals.plus/m/55bdc71d3aebc33752a7dad6526b78248b6c258294fcf8a3ebed1f010da22365.pdf).
As avaliações de gow-native-pc, PS2Recomp, Play! e PCSX2 continuam em
[REFERENCE-AUDIT](REFERENCE-AUDIT.md); nenhum código dessas referências foi integrado.

O ID do codificador não está na ROM0/NVM e não há captura desse registrador entre os recursos locais.
É preciso evidência do hardware compatível para concluir essa rota sem valores inventados.
Também faltam implementação de timer/INTC, serviços IOP/SIF e os demais dispositivos que a execução
vier a exigir. Não se confunde esse trabalho interno pendente com o dado físico ausente.
O próximo trabalho do mesmo jogo fica exclusivamente em [NEXT-TASK](NEXT-TASK.md).
