package com.outbreakmodern.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.cos
import kotlin.math.sin

// The touch controls' rules (TouchLogic.kt) against the numbers in docs/plans/2026-10-26-touch-controls.md section 4.
// Run by tools/ob/android.sh package (gradlew testDebugUnitTest) before the APK is built.
class TouchLogicTest {
    /** A finger at [deg] from straight up (clockwise), [mag] of the radius out. */
    private fun at(deg: Float, mag: Float): PadPoint {
        val r = Math.toRadians(deg.toDouble())
        return PadPoint((sin(r) * mag).toFloat(), (-cos(r) * mag).toFloat())
    }

    @Test
    fun padPointAngleAndMagnitude() {
        assertEquals(0f, at(0f, 1f).angle, 0.01f)
        assertEquals(90f, at(90f, 1f).angle, 0.01f)
        assertEquals(-90f, at(-90f, 0.5f).angle, 0.01f)
        assertEquals(0.5f, at(-90f, 0.5f).mag, 0.001f)
        assertEquals(1f, PadPoint(3f, 0f).mag, 0f) // clamped to the rim
    }

    @Test
    fun tankZonesFollowTheDefaults() {
        val f = 18
        val s = 47
        assertEquals(0, Zones.tank(at(0f, 0.1f), f, s))                              // centre
        assertEquals(Ps2.UP, Zones.tank(at(10f, 0.8f), f, s))                        // straight ahead
        assertEquals(Ps2.UP or Ps2.RIGHT, Zones.tank(at(40f, 0.8f), f, s))           // steer while moving
        assertEquals(Ps2.UP or Ps2.LEFT, Zones.tank(at(-64f, 0.8f), f, s))           // edge of steer (18 + 47 = 65)
        assertEquals(Ps2.LEFT, Zones.tank(at(-66f, 0.8f), f, s))                     // turn on the spot
        assertEquals(Ps2.RIGHT, Zones.tank(at(134f, 0.8f), f, s))
        assertEquals(Ps2.DOWN or Ps2.RIGHT, Zones.tank(at(140f, 0.8f), f, s))        // back and turn
        assertEquals(Ps2.DOWN, Zones.tank(at(170f, 0.8f), f, s))                     // back
        assertEquals(Ps2.DOWN, Zones.tank(at(-175f, 0.8f), f, s))
        assertEquals(Ps2.UP, Zones.tank(at(30f, 0.8f), 35, s))                       // a wider straight zone
    }

    @Test
    fun aimPadHasNoDiagonals() {
        assertEquals(Ps2.UP, Zones.aim(at(30f, 0.9f)))
        assertEquals(Ps2.RIGHT, Zones.aim(at(40f, 0.9f)))
        assertEquals(Ps2.LEFT, Zones.aim(at(-90f, 0.9f)))
        assertEquals(Ps2.DOWN, Zones.aim(at(150f, 0.9f)))
        assertEquals(0, Zones.aim(at(0f, 0.1f)))
        for (deg in -180..180 step 5) {
            val b = Zones.aim(at(deg.toFloat(), 0.9f))
            assertEquals("one direction at $deg", 1, Integer.bitCount(b))
        }
    }

    @Test
    fun dpadIsEightWay() {
        assertEquals(Ps2.UP, Zones.dpad(at(0f, 0.9f)))
        assertEquals(Ps2.UP or Ps2.RIGHT, Zones.dpad(at(45f, 0.9f)))
        assertEquals(Ps2.RIGHT, Zones.dpad(at(90f, 0.9f)))
        assertEquals(Ps2.DOWN or Ps2.LEFT, Zones.dpad(at(-135f, 0.9f)))
        assertEquals(0, Zones.dpad(at(0f, 0.1f)))
    }

    @Test
    fun directionalStickBytes() {
        assertEquals(0x80 to 0x80, Zones.stick(at(30f, 0.05f)))     // dead zone
        assertEquals(0x80 to 0x01, Zones.stick(at(0f, 1f)))         // full up = small y
        assertEquals(0xFF to 0x80, Zones.stick(at(90f, 1f)))        // full right
        assertEquals(0x80 to 0xFF, Zones.stick(PadPoint(0f, 3f)))   // past the rim: clamped
        val (x, y) = Zones.stick(at(-90f, 0.5f))
        assertTrue(x in 63..65 && y == 0x80)
    }

    @Test
    fun runRing() {
        assertTrue(Zones.runs(at(0f, 0.8f), null))
        assertFalse(Zones.runs(at(0f, 0.5f), null))
        assertTrue(Zones.runs(at(0f, 0.8f), Ps2.UP or Ps2.LEFT))    // tank: only with forward
        assertFalse(Zones.runs(at(90f, 0.8f), Ps2.RIGHT))
    }

    @Test
    fun callWheelSnapsToFourDirections() {
        assertEquals(0x80 to 0x00, Zones.wheel(at(10f, 0.8f)))
        assertEquals(0x80 to 0xFF, Zones.wheel(at(170f, 0.8f)))
        assertEquals(0xFF to 0x80, Zones.wheel(at(80f, 0.8f)))
        assertEquals(0x00 to 0x80, Zones.wheel(at(-80f, 0.8f)))
        assertEquals(0x80 to 0x80, Zones.wheel(at(0f, 0.2f)))
        assertEquals("Come on", Zones.wheelName(at(0f, 0.9f)))
        assertEquals("Help", Zones.wheelName(at(-90f, 0.9f)))
        assertNull(Zones.wheelName(at(0f, 0.1f)))
    }

    @Test
    fun flickFiresOnceOnlyWhenFast() {
        val f = FlickDetector()
        f.down(1000, at(0f, 0.1f))
        assertFalse(f.move(1050, at(170f, 0.4f)))  // not far enough yet
        assertTrue(f.move(1100, at(170f, 0.8f)))   // into the back zone within 200 ms
        assertFalse(f.move(1120, at(170f, 0.9f)))  // once per touch
        f.down(2000, at(0f, 0.1f))
        assertFalse(f.move(2300, at(170f, 0.9f)))  // too slow: walking back
        f.down(3000, at(0f, 0.6f))
        assertFalse(f.move(3050, at(170f, 0.9f)))  // started away from the centre
        f.down(4000, at(0f, 0.1f))
        assertFalse(f.move(4050, at(120f, 0.9f)))  // not straight back
    }

    @Test
    fun doubleTapNeedsTwoShortTaps() {
        val d = DoubleTapDetector()
        assertFalse(d.down(1000, at(0f, 0.1f)))
        d.up(1100)
        assertTrue(d.down(1250, at(0f, 0.2f)))     // second tap within 300 ms
        d.up(1300)
        assertFalse(d.down(1400, at(0f, 0.1f)))   // a third quick tap starts a new pair, no second turn
        d.up(1450)
        assertTrue(d.down(1600, at(0f, 0.1f)))    // ... which its next tap completes
        val e = DoubleTapDetector()
        e.down(1000, at(0f, 0.1f))
        e.move(at(0f, 0.9f))                       // a push, not a tap
        e.up(1100)
        assertFalse(e.down(1200, at(0f, 0.1f)))
        val g = DoubleTapDetector()
        g.down(1000, at(0f, 0.1f))
        g.up(1100)
        assertFalse(g.down(1500, at(0f, 0.1f)))    // too late
    }

    @Test
    fun settingsDefaultsAndBadValues() {
        val d = TouchSettings.load { _, def -> def }
        assertEquals(TouchShow.AUTO, d.show)
        assertTrue(d.minimal && d.directional && d.runEdge && d.quickTurn && d.vibrate)
        assertEquals(18, d.forwardZone)
        assertEquals(47, d.steerZone)
        assertEquals(100, d.scale)
        assertEquals(60, d.opacity)
        val vals = mapOf("touch" to "off", "touch_buttons" to "full", "touch_move" to "tank", "touch_scale" to "500",
            "touch_opacity" to "x", "touch_run_edge" to "off", "touch_forward_zone" to "3")
        val t = TouchSettings.load { k, def -> vals[k] ?: def }
        assertEquals(TouchShow.OFF, t.show)
        assertFalse(t.minimal || t.directional || t.runEdge)
        assertEquals(160, t.scale)     // clamped
        assertEquals(60, t.opacity)    // bad value: default
        assertEquals(8, t.forwardZone) // clamped
    }

    @Test
    fun placementRoundTrip() {
        val p = Placement(0.25f, 0.75f, 1.2f, true)
        assertEquals(p, Placement.decode(p.encode()))
        assertEquals("0.2500, 0.7500, 1.20, 1", p.encode())
        assertNull(Placement.decode("1, 2"))
        assertNull(Placement.decode("1.5, 0.5, 1, 0"))  // off screen
        assertNull(Placement.decode(null))
        assertEquals(1.8f, Placement.decode("0.5, 0.5, 9, 0")!!.scale, 0f)
    }

    @Test
    fun buttonSetsByContext() {
        val min = ButtonSets.of(minimal = true, directional = true)
        val walking = ButtonSets.visible(min, aiming = false, placed = emptyMap()).map { it.id }
        val aiming = ButtonSets.visible(min, aiming = true, placed = emptyMap()).map { it.id }
        assertEquals(listOf("pad", "circle", "r1", "cross", "talk", "menu"), walking)
        assertEquals(listOf("pad", "circle", "r1", "cross", "l1", "menu"), aiming)
        val hidden = ButtonSets.visible(min, false, mapOf("talk" to Placement(0.5f, 0.5f, 1f, true))).map { it.id }
        assertFalse("talk" in hidden)
        // Full: the small D-pad only beside the directional stick (T14); every PS2 face and shoulder button present.
        assertTrue(ButtonSets.of(false, true).any { it.kind == Kind.DPAD })
        assertFalse(ButtonSets.of(false, false).any { it.kind == Kind.DPAD })
        val bits = ButtonSets.of(false, true).fold(0) { a, s -> a or s.bits }
        val want = Ps2.CIRCLE or Ps2.CROSS or Ps2.SQUARE or Ps2.TRIANGLE or Ps2.L1 or Ps2.L2 or Ps2.R1 or Ps2.R2 or Ps2.SELECT or Ps2.START
        assertEquals(want, bits and want)
        // Ids are unique in each set (the layout file keys on them).
        for (set in listOf(ButtonSets.MINIMAL, ButtonSets.FULL)) assertEquals(set.size, set.map { it.id }.toSet().size)
    }

    @Test
    fun menuFanPresses() {
        assertEquals(listOf(Ps2.START, Ps2.TRIANGLE, Ps2.SELECT, null), MENU_FAN.map { it.bits })
    }
}
