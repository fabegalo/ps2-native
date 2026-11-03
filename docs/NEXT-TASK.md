# Próxima tarefa curta — continuação da M2.3

A interface serial do MCH (`MCH_RICM`/`MCH_DRD`) e a cadeia Direct RDRAM do datasheet da Rambus estão modeladas e testadas. As três BIOS reais (SCPH-70012/77008/90006) continuam parando no mesmo ponto:

- **Onde:** `SW` em `0x1000F410`, em `0x9FC4135C`, depois de cerca de 637.600 instruções.
- **Por quê:** esse registrador é escrito antes de qualquer transação serial e não tem documentação pública.

A ordem completa dos acessos está em `docs/M2.3-STATUS.md`.

Decisão do mantenedor necessária antes de continuar, nesta ordem:

1. **`F410 = 0`, `F480`/`F4C0`/`F490`:** aceitar como latches sem efeito modelado (hipótese registrada, como os latches DEV9) ou manter a recusa.
2. **`0x1000F520`:** valor de leitura; a BIOS o compara a uma tabela de configurações.
3. **Peças de 128 Mbit:** obter o datasheet da Rambus de 128/144 Mbit (ou equivalente Toshiba/Samsung) com:
   - `SOP 0xE`/`0xB`;
   - o mapa de registradores (`0x4C`, `0x4D`…);
   - o estado ao ligar.
   
   A BIOS usa tudo isso antes do primeiro `SETR`.

Regras que continuam valendo:

- Cada registrador modelado precisa de comportamento documentado e de teste sintético; acessos não modelados continuam falhando com nome.
- Preservar os 18 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
