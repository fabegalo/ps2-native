# Runtime: fronteiras de evento e tradução de memória — 05/11/2026

Código: `c23188e`. Implementação própria BSD; sem alteração de firmware,
aritmética convidada, clocks, renderer ou código proprietário distribuído.

## Gargalo e referência

O perfil anterior do menu registrou 65.205.240 chamadas de sincronização,
IOP e dispositivos em 30 milhões de instruções EE. GS representava cerca
de 1% do tempo exclusivo instrumentado. Esses tempos incluem overhead de
profiling e não são parcelas dos tempos Release abaixo.

O [scheduler do PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp)
foi referência de organização por próximos eventos, sem copiar código ou
transportar seu kernel HLE. O runtime mantém o firmware convidado e a execução do IOP.
A busca linear nas 48 entradas TLB também continuava em cada acesso traduzido.

## Mudanças e contratos

- A sincronização consulta o estado atual antes de entrar nos motores.
  Conserva a fronteira IOP 8:1, mudanças/desconexão GS mesmo sem aposentadoria,
  Q0/Q1 após E, VIF pendente, recusas e timestamps gráficos de checkpoint.
  DMA ativo continua no parser original, inclusive antes do próximo beat,
  para preservar prioridade e recusa imediatas. Não há deadline persistente
  que possa sobreviver a uma escrita de registrador sem invalidação.
- O cache TLB por instância guarda somente o índice de uma correspondência
  única, por página virtual de 4 KiB e ASID. Toda entrada compara apenas
  bits acima dessa página; o caminho conserva seleção da metade, S, V/D,
  modo de cache e endereço físico. Privilégio e alinhamento continuam no
  barramento comum. Bytes, conhecimento e falhas não são armazenados no cache.
- Escritas TLB e reinicializações invalidam o cache. A leitura de checkpoint
  também limpa os índices copiados do destino antes de substituir a TLB.
  Campos serializados e esquema `c6495503…` permanecem idênticos; os helpers
  e metadados host novos participam integralmente da identidade de runtime.
  A troca de runtime continua exigindo opt-in explícito.

## Medição comercial

Windows x64, Ryzen 7 5700X3D, MSVC 19.44, Release, profiling desativado,
mesmos módulos AOT, entrada solta, afinidade 16384 e prioridade AboveNormal.
Retomada do manifesto local `interactive-menu.json`, orçamento de 30 milhões,
CPU GS para isolar o runtime. Carga e hashes finais ficam fora do intervalo;
scanout dos campos originais fica dentro. Nenhuma compilação concorrente
da tarefa. Os executáveis de controle e sincronização isolada foram congelados.

Cinco trios alternados, mais um trio de aquecimento excluído previamente.
Os 25 intervalos por variante correspondem a cinco intervalos de cada execução.

| Variante | Mediana: 30 milhões (s) | CPU total do processo (s) | Campo original (s) |
|---|---:|---:|---:|
| Controle contemporâneo | 11,341572 | 12,640625 | 1,838974 |
| Sincronização isolada | 10,722638 | 12,031250 | 1,733293 |
| Sincronização + cache TLB | **7,776684** | **9,078125** | **1,254745** |

Combinação: throughput mediano +45,84%, tempo de execução −31,43%.
O tempo de CPU do processo, que inclui carga/relatório/hashes, caiu 28,18%.
Faixas de execução: controle 10,182395–14,494990 s; sincronização
9,260945–10,928277 s; combinação 7,751996–7,936340 s. A combinação venceu
os cinco pares contra ambos os controles. Houve interferência externa,
inclusive no aquecimento; todos os valores foram conservados. O percentual
não é promessa de ganho universal nem medição de uma máquina ociosa.

Uma coleta independente anterior da sincronização isolada teve mediana
11,537564 → 10,936279 s (+5,50% de throughput). No trio final houve um par
em que ela perdeu para o controle. O ganho maior vem da combinação com TLB;
não atribuir à sincronização sozinha o resultado total.

Executável de medida: 11.940.352 → 11.949.056 bytes (+8.704).
Cache host: 256 entradas de índice por Memory (2 KiB nesta ABI).
Launcher local `out/launcher-msvc/ps2-native.exe` recompilado com o mesmo núcleo
e AOT. Não houve comparação de tempo de build nem nova sessão visual.

## Equivalência e limites

Os 18 ensaios da matriz conservaram o estado completo
`80c2d0d3f6635d1b24934ca1853b006f592888bd7a9293daa4126c10cf4da9a5`,
25.446.236 instruções AOT e 4.553.764 interpretadas em cada janela.
Validações separadas conferiram seis campos RGBA, VRAM e máscaras completas,
9.766 amostras PCM com timestamps, 12 polls SIO2, estado e ciclos/IRQ:
controle/novo, interpretação pura, checkpoint de 10 + 20 milhões, Vulkan
e Direct3D 12. Originais de firmware, disco e checkpoint foram preservados.

Vinte testes focados passaram: boot/TLB, ASID, permissões, sobreposição,
páginas grandes, contexto/cache EE, AOT, timers/IRQ, IOP/GS, DMA/SIF,
VIF/VU/Q, SPU2, checkpoint e sessão experimental Qt. A primeira verificação
de checkpoint detectou a mudança indevida de identidade pelos helpers;
o esquema original foi restaurado sem afrouxar a validação.

**1,254745 s por campo continua muito acima de tempo real.** Não demonstra
primeira partida ou gameplay. Busca de instruções continua verificando bytes;
despacho/observação e amostragem por instrução permanecem, assim como o
interpretador explícito. Um novo perfil deve medir esses custos após TLB.

CI de `c23188e` ainda não executada: a revisão automática bloqueou o push
para `origin/main`, exigindo autorização específica para publicar as alterações.
Nenhum ensaio comercial permanece ativo ao encerrar. Evidência local em
`out/runtime-final-summary.json`, `runtime-final-performance-results.json`,
`runtime-final-*-results.json` e `runtime-tlb-tests.log`; não distribuir
artefatos comerciais ou seus traces.
