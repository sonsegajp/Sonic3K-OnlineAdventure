# Building Online Adventure from source

Most players should download a release instead. These steps are for developers.

## What you need

- Windows 10/11 x64, **Visual Studio 2022** with the C++ desktop workload, **CMake 3.20+** and **Python 3.10+**.
- The **unmodified Sonic 3 & Knuckles Recomp v0.5.2 source**, including its `segagenesisrecomp` runner, recompiler, `recomp-net`, `rbengine`, SDL2 and ImGui. You also need a checkout matching the shipped v0.5.2 build, called "stock" below.
- **MinHook v1.3.4**.
- Your own combined *Sonic 3 & Knuckles* World ROM, which is used only to generate local code.

No game code, ROM data or generated files are stored in this repository.

## Folder layout

`CMakeLists.txt` expects these paths by default. You can override any of them with `-D`:

| Variable | Default | Meaning |
|---|---|---|
| `S3_ROOT` | `../Sonic3KRecomp` | Sonic 3 & Knuckles Recomp source checkout |
| `STOCK_ROOT` | `../stock-reference` | Matching unmodified v0.5.2 source and build |
| `MINHOOK_ROOT` | `../minhook` | MinHook v1.3.4 |

## Steps

1. Generate the selected native hooks from your ROM. This writes the generated files into your build folder:

   ```powershell
   python tools/prepare_guest.py `
     --sonic-root <STOCK_ROOT> `
     --recompiler <STOCK_ROOT>/build/Release/GenesisRecomp.exe `
     --rom <path to your sonic3k.bin> `
     --build build
   ```

2. Configure and build the Release configuration:

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DOA_TEST_CONTROL=OFF
   cmake --build build --config Release
   ```

3. The outputs are `build/Release/version.dll` and `build/Release/OnlineAdventure.dll`. Install them as described in the README. `OnlineAdventure.dll` goes in `mods/online-adventure/`.

## Test builds

`-DOA_TEST_CONTROL=ON` compiles a local test socket and direct game-memory controls used by integration tests. **Never distribute such a build.** Release builds must use `OA_TEST_CONTROL=OFF`.

Unit tests are in `tests/`. The native integration fixtures that drive several game processes are not part of this repository.
