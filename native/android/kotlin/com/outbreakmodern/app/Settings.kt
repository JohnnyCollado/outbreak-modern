package com.outbreakmodern.app

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File

/**
 * The settings the runner reads, in the app's external files dir: outbreak.ini (the runner's keys at the top, the
 * menu's own under [ui], which the runner skips: PS2Recomp patch 0079) and input.ini (inline comments). Same files
 * and keys as the Windows menu (native/ob_ui/ui/settings_ui.cpp); edited through the C++ ini editor (NativeBridge).
 */
class Settings(context: Context) {
    val filesDir: File = context.getExternalFilesDir(null) ?: context.filesDir
    val outbreakIni = File(filesDir, "outbreak.ini").path
    val inputIni = File(filesDir, "input.ini").path
    val gameUriFile = File(filesDir, "game-uri.txt")
    /** The save folder the player chose (a document tree, SaveCards.kt). */
    val saveFolderFile = File(filesDir, "save-folder-uri.txt")
    /** The game's working copy of the cards, in internal storage (PS2X_SAVES; copied to and from the save folder). */
    val workCardsDir = File(context.filesDir, "memcards")
    /** Graphics drivers (native code): internal storage only (security queue C2; gpu_driver_android.cpp loads here). */
    val driversDir = File(context.filesDir, "drivers")
    /** Where drivers were imported before (the external folder); moved inside on Android 11+ (migrateDrivers). */
    val oldDriversDir = File(filesDir, "drivers")
    val serial = "SLPM-65428"

    fun get(key: String, def: String = ""): String =
        NativeBridge.iniGet(outbreakIni, "", key, false)?.takeIf { it.isNotEmpty() } ?: def

    fun set(key: String, value: String) {
        NativeBridge.iniSet(outbreakIni, "", key, value, false)
    }

    fun ui(key: String, def: String = ""): String =
        NativeBridge.iniGet(outbreakIni, "ui", key, false)?.takeIf { it.isNotEmpty() } ?: def

    fun setUi(key: String, value: String) {
        NativeBridge.iniSet(outbreakIni, "ui", key, value, false)
    }

    fun bind(gamepad: Boolean, key: String, def: String): String =
        NativeBridge.iniGet(inputIni, if (gamepad) "gamepad" else "keyboard", key, true)?.takeIf { it.isNotEmpty() } ?: def

    fun setBind(gamepad: Boolean, key: String, value: String) {
        NativeBridge.iniSet(inputIni, if (gamepad) "gamepad" else "keyboard", key, value, true)
    }

    fun removeBind(gamepad: Boolean, key: String) {
        NativeBridge.iniRemove(inputIni, if (gamepad) "gamepad" else "keyboard", key, true)
    }

    /** A settings path as the runner reads it: relative to the settings folder. */
    fun resolve(value: String): File? = when {
        value.isEmpty() -> null
        value.startsWith("/") -> File(value)
        else -> File(filesDir, value)
    }

    fun memcardsDir(): File = workCardsDir

    /** Where the cards lived before the save folder: the "saves" setting, else memcards in the external files dir. */
    fun legacyCardsDir(): File = resolve(get("saves")) ?: File(filesDir, "memcards")

    // ---- the game disc: a document picked earlier (permission kept), else the one .iso in the files dir ----
    fun savedDiscUri(): Uri? = runCatching { gameUriFile.readText().trim() }.getOrNull()
        ?.takeIf { it.isNotEmpty() }?.let(Uri::parse)

    fun singleIso(): File? = filesDir.listFiles { f -> f.isFile && f.name.lowercase().endsWith(".iso") }
        ?.singleOrNull()

    fun displayName(context: Context, uri: Uri): String = runCatching {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    }.getOrNull() ?: uri.lastPathSegment ?: uri.toString()
}

/** The result of the C++ disc check (NativeBridge.checkDisc). */
data class DiscCheck(val verified: Boolean, val steps: Int, val message: String, val crc: String) {
    companion object {
        val NONE = DiscCheck(false, 0, "Choose your game disc to play.", "")

        fun from(a: Array<String>) = DiscCheck(a[0] == "0", a[1].toIntOrNull() ?: 0, a[2], a.getOrElse(4) { "" })
    }
}

/** Opens [uri] read-only and runs the disc check on its descriptor (/proc/self/fd/<fd>); null when it can't open. */
fun checkDiscUri(context: Context, uri: Uri): DiscCheck? = runCatching {
    context.contentResolver.openFileDescriptor(uri, "r")?.use { pfd ->
        DiscCheck.from(NativeBridge.checkDisc("/proc/self/fd/${pfd.fd}"))
    }
}.getOrNull()
