package com.outbreakmodern.app

import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.roundToInt

// The touch controls' rules, free of Android so they test on the JVM (native/android/test): PS2 button bits, the tank
// pad's and aim pad's zones, the directional stick, the quick-turn gestures, the button sets and their saved layout,
// and the settings. The overlay (TouchOverlay.kt) draws and feeds them. Design: docs/plans/2026-10-26-touch-controls.md
// (decisions T1-T14; the numbers in its section 4).

/** PS2 pad buttons, as libpad's 16-bit mask (the game's mask is active low; here 1 = held). */
object Ps2 {
    const val SELECT = 0x0001
    const val L3 = 0x0002
    const val R3 = 0x0004
    const val START = 0x0008
    const val UP = 0x0010
    const val RIGHT = 0x0020
    const val DOWN = 0x0040
    const val LEFT = 0x0080
    const val L2 = 0x0100
    const val R2 = 0x0200
    const val L1 = 0x0400
    const val R1 = 0x0800
    const val TRIANGLE = 0x1000
    const val CIRCLE = 0x2000
    const val CROSS = 0x4000
    const val SQUARE = 0x8000
}

/** A finger on a round control: offset from its centre over its radius (y down), as magnitude 0..1 and angle. */
data class PadPoint(val dx: Float, val dy: Float) {
    val mag: Float get() = hypot(dx, dy).coerceAtMost(1f)
    /** Degrees from straight up, clockwise positive, -180..180. */
    val angle: Float get() = Math.toDegrees(atan2(dx.toDouble(), (-dy).toDouble())).toFloat()
}

object Zones {
    /** Inside this share of the radius a pad sends nothing. */
    const val PAD_CENTRE = 0.2f
    /** The directional stick's dead zone (share of the radius). */
    const val STICK_CENTRE = 0.12f
    /** Past this share of the radius, a forward push also holds run (✕). */
    const val RUN_RING = 0.68f
    /** Tank pad: turn on the spot up to this angle, then back-and-turn, then back. */
    const val TURN_END = 135f
    const val BACK_TURN_END = 158f
    /** Aim pad: up and down are this wide either side; the rest turns. */
    const val AIM_HALF = 35f

    private fun side(angle: Float) = if (angle > 0) Ps2.RIGHT else Ps2.LEFT

    /** Tank pad (T6, section 4.1): straight ahead, steer while moving, turn, back and turn, back. D-pad bits. */
    fun tank(p: PadPoint, forward: Int, steer: Int): Int {
        if (p.mag < PAD_CENTRE) return 0
        val a = abs(p.angle)
        return when {
            a <= forward -> Ps2.UP
            a <= forward + steer -> Ps2.UP or side(p.angle)
            a < TURN_END -> side(p.angle)
            a < BACK_TURN_END -> Ps2.DOWN or side(p.angle)
            else -> Ps2.DOWN
        }
    }

    /** Aim pad (T8, section 4.2): ↑ gun up, ↓ gun down, sides turn; no diagonals. D-pad bits. */
    fun aim(p: PadPoint): Int {
        if (p.mag < PAD_CENTRE) return 0
        val a = abs(p.angle)
        return when {
            a <= AIM_HALF -> Ps2.UP
            a >= 180f - AIM_HALF -> Ps2.DOWN
            else -> side(p.angle)
        }
    }

    /** A plain 8-way D-pad (Full set beside the directional stick, T14). */
    fun dpad(p: PadPoint): Int {
        if (p.mag < PAD_CENTRE) return 0
        val a = p.angle
        var bits = 0
        if (abs(a) < 67.5f) bits = bits or Ps2.UP
        if (abs(a) > 112.5f) bits = bits or Ps2.DOWN
        if (a in 22.5f..157.5f) bits = bits or Ps2.RIGHT
        if (a in -157.5f..-22.5f) bits = bits or Ps2.LEFT
        return bits
    }

    /** The directional stick (T5): x and y bytes, 0x80 = centre, up = smaller y (the PS2's convention). */
    fun stick(p: PadPoint): Pair<Int, Int> {
        if (p.mag < STICK_CENTRE) return 0x80 to 0x80
        val scale = p.mag / hypot(p.dx, p.dy).coerceAtLeast(1e-6f)
        fun byte(v: Float) = (128f + v * scale * 127f).roundToInt().coerceIn(0, 255)
        return byte(p.dx) to byte(p.dy)
    }

    /** Run at the stick edge: on the directional stick anywhere past the ring; on the tank pad only with ↑. */
    fun runs(p: PadPoint, tankBits: Int?): Boolean =
        p.mag > RUN_RING && (tankBits == null || tankBits and Ps2.UP != 0)

    /** The call wheel (T10): the right stick pushed fully toward the nearest of four directions, or centre. */
    fun wheel(p: PadPoint): Pair<Int, Int> {
        if (p.mag < 0.35f) return 0x80 to 0x80
        val a = p.angle
        return when {
            abs(a) <= 45f -> 0x80 to 0x00
            abs(a) >= 135f -> 0x80 to 0xFF
            a > 0 -> 0xFF to 0x80
            else -> 0x00 to 0x80
        }
    }

    /** The call wheel's phrase in that direction (for its label). */
    fun wheelName(p: PadPoint): String? {
        val (x, y) = wheel(p)
        return when {
            y == 0x00 -> "Come on"
            y == 0xFF -> "Thanks"
            x == 0xFF -> "Go"
            x == 0x00 -> "Help"
            else -> null
        }
    }
}

/** Tank pad quick turn (T6): a fast flick from the centre into the back zone. */
class FlickDetector {
    private var downAt = 0L
    private var startMag = 1f
    private var fired = false

    fun down(t: Long, p: PadPoint) {
        downAt = t
        startMag = p.mag
        fired = false
    }

    /** True once, when the finger reaches the back zone fast enough. */
    fun move(t: Long, p: PadPoint): Boolean {
        if (fired || startMag >= 0.35f || t - downAt > 200 || p.mag <= 0.6f || abs(p.angle) < 149f) return false
        fired = true
        return true
    }
}

/** Directional stick quick turn (T6): a second tap soon after a short tap near the centre. */
class DoubleTapDetector {
    private var lastTapUp = -10_000L
    private var downAt = 0L
    private var maxMag = 0f
    private var second = false

    /** True when this touch is the second tap. */
    fun down(t: Long, p: PadPoint): Boolean {
        second = t - lastTapUp < 300 && p.mag < 0.5f
        downAt = t
        maxMag = p.mag
        lastTapUp = -10_000L
        return second
    }

    fun move(p: PadPoint) {
        maxMag = maxOf(maxMag, p.mag)
    }

    /** A short tap near the centre may start a pair; the second tap of a pair never does (no turn per extra tap). */
    fun up(t: Long) {
        if (!second && t - downAt < 220 && maxMag < 0.5f) lastTapUp = t
        second = false
    }
}

enum class TouchShow { AUTO, ON, OFF }

/** The [ui] touch_* keys of outbreak.ini (section 4.5). */
data class TouchSettings(
    val show: TouchShow = TouchShow.AUTO,
    val minimal: Boolean = true,
    val directional: Boolean = true,
    val forwardZone: Int = 18,
    val steerZone: Int = 47,
    val runEdge: Boolean = true,
    val quickTurn: Boolean = true,
    val scale: Int = 100,
    val opacity: Int = 60,
    val vibrate: Boolean = true,
) {
    companion object {
        /** Reads the keys through [ui] (key, default) -> value; unknown or bad values keep the default. */
        fun load(ui: (String, String) -> String): TouchSettings {
            val d = TouchSettings()
            fun int(k: String, def: Int, lo: Int, hi: Int) = ui(k, def.toString()).trim().toIntOrNull()?.coerceIn(lo, hi) ?: def
            fun on(k: String, def: Boolean) = when (ui(k, if (def) "on" else "off").trim()) { "on" -> true; "off" -> false; else -> def }
            return TouchSettings(
                show = when (ui("touch", "auto").trim()) { "on" -> TouchShow.ON; "off" -> TouchShow.OFF; else -> TouchShow.AUTO },
                minimal = ui("touch_buttons", "minimal").trim() != "full",
                directional = ui("touch_move", "directional").trim() != "tank",
                forwardZone = int("touch_forward_zone", d.forwardZone, 8, 40),
                steerZone = int("touch_steer_zone", d.steerZone, 15, 70),
                runEdge = on("touch_run_edge", d.runEdge),
                quickTurn = on("touch_quick_turn", d.quickTurn),
                scale = int("touch_scale", d.scale, 60, 160),
                opacity = int("touch_opacity", d.opacity, 10, 100),
                vibrate = on("touch_vibrate", d.vibrate),
            )
        }
    }
}

/** What a control does. */
enum class Kind {
    /** The movement pad: directional stick or tank pad, the aim pad while aiming. */
    PAD,
    /** A small 8-way D-pad (Full set, directional). */
    DPAD,
    /** Held while touched. */
    HOLD,
    /** Tap on, tap off (Aim). */
    TOGGLE,
    /** Tap = □ ad-lib; hold = the call wheel (Minimal Talk). */
    TALK,
    /** The call wheel only (Full Call). */
    CALL,
    /** Minimal: a fan of Status / Map / Options / Settings. Full: opens Settings. */
    MENU,
}

/** When a control shows. */
enum class Ctx { ALWAYS, WALKING, AIMING }

/**
 * One control of a button set at its default place. x, y: its centre as a share of the screen; size: diameter (or
 * width) in dp at 100%; round: a circle, else a rounded rectangle 0.62 as tall. label/aimLabel: what it says.
 */
data class ControlSpec(
    val id: String, val kind: Kind, val bits: Int, val glyph: String, val label: String,
    val x: Float, val y: Float, val size: Float, val round: Boolean = true,
    val ctx: Ctx = Ctx.ALWAYS, val aimLabel: String = label,
)

/** A control as the player placed it (touch_layout.ini: "x, y, scale, hidden"). */
data class Placement(val x: Float, val y: Float, val scale: Float = 1f, val hidden: Boolean = false) {
    fun encode(): String = String.format(java.util.Locale.ROOT, "%.4f, %.4f, %.2f, %d", x, y, scale, if (hidden) 1 else 0)

    companion object {
        fun decode(s: String?): Placement? {
            val p = s?.split(',')?.map { it.trim() } ?: return null
            if (p.size != 4) return null
            val x = p[0].toFloatOrNull() ?: return null
            val y = p[1].toFloatOrNull() ?: return null
            val sc = p[2].toFloatOrNull() ?: return null
            if (x !in 0f..1f || y !in 0f..1f) return null
            return Placement(x, y, sc.coerceIn(0.6f, 1.8f), p[3] == "1")
        }
    }
}

object ButtonSets {
    /** touch_layout.ini section of a set. */
    fun section(minimal: Boolean) = if (minimal) "minimal" else "full"

    /** Minimal (T2, section 4.3): walking = pad, Action, Aim, Run, Talk, Menu; aiming swaps Talk for Shift/reload. */
    val MINIMAL = listOf(
        ControlSpec("pad", Kind.PAD, 0, "", "Move", 0.14f, 0.66f, 150f, aimLabel = "Aim"),
        ControlSpec("circle", Kind.HOLD, Ps2.CIRCLE, "○", "Action", 0.88f, 0.64f, 70f, aimLabel = "Fire"),
        ControlSpec("r1", Kind.TOGGLE, Ps2.R1, "R1", "Aim", 0.88f, 0.30f, 60f, aimLabel = "Aiming"),
        ControlSpec("cross", Kind.HOLD, Ps2.CROSS, "✕", "Run", 0.77f, 0.82f, 50f, aimLabel = "Special"),
        ControlSpec("talk", Kind.TALK, Ps2.SQUARE, "", "Talk", 0.75f, 0.47f, 54f, ctx = Ctx.WALKING),
        ControlSpec("l1", Kind.HOLD, Ps2.L1, "L1", "Shift/reload", 0.75f, 0.47f, 58f, ctx = Ctx.AIMING),
        ControlSpec("menu", Kind.MENU, 0, "", "Menu", 0.96f, 0.08f, 40f),
    )

    /** Full (T2, T14): every PS2 button, the call wheel, and a small tank D-pad beside the directional stick. */
    val FULL = listOf(
        ControlSpec("pad", Kind.PAD, 0, "", "Move", 0.13f, 0.68f, 140f, aimLabel = "Aim"),
        ControlSpec("dpad", Kind.DPAD, 0, "", "D-pad", 0.28f, 0.86f, 70f),
        ControlSpec("circle", Kind.HOLD, Ps2.CIRCLE, "○", "Action", 0.89f, 0.62f, 60f, aimLabel = "Fire"),
        ControlSpec("cross", Kind.HOLD, Ps2.CROSS, "✕", "Run", 0.80f, 0.79f, 56f, aimLabel = "Special"),
        ControlSpec("square", Kind.HOLD, Ps2.SQUARE, "□", "Ad-lib", 0.80f, 0.46f, 52f),
        ControlSpec("triangle", Kind.HOLD, Ps2.TRIANGLE, "△", "Map", 0.71f, 0.62f, 50f),
        ControlSpec("r1", Kind.TOGGLE, Ps2.R1, "R1", "Aim", 0.91f, 0.22f, 64f, round = false, aimLabel = "Aiming"),
        ControlSpec("r2", Kind.TOGGLE, Ps2.R2, "R2", "Aim fwd", 0.78f, 0.22f, 56f, round = false),
        ControlSpec("l1", Kind.HOLD, Ps2.L1, "L1", "Reload", 0.09f, 0.22f, 64f, round = false, aimLabel = "Shift/reload"),
        ControlSpec("l2", Kind.HOLD, Ps2.L2, "L2", "Call set", 0.22f, 0.22f, 56f, round = false),
        ControlSpec("call", Kind.CALL, 0, "", "Call", 0.63f, 0.84f, 56f),
        ControlSpec("select", Kind.HOLD, Ps2.SELECT, "SEL", "Options", 0.42f, 0.07f, 50f, round = false),
        ControlSpec("start", Kind.HOLD, Ps2.START, "START", "Status", 0.58f, 0.07f, 50f, round = false),
        ControlSpec("menu", Kind.MENU, 0, "", "Settings", 0.96f, 0.08f, 40f),
    )

    fun of(minimal: Boolean, directional: Boolean): List<ControlSpec> =
        if (minimal) MINIMAL else FULL.filter { it.kind != Kind.DPAD || directional }

    /** The controls showing now: by context, without the hidden ones. */
    fun visible(specs: List<ControlSpec>, aiming: Boolean, placed: Map<String, Placement>): List<ControlSpec> =
        specs.filter { (it.ctx == Ctx.ALWAYS || (it.ctx == Ctx.AIMING) == aiming) && placed[it.id]?.hidden != true }
}

/** The Minimal set's Menu fan (T11): what each entry presses, or null = open the settings menu. */
data class FanItem(val label: String, val hint: String, val bits: Int?)

val MENU_FAN = listOf(
    FanItem("Status", "START", Ps2.START),
    FanItem("Map", "△", Ps2.TRIANGLE),
    FanItem("Options", "SELECT", Ps2.SELECT),
    FanItem("Settings", "this app", null),
)
