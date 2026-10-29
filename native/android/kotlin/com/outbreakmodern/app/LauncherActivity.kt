package com.outbreakmodern.app

import android.Manifest
import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.net.wifi.WifiManager
import android.os.Build
import android.text.InputFilter
import android.widget.EditText
import android.os.Bundle
import android.os.Process
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.LaunchedEffect
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * The launcher (plan decisions A1-A14): the game disc (required, checked by the C++ core), optional texture pack and
 * mods, and the settings pages, before the game. Play starts OutbreakActivity. Controller-first with the step-in
 * navigation (SettingsUi.kt); touch works too.
 */
class LauncherActivity : ComponentActivity(), PickHost {
    private lateinit var settings: Settings
    private lateinit var model: SettingsModel
    private lateinit var input: GamepadInput
    private val nav = MenuNav()
    private var pendingKind = ImportKind.TEXTURES

    private val pickDiscLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let(::onDisc) }
    private val pickTreeLauncher = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri -> uri?.let { startTree(it) } }
    private val pickSaveLauncher = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri -> uri?.let(::onSaveFolder) }
    private val pickZipLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let { startZip(it) } }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.immersive()
        settings = Settings(this)
        model = SettingsModel(this, settings, inGame = false, nav = nav)
        model.host = this
        model.applyDefaultWidescreen() // 16:9 by default until the player picks an aspect ratio
        model.importer = Importer(this, settings, lifecycleScope)
        model.installed = Installed(lifecycleScope) { model.bump() }
        model.restartApp = { RestartActivity.restart(this) }
        model.askText = ::askText
        model.playNow = ::play
        model.onNetModeChanged = ::updateSearch
        input = GamepadInput(lifecycleScope) { onNav(it) }

        lifecycleScope.launch {
            withContext(Dispatchers.IO) { model.refreshDisc() }
            // "Show this screen at start-up" off: straight into the game when the disc checks out (not when the player
            // just quit the game to get here).
            if (settings.ui("launcher", "on") == "off" && blocker() == null && savedInstanceState == null &&
                !intent.getBooleanExtra(EXTRA_FROM_GAME, false)) play()
        }
        setContent {
            AppTheme {
                LaunchedEffect(Unit) { while (true) { delay(500); model.tickCapture() } }
                LaunchedEffect(Unit) { while (true) { delay(1000); model.tickNetwork() } }
                val pages = model.pages()
                SettingsShell(
                    title = "Outbreak Modern",
                    subtitle = "Resident Evil Outbreak File #1 (SLPM-65428)",
                    pages = pages, nav = nav,
                    footerLabels = listOf("Quit", "Play"),
                    footerDim = listOf(false, blocker() != null),
                    footerActions = { if (it == 0) askQuit() else play() },
                    status = model.statusLine(
                        if (!model.disc.verified) (if (model.discName.isEmpty()) "Choose your game disc to play" else "Disc not verified")
                        else if (blocker() != null) "Choose a save folder to play"
                        else "Settings save as you change them"
                    ),
                    backLabel = "quit", startLabel = "play",
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        updateSearch()
    }

    override fun onPause() {
        stopSearch()
        super.onPause()
    }

    // ---- local play: listening for nearby hosts while this screen shows Local play ----
    private var multicast: WifiManager.MulticastLock? = null
    private var askedNotifications = false

    private fun updateSearch() {
        if (settings.ui("net_mode") != "local") return stopSearch()
        if (multicast == null) multicast = runCatching {
            (applicationContext.getSystemService(WIFI_SERVICE) as WifiManager).createMulticastLock("ob-search")
                .apply { setReferenceCounted(false); acquire() }
        }.getOrNull()
        val err = NativeBridge.searchStart()
        if (err.isNotEmpty()) model.note("Can't look for nearby games: $err")
        // The host's Stop button lives in its notification (Android 13+ asks before showing notifications).
        if (Build.VERSION.SDK_INT >= 33 && !askedNotifications) {
            askedNotifications = true // once per run: the request's own dialog pauses and resumes this screen
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
    }

    private fun stopSearch() {
        NativeBridge.searchStop()
        multicast?.let { runCatching { it.release() } }
        multicast = null
    }

    /** A one-line text prompt (addresses, the host name): 40 characters at most. */
    private fun askText(title: String, current: String, onDone: (String) -> Unit) {
        val field = EditText(this).apply {
            setText(current)
            setSingleLine()
            filters = arrayOf(InputFilter.LengthFilter(40))
            setSelection(text.length)
        }
        AlertDialog.Builder(this).setTitle(title).setView(field)
            .setPositiveButton("Save") { _, _ -> onDone(field.text.toString()) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun onNav(a: Nav) {
        nav.handle(a, model.pages(), footerActions = { if (it == 0) askQuit() else play() }, onBackFromList = ::askQuit, onStart = ::play)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) window.immersive() // back from the file picker or another app: hide the bars again
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (model.capturing && model.onCaptureKey(event)) return true
        if (input.onKeyEvent(event)) return true
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
        (!model.capturing && input.onMotionEvent(ev)) || super.dispatchGenericMotionEvent(ev)

    // Quit here closes the whole app (developer, 2026-10-27): the task goes, any game process ends, then this one.
    private fun askQuit() = nav.ask(Confirm("Quit Outbreak Modern?", "Your settings are saved.", "Quit", "Cancel", {
        endGameProcess()
        finishAndRemoveTask()
        window.decorView.postDelayed({ Process.killProcess(Process.myPid()) }, 300)
    }))

    private fun play() {
        if (nav.progress != null) return
        blocker()?.let { why ->
            // The page that fixes it: the disc on Game, the save folder on Memory cards.
            nav.tab = if (!model.disc.verified) 0 else model.pages().indexOfFirst { it.title == "Memory cards" }.coerceAtLeast(0)
            nav.level = Level.LIST
            model.note(why)
            return
        }
        // The working copy made current from the save folder (SaveCards.kt); no game without it.
        val synced = syncCardsIn(settings.workCardsDir, TreeCardFolder(this, TreeCardFolder.chosen(this, settings)!!), settings.legacyCardsDir())
        if (synced.isNotEmpty()) {
            model.note(synced)
            return
        }
        if (settings.ui("card_backup", "on") == "on") {
            val card = File(settings.memcardsDir(), "Mcd001.ps2")
            if (card.isFile) NativeBridge.backupCard(card.path, 5)
        }
        endGameProcess() // a game that did not end cleanly must not be reused: Play always starts a fresh one
        startActivity(Intent(this, OutbreakActivity::class.java))
        finish()
    }

    /** Ends the game's own process (":game", the manifest) if one is still alive; same app, so it may be ended. */
    private fun endGameProcess() {
        val am = getSystemService(ACTIVITY_SERVICE) as android.app.ActivityManager
        am.runningAppProcesses?.filter { it.processName == "$packageName:game" }?.forEach { Process.killProcess(it.pid) }
    }

    /** Why Play can't start yet (the disc, then the save folder), or null. */
    private fun blocker(): String? = playBlocker(model.disc.verified,
        if (model.discName.isEmpty()) "" else model.disc.message, TreeCardFolder.chosen(this, settings) != null)

    // ---- PickHost ----
    override fun pickDisc() = pickDiscLauncher.launch(arrayOf("application/x-iso9660-image", "application/octet-stream", "*/*"))

    override fun pickTree(kind: ImportKind) {
        pendingKind = kind
        pickTreeLauncher.launch(null)
    }

    override fun pickZip(kind: ImportKind) {
        pendingKind = kind
        pickZipLauncher.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
    }

    override fun pickSaveFolder() = pickSaveLauncher.launch(null)

    private fun onSaveFolder(uri: Uri) {
        val kept = runCatching { contentResolver.takePersistableUriPermission(uri, TreeCardFolder.FLAGS) }.isSuccess
        if (!kept) {
            model.note("That folder can't be used for saves. Choose a folder on this device.")
            return
        }
        TreeCardFolder.chosen(this, settings)?.takeIf { it != uri }?.let { old ->
            runCatching { contentResolver.releasePersistableUriPermission(old, TreeCardFolder.FLAGS) }
        }
        settings.saveFolderFile.writeText(uri.toString())
        // The cards there now become the game's; a folder without cards receives the current ones.
        lifecycleScope.launch {
            val err = withContext(Dispatchers.IO) { syncCardsIn(settings.workCardsDir, TreeCardFolder(this@LauncherActivity, uri), settings.legacyCardsDir()) }
            model.note(err.ifEmpty { "Save folder: ${TreeCardFolder.label(uri)}" })
        }
    }

    private fun onDisc(uri: Uri) {
        runCatching { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        lifecycleScope.launch {
            val check = withContext(Dispatchers.IO) { checkDiscUri(this@LauncherActivity, uri) }
            if (check == null) {
                model.note("That file can't be read. If it is a cloud document, copy the disc to this device first.")
                return@launch
            }
            if (check.verified) settings.gameUriFile.writeText(uri.toString())
            model.discName = settings.displayName(this@LauncherActivity, uri)
            model.disc = check
            model.note(if (check.verified) "Disc saved: ${model.discName}" else check.message)
        }
    }

    private fun startTree(uri: Uri) = runImport { onTick, onDone -> model.importer!!.importTree(pendingKind, uri, onTick, onDone) }

    private fun startZip(uri: Uri) = runImport { onTick, onDone -> model.importer!!.importZip(pendingKind, uri, onTick, onDone) }

    private fun runImport(start: (onTick: (Importer.Tick) -> Unit, onDone: (Boolean, String) -> Unit) -> Unit) {
        val label = "Copying ${pendingKind.label}…"
        nav.progress = Progress(label, -1f, "Counting files…") { model.importer?.cancel() }
        start({ t ->
            runOnUiThread {
                val files = if (t.totalFiles > 0) "  -  ${t.doneFiles} of ${t.totalFiles} files" else ""
                nav.progress = nav.progress?.copy(
                    fraction = if (t.totalBytes > 0) (t.doneBytes.toFloat() / t.totalBytes).coerceIn(0f, 1f) else -1f,
                    detail = "${gb(t.doneBytes)} of ${if (t.totalBytes > 0) gb(t.totalBytes) else "?"}$files",
                )
            }
        }, { _, message ->
            runOnUiThread {
                nav.progress = null
                model.installed?.invalidate()
                model.note(message)
            }
        })
    }

    companion object {
        /** Set when Quit game brings the player back here (OutbreakActivity.quitToLauncher): no auto-start. */
        const val EXTRA_FROM_GAME = "fromGame"
    }
}
