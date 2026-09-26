> Source: adapted from the headless harness in adeism/OSkate, branch `arena/01a0cbce-oskate`,
> commit `4bdcb144bb6f82379ee3ebeaddbd1657f65aa263`. Focused runtime regressions also
> incorporate cases from intra-secdsm/OpusSkate commit `62815a02a3102a968956ba793b79357a272fae52`.

# OpusSkate web development harness

Harness headless untuk **`skate.cpp`**: menjalankan kode game yang asli (level, fisika,
trik, skor, NPC, lalu lintas, partikel, audio, replay) **tanpa GPU, tanpa window, tanpa
audio device**, sehingga setiap perubahan bisa diverifikasi lewat angka dan bukan lewat
"kelihatannya jalan". Ini implementasi dari rekomendasi **REVIEW.md §6.7 / §10** dan
prasyarat untuk CI + replay/ghost.

```
$ tools/harness/check.sh
== building (fast) -> /tmp/opus-skate-harness
== running
OpusSkate web harness -- 115 tests selected  [slow suite hidden]
...
  115 passed, 0 failed, 0 pending, 0 unexpected pass  (3.00 s)
  959 586 assertions (8344 per test on average)
HARNESS PASSED
```

## Isi direktori

```
tools/harness/
├── check.sh                  # build + run (lihat --slow/--filter/--tsan/--asan/--junit)
├── harness.h                 # kerangka test kecil: TEST, CHECK, CHECKM, XCHECK, PENDING
├── fixture.h                 # initGame/resetWorld/rebuildLevel, tickPlayer, Sim, hash,
│                             # monkey runner, pembaca-penulis file replay
├── runner.cpp                # main(): #define main oskate_main + #include "skate.cpp",
│                             # daftar test, benchmark, JUnit, mode monkey/replay/dump
├── stub/SDL2/                # stub SDL2 + OpenGL: cukup untuk mengompilasi game headless
└── suites/
    ├── suite_math.cpp        # vektor, matriks, RNG, hash, font, layout HUD
    ├── suite_level.cpp       # geometri level, gap, letter, jalur NPC, jebakan tabrakan
    ├── suite_physics.cpp     # ollie, coyote time, curb, dinding, sungai, ramp, tick rate
    ├── suite_tricks.cpp      # flip, grab, spin, grind, manual, combo, huruf, gap score
    ├── suite_world.cpp       # NPC, dodge, tabrakan, mobil, lampu lalu lintas, partikel
    ├── suite_audio.cpp       # sintesis SFX, voice allocator, sequencer musik, mixer
    ├── suite_perf.cpp        # benchmark + budget (build level, frame, tick, startup)
    └── suite_replay.cpp      # determinisme, rekam→putar ulang, fuzz, monkey coverage
```

## Cara pakai

```bash
tools/harness/check.sh                    # semua test + benchmark (~3 s)
tools/harness/check.sh --no-bench         # hanya test
tools/harness/check.sh --slow             # + pencarian rute huruf yang mahal (~0.2 s)
tools/harness/check.sh --filter tricks    # hanya test yang namanya mengandung "tricks"
tools/harness/check.sh --repeat 3         # jalankan 3x: berburu flakiness & kebocoran state
tools/harness/check.sh --junit out.xml    # laporan JUnit untuk CI
tools/harness/check.sh --tsan             # ThreadSanitizer pada test audio konkurensi
tools/harness/check.sh --asan             # AddressSanitizer + UBSan
```

Build manualnya sama dengan yang dilakukan `check.sh`:

```bash
g++ -O2 -std=c++17 -Itools/harness/stub -Itools/harness -I. -o /tmp/opus-skate-harness tools/harness/runner.cpp
```

Alat bantu interaktif (tanpa mengubah kode):

```bash
/tmp/opus-skate-harness --list                     # daftar test
/tmp/opus-skate-harness --dump                     # isi level: solid, rail, gap, huruf, spawn NPC
/tmp/opus-skate-harness --monkey 30 --seed 7       # fuzz input deterministik 30 detik
/tmp/opus-skate-harness --record run.oskr --monkey 30 && /tmp/opus-skate-harness --replay run.oskr
/tmp/opus-skate-harness --goto "FOUNTAIN GAP"      # pindahkan skater ke sebuah gap (eksperimen manual)
/tmp/opus-skate-harness --bench-only               # tabel benchmark saja
```

## Aturan desain (jangan dilanggar)

1. **Kode game yang diuji, bukan tiruannya.** `runner.cpp` melakukan
   `#define main oskate_main` lalu `#include "skate.cpp"`; tidak ada salinan fisika.
2. **Determinisme.** `resetWorld()` mengembalikan NPC, pigeon, mobil, partikel, timer,
   pemain, audio, dan **semua stream RNG** (`prng`, `npcRng`, `pigeonRng`) ke keadaan
   awal proses. Setiap test harus lulus baik dijalankan sendiri maupun setelah test lain
   (`--repeat` adalah pemeriksanya).
3. **Tanpa GPU/audio.** `stub/SDL2/` menyediakan API yang dipakai game; `initAudio(true)`
   mensintesis semua buffer tanpa membuka device. Untuk menguji mixer dan voice
   allocator, `openFakeAudioDevice()` menyalakan flag `audioDev` sehingga jalur kode itu
   benar-benar berjalan (lock/unlock jadi no-op, seperti audio device yang hilang).
4. **Test di dalam `namespace hns`** dan nama fungsi unik (`TEST(suite, name)`).
5. **Angka dulu, baru opini.** Setiap tes mencetak hasil ukurnya (`printf`) supaya
   kegagalan menjelaskan dirinya sendiri, dan supaya benchmark sekaligus menjadi
   dokumentasi perilaku.

## Perubahan kecil di `skate.cpp` demi testabilitas

Semuanya tidak mengubah perilaku gameplay:

| Perubahan | Alasan |
|---|---|
| `Rng` punya `uint64_t draws` | memberi bukti "siapa yang menarik angka acak"; dipakai untuk membuktikan update NPC/traffic tidak menyentuh RNG gameplay (syarat replay) |
| `npcRng` / `pigeonRng` dinaikkan ke file scope | RNG kosmetik harus bisa ditulis ulang; ini akar penyebab divergensi **60 detik replay** yang sebelumnya membuat ghost/replay tidak mungkin |
| `shopIdx = 0` di awal `buildLevel()` | `buildLevel()` dipakai ulang oleh editor/harness; tanpa reset, hasil rebuild tidak identik (toko memakai warna berbeda) |

## Cakupan per suite (ringkas)

| Suite | Contoh hal yang dijaga |
|---|---|
| math | konvensi matriks, inverse, `wrapPi`, `damp` bebas frame-rate, RNG deterministik, font & layout HUD |
| level | `ground()` grid vs brute force, 615 solid, rail tidak terkubur, 34 gap bisa **dihasilkan** ollie lurus, huruf & spawn aman, tidak ada titik yang terjebak di dinding |
| physics | apex ollie 1.82 m / 1.08 s, push maksimum 10.2 m/s, coyote window 0.14 s, curb naik/turun, wall slam → respawn, sungai, ramp, quarter-pipe, konsisten di 120/60/30 Hz |
| tricks | nama & poin flip, grab harus dilepas, aturan pendaratan spin, grind balance, manual, multiplier combo, huruf, urutan poin garis referensi |
| world | NPC mengikuti jalur dan menghindar, tabrakan pejalan kaki, mobil berhenti di lampu merah dan menjaga jarak, partikel mati di permukaan kolam, HUD tanpa index overrun |
| audio | 17 one-shot ternormalisasi & tanpa DC, batas 32 voice, pola sequencer per 8 bar, mixer tidak pernah > 1.0 (limiter `tanh`), parameter ekstrem/NaN, biaya CPU mixer |
| perf | waktu build level, startup, biaya `ground`/tabrakan/tick, biaya mesh + HUD per frame, sistem dunia per tick, kecepatan simulasi, pertumbuhan tak terbatas |
| replay | hash pemain & dunia identik untuk input identik, rekam→putar ulang (memori & file), seed berbeda menyimpang, fuzz tidak pernah menghasilkan NaN |

## Menambah test

```cpp
// suites/suite_world.cpp
TEST(world, pigeons_never_enter_the_river) {
    initGame();
    resetWorld();
    for (auto& p : pigeons) CHECK(world.ground(p.pos.x, p.pos.z, 6.f).surf != SURF_WATER);
}
```

- `CHECK(cond)` / `CHECKM(cond, "pesan")` → gagal bila tidak benar.
- `PENDING("alasan")` → tandai tes sebagai *pending* (fitur/batas yang diketahui).
  Tes tetap merah bila ada `CHECK` yang gagal, jadi pending tidak menyembunyikan regresi.
- `XCHECK(cond, "fitur apa")` → kebalikannya: lulus berarti fitur yang dulu belum ada
  sekarang ada, dan harness menjadi merah agar ekspektasinya dipromosikan menjadi `CHECK`.
- `TEST_SLOW(...)` → hanya dijalankan dengan `--slow`.

## Benchmark saat ini (acuan regresi)

| Metrik | Nilai | Catatan |
|---|---|---|
| `buildLevel()` | ~3.3 ms | 615 solid, 185 rail, 34 gap, 13 jalur NPC |
| `genSfx()` + normalise | ~62 ms | 17 one-shot, 2 MB @ 44.1 kHz |
| mesh statis | 61 322 tris / 3.37 MB | dibangun sekali saat startup |
| mesh + HUD per frame | ~0.47 ms | budget 16.6 ms @ 60 FPS |
| sistem dunia per tick | ~26 µs @ 120 Hz | NPC + pigeon + mobil + partikel |
| `world.ground()` | ~0.03 µs | 200 000 sampel |
| kecepatan simulasi | ~285× real time | 120 detik gameplay ≈ 0.4 detik |
| mixer 5 detik | ~100 ms | 2 % dari real time |

## CI

`tools/harness/ci/github-actions.yml` berisi workflow siap pakai: menjalankan
`check.sh --junit` di ubuntu + macos, mengunggah laporan JUnit sebagai artefak,
mencetak benchmark, dan menjalankan `--tsan` sebagai job **non-blocking** (karena
temuan B2 masih terbuka). Aktifkan dengan:

```bash
mkdir -p .github/workflows
cp tools/harness/ci/github-actions.yml .github/workflows/harness.yml
```

Harness tidak butuh dependensi apa pun selain compiler C++17, jadi job-nya selesai
dalam hitungan detik dan tidak memerlukan GPU atau perangkat audio.

## Batas yang diketahui

- **Dua tes pending**, keduanya kebutuhan rute khusus yang belum bisa diskrip harness:
  huruf `S` (di atas patung air mancur — butuh kicker) dan `K` (di atas atap taksi),
  serta catatan bahwa pencarian rute hanya mencoba lari lurus + ollie.
- **Tiga rail** di tepi timur teras plasa (x = 58) menempel pada fasad gedung kaca
  sehingga tidak bisa dipakai; `rails_are_supported_and_usable` menahan jumlahnya
  (`blocked <= 3`) sebagai temuan yang belum diperbaiki (REVIEW.md §5).
- `--tsan` **sengaja** melaporkan race pada `aud`/`musicOn` (REVIEW.md **B2**): audio
  callback membaca struct yang ditulis main thread tanpa sinkronisasi. Exit code
  dinonaktifkan supaya CI bisa memantau sampai bug itu diperbaiki.
- Harness tidak menyentuh jalur rendering GL (shader, FBO, draw call) — itu masih perlu
  build game dengan SDL2 di mesin ber-GPU.
