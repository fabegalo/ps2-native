# Próxima tarefa curta — continuação da M2.5: o IOP no caminho de boot

As três BIOS reais passam do `F410` e param no `LW` em `0xBC0003F0` (RAM do IOP), em `QueryBootMode(2)` da inicialização de memória do EE (cerca de 1,85 milhão de instruções; 1,11 milhão no dump de 2006-09). Ver `docs/M2.5-STATUS.md`.

1. **Caminho do IOP no ROM:** analisar o reset do IOP (`PRId < 0x59` → `0xBFC02000`) até a gravação da tabela de modos de boot em `0x3F0` (IOPBOOT/LOADCORE do mesmo ROM0), listando instruções, registradores de I/O e a sincronização com o EE.
2. **Modelo mínimo do IOP:** R3000A com load delay slots e COP0 próprio (`Status.IsC`: escritas isoladas vão ao cache), 2 MiB de RAM visíveis ao EE em `0x1C000000`, relógio de 36,864 MHz (razão 8:1 com o EE, hipótese registrada) e recusa nomeada de todo I/O não modelado.
3. **Execução paralela EE/IOP** no `ps2n-boot`, determinística e com trace dos dois processadores; interpretador × híbrido continuam idênticos.
4. Rodar as três BIOS com `--console=`, registrar o novo bloqueio e o primeiro uso do `F410` pela BIOS real.

Regras que continuam valendo:

- Hipóteses isoladas, nomeadas e testadas; nenhum valor copiado de emulador sem justificativa de hardware.
- Preservar os 20 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
