# Shells: PowerShell, CMD, Git Bash, and WSL

WinTune is a **native Windows** tool (`wintune.exe`). It uses Win32 APIs and
does **not** ship as a Linux ELF binary. That said, developers and users often
work from **Git Bash**, **WSL**, or **PowerShell**. This doc describes what
works in each shell.

---

## Quick matrix

| Task | PowerShell | CMD | Git Bash | WSL |
|------|------------|-----|----------|-----|
| Build / test / package / lint / release | `.\scripts\*.ps1` | `scripts\*.cmd` | `./scripts/<name>` | `./scripts/<name>`* |
| Run `wintune.exe` | `.\build\Release\wintune.exe` | same | `./build/Release/wintune.exe` | `/mnt/c/.../wintune.exe`* |
| Native Linux build of WinTune | — | — | — | **Not supported** |
| SSH into a Windows host running WinTune | yes | yes | yes | from Linux client → Windows OpenSSH |

\* Requires Windows interop (`powershell.exe` / ability to launch `.exe`).

---

## Important rule

**Do not run `*.ps1` with Bash.**

```bash
# Wrong — Bash tries to parse PowerShell and fails on param(
./scripts/build.ps1

# Right — extensionless wrapper calls PowerShell for you
./scripts/build
```

All Bash wrappers live in `scripts/` without a `.ps1` suffix and call
`scripts/_invoke-ps1.sh`, which:

1. Finds `pwsh.exe` or `powershell.exe`
2. Converts the script path to a Windows path (`cygpath` / `wslpath` / MSYS)
3. Runs: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File <script.ps1> ...`

---

## PowerShell (recommended on Windows)

```powershell
.\scripts\build.ps1 -Config Release
.\scripts\test.ps1 -Config Release
.\scripts\lint.ps1
.\scripts\package.ps1 -Config Release -Arch x64 -Zip
.\scripts\release.ps1 -Version 0.1.2
.\scripts\clean.ps1

.\build\Release\wintune.exe doctor
.\build\Release\wintune.exe tui --theme compact
```

---

## CMD

```cmd
scripts\build.cmd
scripts\test.cmd -Config Release
scripts\lint.cmd
scripts\package.cmd -Config Release -Arch x64 -Zip
scripts\release.cmd -Version 0.1.2
scripts\clean.cmd

build\Release\wintune.exe help
```

---

## Git Bash / MSYS2

From the repo root:

```bash
./scripts/build -Config Release
./scripts/test -Config Release
./scripts/lint
./scripts/package -Config Release -Arch x64 -Zip
./scripts/release -Version 0.1.2
./scripts/clean

./build/Release/wintune.exe version
./build/Release/wintune.exe doctor
./build/Release/wintune.exe scan --json
```

Notes:

- Prefer `./scripts/build` over `./scripts/build.sh` (both work).
- Argument style matches PowerShell: `-Config Release`, not `--config`.
- Running `wintune.exe` from Git Bash is normal — it is still the Windows binary.

---

## WSL (Ubuntu, etc.)

WSL does **not** replace Visual Studio / MSVC. Builds still run on the Windows
side through `powershell.exe`.

```bash
cd /mnt/c/Users/<you>/Desktop/TheWindowsTunning

./scripts/build -Config Release
./scripts/test -Config Release

# Run the Windows executable via interop
./build/Release/wintune.exe version
./build/Release/wintune.exe doctor
```

If wrappers fail with “powershell.exe not found”:

1. Confirm Windows interop is enabled (`systemd`/`wsl.conf` not blocking `.exe`).
2. Or open **Windows PowerShell** / **Git Bash** and run the same scripts there.

**What WSL cannot do today**

- Compile WinTune with `gcc`/`clang` as a Linux program (no Win32 APIs).
- Use Linux-native package managers to install `wintune` as an ELF.

WSL is fine for editing, git, and invoking the Windows build + `wintune.exe`.

---

## Running the product (not just building)

| Shell | Example |
|-------|---------|
| PowerShell | `.\wintune.exe doctor` |
| CMD | `wintune.exe doctor` |
| Git Bash | `./wintune.exe doctor` |
| WSL | `./wintune.exe doctor` (Windows exe) |
| Remote SSH (Windows OpenSSH) | `ssh user@windows-host "wintune scan --json"` |

Interactive TUI needs a real console / PTY:

```bash
# Git Bash / Windows Terminal
./wintune.exe tui --safe-terminal

# SSH into Windows
ssh -t user@windows-host "wintune tui --safe-terminal"
```

---

## Script catalog

| Bash / Git Bash / WSL | PowerShell | CMD |
|-----------------------|------------|-----|
| `./scripts/build` | `.\scripts\build.ps1` | `scripts\build.cmd` |
| `./scripts/smoke` | `.\scripts\smoke.ps1` | `scripts\smoke.cmd` |
| `./scripts/lint` | `.\scripts\lint.ps1` | `scripts\lint.cmd` |
| `./scripts/package` | `.\scripts\package.ps1` | `scripts\package.cmd` |
| `./scripts/release` | `.\scripts\release.ps1` | `scripts\release.cmd` |
| `./scripts/clean` | `.\scripts\clean.ps1` | `scripts\clean.cmd` |

Shared implementation: `scripts/_invoke-ps1.sh`.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `syntax error near unexpected token` on `param(` | You ran a `.ps1` with Bash. Use `./scripts/build` instead. |
| `powershell.exe not found` in WSL | Enable Windows interop, or use PowerShell/Git Bash outside WSL. |
| `This app can't run on your PC` | Wrong ZIP architecture (x64 vs ARM64). See `ARCH.txt`. |
| TUI exits immediately | Non-interactive stdin/stdout. Use a real terminal or `ssh -t`. |
| Path looks like `/mnt/c/...` to PowerShell | Wrappers convert via `wslpath`; update wrappers if you call `.ps1` manually. |
