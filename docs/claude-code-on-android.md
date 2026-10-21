# Claude Code on an Android tablet (Galaxy Tab S9)

Written 2026-10-21 for Johnny's Tab S9, before anyone had tried it. **Unverified end to end.**
Fill in the checkpoint results as you go; corrections belong here and in a `reports/` entry.

**Status of support (Confirmed, Claude Code docs, 2026-10-21):** Anthropic supports macOS,
Windows and Linux only. Android and Termux are not mentioned. Plain Termux is **reported broken**
in recent builds (no Android build; `claude` says its native binary is missing;
anthropics/claude-code issue #50270). The route users report working is **Ubuntu inside
Termux (proot-distro)** with the official installer, because that gives the standard Linux arm64
build a normal glibc environment.

Why bother: a session running on the tablet can reach its files directly (pull archives out of
the game ISO, upload them, later install a test APK) and, with `claude remote-control`, be driven
from the Claude app on a phone. It does **not** replace the cloud session: recompiling and
booting need a Linux x86-64 machine. The repo (`docs/`, `reports/`, `results/`, `CLAUDE.md`) is
the shared memory between sessions.

## Phase 1: Termux

1. Install Termux from **F-Droid** or from Termux's GitHub releases, not the Play Store (that
   build is abandoned). Use one source for Termux and any Termux add-ons; mixing sources fails
   with `INSTALL_FAILED_SHARED_USER_INCOMPATIBLE`.
2. `pkg update && pkg upgrade`
3. `termux-setup-storage` (grant the permission). Storage appears at `~/storage/shared`.

Checkpoint: `ls ~/storage/shared` lists Download, DCIM, ...  Result: ______

## Phase 2: Ubuntu inside Termux (~2 GB)

```
pkg install proot-distro
proot-distro install ubuntu
proot-distro login ubuntu
apt update && apt install -y curl git
```

Checkpoint: `uname -m` → `aarch64`; `cat /etc/os-release` → Ubuntu.  Result: ______

## Phase 3: Claude Code

Inside Ubuntu: `curl -fsSL https://claude.ai/install.sh | bash`, then `exit` and
`proot-distro login ubuntu` again so PATH is reloaded.

Checkpoint: `claude --version` prints a version.  Result: ______
If it says the native binary is missing or "Bad system call": you're outside Ubuntu, or the
build changed. Record the exact message.

## Phase 4: Login

`claude`, then press **`c`** to copy the login URL (no browser can open from the terminal),
paste it into Chrome on the tablet, sign in, and paste any code it shows back at the
`Paste code here if prompted` prompt. If it loops, copy the URL by hand instead of tapping it.
Needs a Pro/Max/Team/Enterprise plan. Alternative with no browser: `claude setup-token` on a
machine that has one, then `CLAUDE_CODE_OAUTH_TOKEN` on the tablet (model requests only, no
Remote Control).

Checkpoint: the Claude prompt appears.  Result: ______

## Phase 5: Game files

Storage is at the same path inside Ubuntu: `cd ~/storage/shared/Download`. If it's empty there,
re-run `termux-setup-storage` from plain Termux and re-enter Ubuntu.

## Phase 6 (optional): Remote Control

`claude remote-control` should make the session appear in the Claude app under Code. Whether
this works inside Termux is **Unknown** (the docs only cover Android as the client side). If the
session keeps dying: Android battery settings → Termux → Unrestricted; Termux notification →
Acquire wakelock.

Checkpoint: the session shows in the app and answers.  Result: ______

## If the official route fails

- `ferrumclaudepilgrim/claude-code-android`: a third-party installer (native Termux via
  glibc-runner, or full Ubuntu). Not from Anthropic: read its `install.sh` first.
- Pinning an old version in plain Termux reportedly still runs: `pkg install nodejs`,
  `npm i -g @anthropic-ai/claude-code@2.1.112`. Frozen; it will fall behind.

Sources: code.claude.com/docs/en/setup.md, .../authentication.md, .../remote-control.md;
jonathansblog.co.uk/?p=30143 (user report); github.com/anthropics/claude-code/issues/50270.
