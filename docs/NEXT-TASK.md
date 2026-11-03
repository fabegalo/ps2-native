# Próxima tarefa curta — continuação da M2.1

A BIOS real (SCPH-70012/77008/90006) para no `LBU` de `0x1F803204` (*TOOL board id*, ver `docs/M2.1-STATUS.md`). A evidência foi reunida (seção *Evidência sobre `0x1F803204`* do status):

- **Fato:** o varejo exige um byte fora das classes de placa de ferramenta (nibble alto 1–3 no EE; `0x60/0x61` no IOP).
- **Hipótese mais sustentada:** `0xFF`, barramento sem dispositivo, medido em `0xBF803800` pelo banco do PS2Ident.

Falta a decisão do mantenedor sobre modelar a leitura como "placa de ferramenta ausente" com esse valor marcado como hipótese, com teste sintético. Em seguida, seguindo o trace da BIOS real, modelar somente o que ela exigir, nesta ordem prevista pela leitura estática do firmware: registradores privilegiados do GS escritos pela rotina de vídeo (`SMODE1/2`, `SRFSH`, `SYNCH1/2`, `SYNCV`, `CSR` — escrita retida e leitura só com valor comprovado), a calibração do relógio no módulo `RDRAM` (T0 em HBLANK + `Count`, já modelados) e o MCH (`MCH_RICM`/`MCH_DRD` em `0x1000F430/0x1000F440`). A prova estática do salto `lui/ori/jr` para destinos constantes ampliaria a cobertura AOT da BIOS. Cada registrador modelado precisa de comportamento documentado e de teste sintético; acessos não modelados continuam falhando com nome. Preservar os 16 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos; nenhum firmware, trace ou tradução da BIOS no repositório. Commit direto na main, sem PR nem Release.
