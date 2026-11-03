# Próxima tarefa curta — continuação da M2.6: VBLANK do IOP e a NVM real

Sem a NVM do console, as três BIOS reais param no `ReadConfig` (`S 0x41`) do `EECONF`; com uma NVM sintética (diagnóstico local, não boot), o IOP segue até ~436 mil instruções e para quando as interrupções são reabilitadas com VBLANK início/fim habilitados em `I_MASK`. Ver `docs/M2.6-STATUS.md`.

1. **Insumo do mantenedor:** a NVM do console correspondente a cada perfil (1024 bytes no formato do PS2Ident, `sceCdReadNVM` 0..511), extraída do próprio console, com procedência e SHA-256, fora do repositório. Com ela, rodar as três BIOS com `--nvm=` e registrar as contagens reais.
2. **VBLANK do IOP (IRQ 0/11):** tempo vertical do CRTC do GS a partir de `SMODE1`/`SYNCV` escritos pelo EE, com sincronização do IOP antes de cada escrita que muda o tempo; bordas de início/fim com evidência (ou hipótese nomeada e testada); recusar enquanto o modo de vídeo não estiver definido.
3. Seguir o `IOPBTCONF` (`IOMAN`, `MODLOAD`, `SIFMAN`/`SIFCMD`, `CDVDMAN`): SIF e DMA EE↔IOP, comparando interpretador × híbrido a cada bloqueio.

Regras que continuam valendo:

- Hipóteses isoladas, nomeadas e testadas; nenhum valor copiado de emulador sem justificativa de hardware.
- Nada artificial em `0x3C0`/`0x3F0` da RAM do IOP; nenhuma NVM inventada ou distribuída.
- Preservar os 23 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, NVM, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
