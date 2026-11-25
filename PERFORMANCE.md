# PERFORMANCE — GTA VI Xbox no Windows via VIctory (DexNom)

Medido nesta máquina: **Intel Core i3-10105 (4 núcleos / 8 threads), 32 GB DDR4,
GeForce RTX 3050 8 GB, 2× 1920×1080, Windows 11**. Jogo:
`GTA6.Xbox.v1.0.2.1-CYBERLEEK\game_scarlett_master.exe` (TitleID `7952987A`).

Todos os números vêm da linha de frame do próprio runtime
(`victory_gpu::host`), que separa o tempo do frame em preparação, espera da GPU
e cópia de volta. Nenhum número aqui é estimado.

## Linha de base

Primeira medida confiável, build do DexNom em `921d8d3`, sem
`--features diagnostics`, dentro do mundo (interior aceso):

| Medida | Valor |
|---|---|
| fps | 1,3–1,6 |
| ms/frame | 600–890 (picos de 5,4–6,9 s durante streaming) |
| draws/frame | 6 886–8 074 |
| RAM do processo | ~16 GB |
| CPU | 70–97 % |
| GPU (medidor do runtime) | 18–26 % busy |
| crashes | 0 |

A GPU ficar a 20 % com a CPU a 90 % já dizia onde estava o gargalo: **a thread
de submissão**, não a placa.

## Onde o frame ia (antes)

`copying back` 345–500 ms de um frame de ~665 ms. O log detalhado
(`host copies in the card's memory`, `shader writes looked for on the CPU`)
mostrou o seguinte:

| Etapa | Custo por frame | Tráfego |
|---|---|---|
| espera dos fetches da GPU (5 por frame) | 150–197 ms | 1,9 MB |
| comparação CPU do que os shaders escreveram | 267–309 ms | 712 MB |
| snapshots dos ranges declarados | 130–169 ms | 690 MB |
| cópias título → cópia do host | 155–207 ms | 172–211 MB |
| trabalho real da GPU | 94–200 ms | — |

Dois fatos explicam tudo:

1. **cinco esperas por frame para mover 1,9 MB.** Cada fetch era um
   `ExecuteCommandLists` + `Signal` + `WaitForSingleObject` que drenava a fila
   inteira antes de copiar 12 KB por range.
2. **1,4 GB de tráfego de memória por frame** para descobrir 1,9 MB de bytes
   alterados. O snapshot copia o range declarado inteiro (690 MB), e a
   comparação lê os dois lados (712 MB × 2). Num i3 com 4 núcleos isso é o
   frame inteiro.

## Alterações feitas

### 1. O fetch da GPU não espera mais no meio do frame

`crates/victory-gpu/src/host.rs`: `relay_card_back` emitia o fetch e esperava
por ele na hora. Agora ele emite, guarda os pieces em `RelayFetch::pending`, e
`relay_fetch_merge` só espera **depois** do merge na CPU das outras cópias — a
GPU executa o fetch enquanto a CPU trabalha.

| | antes | depois |
|---|---|---|
| fetch por frame | 150–197 ms | **1,5–3,5 ms** |

### 2. Mais threads na comparação e no snapshot

`crates/victory-gpu/src/shadow.rs`: o pool de `wide()` era
`available_parallelism()/4` (2 threads num i3 de 8 threads lógicos). Passou a
`/2` (4 threads), com teto 8.

### 3. Instrumentação permanente do "copying back"

`BACK_PARTS` em `host.rs` separa o tempo de cópia de volta em merge do relé,
fetch do relé, card fetch/merge do settle e do submit, e snapshots. É o que
torna cada otimização seguinte mensurável.

### 4. `VICTORY_AUTO_ENTER` (automação de teste)

`crates/victory-hle/src/modules/gameinput.rs`: `VICTORY_AUTO_ENTER=<segundos>`
aperta A (Enter no pad substituto) a cada N segundos por cinco minutos, e
`VICTORY_KEYS` deixou de exigir `--features diagnostics`. Serve para rodar
testes sem ninguém no teclado — a tela de seleção de conta do título só aceita
botão de controle, e com um controle real conectado o teclado não a alcança.

## Resultado medido

Build atual, dentro do mundo, mesma cena (interior da cela com a janela gradeada):

| | antes | agora |
|---|---|---|
| fps | 1,3–1,6 | **1,8–2,1** |
| ms/frame | 600–890 | **486–546** |
| pior frame (streaming) | 5 400–6 900 ms | 5 400–6 900 ms (não mexido) |
| draws/frame | 6 886–8 074 | 5 000–6 900 |
| CPU | 70–97 % | 48 % |
| GPU (medidor do runtime) | 18–26 % | 35 % |
| imagem | correta | correta |
| crashes | 0 | 0 |

Onde o frame vai agora:

| Etapa | Custo | Antes |
|---|---|---|
| comparação do que os shaders escreveram (relé) | 168–227 ms | 267–309 ms |
| snapshots dos ranges declarados | 94–119 ms | 130–169 ms |
| cópias título → host | 99–142 ms | 155–207 ms |
| trabalho/espera da GPU | 136–224 ms | 94–200 ms |
| fetch da GPU | **1,4–2,1 ms** | 150–197 ms |

## Atualização do upstream (`921d8d3` → `2e01ab4`)

Três commits do DexNom entraram por merge, sem perder nada do que está aqui:

| Commit | O que traz |
|---|---|
| `b8ff22e` | sincroniza 15 arquivos do renderer privado: descriptor tables, draw, memória, `material_demand`, `shadow`, `texture`, emissão de shader |
| `4775e8a` | isola o teste de leitura de material do avanço concorrente de frame (só teste) |
| `2e01ab4` | texturas de teste em cache ficam dentro da memória reservada (só teste) |

O que interessa para o fps é o primeiro, e num ponto específico: o pool de
snapshots. Antes eram **8 buffers** reaproveitados; agora é um `SnapshotPool`
com orçamento de **512 MiB / 512 buffers**, escolhendo o menor backing que
serve. O comentário do upstream diz o motivo: *"limiting this pool to eight
buffers discarded already committed pages even when most of the byte budget was
free"*. Ou seja — o custo de snapshot que este fork mediu (94–119 ms) era em
parte alocação nova e page fault, não cópia.

### Medido com o merge, mesma build, mundo carregado

| | antes do merge | com o merge |
|---|---|---|
| fps | 1,8–2,1 | **1,5–2,0** |
| ms/frame (cena leve, ~1 000 draws) | 486–546 | **499–506** |
| ms/frame (cena pesada, ~5 000 draws) | — | 588–760 |
| draws/frame | 5 000–6 900 | 976–5 182 |
| cópias título → host | 99–142 ms | 64–182 ms |
| trabalho/espera da GPU | 136–224 ms | 122–410 ms |
| relay merge | 168–227 ms | 124–306 ms |
| snapshots | 94–119 ms | 99–134 ms |
| imagem | correta | correta |
| crashes | 0 | 0 |

**Veredito: o merge é neutro, não é regressão.** Na mesma cena (o interior do
começo, ~1 000 draws, cache quente) deu **499–506 ms** contra 486–546 da build
anterior — dentro da variação de cena. Os 588–760 ms da primeira medição eram
uma cena de **5 000 draws**, não o merge. O número anterior de "70–150 ms de
custo" estava errado: era comparação de cenas diferentes.

O pool novo de snapshots do upstream também não mudou nada aqui: 99–113 ms com
ele, 94–119 sem.

### Cache de shader: o merge invalida os 5 GB

O merge mexeu em `victory-shader/src/emit/lower.rs` e `typed_views.rs`, e isso
muda o hash de todo shader traduzido. Consequência medida: o primeiro run depois
do merge passou **~4 minutos a 10–12,5 s por frame** com
`dropped 764 without a host pipeline (961 being made)`, reescrevendo 72 885
arquivos de cache (5,2 GB). Depois assentou em 588–760 ms.

Para não pagar isso dentro do jogo, `warm-shaders.bat` roda
`victory-warm.exe`, que traduz e compila tudo para o cache antes:

```bat
target\release\victory-warm.exe "D:\...\game_scarlett_master.exe"
```

Regra prática: **mudou o emissor de shader (qualquer merge do upstream mexe),
rode o `warm-shaders.bat` antes de medir fps.** Medir com cache frio dá 12 s por
frame e não diz nada sobre o runtime.

## O jogo estava renderizando em 4K

O runtime informa ao título qual televisão está plugada. O default do
`display_size()` em `crates/victory-gpu/src/dxgi.rs` é **3840×2160** — com um
`shortcut:` do próprio autor dizendo que ainda não usa o tamanho do monitor do
PC. O log de toda run provava:

```
INFO victory_gpu::dxgi: display mode reported to the title width=3840 height=2160
```

Numa janela de 1920×1080. O jogo montava os alvos em 4K, desenhava **4× os
pixels** e o runtime encolhia para a janela. Consequência medida: alvos de 4K
comendo VRAM que faltava para as texturas (9 194 esperando) e GPU a 30–50 %.

`VICTORY_DISPLAY_MODE=1920x1080` diz a verdade ao título. Medido, mesma cena:

| | 4K reportado | 1080p reportado |
|---|---|---|
| imagem | camisa chapada, chão preto, carro flutuando, cubo cinza | pôr do sol, god rays, chão texturizado, plantas, gente no pátio, vidro com reflexo |
| GPU ocupada | 30–50 % | **21–27 %** |
| espera da GPU | 122–410 ms | **122–256 ms** |
| ms/frame | 588–760 | **573–650** |
| fps | 1,5–2,0 | 1,5–1,7 |
| texturas esperando | 9 194 | 17 051 → 8 996 caindo |

**O ganho maior não é fps, é imagem e teto.** O frame agora tem GPU a 21–27 % e
CPU a 47 %: o gargalo virou 100 % o caminho de cópia na CPU (`relay merge
230–312 ms + snapshots 126–140 ms`), não a placa. Antes a GPU mascarava isso.

Prova: print da cena em `logs/shot-1080p.png` (fora do repo, `/logs/` é ignorado).

## O que foi tentado e descartado (com o número)

| Tentativa | Resultado |
|---|---|
| `VICTORY_CARD_BUFFERS_MIN_MB=1` | no-op: o padrão já é 1 MiB |
| `VICTORY_CARD_REHASH_RUNS=1` + `VICTORY_JOIN_COPIES=1` | pior: 660–1 193 ms, mais tráfego |
| `VICTORY_CARD_BUFFERS=0` | quebra o boot: fica no menu, 30 fps, 13 draws |
| `VICTORY_INPLACE=2` + `VICTORY_CARD_BUFFERS=0` + `VICTORY_RELAY=0` | pior: 912–1 386 ms; o in-place é recusado nas cópias grandes (memória de view) |
| `VICTORY_RELAY=1` sozinho | pior: desliga o `relay_exact` |
| `VICTORY_CARD_BUFFERS_IN_RAM=1` (compare na GPU lendo a RAM) | pior: 710–748 ms, GPU 459–467 ms de espera; 700 MB por frame pela PCIe custa mais que a comparação na CPU. Fica no código, desligado |
| `VICTORY_FAST_COPY=1` (`memcpy` com SEH, `fastcopy.c`) | pior: cópias 127–205 ms contra 104–147; um range com uma página faltando falha inteiro e o retry de 64 KiB custa mais que a cópia do kernel. Fica no código, desligado |
| pool de `wide()`: 2 → 4 → 6 threads | melhora pequena e real: snapshots 130 → 94 ms |

## Por que não dá para ir muito além sem mudar a arquitetura

Com CPU a 48 % e GPU a 35 %, nenhum dos dois está saturado: o frame é
**latência de sincronização e banda de memória**, não cálculo. O caminho de
volta move **1,4 GB por frame** para descobrir **1,9 MB** de bytes alterados:

- snapshot: copia os 705 MB de ranges declarados (o "antes" de cada um);
- comparação: lê os 705 MB do snapshot e os 705 MB da cópia, e acha 1,9 MB.

Nesta máquina (4 núcleos, DDR4) isso roda a ~6,5 GB/s com 6 threads, que é o
teto prático da banda. **A única forma de cortar é parar de tocar nos 705 MB**:
uma máscara de páginas sujas escrita pelos próprios shaders na GPU — 1 bit por
página de 64 KiB, 1,4 KB por frame em vez de 1,4 GB. O próprio código pede isso
no comentário de `Shadow::writing`.

A segunda parte do teto é a GPU: 136–224 ms de trabalho por frame nesta RTX
3050 dentro do mundo. Mesmo com CPU zero, isso limita o frame a ~5–7 fps. Para
30 fps estáveis seria preciso reduzir resolução, draws ou qualidade — e isso é
uma decisão de imagem, não de código.

## Próximas otimizações, por ordem de retorno

1. **Dirty mask na GPU** (o item que falta): cada store dos shaders traduzidos
   marca 1 bit por página de 64 KiB; a CPU lê 1,4 KB por frame. Elimina
   snapshot (94–119 ms) e comparação (168–227 ms) de uma vez.
2. **Paralelizar os landings do relé**: com CPU a 48 %, as 4 landings por frame
   são seriais na thread do relé.
3. **Cópias título → host**: 1,1 GB/s via `ReadProcessMemory`. O `memcpy`
   protegido existe (`fastcopy.c`) mas precisa de um grão de bloco que não
   pague o custo da exceção; a tentativa de 64 KiB não pagou.

