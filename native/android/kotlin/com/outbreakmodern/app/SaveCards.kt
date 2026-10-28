package com.outbreakmodern.app

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.FileObserver
import android.os.Handler
import android.os.HandlerThread
import android.provider.DocumentsContract
import java.io.File

/**
 * The save folder (developer, 2026-10-28): the player chooses a folder for the memory cards before the game can start.
 * The game opens its cards by path (native/pcsx2_sio2), and a folder picked through Android's picker has no path, so
 * the game plays on a working copy in the app's internal storage (PS2X_SAVES, OutbreakActivity). The cards are copied
 * in from the chosen folder at Play and back out while the game runs (on a change, on pause, at quit). A working copy
 * changed since its last copy-out is marked unsynced; such a copy wins over the folder's at the next Play, so a game
 * that was killed before it copied out loses nothing.
 */
interface CardFolder {
    fun has(name: String): Boolean
    /** The folder's card copied over [to]; false when it can't be read. */
    fun copyIn(name: String, to: File): Boolean
    /** [from] copied over the folder's card (made if missing); false when it can't be written. */
    fun copyOut(from: File, name: String): Boolean
}

private const val UNSYNCED = ".unsynced"
private val markLock = Any()
private var changes = 0L // marks made in this process: a copy-out clears the mark only if none came during it

fun markCardsUnsynced(work: File) = synchronized(markLock) {
    changes++
    runCatching { work.mkdirs(); File(work, UNSYNCED).createNewFile() }
    Unit
}

fun cardsUnsynced(work: File) = File(work, UNSYNCED).exists()

/**
 * Before the game starts: the working copy made current from the chosen folder ([legacy] = the cards' old default
 * folder, moved in when neither side has a card yet). "" when done, else the reason (the game must not start).
 */
fun syncCardsIn(work: File, folder: CardFolder, legacy: File?): String {
    work.mkdirs()
    val unsynced = cardsUnsynced(work)
    for (name in obCardNames) {
        val card = File(work, name)
        val ok = when {
            unsynced && card.isFile -> folder.copyOut(card, name)
            folder.has(name) -> {
                // Into a side file first: a failed read leaves the working copy as it was.
                val part = File(work, "$name.part")
                val copied = folder.copyIn(name, part) && (!card.exists() || card.delete()) && part.renameTo(card)
                part.delete()
                copied
            }
            card.isFile -> folder.copyOut(card, name)
            legacy != null && File(legacy, name).isFile -> runCatching { File(legacy, name).copyTo(card, overwrite = true) }.isSuccess &&
                folder.copyOut(card, name)
            else -> true
        }
        if (!ok) return "The save folder can't be reached. Choose it again on the Memory cards page."
    }
    File(work, UNSYNCED).delete()
    return ""
}

/** While the game runs: every working card copied out. "" when done, else the reason (the copy stays unsynced). */
fun syncCardsOut(work: File, folder: CardFolder): String {
    // The mark stays through the copy: a copy cut short (the process ended) leaves it, and the next Play copies out again.
    val before = synchronized(markLock) { changes }
    for (name in obCardNames) {
        val card = File(work, name)
        if (card.isFile && !folder.copyOut(card, name)) return "The save folder can't be written"
    }
    synchronized(markLock) { if (changes == before) File(work, UNSYNCED).delete() }
    return ""
}

/** Why Play can't start yet, or null: the disc first, then the save folder. */
fun playBlocker(discVerified: Boolean, discMessage: String, hasSaveFolder: Boolean): String? = when {
    !discVerified -> discMessage.ifEmpty { "Choose your game disc first" }
    !hasSaveFolder -> "Choose a save folder for your memory cards first"
    else -> null
}

val obCardNames = listOf("Mcd001.ps2", "Mcd002.ps2")

/**
 * In the game's process: the working cards watched; each write marks them unsynced, and they are copied out once the
 * game has stopped writing for [QUIET_MS] (a save writes many pages in a burst). [flush] copies out now (pause, quit).
 */
class CardSync(private val work: File, private val folder: CardFolder) {
    private val thread = HandlerThread("CardSync").apply { start() }
    private val handler = Handler(thread.looper)
    private val copy = Runnable { syncCardsOut(work, folder) }
    private val observer = object : FileObserver(work, MODIFY or CLOSE_WRITE) {
        override fun onEvent(event: Int, path: String?) {
            if (path !in obCardNames) return
            markCardsUnsynced(work)
            handler.removeCallbacks(copy)
            handler.postDelayed(copy, QUIET_MS)
        }
    }

    init {
        work.mkdirs()
        observer.startWatching()
    }

    fun flush() {
        if (!cardsUnsynced(work)) return
        handler.removeCallbacks(copy)
        handler.post(copy)
    }

    fun stop() {
        observer.stopWatching()
        flush()
        thread.quitSafely()
    }

    companion object {
        const val QUIET_MS = 2000L
    }
}

/** The chosen folder through Android's document access (a tree from the folder picker, kept with read+write). */
class TreeCardFolder(private val context: Context, private val tree: Uri) : CardFolder {
    private val resolver get() = context.contentResolver

    private fun find(name: String): Uri? = runCatching {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        resolver.query(children, arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME),
            null, null, null)?.use { c ->
            while (c.moveToNext()) if (c.getString(1) == name) return@runCatching DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0))
            null
        }
    }.getOrNull()

    override fun has(name: String) = find(name) != null

    override fun copyIn(name: String, to: File): Boolean = runCatching {
        val uri = find(name) ?: return false
        resolver.openInputStream(uri)!!.use { input -> to.outputStream().use { input.copyTo(it) } }
        true
    }.getOrDefault(false)

    override fun copyOut(from: File, name: String): Boolean = runCatching {
        val uri = find(name) ?: DocumentsContract.createDocument(resolver,
            DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree)), "application/octet-stream", name)!!
        resolver.openOutputStream(uri, "wt")!!.use { out -> from.inputStream().use { it.copyTo(out) } }
        true
    }.getOrDefault(false)

    companion object {
        const val FLAGS = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION

        /** The chosen folder, when one is set and its read+write permission is still held. */
        fun chosen(context: Context, settings: Settings): Uri? {
            val uri = runCatching { settings.saveFolderFile.readText().trim() }.getOrNull()?.takeIf { it.isNotEmpty() }?.let(Uri::parse)
                ?: return null
            return uri.takeIf { context.contentResolver.persistedUriPermissions.any { p -> p.uri == it && p.isReadPermission && p.isWritePermission } }
        }

        /** "Internal storage › Outbreak/Saves"-style name for the menu. */
        fun label(uri: Uri): String = runCatching {
            val id = DocumentsContract.getTreeDocumentId(uri)
            val (vol, path) = id.substringBefore(':') to id.substringAfter(':', "")
            (if (vol == "primary") "Internal storage" else vol) + if (path.isEmpty()) "" else " › $path"
        }.getOrDefault(uri.toString())
    }
}
