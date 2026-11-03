# Próxima tarefa curta — continuação da M2.2

As três BIOS reais (SCPH-70012/77008/90006) agora executam cerca de 637.600 instruções (ver `docs/M2.2-STATUS.md`):

- programam o CRTC do GS e calibram o relógio por HBLANK;
- pulsam o DEV9;
- imprimem pelo EE SIO;
- param no `SW` em `0x1000F410` (MCH), em `0x9FC4135C`.

O bloco `0x1000F400–0x1000F4C0` (comando `F410` com bit de ocupado, `MCH_RICM`/`MCH_DRD` para a cadeia Direct RDRAM, `F480` lido de volta) não tem documentação pública. A recusa permanece até haver evidência.

Próximo passo, nesta ordem:

1. Reunir evidência verificável do protocolo do MCH:
   - documentação da Rambus/Toshiba sobre o RAC e o controlador Direct RDRAM;
   - os datasheets das RDRAM usadas no PS2 (registradores `INIT`/`CNFGA`/`CNFGB`/`DEVID` e comandos seriais);
   - comportamento observável do PCSX2, só como referência.
2. Registrar fatos, hipóteses e lacunas antes de qualquer modelo.
3. Apresentar ao mantenedor a decisão de modelar (ou não) a conclusão do comando `F410` e a cadeia serial.

Regras que continuam valendo:

- Cada registrador modelado precisa de comportamento documentado e de teste sintético; acessos não modelados continuam falhando com nome.
- Preservar os 17 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
