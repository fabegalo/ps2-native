# Próxima tarefa curta — continuação da M2.4

As três BIOS reais (SCPH-70012/77008/90006) param no `SW` em `0x1000F410` (`0x9FC4135C`, cerca de 637.600 instruções). Não há evidência de hardware para os próximos passos (ver `docs/M2.4-STATUS.md`).

Insumos necessários do mantenedor, nesta ordem:

1. **`F520`:** valor de `D_ENABLER` (`0x1000F520`) logo após o reset, medido em cada console real (SCPH-70012/77008/90006). A BIOS o usa como chave de uma tabela de 16 gerações do MCH. Será registrado por perfil de hardware, sem valor global.
2. **Peças de 128/144 Mbit:** datasheet com:
   - `SOP 0xE`/`0xB`;
   - o estado ao ligar;
   - o layout de `TPARM`.
   
   A BIOS usa tudo isso antes do primeiro `SETR`.
3. **`F410 = 0` e `F480`/`F4C0`/`F490`:** decidir entre escrita sem efeito modelado (hipótese registrada) e recusa até haver documentação.

Com esses insumos: modelar cada item com teste sintético, rodar as três BIOS no interpretador e no modo híbrido e registrar o novo bloqueio.

Regras que continuam valendo:

- Sem valores copiados de emuladores sem justificativa de hardware; acessos não modelados continuam falhando com nome.
- Preservar os 19 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
