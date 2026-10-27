package com.outbreakmodern.app

import android.view.Window
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

/**
 * Fullscreen as a game (developer, 2026-10-27: the navigation bar covered the launcher's right edge): the status and
 * navigation bars hidden, a swipe from the screen's edge shows them for a moment (Android's immersive mode, its form of
 * exclusive fullscreen). [underCutout]: also draw beside the camera cutout (the game picture and the touch controls);
 * the menus keep clear of it. Call again when a window regains focus: system pickers and dialogs bring the bars back.
 */
fun Window.immersive(underCutout: Boolean = false) {
    WindowCompat.setDecorFitsSystemWindows(this, false)
    attributes = attributes.apply {
        layoutInDisplayCutoutMode = if (underCutout) WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        else WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT
    }
    WindowInsetsControllerCompat(this, decorView).apply {
        hide(WindowInsetsCompat.Type.systemBars())
        systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }
}
