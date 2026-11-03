# Próxima tarefa curta — continuação da M2.7: o controlador não documentado do kernel e a NVM real

Sem a NVM do console, as três BIOS reais param no `ReadConfig` (`S 0x41`) do `EECONF`. Com uma NVM sintética (diagnóstico local, não boot), o IOP passa por VBLANK, `SIFMAN` e espera `MSFLG` do EE. O EE para na primeira leitura de status do controlador não documentado em `0x1F801900` (`0x1A000000` na SCPH-70012), logo depois do reset do GS. Ver `docs/M2.7-STATUS.md`.

1. **Insumo do mantenedor:** a NVM do console correspondente a cada perfil (1024 bytes no formato do PS2Ident), extraída do próprio console, com procedência e SHA-256, fora do repositório.
2. **Insumo do mantenedor:** evidência do dispositivo em `0x1F801900`/`0x1A000000`:
   - documentação que o identifique (endereço de escravo `0x42`, registrador `0x7E`);
   - ou, do console do perfil, os valores que ele responde: status `0x1908` durante e depois da transação, e o byte lido em `0x1900`.
3. **Com o kernel seguindo:** a reescrita do `SYNCV` deve devolver o VBLANK conhecido ao IOP. Depois vem a inicialização do SIF no EE (`MSFLG 0x10000`), e o `SIFMAN` faz `sceSifSetDChain`. Ali será preciso modelar o DMA do SIF (canais 9/10 do IOP, 5/6 do EE), com o enquadramento palavra/*quadword* e a profundidade das FIFOs estabelecidos por evidência, antes de mover bytes.

Regras que continuam valendo:

- Hipóteses isoladas, nomeadas e testadas; nenhum valor copiado de emulador sem justificativa de hardware.
- Nenhuma resposta inventada para o dispositivo não documentado, nenhuma NVM inventada ou distribuída.
- Preservar os 24 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, NVM, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
