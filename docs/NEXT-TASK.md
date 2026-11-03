# Próxima tarefa curta — continuação da M2.8: ID do codificador de vídeo e o estado do GS depois do reset

Com a BIOS e a NVM reais da SCPH-70004, o boot real chega a "Initialize GS". O EE para na leitura do registrador `0x7E` do codificador de vídeo (escravo serial `0x42`, mestre em `0x1A000000`), após 17.690.079 instruções. O IOP para no laço do `SIFMAN`, com VBLANK desconhecido depois do reset do GS, após 2.210.789 instruções. Ver `docs/M2.8-STATUS.md`.

1. **Insumo do mantenedor:** medir, no console SCPH-70004 (ou num slim da mesma geração), os bytes dos registradores `0x7E` e `0x7F` do codificador de vídeo, ou trazer a documentação do chip. Com isso, o valor entra no perfil como medição e não como hipótese.
2. **Estado do GS depois do reset (`CSR.RESET`):** o valor de `IMR`, os bits de `CSR` e o comportamento do PCRTC (fase do VBLANK) com `SMODE1.PRST = 1` até a reescrita do `SYNCV`. O kernel lê `I_STAT` do EE logo depois do pacote do GIF. As fontes do `INTC` do EE (GS, VBLANK, timers) precisam desses fatos para ter bits conhecidos.
3. **Depois do `SetGsCrt`:** a reescrita do `SYNCV` devolve o VBLANK conhecido ao IOP. Em seguida vêm a inicialização do SIF no EE (`MSFLG 0x10000`) e o `sceSifSetDChain`. Ali o DMA do SIF (canais 9/10 do IOP, 5/6 do EE) deve ser modelado com o enquadramento palavra/*quadword* estabelecido por evidência ou teste próprio.
4. **NVM real dos outros perfis** (SCPH-70012, SCPH-77008, SCPH-90000), com procedência e SHA-256, fora do repositório.

Regras que continuam valendo:

- Hipóteses isoladas, nomeadas e testadas; nenhum valor copiado de emulador sem justificativa de hardware.
- Valores assumidos só em diagnóstico explícito (`--assume-video-encoder`); nunca como resultado de boot.
- Preservar os 25 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, NVM, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
