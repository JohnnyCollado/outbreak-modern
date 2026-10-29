package com.outbreakmodern.app

import android.app.NativeActivity
import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.net.Uri
import android.os.Bundle
import android.os.Process
import android.system.Os
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ViewGroup
import android.view.WindowManager
import androidx.activity.ComponentDialog
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.platform.ComposeView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay

/**
 * The game: the framework's NativeActivity runs libps2EntryRunner.so (android.app.lib_name in the manifest). Before
 * it starts, the disc chosen in the launcher is opened here and handed to the runner as /proc/self/fd/<n>
 * (PS2X_GAME_PATH), and the runner's own launcher is off (PS2X_LAUNCHER=0, PS2Recomp patch 0078). The in-game menu is
 * a Compose dialog over the game, opened by the runner (native/ob_ui/android/android_menu.h: showMenu) while it holds
 * the game; nativeMenuClosed tells it how the menu closed. The touch controls (TouchOverlay) are a second window over
 * the game, hidden while the menu is open.
 */
class OutbreakActivity : NativeActivity() {
    private var discFd = -1
    private var menu: ComponentDialog? = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    // The touch controls (TouchOverlay.kt): a window over the game, made once the game's window is attached.
    private var touch: TouchOverlay? = null
    // The memory cards: the game plays on a working copy, copied out to the chosen save folder (SaveCards.kt).
    private var cards: CardSync? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        val settings = Settings(this)
        settings.savedDiscUri()?.let { uri ->
            discFd = openDocument(uri.toString())
            if (discFd >= 0) Os.setenv("PS2X_GAME_PATH", "/proc/self/fd/$discFd", true)
        }
        Os.setenv("PS2X_LAUNCHER", "0", true)
        // A driver folder imported before the importer's naming fix: renamed, and the choice follows (DriverNames.kt).
        settings.get("gpu_driver", "system").let { was ->
            migrateDrivers(settings.oldDriversDir, settings.driversDir, android.os.Build.VERSION.SDK_INT)
            val now = repairDriverFolders(settings.driversDir, was)
            if (now != was) settings.set("gpu_driver", now)
        }
        // Online play: the Network page's choices (NetPlay.kt chosenNetEnv; empty = offline), on the interface that
        // reaches the host (PCSX2's automatic choice can't read Android's routes: reports/2026-10-26-online-play.md
        // entries 53, 63). With online play off, the developer's <external files>/net.env (PS2X_ lines, DevEnv.kt).
        val ui = { key: String -> settings.ui(key) }
        val iface = interfaceFor(joinTarget(ui), localLinks(), localLink(this)?.iface ?: "")
        val dev = getExternalFilesDir(null)?.resolve("net.env")?.takeIf { it.isFile }?.let { parseDevEnv(it.readText()) }
        val debuggable = applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE != 0
        for ((key, value) in gameEnv(chosenNetEnv(ui, iface), dev ?: emptyList(), devAllowed = debuggable))
            Os.setenv(key, value, true)
        // After net.env: the save folder decides where the cards are. The launcher copied them in at Play.
        TreeCardFolder.chosen(this, settings)?.let { tree ->
            Os.setenv("PS2X_SAVES", settings.workCardsDir.path, true)
            cards = CardSync(settings.workCardsDir, TreeCardFolder(this, tree))
        }
        super.onCreate(savedInstanceState)
        window.immersive(underCutout = true)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) window.immersive(underCutout = true) // after the menu, another app or a swipe: bars hidden again
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (touch == null) touch = TouchOverlay(this, scope).also { it.attach() }
    }

    override fun onPause() {
        touch?.releaseAll() // nothing stays pressed while the game is in the background
        cards?.flush() // the player may leave the app here: the save folder gets the latest cards
        super.onPause()
    }

    override fun onDestroy() {
        cards?.stop()
        cards = null
        touch?.destroy()
        touch = null
        scope.cancel()
        super.onDestroy()
    }

    /** Opens a document read-only and hands its descriptor over (kept open for the process); -1 on failure. */
    fun openDocument(uri: String): Int = runCatching {
        contentResolver.openFileDescriptor(Uri.parse(uri), "r")!!.detachFd()
    }.getOrDefault(-1)

    /** Called by the runner (native thread) when the menu should open. */
    fun showMenu() {
        runOnUiThread { if (menu == null) openMenu() }
    }

    private external fun nativeMenuClosed(quit: Boolean, inputChanged: Boolean, settingsChanged: Boolean)

    /**
     * Quit game (developer, 2026-10-27): the game stops completely and the player is back in the launcher. The launcher
     * is started first (it lives in the app's main process), then the runner is told to quit (it ends this ":game"
     * process with _Exit once its loop has stopped); should it not end within [QUIT_GRACE_MS], this process is ended
     * here, so the next Play always starts from a fresh process.
     */
    private fun quitToLauncher(model: SettingsModel) {
        touch?.destroy()
        touch = null
        cards?.flush() // a copy this process can't finish stays marked: the next Play completes it
        startActivity(Intent(this, LauncherActivity::class.java).putExtra(LauncherActivity.EXTRA_FROM_GAME, true)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP))
        nativeMenuClosed(true, model.inputChanged, model.settingsChanged)
        finish()
        Thread {
            Thread.sleep(QUIT_GRACE_MS)
            Process.killProcess(Process.myPid())
        }.apply { isDaemon = true }.start()
    }

    private fun openMenu() {
        val nav = MenuNav()
        val model = SettingsModel(this, Settings(this), inGame = true, nav = nav)
        var closed = false
        lateinit var dialog: ComponentDialog
        touch?.suspend(true)
        // Touch controls > Edit layout: the menu steps aside (the game stays held) until Done.
        model.editTouchLayout = touch?.let { t -> { dialog.hide(); t.startEdit { dialog.show(); dialog.window?.immersive() } } }
        fun close(quit: Boolean) {
            if (closed) return
            closed = true
            dialog.dismiss()
            menu = null
            if (quit) {
                quitToLauncher(model)
                return
            }
            touch?.suspend(false) // reads the touch settings again
            nativeMenuClosed(false, model.inputChanged, model.settingsChanged)
        }
        val footer = { i: Int -> if (i == 0) nav.ask(Confirm("Quit the game?", "Progress since your last save is lost.", "Quit game", "Cancel", { close(true) })) else close(false) }
        val input = GamepadInput(scope) { a ->
            nav.handle(a, model.pages(), footerActions = footer, onBackFromList = { close(false) }, onStart = { close(false) })
        }
        dialog = object : ComponentDialog(this, android.R.style.Theme_Material_NoActionBar_Fullscreen) {
            override fun dispatchKeyEvent(event: KeyEvent): Boolean {
                if (model.capturing && model.onCaptureKey(event)) return true
                return input.onKeyEvent(event) || super.dispatchKeyEvent(event)
            }

            override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
                (!model.capturing && input.onMotionEvent(ev)) || super.dispatchGenericMotionEvent(ev)
        }
        dialog.window?.apply {
            setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS)
        }
        dialog.setCancelable(false)
        dialog.setContentView(ComposeView(this).apply {
            setContent {
                AppTheme {
                    LaunchedEffect(Unit) { while (true) { delay(500); model.tickCapture() } }
                    SettingsShell(
                        title = "Game paused",
                        subtitle = "Sound and timers stopped",
                        pages = model.pages(), nav = nav,
                        footerLabels = listOf("Quit game", "Resume"),
                        footerDim = listOf(false, false),
                        footerActions = footer,
                        status = model.statusLine("Settings save as you change them"),
                        backLabel = "resume", startLabel = "resume",
                        backdrop = androidx.compose.ui.graphics.Color(0xD90F1115), // the paused game shows through, dimmed
                    )
                }
            }
        })
        menu = dialog
        dialog.show()
        dialog.window?.immersive() // a dialog has its own bar state: keep them hidden over the game
    }

    companion object {
        /** How long the runner gets to end this process by itself after Quit game (it _Exits once its loop stops). */
        private const val QUIT_GRACE_MS = 1500L
    }
}
