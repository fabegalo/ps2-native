# Próxima tarefa curta — codificador de vídeo, fase vertical e INTC do EE

O GS já produz framebuffer sintético por comandos reais do convidado, mas a BIOS/NVM da SCPH-70004 continua em "Initialize GS": EE em `0x80007D20`, após 17.690.079 instruções, na leitura do ID `0x7E`. O IOP para em `0x000040B0`, após 2.210.789 instruções. Nenhum pixel da BIOS foi gerado. Ver `docs/M2.9-STATUS.md`.

1. **Insumo do mantenedor:** medir, no console SCPH-70004 (ou num slim da mesma geração), os bytes dos registradores `0x7E` e `0x7F` do codificador de vídeo, ou trazer a documentação do chip. Com isso, o valor entra no perfil como medição e não como hipótese.
2. **Estado do GS depois do reset (`CSR.RESET`):** o IMR já tem todas as máscaras documentadas em 1. Ainda faltam os bits de CSR e a fase do PCRTC/VBLANK com `SMODE1.PRST = 1` até a reescrita de SYNCV. No diagnóstico com IDs assumidos, `LW I_STAT` bloqueia em `0x8000E628`; implementar fontes e entrega de exceções do INTC sem substituir pending desconhecido por zero.
3. **Depois do `SetGsCrt`:** a reescrita do `SYNCV` devolve o VBLANK conhecido ao IOP. Em seguida vêm a inicialização do SIF no EE (`MSFLG 0x10000`) e o `sceSifSetDChain`. Ali o DMA do SIF (canais 9/10 do IOP, 5/6 do EE) deve ser modelado com o enquadramento palavra/*quadword* estabelecido por evidência ou teste próprio.
4. **Saída real pelo GS:** após vencer os bloqueios com evidência, observar os comandos do firmware e estender o subconjunto compartilhado de desenho/scanout necessário (interlace, formatos, efeitos ou primitivas). Usar a API `Framebuffer`/`BootOptions::on_frame`; manter o launcher separado. Obter NVM correspondente antes de usar os demais perfis.

Regras que continuam valendo:

- Hipóteses isoladas, nomeadas e testadas; nenhum valor copiado de emulador sem justificativa de hardware.
- Valores assumidos só em diagnóstico explícito (`--assume-video-encoder`); nunca como resultado de boot.
- Preservar as regressões e o digest sintético GS, a semântica compartilhada AOT/interpretador e a CI de seis alvos.
- Nenhum firmware, NVM, trace ou tradução da BIOS no repositório.
- Commit direto na main, sem PR nem Release.
