package com.outbreakmodern.app

import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.abs

// Adapted from PlayFieldPortal's GamepadInputHandler (feature-xmb/gamepad/GamepadInputHandler.kt, same developer):
// the same dead zone with hysteresis, accelerating held-direction repeat, D-pad duplicate suppression and
// "newest axis wins" diagonal rule, trimmed to a fixed mapping (no remapping, Hilt or Timber).

enum class Nav { UP, DOWN, LEFT, RIGHT, A, B, X, Y, L1, R1, START, SELECT }

private const val STICK_DEAD_ZONE = 0.5f
private const val STICK_RELEASE_FACTOR = 0.6f
private const val HAT_DEAD_ZONE = 0.5f
private const val DUPLICATE_WINDOW_MS = 80L
private const val STICK_FULL_TILT = 0.9f
// PlayFieldPortal's STANDARD repeat: 250 ms before the first repeat, then 110 ms tightening to 50 ms over 5 repeats.
private const val INITIAL_DELAY_MS = 250L
private const val BASE_INTERVAL_MS = 110L
private const val FAST_INTERVAL_MS = 50L
private const val RAMP_STEPS = 5

class GamepadInput(private val scope: CoroutineScope, private val onAction: (Nav) -> Unit) {
    private var repeatJob: Job? = null
    private var lastStickAction: Nav? = null
    @Volatile private var stickMagnitude = 0f
    private var prevHatX = 0f
    private var prevHatY = 0f
    private val lastDirectionalEmitAt = mutableMapOf<Nav, Long>()

    private fun actionFor(keyCode: Int): Nav? = when (keyCode) {
        KeyEvent.KEYCODE_DPAD_UP -> Nav.UP
        KeyEvent.KEYCODE_DPAD_DOWN -> Nav.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> Nav.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> Nav.RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER,
        KeyEvent.KEYCODE_NUMPAD_ENTER, KeyEvent.KEYCODE_SPACE -> Nav.A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_ESCAPE -> Nav.B
        KeyEvent.KEYCODE_BUTTON_X -> Nav.X
        KeyEvent.KEYCODE_BUTTON_Y -> Nav.Y
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_PAGE_UP -> Nav.L1
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_PAGE_DOWN -> Nav.R1
        KeyEvent.KEYCODE_BUTTON_START -> Nav.START
        KeyEvent.KEYCODE_BUTTON_SELECT -> Nav.SELECT
        else -> null
    }

    /** From dispatchKeyEvent. True = consumed. */
    fun onKeyEvent(event: KeyEvent): Boolean {
        val action = actionFor(event.keyCode) ?: return false
        return when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                if (event.repeatCount == 0) {
                    if (action.isDirectional() && isDuplicateDirection(action)) return true
                    if (action.isDirectional()) startRepeat(action) // armed before emitting (see PFP's note)
                    emit(action, physical = true)
                }
                true
            }
            KeyEvent.ACTION_UP -> {
                if (action.isDirectional()) cancelRepeat()
                true
            }
            else -> false
        }
    }

    /** From dispatchGenericMotionEvent: the left stick and the hat (D-pad as axes). */
    fun onMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false
        val x = event.getAxisValue(MotionEvent.AXIS_X)
        val y = event.getAxisValue(MotionEvent.AXIS_Y)
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        val flat = runCatching { event.device?.getMotionRange(MotionEvent.AXIS_X)?.flat }.getOrNull() ?: 0f
        val motionAction = hatDirection(hatX, hatY) ?: stickDirection(x, y, flat)
        stickMagnitude = if (motionAction != null) maxOf(abs(x), abs(y), abs(hatX), abs(hatY)) else 0f
        if (motionAction != lastStickAction) {
            cancelRepeat()
            lastStickAction = motionAction
            if (motionAction != null && !isDuplicateDirection(motionAction)) {
                startRepeat(motionAction)
                emit(motionAction, physical = true)
            }
        }
        prevHatX = hatX
        prevHatY = hatY
        return motionAction != null
    }

    private fun emit(action: Nav, physical: Boolean = false) {
        if (physical && action.isDirectional()) lastDirectionalEmitAt[action] = SystemClock.uptimeMillis()
        onAction(action)
    }

    private fun startRepeat(action: Nav) {
        repeatJob?.cancel()
        repeatJob = scope.launch {
            delay(INITIAL_DELAY_MS)
            var step = 0
            while (true) {
                emit(action)
                step++
                val ramped = if (step >= RAMP_STEPS) FAST_INTERVAL_MS
                else BASE_INTERVAL_MS - (BASE_INTERVAL_MS - FAST_INTERVAL_MS) * step / RAMP_STEPS
                delay(if (stickMagnitude >= STICK_FULL_TILT) FAST_INTERVAL_MS else ramped)
            }
        }
    }

    fun cancelRepeat() {
        repeatJob?.cancel()
        repeatJob = null
    }

    private fun stickDirection(x: Float, y: Float, flat: Float): Nav? {
        val activation = maxOf(STICK_DEAD_ZONE, flat)
        val release = activation * STICK_RELEASE_FACTOR
        val strong = when {
            y < -activation -> Nav.UP
            y > activation -> Nav.DOWN
            x < -activation -> Nav.LEFT
            x > activation -> Nav.RIGHT
            else -> null
        }
        if (strong != null) return strong
        val engaged = lastStickAction ?: return null
        val still = when (engaged) {
            Nav.UP -> y < -release
            Nav.DOWN -> y > release
            Nav.LEFT -> x < -release
            Nav.RIGHT -> x > release
            else -> false
        }
        return if (still) engaged else null
    }

    // "The axis you just pressed wins" (PFP hatDirectionNewestFirst): a D-pad rolled through a diagonal answers the
    // new press at once instead of waiting for the held one to be released.
    private fun hatDirection(hatX: Float, hatY: Float): Nav? {
        val xOn = abs(hatX) > HAT_DEAD_ZONE
        val yOn = abs(hatY) > HAT_DEAD_ZONE
        val xDir = if (hatX < 0) Nav.LEFT else Nav.RIGHT
        val yDir = if (hatY < 0) Nav.UP else Nav.DOWN
        return when {
            !xOn && !yOn -> null
            xOn && !yOn -> xDir
            yOn && !xOn -> yDir
            abs(prevHatX) <= HAT_DEAD_ZONE && abs(prevHatY) > HAT_DEAD_ZONE -> xDir
            abs(prevHatY) <= HAT_DEAD_ZONE && abs(prevHatX) > HAT_DEAD_ZONE -> yDir
            lastStickAction == xDir || lastStickAction == yDir -> lastStickAction
            else -> yDir
        }
    }

    private fun isDuplicateDirection(action: Nav): Boolean {
        if (!action.isDirectional()) return false
        val last = lastDirectionalEmitAt[action] ?: return false
        return SystemClock.uptimeMillis() - last < DUPLICATE_WINDOW_MS
    }

    private fun Nav.isDirectional() = this == Nav.UP || this == Nav.DOWN || this == Nav.LEFT || this == Nav.RIGHT
}

/** Binding capture: the input.ini name of a key (keyboard) or button (gamepad) event, as raylib names them. */
object InputNames {
    fun keyboard(keyCode: Int): String? = when (keyCode) {
        in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> ('A' + (keyCode - KeyEvent.KEYCODE_A)).toString()
        in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 ->
            listOf("ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE")[keyCode - KeyEvent.KEYCODE_0]
        in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 -> "F" + (keyCode - KeyEvent.KEYCODE_F1 + 1)
        in KeyEvent.KEYCODE_NUMPAD_0..KeyEvent.KEYCODE_NUMPAD_9 -> "KP_" + (keyCode - KeyEvent.KEYCODE_NUMPAD_0)
        KeyEvent.KEYCODE_SPACE -> "SPACE"
        KeyEvent.KEYCODE_ENTER -> "ENTER"
        KeyEvent.KEYCODE_DEL -> "BACKSPACE"
        KeyEvent.KEYCODE_FORWARD_DEL -> "DELETE"
        KeyEvent.KEYCODE_TAB -> "TAB"
        KeyEvent.KEYCODE_ESCAPE -> "ESCAPE"
        KeyEvent.KEYCODE_DPAD_UP -> "UP"
        KeyEvent.KEYCODE_DPAD_DOWN -> "DOWN"
        KeyEvent.KEYCODE_DPAD_LEFT -> "LEFT"
        KeyEvent.KEYCODE_DPAD_RIGHT -> "RIGHT"
        KeyEvent.KEYCODE_PAGE_UP -> "PAGE_UP"
        KeyEvent.KEYCODE_PAGE_DOWN -> "PAGE_DOWN"
        KeyEvent.KEYCODE_MOVE_HOME -> "HOME"
        KeyEvent.KEYCODE_MOVE_END -> "END"
        KeyEvent.KEYCODE_INSERT -> "INSERT"
        KeyEvent.KEYCODE_SHIFT_LEFT -> "LEFT_SHIFT"
        KeyEvent.KEYCODE_SHIFT_RIGHT -> "RIGHT_SHIFT"
        KeyEvent.KEYCODE_CTRL_LEFT -> "LEFT_CONTROL"
        KeyEvent.KEYCODE_CTRL_RIGHT -> "RIGHT_CONTROL"
        KeyEvent.KEYCODE_ALT_LEFT -> "LEFT_ALT"
        KeyEvent.KEYCODE_ALT_RIGHT -> "RIGHT_ALT"
        KeyEvent.KEYCODE_COMMA -> "COMMA"
        KeyEvent.KEYCODE_PERIOD -> "PERIOD"
        KeyEvent.KEYCODE_MINUS -> "MINUS"
        KeyEvent.KEYCODE_EQUALS -> "EQUAL"
        KeyEvent.KEYCODE_SEMICOLON -> "SEMICOLON"
        KeyEvent.KEYCODE_APOSTROPHE -> "APOSTROPHE"
        KeyEvent.KEYCODE_SLASH -> "SLASH"
        KeyEvent.KEYCODE_BACKSLASH -> "BACKSLASH"
        KeyEvent.KEYCODE_LEFT_BRACKET -> "LEFT_BRACKET"
        KeyEvent.KEYCODE_RIGHT_BRACKET -> "RIGHT_BRACKET"
        KeyEvent.KEYCODE_GRAVE -> "GRAVE"
        else -> null
    }

    // raylib's Android gamepad mapping (rcore_android.c AndroidTranslateGamepadButton).
    fun gamepad(keyCode: Int): String? = when (keyCode) {
        KeyEvent.KEYCODE_BUTTON_A -> "RIGHT_FACE_DOWN"
        KeyEvent.KEYCODE_BUTTON_B -> "RIGHT_FACE_RIGHT"
        KeyEvent.KEYCODE_BUTTON_X -> "RIGHT_FACE_LEFT"
        KeyEvent.KEYCODE_BUTTON_Y -> "RIGHT_FACE_UP"
        KeyEvent.KEYCODE_BUTTON_L1 -> "LEFT_TRIGGER_1"
        KeyEvent.KEYCODE_BUTTON_R1 -> "RIGHT_TRIGGER_1"
        KeyEvent.KEYCODE_BUTTON_L2 -> "LEFT_TRIGGER_2, LEFT_TRIGGER"
        KeyEvent.KEYCODE_BUTTON_R2 -> "RIGHT_TRIGGER_2, RIGHT_TRIGGER"
        KeyEvent.KEYCODE_BUTTON_THUMBL -> "LEFT_THUMB"
        KeyEvent.KEYCODE_BUTTON_THUMBR -> "RIGHT_THUMB"
        KeyEvent.KEYCODE_BUTTON_START -> "MIDDLE_RIGHT"
        KeyEvent.KEYCODE_BUTTON_SELECT -> "MIDDLE_LEFT"
        KeyEvent.KEYCODE_DPAD_UP -> "LEFT_FACE_UP"
        KeyEvent.KEYCODE_DPAD_DOWN -> "LEFT_FACE_DOWN"
        KeyEvent.KEYCODE_DPAD_LEFT -> "LEFT_FACE_LEFT"
        KeyEvent.KEYCODE_DPAD_RIGHT -> "LEFT_FACE_RIGHT"
        else -> null
    }

    fun padLabel(names: String): String = when (names.substringBefore(',').trim()) {
        "RIGHT_FACE_DOWN" -> "A"
        "RIGHT_FACE_RIGHT" -> "B"
        "RIGHT_FACE_LEFT" -> "X"
        "RIGHT_FACE_UP" -> "Y"
        "LEFT_FACE_UP" -> "D-pad up"
        "LEFT_FACE_DOWN" -> "D-pad down"
        "LEFT_FACE_LEFT" -> "D-pad left"
        "LEFT_FACE_RIGHT" -> "D-pad right"
        "LEFT_TRIGGER_1" -> "L1"
        "RIGHT_TRIGGER_1" -> "R1"
        "LEFT_TRIGGER_2", "LEFT_TRIGGER" -> "L2"
        "RIGHT_TRIGGER_2", "RIGHT_TRIGGER" -> "R2"
        "LEFT_THUMB" -> "Left stick click"
        "RIGHT_THUMB" -> "Right stick click"
        "MIDDLE_RIGHT" -> "Start"
        "MIDDLE_LEFT" -> "Select"
        else -> names.substringBefore(',').trim()
    }
}
