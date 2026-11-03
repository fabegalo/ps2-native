# Contribuindo / Contributing

## Critério de aceite de uma funcionalidade

- Explicar o comportamento esperado do hardware convidado e sua fonte de referência.
- Implementar sem retorno de sucesso falso, hacks por jogo não documentados ou silenciar exceções.
- Adicionar caso MIPS sintético, teste CTest reproduzível e estado de compatibilidade atualizado em `docs/M0-STATUS.md`.
- Executar `cmake --build out/build` e `ctest --test-dir out/build --output-on-failure`.
- Não adicionar BIOS, firmware, jogos ou código traduzido proprietário. Não integrar GPL em código anunciado como BSD.
- Manter uma única implementação comum; somente backends de plataforma podem divergir. Não criar cópias de ISOs, SDKs ou builds para cada plataforma.

## Commits

Conventional Commits, sem mensagens publicitárias nem assinaturas automáticas de agentes. Exemplos:

`feat(ee): add verified JALR dispatch`

`test(aot): validate branch delay behavior`

`fix(elf): reject invalid segment sizes`

No releases until end-user functionality is actually executable and verified.
