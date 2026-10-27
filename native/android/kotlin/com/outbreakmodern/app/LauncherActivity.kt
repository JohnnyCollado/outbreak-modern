package com.outbreakmodern.app

import android.content.Intent
import android.net.Uri
import android.os.Bundle
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
    private val pickZipLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let { startZip(it) } }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = Settings(this)
        model = SettingsModel(this, settings, inGame = false, nav = nav)
        model.host = this
        model.applyDefaultWidescreen() // 16:9 by default until the player picks an aspect ratio
        model.importer = Importer(this, settings, lifecycleScope)
        model.installed = Installed(lifecycleScope) { model.bump() }
        model.restartApp = { RestartActivity.restart(this) }
        input = GamepadInput(lifecycleScope) { onNav(it) }

        lifecycleScope.launch {
            withContext(Dispatchers.IO) { model.refreshDisc() }
            // "Show this screen at start-up" off: straight into the game when the disc checks out.
            if (settings.ui("launcher", "on") == "off" && model.disc.verified && savedInstanceState == null) play()
        }
        setContent {
            AppTheme {
                LaunchedEffect(Unit) { while (true) { delay(500); model.tickCapture() } }
                val pages = model.pages()
                SettingsShell(
                    title = "Outbreak Modern",
                    subtitle = "Resident Evil Outbreak File #1 (SLPM-65428)",
                    pages = pages, nav = nav,
                    footerLabels = listOf("Quit", "Play"),
                    footerDim = listOf(false, !model.disc.verified),
                    footerActions = { if (it == 0) askQuit() else play() },
                    status = model.statusLine(
                        if (!model.disc.verified) (if (model.discName.isEmpty()) "Choose your game disc to play" else "Disc not verified")
                        else "Settings save as you change them"
                    ),
                    backLabel = "quit", startLabel = "play",
                )
            }
        }
    }

    private fun onNav(a: Nav) {
        nav.handle(a, model.pages(), footerActions = { if (it == 0) askQuit() else play() }, onBackFromList = ::askQuit, onStart = ::play)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (model.capturing && model.onCaptureKey(event)) return true
        if (input.onKeyEvent(event)) return true
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
        (!model.capturing && input.onMotionEvent(ev)) || super.dispatchGenericMotionEvent(ev)

    private fun askQuit() = nav.ask(Confirm("Quit Outbreak Modern?", "Your settings are saved.", "Quit", "Cancel", { finishAndRemoveTask() }))

    private fun play() {
        if (nav.progress != null) return
        if (!model.disc.verified) {
            nav.tab = 0
            nav.level = Level.LIST
            model.note(if (model.discName.isEmpty()) "Choose your game disc first" else model.disc.message)
            return
        }
        if (settings.ui("card_backup", "on") == "on") {
            val card = File(settings.memcardsDir(), "Mcd001.ps2")
            if (card.isFile) NativeBridge.backupCard(card.path, 5)
        }
        startActivity(Intent(this, OutbreakActivity::class.java))
        finish()
    }

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
}
