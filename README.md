# Scarlett VIctory Bench

A fork of [DexNom/VIctory](https://github.com/DexNom/VIctory), which is itself a
community fork of [Marvisak/VIctory](https://github.com/Marvisak/VIctory): it
runs an Xbox Series executable from your own game installation and translates
its GPU work for Direct3D 12 on Windows x64.

This fork exists for one reason: **the frame the runtime produces was bound by
memory traffic, not by the GPU, and nobody had measured it.** Everything here is
a measurement, the fix that came out of it, and the ones that were tried and
made it worse (kept in the code, switched off, with the numbers in the comment
above them).

Game files, executables, saves and generated shader caches stay on your own
computer. Nothing from the game is in this repository — see
[`docs/asset-policy.md`](docs/asset-policy.md).

## What changed, and why

Measured on **Intel Core i3-10105 (4 cores / 8 threads), 32 GB DDR4, GeForce RTX
3050 8 GB, Windows 11**, inside the world, same scene. Upstream's own numbers
come from a Radeon RX 7900 XTX; this fork is the NVIDIA side of that, and the
interior lighting the upstream test report calls wrong renders here (lit
ceiling lamps, bunks, doorway not overexposed):

| | before | after |
|---|---|---|
| fps | 1.3–1.6 | **1.7–2.0** |
| ms/frame | 600–890 | **506–595** |
| CPU | 70–97 % | **46–53 %** |
| GPU (the runtime's own count) | 18–26 % | 31–47 % |
| image | materials in fallback: flat clothes, black ground | **lit and textured** |
| crashes | 0 | 0 |

The GPU sitting at 20 % with the CPU at 90 % was the tell: the submitting
thread was the bottleneck, and the frame line said where it went. The image
was the second tell, and it had a different cause — a display mode the title
had no business believing (see below).

### 1. The title was rendering at 4K into a 1080p window

`display_size()` in `crates/victory-gpu/src/dxgi.rs` reports **3840×2160** to the
title by default — the console mode, not the PC's display. Every run's log said
so:

```
INFO victory_gpu::dxgi: display mode reported to the title width=3840 height=2160
```

So the title built 4K render targets, drew 8.3 million pixels a frame, and the
runtime squeezed the result into a 1920×1080 window. The 4K targets also ate the
VRAM the textures needed (9 194 waiting).

`VICTORY_DISPLAY_MODE=1920x1080` tells it the truth. Same scene, measured:

| | 4K reported | 1080p reported |
|---|---|---|
| image | flat-shirted characters, black ground, floating cars | sunset, god rays, textured ground, plants, people in the yard, glass with reflection |
| GPU busy | 30–50 % | **21–27 %** |
| GPU wait | 122–410 ms | **122–256 ms** |
| ms/frame | 588–760 | **573–650** |

The bigger win is the image and the ceiling, not the frame rate: with the GPU at
21–27 % and the CPU at 47 %, the frame is now entirely the copy path on the CPU.
The screenshots stay out of the repository: game files and run logs never go in.

### 2. The GPU fetch no longer blocks the frame

`relay_card_back` in `crates/victory-gpu/src/host.rs` emitted a fetch of the
bytes shaders wrote to copies in the card's memory and then **waited for it on
the spot** — five times a frame, to move 1.9 MB.

It now emits the fetch, parks the pieces in `RelayFetch::pending`, and
`relay_fetch_merge` only waits **after** the CPU has merged the other copies.
The card runs the fetch while the CPU works.

| | before | after |
|---|---|---|
| fetch per frame | 150–197 ms | **1.4–2.1 ms** |

### 3. The `copying back` cost is now split and reported

`BACK_PARTS` separates the time into relay merge, relay fetch, settle card
fetch/merge, submit card fetch/merge and snapshots, printed on its own line
every five seconds. Every optimisation below was chosen from that line.

### 4. More threads where the memory work is

`wide()` in `crates/victory-gpu/src/shadow.rs` sized its pool at
`available_parallelism() / 4` — two threads on an eight-thread machine — and is
now `available_parallelism() - 2`, capped at 8. Snapshots went from 130 ms to
94 ms.

### 5. `VICTORY_AUTO_ENTER` — a run with nobody at the keyboard

`crates/victory-hle/src/modules/gameinput.rs`. The title's account picker takes
a **pad** button, and with no pad opened the keyboard drives the stand-in pad —
but only while the window has focus, which a headless run does not have.
`VICTORY_AUTO_ENTER=<seconds>` presses A every that many seconds for five
minutes by injecting the key into the keyboard state the stand-in pad reads.
`VICTORY_KEYS` no longer needs `--features diagnostics`.

## What was tried and made it worse

Kept in the code, off by default, with the reason and the number written above
the switch.

| Switch | Result |
|---|---|
| `VICTORY_CARD_BUFFERS_IN_RAM=1` — compare on the card over the copy in the PC's memory | 710–748 ms; 700 MB a frame over the bus costs more than the CPU's compare |
| `VICTORY_FAST_COPY=1` — `memcpy` guarded by SEH (`fastcopy.c`) | copies 127–205 ms against 104–147; one missing page fails the whole range |
| `VICTORY_INPLACE=2` | the driver refuses the big buffers (view memory) |
| `VICTORY_CARD_BUFFERS=0` | the world never loads |
| `VICTORY_CARD_REHASH_RUNS=1` + `VICTORY_JOIN_COPIES=1` | 660–1193 ms, more traffic |

## Where the frame goes now

| Stage | Cost per frame |
|---|---|
| comparing what shaders wrote (relay) | 168–227 ms |
| snapshots of the declared ranges | 94–119 ms |
| title → host copies | 99–142 ms |
| GPU work and wait | 136–224 ms |
| GPU fetch | 1.4–2.1 ms |

CPU at 48 % and GPU at 35 % means neither is saturated: the frame is
**synchronisation latency and memory bandwidth**. The readback path moves
**1.4 GB per frame to find 1.9 MB of changed bytes** — 705 MB of snapshot and
705 MB × 2 of comparison, at ~6.5 GB/s with six threads, which is this
machine's practical ceiling.

The next step is a **dirty mask written by the shaders themselves** — one bit
per 64 KiB page, 1.4 KB a frame instead of 1.4 GB. The comment on
`Shadow::writing` has asked for it since before this fork.

The second half of the ceiling is the GPU: 136–224 ms a frame on an RTX 3050
inside the world. Even with a free CPU that is ~5–7 fps. 30 fps here would mean
fewer draws, a lower resolution or lower quality — an image decision, not a
code one.

Full numbers, method and every discarded attempt:
[`PERFORMANCE.md`](PERFORMANCE.md).

## Run it on your PC

1. 64-bit Windows and a current Direct3D 12 driver. The game uses mesh shaders
   and Shader Model 6.6.
2. Rust for Windows and Visual Studio Build Tools with **Desktop development
   with C++** (x64 MSVC toolset and a Windows 10/11 SDK).
3. `powershell -File .\Initialize-FormatTables.ps1` — verifies and installs the
   BCPack format tables (SHA-256 checked; `-FromCheckout <dir>` takes them from
   a local checkout instead of downloading).
4. `cargo build --release -p victory --bins`
5. `start.bat` — edit the game path in it first. It sets `VICTORY_AUTO_ENTER=8`
   so the account picker passes on its own; `0` turns that off.

`start-rt.bat` runs the same build with `--rt` (structure builder, ray traversal
and light resolve). On an RTX 3050 expect seconds per frame.

## Keeping up with upstream

Nothing syncs by itself — GitHub does not merge fork into fork, and upstream
opens no pull requests here. `origin` still points at `DexNom/VIctory`, so:

```powershell
.\sync-upstream.ps1            # fetch, merge, build, push if the build is green
.\sync-upstream.ps1 -NoPush    # fetch and build only
```

It stops on the first conflict and leaves the merge open to resolve by hand;
the one that recurs is `host.rs`, where this fork's `BACK_PARTS` timing sits in
the same lines as upstream's `WriteGroups`. `fork` is the remote this repo
pushes to.

**After any merge that touches the shader emitter, run `warm-shaders.bat`
before measuring.** Shader hashes change, the 5 GB cache is invalid, and the
first run then spends minutes at 10–12 s a frame compiling pipelines mid-game
(`dropped 764 without a host pipeline (961 being made)`). The preloader
translates and compiles everything up front instead.

Measured cost of the merge itself (`921d8d3` → `2e01ab4`) in this fork: **none
on the same scene** — 499–506 ms against 486–546 ms before it, with a warm
shader cache and ~1 000 draws. The 588–760 ms seen at first was a 5 000-draw
scene, not the merge.

## Upstream

- [DexNom/VIctory](https://github.com/DexNom/VIctory) — the snapshot this fork
  starts from (`921d8d3`).
- [Marvisak/VIctory](https://github.com/Marvisak/VIctory) — the original
  Radeon-tested runtime.

Their README is kept as [`README.upstream.md`](README.upstream.md), and
`BENCHMARK.md` as it was.

## Licence

See [`LICENSE`](LICENSE).
