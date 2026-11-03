# Próxima tarefa curta — continuação da M2.5: a configuração da NVM do MECHACON

As três BIOS reais concluem a configuração de memória, iniciam o kernel em RAM e param no `LW` em `0xBC0003C0` (PC `0x80007504`): o `EECONF` do IOP abre a configuração do MECHACON (`S 0x40`) e pede `ReadConfig` (`S 0x41`), que lê a NVM (EEPROM) do próprio console. Ver `docs/M2.5-STATUS.md`.

1. **Insumo do mantenedor:** uma imagem da NVM do console correspondente a cada perfil (dump do próprio console, com procedência e SHA-256, fora do repositório), ou decisão registrada sobre como obtê-la. Nenhum conteúdo padrão inventado.
2. **`--nvm=ARQUIVO` por perfil:** carregar a imagem, validar tamanho e vínculo com o perfil/ROMVER, e servir `ReadConfig` a partir do layout de configuração documentado para a revisão do MECHACON (layout com evidência; recusar o que não tiver).
3. Rodar as três BIOS, comparar interpretador × híbrido e seguir para o próximo componente (provavelmente SIF/entrega de interrupções do EE).

Regras que continuam valendo:

- Hipóteses isoladas, nomeadas e testadas; nenhum valor copiado de emulador sem justificativa de hardware.
- Preservar os 21 testes, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, NVM, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
