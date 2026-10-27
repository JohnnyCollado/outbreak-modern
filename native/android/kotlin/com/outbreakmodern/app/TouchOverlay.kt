package com.outbreakmodern.app

import android.app.Activity
import android.graphics.drawable.ColorDrawable
import android.os.SystemClock
import android.view.HapticFeedbackConstants
import android.view.InputDevice
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import androidx.activity.ComponentDialog
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.changedToDown
import androidx.compose.ui.input.pointer.changedToUp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.io.File
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

/**
 * The touch controls over the game (docs/plans/2026-10-26-touch-controls.md): a full-screen, never-focused dialog
 * window above OutbreakActivity's NativeActivity window. Never focused, so a controller's keys still go to the game
 * (raylib's input queue); its own touches become a PS2 pad through NativeBridge.touchPad, merged into the first pad at
 * the game's next read (native/ob_ui/core/touch_pad.h). Rules live in TouchLogic.kt. Classic camera only: the Modern
 * camera's layout (T7, T9) waits for that camera.
 *
 * Two layouts: play (Minimal or Full) and the menu layout (a D-pad, design section 8), chosen by the game's own screen
 * (NativeBridge.gameScreen: its menu flag, read from its RAM through PS2Recomp patch 0081) or by the toggle button.
 *
 * Shown per the "touch" setting: Off = no window (touches reach the game as before), On = always, Auto (default) =
 * hidden while a controller or keyboard plays (NativeBridge.physicalPadReads rises) and back on the next touch; at
 * start hidden when a gamepad is connected. Hidden while the in-game menu is open, except in Edit layout.
 */
class TouchOverlay(private val activity: Activity, private val scope: CoroutineScope) {
    private val settings = Settings(activity)
    private val layoutFile = File(settings.filesDir, "touch_layout.ini").path
    private var dialog: ComponentDialog? = null
    private var view: View? = null
    private var suspended = false
    private var onEditDone: (() -> Unit)? = null

    // ---- state the drawing reads ----
    private var cfg by mutableStateOf(TouchSettings())
    private var playSpecs by mutableStateOf(ButtonSets.of(true, true))
    private val placed = mutableStateMapOf<String, Placement>() // "<section>/<id>": every set's placements
    private val chooser = LayoutChooser()
    private var layoutRev by mutableIntStateOf(0) // the chooser changed: read the layout again
    private var r1On by mutableStateOf(false)
    private var r2On by mutableStateOf(false)
    private var shown by mutableStateOf(true)
    private var editing by mutableStateOf(false)
    private var editView by mutableStateOf(EditView.WALKING)
    private var selected by mutableStateOf<String?>(null)
    private var fanOpen by mutableStateOf(false)
    private val knobs = mutableStateMapOf<String, PadPoint>()
    private val held = mutableStateMapOf<String, Boolean>()
    private var wheelAt by mutableStateOf<PadPoint?>(null)
    private var wheelOwner by mutableStateOf<String?>(null)
    private var redraw by mutableIntStateOf(0)

    // ---- touch bookkeeping ----
    private var size = Size.Zero
    private var density = 1f
    private val owner = HashMap<PointerId, String>()      // finger -> control id
    private val grab = HashMap<PointerId, Offset>()       // edit mode: finger -> offset from the control's centre
    private val ignored = HashSet<PointerId>()
    private val flick = FlickDetector()
    private val doubleTap = DoubleTapDetector()
    private var talkTimer: Job? = null
    private var pulseBits = 0
    private var lastSent = -1L
    private var lastTouch = 0L
    private var lastPhysical = 0

    private val aiming get() = r1On || r2On

    private enum class EditView(val label: String) { WALKING("walking"), AIMING("aiming"), MENU("menu screens") }

    /** The menu layout (T15) shows: the game's menu screens or the player's toggle; in Edit layout, its own view. */
    private val menuNow: Boolean get() {
        @Suppress("UNUSED_VARIABLE") val r = layoutRev
        return if (editing) editView == EditView.MENU else chooser.menu
    }

    /** The set showing now and its touch_layout.ini section. */
    private val specs: List<ControlSpec> get() = if (menuNow) ButtonSets.MENU else playSpecs
    private val section: String get() = if (menuNow) ButtonSets.MENU_SECTION else ButtonSets.section(cfg.minimal)
    private fun key(id: String) = "$section/$id"

    // ---- the window ----

    /** Creates the window once the game's window is attached (OutbreakActivity.onAttachedToWindow). */
    fun attach() {
        density = activity.resources.displayMetrics.density
        reload()
    }

    /** Reads the settings and the layout again; shows, hides or removes the window to match. */
    fun reload() {
        cfg = TouchSettings.load { k, d -> settings.ui(k, d) }
        playSpecs = ButtonSets.of(cfg.minimal, cfg.directional)
        chooser.reset()
        layoutRev++
        loadPlacements()
        if (cfg.show == TouchShow.OFF && !editing) {
            remove()
            return
        }
        if (cfg.show == TouchShow.AUTO && dialog == null) shown = !gamepadConnected()
        if (cfg.show == TouchShow.ON) shown = true
        ensureWindow()
        applyWindowVisibility()
    }

    /** The in-game menu opened (true) or closed (false): hidden meanwhile, everything let go. */
    fun suspend(on: Boolean) {
        suspended = on
        releaseAll()
        if (!on) reload() else applyWindowVisibility()
    }

    /** Edit layout from the in-game menu (the game stays held): [done] runs when the player taps Done. */
    fun startEdit(done: () -> Unit) {
        onEditDone = done
        editing = true
        editView = if (chooser.menu) EditView.MENU else EditView.WALKING
        selected = null
        fanOpen = false
        releaseAll()
        reload()
    }

    /** The activity is going away or to the background: nothing stays pressed. */
    fun releaseAll() {
        owner.clear()
        grab.clear()
        ignored.clear()
        knobs.clear()
        held.clear()
        wheelAt = null
        wheelOwner = null
        talkTimer?.cancel()
        pulseBits = 0
        r1On = false
        r2On = false
        fanOpen = false
        send(force = true)
    }

    fun destroy() {
        releaseAll()
        remove()
    }

    private fun remove() {
        dialog?.dismiss()
        dialog = null
        view = null
    }

    private fun applyWindowVisibility() {
        val d = dialog ?: return
        if (suspended && !editing) d.hide() else if (!d.isShowing) d.show()
    }

    private fun gamepadConnected(): Boolean = InputDevice.getDeviceIds().any { id ->
        val d = InputDevice.getDevice(id) ?: return@any false
        !d.isVirtual && (d.sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            d.sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK)
    }

    private fun ensureWindow() {
        if (dialog != null) return
        val d = ComponentDialog(activity, android.R.style.Theme_Material_NoActionBar_Fullscreen)
        d.window?.apply {
            setBackgroundDrawable(ColorDrawable(android.graphics.Color.TRANSPARENT))
            setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
            // Never focused: the controller's keys and the Back button stay with the game's window.
            addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS)
            attributes = attributes.apply {
                layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }
        }
        d.setCancelable(false)
        val v = ComposeView(activity).apply { setContent { AppTheme { Overlay() } } }
        d.setContentView(v)
        dialog = d
        view = v
    }

    // ---- placements (touch_layout.ini, one section per button set) ----

    /** Every set this player can see: the chosen play set and the menu set. */
    private fun allSets() = listOf(ButtonSets.section(cfg.minimal) to playSpecs, ButtonSets.MENU_SECTION to ButtonSets.MENU)

    private fun loadPlacements() {
        placed.clear()
        for ((sec, list) in allSets())
            for (s in list)
                Placement.decode(NativeBridge.iniGet(layoutFile, sec, s.id, false))?.let { placed["$sec/${s.id}"] = it }
    }

    private fun placement(s: ControlSpec) = placed[key(s.id)] ?: Placement(s.x, s.y)

    private fun savePlacements() {
        for ((sec, list) in allSets())
            for (s in list) {
                val p = placed["$sec/${s.id}"]
                if (p == null) NativeBridge.iniRemove(layoutFile, sec, s.id, false)
                else NativeBridge.iniSet(layoutFile, sec, s.id, p.encode(), false)
            }
    }

    // ---- geometry ----

    private fun centre(s: ControlSpec): Offset {
        val p = placement(s)
        return Offset(p.x * size.width, p.y * size.height)
    }

    /** Half the width, in pixels. */
    private fun radius(s: ControlSpec): Float = s.size / 2f * density * cfg.scale / 100f * placement(s).scale

    private fun halfHeight(s: ControlSpec) = if (s.round) radius(s) else radius(s) * 0.62f

    private fun contains(s: ControlSpec, at: Offset, slop: Float): Boolean {
        val c = centre(s)
        val d = at - c
        return if (s.round) d.getDistance() <= radius(s) * slop
        else abs(d.x) <= radius(s) * slop && abs(d.y) <= halfHeight(s) * slop
    }

    private fun padPoint(s: ControlSpec, at: Offset): PadPoint {
        val d = at - centre(s)
        val r = radius(s)
        return PadPoint(d.x / r, d.y / r)
    }

    private val aimNow: Boolean get() = if (editing) editView == EditView.AIMING else aiming

    private fun showing(): List<ControlSpec> = ButtonSets.visible(specs, aimNow) { placed[key(it)]?.hidden == true }

    private fun hit(at: Offset): ControlSpec? {
        val list = showing()
        // Exact hits first (the topmost drawn last), then a 25% margin round each control.
        return list.lastOrNull { contains(it, at, 1f) } ?: list.lastOrNull { contains(it, at, 1.25f) }
    }

    private fun fanRects(): List<Rect> {
        val menu = specs.firstOrNull { it.kind == Kind.MENU } ?: return emptyList()
        val c = centre(menu)
        val w = 150f * density
        val h = 40f * density
        val gap = 6f * density
        val left = (c.x - radius(menu) - w).coerceAtLeast(gap)
        return MENU_FAN.indices.map { i ->
            val top = c.y - radius(menu) + i * (h + gap) + radius(menu) * 2 + gap
            Rect(left, top, left + w, top + h)
        }
    }

    // ---- touch ----

    private fun haptic() {
        if (cfg.vibrate) view?.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
    }

    private fun down(id: PointerId, at: Offset) {
        val now = SystemClock.uptimeMillis()
        lastTouch = now
        if (editing) {
            val s = hit(at)
            selected = s?.id
            if (s != null) grab[id] = at - centre(s)
            return
        }
        if (!shown) {
            shown = true // Auto: back on a touch; this touch only wakes it
            ignored += id
            return
        }
        if (fanOpen) {
            val i = fanRects().indexOfFirst { it.contains(at) }
            fanOpen = false
            ignored += id
            if (i >= 0) {
                haptic()
                val item = MENU_FAN[i]
                if (item.bits == null) NativeBridge.requestMenu() else pulse(item.bits, 150)
            }
            return
        }
        val s = hit(at)
        if (s == null) {
            ignored += id
            return
        }
        haptic()
        owner[id] = s.id
        when (s.kind) {
            Kind.PAD -> {
                val p = padPoint(s, at)
                knobs[s.id] = p
                if (!aiming && cfg.quickTurn) {
                    if (cfg.directional) {
                        if (doubleTap.down(now, p)) pulse(Ps2.DOWN or Ps2.CROSS, 150)
                    } else flick.down(now, p)
                }
            }
            Kind.DPAD -> knobs[s.id] = padPoint(s, at)
            Kind.HOLD -> held[s.id] = true
            Kind.TOGGLE -> if (s.bits == Ps2.R1) r1On = !r1On else r2On = !r2On
            Kind.TALK -> {
                held[s.id] = true
                talkTimer?.cancel()
                talkTimer = scope.launch {
                    delay(300)
                    if (owner[id] == s.id) {
                        wheelOwner = s.id
                        wheelAt = padPoint(s, at)
                        send()
                    }
                }
            }
            Kind.CALL -> {
                held[s.id] = true
                wheelOwner = s.id
                wheelAt = padPoint(s, at)
            }
            Kind.MENU -> {
                owner.remove(id)
                ignored += id
                if (cfg.minimal && !menuNow) fanOpen = !fanOpen else NativeBridge.requestMenu()
            }
            Kind.LAYOUT -> {
                owner.remove(id)
                chooser.toggle()
                switchedLayout()
                ignored += id
            }
        }
        send()
    }

    /** The other layout shows: every finger and toggle let go (they belonged to the old controls). */
    private fun switchedLayout() {
        releaseAll()
        layoutRev++
    }

    private fun move(id: PointerId, at: Offset) {
        lastTouch = SystemClock.uptimeMillis()
        if (editing) {
            val s = specs.firstOrNull { it.id == selected } ?: return
            val off = grab[id] ?: return
            if (size.width <= 0f || size.height <= 0f) return
            val c = at - off
            val old = placement(s)
            placed[key(s.id)] = old.copy(x = (c.x / size.width).coerceIn(0.02f, 0.98f), y = (c.y / size.height).coerceIn(0.03f, 0.97f))
            return
        }
        val sid = owner[id] ?: return
        val s = specs.firstOrNull { it.id == sid } ?: return
        when (s.kind) {
            Kind.PAD -> {
                val p = padPoint(s, at)
                knobs[sid] = p
                if (!aiming && cfg.quickTurn) {
                    if (cfg.directional) doubleTap.move(p)
                    else if (flick.move(SystemClock.uptimeMillis(), p)) pulse(Ps2.DOWN or Ps2.CROSS, 150)
                }
            }
            Kind.DPAD -> knobs[sid] = padPoint(s, at)
            Kind.TALK, Kind.CALL -> if (wheelOwner == sid) wheelAt = padPoint(s, at)
            else -> return
        }
        send()
    }

    private fun up(id: PointerId) {
        grab.remove(id)
        if (ignored.remove(id)) return
        val sid = owner.remove(id) ?: return
        val s = specs.firstOrNull { it.id == sid } ?: return
        when (s.kind) {
            Kind.PAD -> {
                knobs.remove(sid)
                if (cfg.directional) doubleTap.up(SystemClock.uptimeMillis())
            }
            Kind.DPAD -> knobs.remove(sid)
            Kind.HOLD -> held.remove(sid)
            Kind.TALK -> {
                talkTimer?.cancel()
                held.remove(sid)
                if (wheelOwner == sid) {
                    wheelOwner = null
                    wheelAt = null
                } else pulse(Ps2.SQUARE, 150) // a tap: ad-lib
            }
            Kind.CALL -> {
                held.remove(sid)
                wheelOwner = null
                wheelAt = null
            }
            else -> {}
        }
        send()
    }

    private fun pulse(bits: Int, ms: Long) {
        pulseBits = pulseBits or bits
        send()
        scope.launch {
            delay(ms)
            pulseBits = pulseBits and bits.inv()
            send()
        }
    }

    /** What the fingers hold now, to the game when it changed. */
    private fun send(force: Boolean = false) {
        var bits = pulseBits
        var lx = 0x80
        var ly = 0x80
        var rx = 0x80
        var ry = 0x80
        if (r1On) bits = bits or Ps2.R1
        if (r2On) bits = bits or Ps2.R2
        for (s in specs) {
            when (s.kind) {
                Kind.HOLD -> if (held[s.id] == true) bits = bits or s.bits
                Kind.PAD -> {
                    val p = knobs[s.id] ?: continue
                    if (aiming) bits = bits or Zones.aim(p)
                    else if (cfg.directional) {
                        val (x, y) = Zones.stick(p)
                        lx = x
                        ly = y
                        if (cfg.runEdge && Zones.runs(p, null)) bits = bits or Ps2.CROSS
                    } else {
                        val t = Zones.tank(p, cfg.forwardZone, cfg.steerZone)
                        bits = bits or t
                        if (cfg.runEdge && Zones.runs(p, t)) bits = bits or Ps2.CROSS
                    }
                }
                Kind.DPAD -> knobs[s.id]?.let { bits = bits or if (s.four) Zones.dpad4(it) else Zones.dpad(it) }
                else -> {}
            }
        }
        wheelAt?.let { val (x, y) = Zones.wheel(it); rx = x; ry = y }
        val packed = bits.toLong() or (rx.toLong() shl 16) or (ry.toLong() shl 24) or (lx.toLong() shl 32) or (ly.toLong() shl 40)
        if (packed != lastSent || force) {
            lastSent = packed
            NativeBridge.touchPad(bits, rx, ry, lx, ly)
        }
        redraw++
    }

    private fun finishEdit() {
        savePlacements()
        editing = false
        selected = null
        val done = onEditDone
        onEditDone = null
        reload()
        done?.invoke()
    }

    /** Reset: the set being edited goes back to its defaults (the other sets keep theirs). */
    private fun resetLayout() {
        placed.keys.filter { it.startsWith("$section/") }.forEach { placed.remove(it) }
        selected = null
    }

    // ---- drawing ----

    @Composable
    private fun Overlay() {
        val tm = rememberTextMeasurer()
        LaunchedEffect(Unit) {
            // Ten times a second: the game's screen picks the layout (T16; native/ob_ui/core/game_screen.h). Every
            // fourth: Auto, a controller or keyboard in use hides the controls (not while a finger was on them lately).
            lastPhysical = NativeBridge.physicalPadReads()
            var tick = 0
            while (true) {
                delay(100)
                if (!editing && chooser.game(NativeBridge.gameScreen())) switchedLayout()
                if (++tick % 4 != 0) continue
                val n = NativeBridge.physicalPadReads()
                if (n != lastPhysical) {
                    lastPhysical = n
                    if (cfg.show == TouchShow.AUTO && !editing && shown && SystemClock.uptimeMillis() - lastTouch > 1500) {
                        shown = false
                        releaseAll()
                    }
                }
            }
        }
        Box(Modifier.fillMaxSize()) {
            Canvas(
                Modifier
                    .fillMaxSize()
                    .onSizeChanged { size = Size(it.width.toFloat(), it.height.toFloat()) }
                    .pointerInput(Unit) {
                        awaitPointerEventScope {
                            while (true) {
                                val ev = awaitPointerEvent()
                                for (c in ev.changes) {
                                    when {
                                        c.changedToDown() -> down(c.id, c.position)
                                        c.changedToUp() -> up(c.id)
                                        c.pressed && c.position != c.previousPosition -> move(c.id, c.position)
                                    }
                                    c.consume()
                                }
                            }
                        }
                    },
            ) {
                @Suppress("UNUSED_VARIABLE") val r = redraw // read: draw again after each send
                if (editing) drawRect(Color(0x66000000))
                if (!shown && !editing) return@Canvas
                for (s in showing()) drawControl(tm, s)
                if (fanOpen) drawFan(tm)
            }
            if (editing) EditBar()
        }
    }

    private fun alpha(): Float = if (editing) 0.9f else cfg.opacity / 100f

    private fun DrawScope.label(tm: TextMeasurer, text: String, at: Offset, sizeSp: Float, color: Color = Color.White) {
        if (text.isEmpty()) return
        val layout = tm.measure(text, TextStyle(color = color, fontSize = sizeSp.sp))
        drawText(layout, topLeft = Offset(at.x - layout.size.width / 2f, at.y - layout.size.height / 2f))
    }

    private fun DrawScope.drawControl(tm: TextMeasurer, s: ControlSpec) {
        val a = alpha()
        val c = centre(s)
        val r = radius(s)
        val line = Color.White.copy(alpha = 0.8f * a)
        val fill = Color(0xFF141414).copy(alpha = 0.55f * a)
        val on = when (s.kind) {
            Kind.TOGGLE -> if (s.bits == Ps2.R1) r1On else r2On
            Kind.HOLD, Kind.TALK, Kind.CALL -> held[s.id] == true
            else -> false
        }
        val bg = if (on) Accent.copy(alpha = 0.7f * a) else fill
        when (s.kind) {
            Kind.PAD -> drawPad(tm, s, c, r, a)
            Kind.DPAD -> {
                drawCircle(fill, r, c)
                drawCircle(line, r, c, style = Stroke(1.5f * density))
                for ((i, g) in listOf("▲", "▶", "▼", "◀").withIndex()) {
                    val ang = Math.toRadians(i * 90.0 - 90.0)
                    label(tm, g, Offset(c.x + cos(ang).toFloat() * r * 0.62f, c.y + sin(ang).toFloat() * r * 0.62f), 11f, line)
                }
                knobs[s.id]?.let { drawCircle(Color.White.copy(alpha = 0.5f * a), r * 0.3f, Offset(c.x + it.dx * r, c.y + it.dy * r)) }
            }
            else -> {
                if (s.round) {
                    drawCircle(bg, r, c)
                    drawCircle(line, r, c, style = Stroke(1.5f * density))
                } else {
                    val h = halfHeight(s)
                    drawRoundRect(bg, Offset(c.x - r, c.y - h), Size(r * 2, h * 2), CornerRadius(10f * density))
                    drawRoundRect(line, Offset(c.x - r, c.y - h), Size(r * 2, h * 2), CornerRadius(10f * density), style = Stroke(1.5f * density))
                }
                val text = s.labelFor(aimNow, chooser.controlType)
                val glyph = when (s.kind) {
                    Kind.MENU -> if (cfg.minimal && !menuNow) "≡" else "⚙"
                    Kind.TALK, Kind.CALL -> "…"
                    else -> s.glyph
                }
                val tc = Color.White.copy(alpha = a.coerceAtLeast(0.6f))
                if (s.kind == Kind.MENU) label(tm, glyph, c, 18f, tc)
                else {
                    label(tm, glyph, Offset(c.x, c.y - r * 0.18f), if (glyph.length > 2) 10f else 16f, tc)
                    label(tm, text, Offset(c.x, c.y + r * 0.38f), 9f, tc)
                }
                if (wheelOwner == s.id) drawWheel(tm, c, r)
            }
        }
        if (editing && selected == s.id) {
            val h = halfHeight(s)
            drawRect(Color(0xFF85B7EB), Offset(c.x - r - 6, c.y - h - 6), Size(r * 2 + 12, h * 2 + 12),
                style = Stroke(2f * density, pathEffect = PathEffect.dashPathEffect(floatArrayOf(12f, 8f))))
        }
    }

    private fun DrawScope.wedge(color: Color, c: Offset, r: Float, from: Float, to: Float) {
        // Our angles: 0 = up, clockwise; Compose's arcs: 0 = right, clockwise.
        drawArc(color, from - 90f, to - from, useCenter = true, topLeft = Offset(c.x - r, c.y - r), size = Size(r * 2, r * 2))
    }

    private fun DrawScope.drawPad(tm: TextMeasurer, s: ControlSpec, c: Offset, r: Float, a: Float) {
        val text = Color.White.copy(alpha = a.coerceAtLeast(0.6f))
        val line = Color.White.copy(alpha = 0.6f * a)
        if (aimNow) {
            val blue = Color(0xFF378ADD).copy(alpha = 0.5f * a)
            val grey = Color(0xFFB4B2A9).copy(alpha = 0.25f * a)
            wedge(blue, c, r, -Zones.AIM_HALF, Zones.AIM_HALF)
            wedge(blue, c, r, 180f - Zones.AIM_HALF, 180f + Zones.AIM_HALF)
            wedge(grey, c, r, Zones.AIM_HALF, 180f - Zones.AIM_HALF)
            wedge(grey, c, r, 180f + Zones.AIM_HALF, 360f - Zones.AIM_HALF)
            label(tm, "gun up", Offset(c.x, c.y - r * 0.7f), 10f, text)
            label(tm, "gun down", Offset(c.x, c.y + r * 0.7f), 10f, text)
            label(tm, "⟲", Offset(c.x - r * 0.7f, c.y), 13f, text)
            label(tm, "⟳", Offset(c.x + r * 0.7f, c.y), 13f, text)
        } else if (cfg.directional) {
            drawCircle(Color(0xFF141414).copy(alpha = 0.5f * a), r, c)
            label(tm, "screen up", Offset(c.x, c.y - r * 0.8f), 9f, text)
        } else {
            val f = cfg.forwardZone.toFloat()
            val st = f + cfg.steerZone
            val green = Color(0xFF5DCAA5)
            val amber = Color(0xFFEF9F27)
            val grey = Color(0xFFB4B2A9)
            wedge(green.copy(alpha = 0.55f * a), c, r, -f, f)
            wedge(green.copy(alpha = 0.28f * a), c, r, f, st)
            wedge(green.copy(alpha = 0.28f * a), c, r, -st, -f)
            wedge(grey.copy(alpha = 0.22f * a), c, r, st, Zones.TURN_END)
            wedge(grey.copy(alpha = 0.22f * a), c, r, -Zones.TURN_END, -st)
            wedge(amber.copy(alpha = 0.25f * a), c, r, Zones.TURN_END, Zones.BACK_TURN_END)
            wedge(amber.copy(alpha = 0.25f * a), c, r, -Zones.BACK_TURN_END, -Zones.TURN_END)
            wedge(amber.copy(alpha = 0.45f * a), c, r, Zones.BACK_TURN_END, 360f - Zones.BACK_TURN_END)
            label(tm, "fwd", Offset(c.x, c.y - r * 0.78f), 10f, text)
            label(tm, "back", Offset(c.x, c.y + r * 0.82f), 10f, text)
            label(tm, "⟲", Offset(c.x - r * 0.8f, c.y), 13f, text)
            label(tm, "⟳", Offset(c.x + r * 0.8f, c.y), 13f, text)
        }
        drawCircle(line, r, c, style = Stroke(2f * density))
        drawCircle(Color.Black.copy(alpha = 0.35f * a), r * Zones.PAD_CENTRE, c)
        if (!aimNow && cfg.runEdge)
            drawCircle(Color.White.copy(alpha = 0.55f * a), r * Zones.RUN_RING, c,
                style = Stroke(1.5f * density, pathEffect = PathEffect.dashPathEffect(floatArrayOf(10f, 10f))))
        val p = knobs[s.id]
        val k = if (p == null) c else Offset(c.x + p.dx.coerceIn(-1f, 1f) * r, c.y + p.dy.coerceIn(-1f, 1f) * r)
        drawCircle(Color.White.copy(alpha = 0.6f * a.coerceAtLeast(0.5f)), r * 0.22f, k)
        label(tm, if (aimNow) s.aimLabel else s.label, Offset(c.x, c.y + r + 10f * density), 10f, text)
    }

    private fun DrawScope.drawWheel(tm: TextMeasurer, c: Offset, r: Float) {
        val wr = maxOf(r * 2.4f, 80f * density)
        drawCircle(Color(0xFF141414).copy(alpha = 0.85f), wr, c)
        drawCircle(Color.White.copy(alpha = 0.7f), wr, c, style = Stroke(1.5f * density))
        val pick = wheelAt?.let { Zones.wheelName(it) }
        for ((name, off) in listOf("Come on" to Offset(0f, -0.7f), "Thanks" to Offset(0f, 0.7f), "Help" to Offset(-0.65f, 0f), "Go" to Offset(0.65f, 0f)))
            label(tm, name, Offset(c.x + off.x * wr, c.y + off.y * wr), 12f, if (name == pick) Accent else Color.White)
    }

    private fun DrawScope.drawFan(tm: TextMeasurer) {
        for ((i, rect) in fanRects().withIndex()) {
            drawRoundRect(Color(0xFF141414).copy(alpha = 0.9f), rect.topLeft, rect.size, CornerRadius(8f * density))
            drawRoundRect(Color.White.copy(alpha = 0.7f), rect.topLeft, rect.size, CornerRadius(8f * density), style = Stroke(1.5f * density))
            val item = MENU_FAN[i]
            label(tm, item.label, Offset(rect.left + rect.width * 0.36f, rect.center.y), 14f)
            label(tm, item.hint, Offset(rect.left + rect.width * 0.78f, rect.center.y), 10f, Color.White.copy(alpha = 0.6f))
        }
    }

    @Composable
    private fun EditBar() {
        val sel = specs.firstOrNull { it.id == selected }
        val hidden = specs.filter { placed[key(it.id)]?.hidden == true }
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
            Column(
                Modifier
                    .padding(top = 8.dp)
                    .background(Color(0xE6171A20), RoundedCornerShape(12.dp))
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text("Edit layout: drag a control; tap one to size or hide it", fontSize = 13.sp, color = Color.White)
                    OutlinedButton(onClick = { editView = EditView.entries[(editView.ordinal + 1) % EditView.entries.size]; selected = null }) {
                        Text("Showing: ${editView.label}", fontSize = 12.sp)
                    }
                    OutlinedButton(onClick = { resetLayout() }) { Text("Reset", fontSize = 12.sp) }
                    Button(onClick = { finishEdit() }) { Text("Done", fontSize = 12.sp) }
                }
                if (sel != null) {
                    val p = placement(sel)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text("${sel.glyph} ${sel.label}".trim(), fontSize = 13.sp, color = Color.White)
                        Text("Size ${(p.scale * 100).toInt()}%", fontSize = 12.sp, color = Color.White)
                        Slider(
                            value = p.scale, onValueChange = { placed[key(sel.id)] = p.copy(scale = (it * 20).toInt() / 20f) },
                            valueRange = 0.6f..1.8f, modifier = Modifier.width(220.dp),
                        )
                        OutlinedButton(onClick = { placed[key(sel.id)] = p.copy(hidden = true); selected = null }) { Text("Hide", fontSize = 12.sp) }
                    }
                }
                if (hidden.isNotEmpty()) {
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text("Hidden:", fontSize = 12.sp, color = Color.White)
                        for (h in hidden)
                            OutlinedButton(onClick = { placed[key(h.id)] = placement(h).copy(hidden = false) }) {
                                Text("Show ${h.label}", fontSize = 12.sp)
                            }
                    }
                }
            }
        }
    }
}
