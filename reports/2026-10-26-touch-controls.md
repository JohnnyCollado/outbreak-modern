# Android touch controls

- **Date:** 2026-10-26
- **Who:** developer (Johnny) and a Claude Code session (design, comparison, implementation)
- **Goal:** build the on-screen touch controls of `docs/plans/2026-10-26-touch-controls.md` (decisions T1-T14) for
  the Classic camera, with the settings toggle, size and opacity, and editing; all suites green, no warnings.
- **Outcome:** built on branch `android-touch-controls`; see Summary for what is verified and what waits for the
  developer's device test.
- **Versions:** outbreak-modern `main` f386a8a + this branch; PS2Recomp patches 0001-0080 (no new patch); AGP 9.4.0,
  Gradle 9.7.1, Kotlin 2.4.10, Compose BOM 2026.08.00, JUnit 4.13.2 (local Gradle cache); NDK as `docs/ops/pins.md`.

## Summary

The touch controls need no runtime patch: PS2Recomp patch 0078 already lets the settings UI filter every pad read
(`PS2ExternalUi::filterPad`), so the overlay's state is merged there as a second controller
(`native/ob_ui/core/touch_pad.*`, lock-free). The overlay is a never-focused dialog window over the NativeActivity, so
a controller's keys keep reaching the game while the window takes the touches. The Modern camera does not exist in
the native port yet (both menus say "coming later"), so only the Classic layouts are built; T7 and T9 wait for it.

## Comparison with the design (before building)

| Design | Code before | Built |
|---|---|---|
| Overlay over the game | none (Android: only the Kotlin menu dialog) | `TouchOverlay.kt`: never-focused full-screen dialog window |
| Touch into the game's pad | `filterPad` hook (patch 0078, `ui_hook.cpp:384`) | `obui::touchPad().merge` first in `obui::filterPad` (port 0), before the menu gate |
| Auto: hide on controller use | nothing told Kotlin | physical-use counter in the same merge, polled every 250 ms (`NativeBridge.physicalPadReads`) |
| Menu > Settings from the overlay | menu opened only by Back / F1 / Esc | `NativeBridge.requestMenu` -> `takeMenuRequest` in `hostFrame` |
| Settings rows (section 4.5) | Controls page: bindings, dead zone | "Touch controls" section on the Controls page, launcher and in game |
| Edit layout over the held game | - | the menu dialog steps aside, the game stays held, Done brings it back |
| Modern layout (T7, T9) | Modern camera "coming later" | not built |

## Struggles

### 1. The CRLF test file did not take a scripted edit

- **Seen:** a Python string replace into `native/ob_ui/tests/ob_ui_tests.cpp` reported nothing changed.
- **Found by:** `grep -c TouchTests` = 0, `file` = "with CRLF line terminators".
- **Cause:** the search strings had LF line ends; the file has CRLF (as the other ob_ui core files).
- **Fix:** made the edit with the Edit tool; the new `touch_pad.*` files converted to CRLF to match.
- **Evidence:** ob_ui 108/108 (99 before + 9 touch tests).
- **Take-away:** check `file <path>` before a scripted edit; prefer the Edit tool (memory "heredoc escaping trap").

### 2. A third quick tap would fire a second quick turn

- **Seen:** while writing the double-tap test: the second tap's release counted as a fresh tap, so tap-tap-tap turned
  her twice.
- **Found by:** writing `doubleTapNeedsTwoShortTaps` in `TouchLogicTest.kt`.
- **Cause:** `DoubleTapDetector.up` recorded every short tap, the pair's second one included.
- **Fix:** the second tap of a pair never starts a new pair.
- **Evidence:** `TouchLogicTest.doubleTapNeedsTwoShortTaps`.
- **Take-away:** gesture detectors need a test for the repeat case, not only the first fire.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| (design) Classic stick = mostly tank | the left stick is directional in the Classic camera too | the developer, from another session | design T5, T13, T14 |
| (design) the aim pad may need a directional variant | aiming returns to tank-style aiming from either scheme | the developer | design T8 |

## Still open

- Device test by the developer (the Odin or a phone): feel of the zones and sizes, Auto hiding, Edit layout, the call
  wheel (whether calls need R3 as well as the right stick), L2's role, the quick-turn timings.
- Section 5 of the design: the directional stick's "walk and turn" range and camera cuts; take the stick session's
  findings first.
- Modern camera layout (T7, T9) when that camera exists.

## Useful for other developers

- A second input source can join the game's pad without touching the runtime: merge it in `obui::filterPad`
  (`native/ob_ui/ui/settings_ui.cpp`) before the menu gate, so the menu's release rules cover it too.
- A never-focused dialog (`FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCH_MODAL`) over a NativeActivity takes touches while keys
  and gamepad events keep going to the native input queue.
- Kotlin unit tests run in `android.sh package` (`gradlew testDebugUnitTest assembleDebug`); the step prints
  "kotlin tests: n/n, kotlin warnings: n".
