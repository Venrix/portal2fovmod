# Portal 2 Persistent FOV Mod

A minimal Portal 2 plugin that adds one console command:

```
fov <value>
```

Forces your field of view to `<value>` (**45–140**) and keeps it there across map
loads; use `0` to turn it off. The value is saved (to `fovmod.cfg`), so it sticks
across game restarts.

A stripped-down fork of [SourceAutoRecord](https://github.com/p2sr/SourceAutoRecord)
(MIT) with everything but FOV forcing removed.

## Usage

1. Get `fovmod.dll` (Windows) or `fovmod.so` (Linux) and `fovmod.vdf` — download
   them from the [latest release](../../releases), or build from source (see
   [Build](#build)).
2. Drop the binary into your **Portal 2 install folder** (next to `portal2.exe`).
3. Copy `fovmod.vdf` into `Portal 2/portal2/addons/` (create the `addons` folder
   if it doesn't exist). The engine scans that folder at launch and loads every
   plugin listed, so `fov` is available from the console every session.

In the console (needs `-console`), `fov 90` forces the FOV to 90;
`fov 0` disables forcing. The value is remembered across restarts, so you
normally only set it once.

The `.vdf` points at `"file" "fovmod"`, resolved relative to the install root
next to `portal2.exe`, where the binary lives. If the plugin fails to load, move
`fovmod.dll` next to the `.vdf` in `addons/` and change the line to
`"file" "addons/fovmod"`.

## Build

Only needed if you're not using a prebuilt [release](../../releases).

### Windows (Visual Studio)

Open `fovmod.sln`, select **Release | x86**, and build. Output goes to
`bin\fovmod.dll`.

> Must be **32-bit** (Win32). Portal 2 is a 32-bit process. The static CRT
> (`/MT`) is required.

### Linux

```sh
make
```

Produces `fovmod.so`. Needs a 32-bit g++ toolchain (`-m32`).

## Compatibility

Offsets target **Portal 2 build 9568** (current retail), 32-bit, Windows & Linux.
Other Source games / builds would need different vtable offsets in `src/sdk.hpp`.
