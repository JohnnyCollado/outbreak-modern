package com.outbreakmodern.app

import android.content.Context
import android.os.Build
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File

/** What the launcher asks its activity for (Storage Access Framework pickers). */
interface PickHost {
    fun pickDisc()
    fun pickTree(kind: ImportKind)
    fun pickZip(kind: ImportKind)
}

/**
 * The menu's pages and the settings behind them, shared by the launcher (inGame = false) and the in-game menu
 * (inGame = true). Same pages, keys, wording and launch-only rules as the Windows menu (native/ob_ui/ui/settings_ui.cpp).
 * Every change is saved at once; [rev] makes Compose read the pages again.
 */
class SettingsModel(private val context: Context, val settings: Settings, val inGame: Boolean, val nav: MenuNav) {
    var rev by mutableIntStateOf(0)
    var status by mutableStateOf("")
    private var statusUntil = 0L
    var host: PickHost? = null
    var importer: Importer? = null
    var installed: Installed? = null
    /** Restarts the app's process (a new graphics driver loads only in a fresh process). */
    var restartApp: (() -> Unit)? = null
    /** The graphics driver this process started with: a restart is needed while the setting differs from it. */
    private val driverAtStart = settings.get("gpu_driver", "system")
    val driverChanged get() = settings.get("gpu_driver", "system") != driverAtStart
    var inputChanged = false
    var settingsChanged = false
    /** In game: opens the touch controls' Edit layout over the held game (OutbreakActivity). */
    var editTouchLayout: (() -> Unit)? = null

    // The game disc (launcher).
    var disc by mutableStateOf(DiscCheck.NONE)
    var discName by mutableStateOf("")

    // Binding capture: the row being rebound and when it started (the press that started it is ignored).
    private var captureKey: String? = null
    private var captureGamepad = false
    private var captureStart = 0L

    fun bump() { rev++ }

    fun note(text: String) {
        status = text
        statusUntil = SystemClock.uptimeMillis() + 4000
        bump()
    }

    fun statusLine(default: String): String = if (SystemClock.uptimeMillis() < statusUntil) status else default

    // ---- binding capture ----
    val capturing get() = captureKey != null

    /** Raw key events while capturing (before navigation): the next press becomes the binding. True = consumed. */
    fun onCaptureKey(event: KeyEvent): Boolean {
        val key = captureKey ?: return false
        if (SystemClock.uptimeMillis() - captureStart > 6000) {
            captureKey = null
            note("Binding not changed")
            return false
        }
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount != 0) return true
        if (SystemClock.uptimeMillis() - captureStart < 250) return true
        val fromPad = event.source and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
        if (!captureGamepad && event.keyCode == KeyEvent.KEYCODE_ESCAPE) {
            captureKey = null
            note("Binding not changed")
            return true
        }
        val name = if (captureGamepad) InputNames.gamepad(event.keyCode) else if (!fromPad) InputNames.keyboard(event.keyCode) else null
        when {
            name == null -> return true
            !captureGamepad && (name == "F1" || name == "ESCAPE") -> note("$name opens the menu. Pick another.")
            else -> {
                val stick = STICK_ROWS.firstOrNull { it.id == key }
                if (stick != null) {
                    val list = settings.bind(false, stick.iniKey, stick.listDefault)
                    val parts = list.split(',').map { it.trim() }.toMutableList()
                    while (parts.size < 4) parts += ""
                    parts[stick.index] = name
                    settings.setBind(false, stick.iniKey, parts.joinToString(", "))
                } else settings.setBind(captureGamepad, key, name)
                inputChanged = true
                note("Saved: binding")
            }
        }
        captureKey = null
        bump()
        return true
    }

    fun tickCapture() {
        if (captureKey != null && SystemClock.uptimeMillis() - captureStart > 6000) {
            captureKey = null
            note("Binding not changed")
        }
    }

    // ---- disc ----
    fun refreshDisc() {
        val uri = settings.savedDiscUri()
        if (uri != null) {
            discName = settings.displayName(context, uri)
            disc = checkDiscUri(context, uri) ?: DiscCheck(false, 0, "The disc chosen earlier can't be opened. Choose it again.", "")
        } else {
            val iso = settings.singleIso()
            discName = iso?.name ?: ""
            disc = iso?.let { DiscCheck.from(NativeBridge.checkDisc(it.path)) } ?: DiscCheck.NONE
        }
        bump()
    }

    // ---- pages ----
    fun pages(): List<SPage> {
        @Suppress("UNUSED_VARIABLE") val r = rev // read: recompose after changes
        val list = mutableListOf<SPage>()
        if (!inGame) list += gamePage()
        list += modsPage()
        list += controlsPage()
        list += cameraPage()
        list += displayPage()
        if (!inGame) list += cardsPage()
        list += audioPage()
        if (!inGame) {
            list += driverPage()
            list += advancedPage()
        }
        return list
    }

    private fun launchOnlyNote(rows: MutableList<SRow>, key: String) {
        if (inGame) rows += SRow.Info(key, "Change this in the launcher. It applies the next time the game starts.", Tone.MUTED)
    }

    private fun gamePage(): SPage {
        val rows = mutableListOf<SRow>()
        rows += SRow.Header("h-disc", "Game disc (required)")
        rows += SRow.Action("disc", "Game disc", discName.ifEmpty { "Choose…" }) { host?.pickDisc() }
        val steps = listOf("File opens and reads as a disc image", "Disc boots SLPM_654.28 (SYSTEM.CNF)", "Game code matches this build")
        steps.forEachIndexed { i, s ->
            val passed = disc.steps > i
            val failed = discName.isNotEmpty() && disc.steps == i && !disc.verified
            rows += SRow.Info("step$i", (if (passed) "✓  " else if (failed) "✕  " else "○  ") + s,
                if (passed) Tone.GOOD else if (failed) Tone.BAD else Tone.MUTED)
        }
        rows += SRow.Info("disc-msg", disc.message, if (disc.verified) Tone.GOOD else Tone.BAD)
        rows += SRow.Action("disc-check", "Check again") { refreshDisc() }
        discRows(rows)
        textureRows(rows)
        storageRows(rows)
        rows += SRow.Header("h-start", "Start-up")
        rows += SRow.Toggle("launcher", "Show this screen at start-up", settings.ui("launcher", "on") != "off") {
            settings.setUi("launcher", if (it) "on" else "off")
            note("Saved: start-up screen")
        }
        rows += SRow.Info("launcher-hint", "Off boots straight into the game when the disc checks out. The Back button still opens the menu in game.", Tone.MUTED)
        return SPage("Game", rows)
    }

    // ---- what is installed (developer request 2026-10-26: what, how much space, delete it) ----

    private fun sizeOf(f: File) = installed?.describe(f) ?: ""

    /** A delete with a confirm that names what and how much; the default choice is Cancel (MenuNav.ask). */
    private fun confirmDelete(title: String, what: String, targets: List<File>, after: () -> Unit = {}) {
        val bytes = targets.mapNotNull { installed?.bytesOrNull(it) }.sum()
        val size = if (bytes > 0) " (${gb(bytes)})" else ""
        nav.ask(Confirm(title, "Delete $what$size? This can't be undone.", "Delete", "Cancel", {
            note("Deleting $what…")
            installed?.delete(targets) { ok ->
                if (ok) after()
                note(if (ok) "Deleted $what" else "Could not delete all of $what")
            }
        }))
    }

    private fun discRows(rows: MutableList<SRow>) {
        val uri = settings.savedDiscUri()
        val isos = settings.filesDir.listFiles { f -> f.isFile && f.name.lowercase().endsWith(".iso") }?.sortedBy { it.name }.orEmpty()
        if (uri == null && isos.isEmpty()) return
        rows += SRow.Header("h-disc-store", "Disc on this device")
        if (uri != null)
            rows += SRow.Buttons("disc-link", "Linked: $discName", "Read from where you picked it. Forget doesn't delete your file.",
                listOf("Forget")) {
                runCatching { context.contentResolver.releasePersistableUriPermission(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION) }
                settings.gameUriFile.delete()
                refreshDisc()
                note("Disc link forgotten")
            }
        for (iso in isos) {
            val inUse = uri == null && isos.size == 1
            rows += SRow.Buttons("iso-${iso.name}", iso.name, "Copy in the app's storage  -  ${sizeOf(iso)}", listOf("Delete"),
                badge = if (inUse) "In use" else "", danger = setOf(0)) {
                val bytes = installed?.bytesOrNull(iso)
                nav.ask(Confirm("Delete the disc image copy?",
                    "Delete the disc image copy${bytes?.let { " (${gb(it)})" } ?: ""}? You can pick your disc again from any folder.",
                    "Delete", "Cancel", {
                        installed?.delete(listOf(iso)) { ok ->
                            refreshDisc()
                            note(if (ok) "Disc image copy deleted" else "Could not delete the disc image copy")
                        }
                    }))
            }
        }
    }

    private fun textureRows(rows: MutableList<SRow>) {
        rows += SRow.Header("h-textures", "Texture pack (optional)")
        val dir = File(settings.filesDir, "textures")
        val folder = File(dir, settings.serial)
        val zip = File(dir, "${settings.serial}.zip")
        val haveFolder = folder.isDirectory
        val haveZip = zip.isFile
        val active = settings.get("textures").isNotEmpty()
        if (!haveFolder && !haveZip)
            rows += SRow.Info("tex-none", "None installed: the game's original textures", Tone.MUTED)
        // Both present: PCSX2 loads both. It scans <root>/<serial>/replacements first and then adds <root>/<serial>.zip
        // (GSTextureReplacements.cpp ReloadReplacementMap, :440-446), and keeps the first entry per texture
        // (AddReplacementTexture's emplace, :388), so where they overlap the folder's copy is used.
        val both = haveFolder && haveZip
        val clearIfLast = { if (!folder.exists() && !zip.exists()) settings.set("textures", "") }
        if (haveFolder)
            rows += SRow.Buttons("tex-folder", "Folder  ${settings.serial}",
                sizeOf(folder) + if (both) "  -  used first where both have a texture" else "",
                listOf("Delete"), badge = if (active) "In use" else "", danger = setOf(0)) {
                confirmDelete("Delete the texture pack folder?", "the texture pack folder", listOf(folder), clearIfLast)
            }
        if (haveZip)
            rows += SRow.Buttons("tex-zip", "Zip  ${settings.serial}.zip",
                sizeOf(zip) + if (both) "  -  also loaded: only textures the folder lacks come from it" else "",
                listOf("Delete"), badge = if (active) "In use" else "", danger = setOf(0)) {
                confirmDelete("Delete the texture pack zip?", "the texture pack zip", listOf(zip), clearIfLast)
            }
        if (both) rows += SRow.Info("tex-both", "Both are loaded, which takes longer and more memory. Keeping one is enough.", Tone.MUTED)
        rows += SRow.Buttons("tex-import", "Import a texture pack", "From a folder or a .zip", listOf("Folder", "Zip")) {
            startImport(ImportKind.TEXTURES, folder = it == 0)
        }
        if (active && (haveFolder || haveZip))
            rows += SRow.Action("tex-off", "Don't use it", "files stay on the device") {
                settings.set("textures", "")
                note("Saved: texture pack off")
            }
        else if (!active && (haveFolder || haveZip))
            rows += SRow.Action("tex-on", "Use the installed pack") {
                settings.set("textures", "textures")
                note("Saved: texture pack on")
            }
    }

    // ---- Mods (developer 2026-10-26: "manage/enable and disable what they want to use") ----
    // A mod is a .pnach in mods/<serial>/. Off = renamed to <name>.pnach.disabled: the runner loads only files whose
    // extension is ".pnach" (PS2Recomp patch 0050, ps2xRuntime/src/lib/ps2_pnach.cpp:221), so it skips them.
    private fun modTitle(f: File): String {
        val lines = runCatching { f.readLines() }.getOrDefault(emptyList()).map { it.trim() }
        val section = lines.firstOrNull { it.startsWith("[") && it.endsWith("]") }?.trim('[', ']')?.trim()
        val desc = lines.firstOrNull { it.startsWith("description=", ignoreCase = true) }?.substringAfter('=')?.trim()
        val title = lines.firstOrNull { it.startsWith("gametitle=", ignoreCase = true) }?.substringAfter('=')?.trim()
        return listOf(section, desc, title).firstOrNull { !it.isNullOrEmpty() } ?: f.name.substringBefore(".pnach")
    }

    private fun modsPage(): SPage {
        val rows = mutableListOf<SRow>()
        val dir = File(settings.filesDir, "mods/${settings.serial}")
        if (inGame) rows += SRow.Info("mods-ingame", "Changes apply the next time the game starts.", Tone.MUTED)
        rows += SRow.Info("mods-hint", "Mods are PCSX2 .pnach patches for ${settings.serial}. Turn each on or off; off keeps the file.", Tone.MUTED)
        // Widescreen mods (the player's own widescreen .pnach or the menu's ob-widescreen.pnach; one rule with Windows:
        // native/ob_ui/core/widescreen.h) are not mods here: Display > Aspect ratio alone switches them (developer,
        // 2026-10-26), so they never show, count, toggle, or go with a delete.
        val files = dir.listFiles { f -> f.isFile && (f.name.lowercase().endsWith(".pnach") || f.name.lowercase().endsWith(".pnach.disabled")) &&
            !NativeBridge.isWidescreenModFile(f.absolutePath) }?.sortedBy { it.name.lowercase() }.orEmpty()
        if (files.isEmpty()) rows += SRow.Info("mods-none", "None installed", Tone.MUTED)
        val modsOn = settings.get("mods").isNotEmpty()
        for (f in files) {
            val on = f.name.lowercase().endsWith(".pnach")
            val base = f.name.removeSuffix(".disabled")
            val label = modTitle(f)
            if (inGame) {
                rows += SRow.Info("mod-$base", "$label  -  ${if (on) "On" else "Off"}", if (on) Tone.NORMAL else Tone.MUTED)
                continue
            }
            rows += SRow.Buttons("mod-$base", label, "${base}  -  ${sizeOf(f)}", listOf(if (on) "Turn off" else "Turn on", "Delete"),
                badge = if (on) "On" else "", danger = setOf(1)) { i ->
                if (i == 0) {
                    val to = File(dir, if (on) "$base.disabled" else base)
                    if (f.renameTo(to)) {
                        if (!on && !modsOn) settings.set("mods", "mods")
                        installed?.invalidate()
                        note("${label}: ${if (on) "off" else "on"} (next start)")
                    } else note("Could not rename ${f.name}")
                    bump()
                } else confirmDelete("Delete this mod?", label, listOf(f))
            }
        }
        if (!inGame) {
            if (files.size > 1) {
                val total = files.sumOf { installed?.bytesOrNull(it) ?: 0L }
                rows += SRow.Buttons("mods-all", "All ${files.size} mods", gb(total),
                    listOf("Delete all"), danger = setOf(0)) { confirmDelete("Delete all mods?", "${files.size} mod files", files) }
            }
            rows += SRow.Buttons("mods-import", "Import mods", ".pnach files from a folder or a .zip", listOf("Folder", "Zip")) {
                startImport(ImportKind.MODS, folder = it == 0)
            }
            if (modsOn && files.isNotEmpty())
                rows += SRow.Action("mods-off", "Don't load any mods", "files stay on the device") {
                    settings.set("mods", "")
                    note("Saved: mods off")
                }
            else if (!modsOn && files.isNotEmpty())
                rows += SRow.Action("mods-on", "Load the mods turned on above") {
                    settings.set("mods", "mods")
                    note("Saved: mods on")
                }
        }
        return SPage("Mods", rows)
    }

    private fun storageRows(rows: MutableList<SRow>) {
        rows += SRow.Header("h-storage", "Storage")
        val used = installed?.bytesOrNull(settings.filesDir)
        val free = Installed.freeBytes(settings.filesDir)
        installed?.size(settings.filesDir) // starts the measure
        rows += SRow.Info("storage", "Using ${used?.let { gb(it) } ?: "…"} of the app's storage; ${gb(free)} free", Tone.MUTED)
    }

    fun startImport(kind: ImportKind, folder: Boolean) {
        val have = importer?.existing(kind).orEmpty()
        val go = { if (folder) host?.pickTree(kind) else host?.pickZip(kind) }
        if (have.isNotEmpty() && kind == ImportKind.TEXTURES)
            nav.ask(Confirm("Replace the texture pack?", "This replaces the texture pack already on the device ($have). " +
                "The old one is removed only after the new one is copied.", "Replace", "Cancel", { go() }))
        else go()
    }

    private fun controlsPage(): SPage {
        val rows = mutableListOf<SRow>()
        val pad = settings.ui("bind_device", "controller") == "controller"
        rows += SRow.Choice("device", "Bindings for", listOf("Keyboard", "Controller"), if (pad) 1 else 0) {
            settings.setUi("bind_device", if (it == 1) "controller" else "keyboard")
            bump()
        }
        rows += SRow.Info("layout", "Layout for the Classic camera (PS2 buttons). The Modern camera gets its own when it arrives.", Tone.MUTED)
        for (b in BIND_ROWS) {
            val v = settings.bind(pad, b.key, if (pad) b.gamepad else b.keyboard)
            val shown = if (pad) InputNames.padLabel(v) else v.substringBefore(',').trim()
            val capturing = captureKey == b.key
            rows += SRow.Action(b.key, b.label,
                if (capturing) (if (pad) "Press a button… (6 s)" else "Press a key… (Esc cancels)") else shown) {
                captureKey = b.key
                captureGamepad = pad
                captureStart = SystemClock.uptimeMillis()
                bump()
            }
        }
        if (!pad) for (s in STICK_ROWS) {
            val list = settings.bind(false, s.iniKey, s.listDefault)
            val part = list.split(',').map { it.trim() }.getOrNull(s.index).orEmpty().ifEmpty { s.def }
            rows += SRow.Action(s.id, s.label, if (captureKey == s.id) "Press a key… (Esc cancels)" else part) {
                captureKey = s.id
                captureGamepad = false
                captureStart = SystemClock.uptimeMillis()
                bump()
            }
        }
        if (pad) {
            val dz = ((settings.bind(true, "deadzone_left", "0.239540").toFloatOrNull() ?: 0.24f) * 100).toInt()
            rows += SRow.Slider("deadzone", "Stick dead zone", dz, 0, 50, 1, "%") { v ->
                val s = String.format("%.6f", v / 100.0)
                settings.setBind(true, "deadzone_left", s)
                settings.setBind(true, "deadzone_right", s)
                inputChanged = true
                bump()
            }
        }
        rows += SRow.Action("reset", "Reset to defaults") {
            nav.ask(Confirm("Reset the ${if (pad) "controller" else "keyboard"} bindings?", "Every button goes back to its default.", "Reset", "Cancel", {
                BIND_ROWS.forEach { settings.removeBind(pad, it.key) }
                if (!pad) { settings.removeBind(false, "stick_left"); settings.removeBind(false, "stick_right") }
                inputChanged = true
                note("Defaults restored")
            }))
        }
        touchRows(rows)
        return SPage("Controls", rows)
    }

    // ---- touch controls (docs/plans/2026-10-26-touch-controls.md section 4.5; the overlay reads them as the menu
    // closes: TouchOverlay.reload) ----
    private fun touchRows(rows: MutableList<SRow>) {
        val t = TouchSettings.load { k, d -> settings.ui(k, d) }
        fun onOff(key: String, on: Boolean, what: String) {
            settings.setUi(key, if (on) "on" else "off")
            note("Saved: $what")
        }
        rows += SRow.Header("h-touch", "Touch controls")
        rows += SRow.Choice("touch", "Show touch controls", listOf("Auto", "On", "Off"), t.show.ordinal) {
            settings.setUi("touch", listOf("auto", "on", "off")[it])
            note("Saved: touch controls")
        }
        rows += SRow.Info("touch-hint", when (t.show) {
            TouchShow.AUTO -> "Auto: hidden while a controller is in use, back on the next touch."
            TouchShow.ON -> "Always shown, even with a controller."
            TouchShow.OFF -> "Never shown. For controller players and handhelds."
        }, Tone.MUTED)
        if (t.show == TouchShow.OFF) return
        rows += SRow.Choice("touch_buttons", "Buttons", listOf("Minimal", "Full"), if (t.minimal) 0 else 1) {
            settings.setUi("touch_buttons", if (it == 0) "minimal" else "full")
            note("Saved: touch buttons")
        }
        rows += SRow.Choice("touch_move", "Movement", listOf("Directional", "Tank"), if (t.directional) 0 else 1) {
            settings.setUi("touch_move", if (it == 0) "directional" else "tank")
            note("Saved: touch movement")
        }
        rows += SRow.Info("touch-move-hint", if (t.directional) "Push where she should go on screen, as the game's left stick. Double tap = quick turn."
            else "Tank controls, as the game's D-pad: forward, back, turn. Flick back = quick turn.", Tone.MUTED)
        if (!t.directional) {
            rows += SRow.Slider("touch_forward_zone", "Straight-ahead zone", t.forwardZone, 8, 40, 1, "°") {
                settings.setUi("touch_forward_zone", it.toString())
                bump()
            }
            rows += SRow.Slider("touch_steer_zone", "Steer-while-moving zone", t.steerZone, 15, 70, 1, "°") {
                settings.setUi("touch_steer_zone", it.toString())
                bump()
            }
        }
        rows += SRow.Toggle("touch_run_edge", "Run at the stick's edge", t.runEdge) { onOff("touch_run_edge", it, "run at the edge") }
        rows += SRow.Toggle("touch_quick_turn", "Quick turn gesture", t.quickTurn) { onOff("touch_quick_turn", it, "quick turn gesture") }
        rows += SRow.Slider("touch_scale", "Size", t.scale, 60, 160, 5, "%") {
            settings.setUi("touch_scale", it.toString())
            bump()
        }
        rows += SRow.Slider("touch_opacity", "Opacity", t.opacity, 10, 100, 5, "%") {
            settings.setUi("touch_opacity", it.toString())
            bump()
        }
        rows += SRow.Toggle("touch_vibrate", "Vibrate on press", t.vibrate) { onOff("touch_vibrate", it, "vibrate on press") }
        val edit = editTouchLayout
        if (edit != null) rows += SRow.Action("touch-edit", "Edit layout", "over the game") { edit() }
        else rows += SRow.Action("touch-edit", "Edit layout", "in game: menu > Controls", enabled = false) {}
    }

    private fun cameraPage(): SPage = SPage("Camera", listOf(
        SRow.Choice("camera", "Camera", listOf("Classic"), 0) {},
        SRow.Info("classic", "The original fixed angles, as on PS2.", Tone.MUTED),
        SRow.Action("modern", "Modern camera", "coming later", enabled = false) {},
        SRow.Info("modern-hint", "Over the shoulder: the right stick turns the camera, free aim. Its options appear when it arrives.", Tone.MUTED),
    ))

    private fun displayPage(): SPage {
        val rows = mutableListOf<SRow>()
        val ups = listOf(1, 2, 3, 4, 6)
        val upNames = listOf("Native PS2 (1x)", "720p (2x)", "1080p (3x)", "1440p (4x)", "4K (6x)")
        val up = settings.get("upscale", "3").toIntOrNull() ?: 3
        rows += SRow.Choice("upscale", "Render resolution", upNames, ups.indexOf(up).coerceAtLeast(0), enabled = !inGame) {
            settings.set("upscale", ups[it].toString())
            note("Saved: render resolution")
        }
        launchOnlyNote(rows, "upscale-note")
        val vs = settings.ui("vsync", "on")
        rows += SRow.Choice("vsync", "VSync", listOf("On", "On (strict)", "Off"), when (vs) { "strict" -> 1; "off" -> 2; else -> 0 }) {
            settings.setUi("vsync", listOf("on", "strict", "off")[it])
            settingsChanged = true
            note("Saved: vsync")
        }
        rows += SRow.Info("vsync-hint", "On: no tearing, little delay. Strict: classic vsync, one frame more delay. Off: may tear.", Tone.MUTED)
        val wide = widescreenDir()?.let { NativeBridge.widescreenEnabled(it.absolutePath) } ?: false
        rows += SRow.Choice("aspect", "Aspect ratio", listOf("4:3", "16:9"), if (wide) 1 else 0, enabled = !inGame) { setWidescreen(it == 1) }
        val smooth = settings.get("scaling", "smooth") == "smooth"
        rows += SRow.Choice("scaling", "Pixel scaling", listOf("Smooth", "Sharp"), if (smooth) 0 else 1, enabled = !inGame) {
            settings.set("scaling", if (it == 0) "smooth" else "sharp")
            note("Saved: pixel scaling")
        }
        launchOnlyNote(rows, "display-note")
        rows += SRow.Toggle("fps", "Show FPS", settings.ui("show_fps", "off") == "on") {
            settings.setUi("show_fps", if (it) "on" else "off")
            settingsChanged = true
            note("Saved: FPS readout")
        }
        return SPage("Display", rows)
    }

    // <mods>/<serial>/ when a mods folder is set (the aspect ratio reads it), else null.
    private fun widescreenDir(): File? = settings.get("mods").takeIf { it.isNotEmpty() }
        ?.let { settings.resolve(it) }?.let { File(it, settings.serial) }

    // Aspect ratio over the widescreen mods (native/ob_ui/core/widescreen.h, as the Windows menu): 16:9 turns on one
    // (the player's own widescreen .pnach first) or writes ob-widescreen.pnach; 4:3 renames every enabled one to
    // .pnach.disabled. Nothing is deleted. A mods folder beside the settings file if none is set.
    // Widescreen is the default (developer, 2026-10-26; as obui::prepare in settings_ui.cpp): until an aspect ratio
    // has been chosen ([ui] widescreen unset), turn the 16:9 mod on so the launcher and the game both start in 16:9.
    fun applyDefaultWidescreen() {
        if (settings.ui("widescreen").isNotEmpty()) return
        if (settings.get("mods").isEmpty()) settings.set("mods", "mods")
        val dir = widescreenDir() ?: return
        if (NativeBridge.setWidescreen(dir.absolutePath, true).isEmpty()) settings.setUi("widescreen", "on")
    }

    private fun setWidescreen(on: Boolean) {
        if (settings.get("mods").isEmpty()) settings.set("mods", "mods")
        val dir = widescreenDir() ?: return
        val err = NativeBridge.setWidescreen(dir.absolutePath, on)
        if (err.isEmpty()) {
            settings.setUi("widescreen", if (on) "on" else "off")
            installed?.invalidate()
            note("Saved: aspect ratio")
        } else note(err)
        bump()
    }

    private fun cardsPage(): SPage {
        val rows = mutableListOf<SRow>()
        val dir = settings.memcardsDir()
        rows += SRow.Header("h-folder", "Save folder")
        rows += SRow.Info("folder", dir.path, Tone.MUTED)
        rows += SRow.Info("folder-hint", "A new card is created here. The game asks to format it the first time you save.", Tone.MUTED)
        rows += SRow.Header("h-cards", "Cards")
        for (slot in 0..1) {
            val name = if (slot == 0) "Mcd001.ps2" else "Mcd002.ps2"
            val c = NativeBridge.readCard(File(dir, name).path)
            val saves = c.drop(4)
            var what = when {
                c[0] != "1" -> if (slot == 0) "none yet (made at start-up)" else "none"
                c[1] != "1" -> "blank (the game formats it when you first save)"
                else -> "${saves.size} save(s)"
            }
            if ("BISLPM-65428NETBIO" in saves) what += ", holds File #1 data"
            rows += SRow.Info("slot$slot", "Slot ${slot + 1} ($name): $what")
        }
        rows += SRow.Action("backup", "Back up slot 1 now") {
            val err = NativeBridge.backupCard(File(dir, "Mcd001.ps2").path, 5)
            note(if (err.isEmpty()) "Backed up slot 1 (the newest 5 are kept)" else "Backup failed: $err")
        }
        rows += SRow.Toggle("card_backup", "Back up slot 1 at each launch", settings.ui("card_backup", "on") == "on") {
            settings.setUi("card_backup", if (it) "on" else "off")
            note("Saved: card backup")
        }
        return SPage("Memory cards", rows)
    }

    private fun audioPage(): SPage {
        val vol = settings.ui("volume", "100").toIntOrNull() ?: 100
        return SPage("Audio", listOf(
            SRow.Slider("volume", "Master volume", vol, 0, 100, 5, "%") {
                settings.setUi("volume", it.toString())
                settingsChanged = true
                bump()
            },
            SRow.Info("audio-hint", "The sound device is the system's default.", Tone.MUTED),
        ))
    }

    private fun driverPage(): SPage {
        val rows = mutableListOf<SRow>()
        val cur = settings.get("gpu_driver", "system")
        rows += SRow.Info("driver-hint", "The Vulkan driver the game draws with. Install several and choose between them; " +
            "the app needs a restart for a new choice to take effect.", Tone.MUTED)
        if (driverChanged)
            rows += SRow.Buttons("drv-restart", "Restart needed", "Restart the app for the new driver to take effect.", listOf("Restart now")) {
                restartApp?.invoke()
            }
        val choose = { name: String ->
            if (name != settings.get("gpu_driver", "system")) {
                settings.set("gpu_driver", name)
                note("Saved: graphics driver. Restart the app for it to take effect.")
            }
        }
        rows += SRow.Buttons("drv-system", "System driver (recommended)", "The phone's own Vulkan driver",
            if (cur == "system") emptyList() else listOf("Select"), badge = if (cur == "system") "In use" else "") { choose("system") }
        val adreno = File("/dev/kgsl-3d0").exists()
        File(settings.filesDir, "drivers").listFiles()?.filter { it.isDirectory }?.sortedBy { it.name.lowercase() }?.forEach { d ->
            val meta = File(d, "meta.json").takeIf { it.isFile }?.readText().orEmpty()
            fun field(k: String) = Regex("\"$k\"\\s*:\\s*\"?([^\",}]*)").find(meta)?.groupValues?.get(1)?.trim().orEmpty()
            val minApi = field("minApi").toIntOrNull() ?: 0
            val lib = field("libraryName")
            val why = when {
                meta.isEmpty() -> "meta.json missing"
                !adreno -> "needs an Adreno GPU"
                lib.isNotEmpty() && !File(d, lib).isFile -> "$lib missing"
                else -> ""
            }
            // A package made for a newer Android can still be chosen (developer, 2026-10-26): say why it may misbehave.
            // The game loads it anyway and falls back to the system driver if it fails (gpu_driver.cpp MinApiWarning).
            val newer = minApi > Build.VERSION.SDK_INT
            val warn = if (newer && why.isEmpty())
                "Made for a newer Android (API $minApi; this device has ${Build.VERSION.SDK_INT}): it may have bugs or graphical glitches. If it fails to start, the system driver is used."
            else ""
            val title = listOf(field("name"), field("driverVersion")).filter { it.isNotEmpty() }.joinToString(" ")
            val detail = listOf(if (title.isNotEmpty()) d.name else "", sizeOf(d), why, warn).filter { it.isNotEmpty() }
                .joinToString("  -  ")
            val inUse = cur == d.name
            val buttons = if (inUse || why.isNotEmpty()) listOf("Delete") else listOf("Select", "Delete")
            rows += SRow.Buttons("drv-${d.name}", title.ifEmpty { d.name }, detail, buttons,
                badge = if (inUse) "In use" else "", danger = setOf(buttons.size - 1)) { i ->
                if (buttons[i] == "Select") choose(d.name)
                else {
                    // The GS copies a driver into the internal files dir before loading it (gpu_driver_android.cpp):
                    // both copies go.
                    val internal = File(context.filesDir, "drivers/${d.name}")
                    confirmDelete("Delete this driver?", "the driver ${d.name}", listOf(d, internal)) {
                        if (settings.get("gpu_driver", "system") == d.name) settings.set("gpu_driver", "system")
                    }
                }
            }
        }
        rows += SRow.Buttons("drv-add", "Add a driver", "From a folder or a .zip (a libadrenotools package with meta.json)",
            listOf("Folder", "Zip")) { startImport(ImportKind.DRIVER, folder = it == 0) }
        rows += SRow.Header("h-last", "Last run")
        val last = File(settings.filesDir, "gpu-last-run.txt").takeIf { it.isFile }?.readText()?.trim().orEmpty()
        last.ifEmpty { "Not known yet." }.lines().take(3).forEachIndexed { i, l -> rows += SRow.Info("last$i", l, Tone.MUTED) }
        rows += SRow.Header("h-adv", "Advanced")
        val reads = listOf("auto", "copy", "barrier", "fetch")
        rows += SRow.Choice("self-read", "Self-read (testing)", reads, reads.indexOf(settings.get("gs_self_read", "auto")).coerceAtLeast(0)) {
            settings.set("gs_self_read", reads[it])
            note("Saved: self-read")
        }
        return SPage("Graphics driver", rows)
    }

    private fun advancedPage(): SPage {
        val v = settings.get("vu1")
        return SPage("Advanced", listOf(
            SRow.Choice("vu1", "VU1 engine", listOf("Default", "Recompiler (mvu, fast)", "Interpreter (interp, reference)"),
                when (v) { "mvu" -> 1; "interp" -> 2; else -> 0 }) {
                settings.set("vu1", listOf("", "mvu", "interp")[it])
                note("Saved: VU1 engine")
            },
            SRow.Info("vu1-hint", "The PS2's vector unit that transforms the 3D.", Tone.MUTED),
        ))
    }

    companion object {
        data class Bind(val key: String, val label: String, val keyboard: String, val gamepad: String)
        data class Stick(val id: String, val iniKey: String, val index: Int, val label: String, val def: String, val listDefault: String)

        val BIND_ROWS = listOf(
            Bind("cross", "Cross", "K", "RIGHT_FACE_DOWN"), Bind("circle", "Circle", "L", "RIGHT_FACE_RIGHT"),
            Bind("square", "Square", "J", "RIGHT_FACE_LEFT"), Bind("triangle", "Triangle", "I", "RIGHT_FACE_UP"),
            Bind("l1", "L1", "Q", "LEFT_TRIGGER_1"), Bind("r1", "R1", "E", "RIGHT_TRIGGER_1"),
            Bind("l2", "L2", "ONE", "LEFT_TRIGGER_2, LEFT_TRIGGER"), Bind("r2", "R2", "THREE", "RIGHT_TRIGGER_2, RIGHT_TRIGGER"),
            Bind("l3", "L3", "TWO", "LEFT_THUMB"), Bind("r3", "R3", "FOUR", "RIGHT_THUMB"),
            Bind("start", "Start", "ENTER", "MIDDLE_RIGHT"), Bind("select", "Select", "BACKSPACE", "MIDDLE_LEFT"),
            Bind("up", "D-pad up", "UP", "LEFT_FACE_UP"), Bind("down", "D-pad down", "DOWN", "LEFT_FACE_DOWN"),
            Bind("left", "D-pad left", "LEFT", "LEFT_FACE_LEFT"), Bind("right", "D-pad right", "RIGHT", "LEFT_FACE_RIGHT"),
        )
        val STICK_ROWS = listOf(
            Stick("ls-up", "stick_left", 0, "Left stick up", "W", "W, S, A, D"),
            Stick("ls-down", "stick_left", 1, "Left stick down", "S", "W, S, A, D"),
            Stick("ls-left", "stick_left", 2, "Left stick left", "A", "W, S, A, D"),
            Stick("ls-right", "stick_left", 3, "Left stick right", "D", "W, S, A, D"),
            Stick("rs-up", "stick_right", 0, "Right stick up", "T", "T, G, F, H"),
            Stick("rs-down", "stick_right", 1, "Right stick down", "G", "T, G, F, H"),
            Stick("rs-left", "stick_right", 2, "Right stick left", "F", "T, G, F, H"),
            Stick("rs-right", "stick_right", 3, "Right stick right", "H", "T, G, F, H"),
        )
    }
}
