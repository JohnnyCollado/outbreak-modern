package com.outbreakmodern.app

import android.content.Context
import android.net.Uri
import android.os.StatFs
import android.provider.DocumentsContract
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.InputStream
import java.util.zip.ZipInputStream
import kotlinx.coroutines.currentCoroutineContext

/** What an import is for (plan decision A13). */
enum class ImportKind(val label: String) { TEXTURES("texture pack"), MODS("mods"), DRIVER("graphics driver") }

/**
 * Texture packs, mods and graphics drivers into the app's files dir, from a folder (Storage Access Framework tree,
 * walked with DocumentsContract) or a .zip, on a coroutine with progress and cancel. Everything lands in a staging
 * folder first and moves into place only when complete, so a cancelled or failed import never leaves half a pack, and
 * the previous pack goes only after the new one is in.
 */
class Importer(private val context: Context, private val settings: Settings, private val scope: CoroutineScope) {
    private var job: Job? = null
    private val buffer = ByteArray(1 shl 20)

    data class Tick(val doneBytes: Long, val totalBytes: Long, val doneFiles: Int, val totalFiles: Int)

    fun cancel() {
        job?.cancel()
    }

    /** What an import of [kind] would replace ("" = nothing). */
    fun existing(kind: ImportKind): String {
        val f = settings.filesDir
        return when (kind) {
            ImportKind.TEXTURES -> listOfNotNull(
                File(f, "textures/${settings.serial}").takeIf { it.isDirectory }?.let { "the ${settings.serial} folder" },
                File(f, "textures/${settings.serial}.zip").takeIf { it.isFile }?.let { "${settings.serial}.zip" },
            ).joinToString(" and ")
            ImportKind.MODS -> File(f, "mods/${settings.serial}").listFiles()?.count {
                it.name.endsWith(".pnach") && !NativeBridge.isWidescreenModFile(it.absolutePath) }
                ?.takeIf { it > 0 }?.let { "$it .pnach file(s)" } ?: ""
            ImportKind.DRIVER -> ""
        }
    }

    fun importTree(kind: ImportKind, tree: Uri, onTick: (Tick) -> Unit, onDone: (Boolean, String) -> Unit) =
        run(onDone) {
            val root = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
            val files = mutableListOf<Pair<Uri, String>>() // document, relative path
            var total = 0L
            walk(tree, DocumentsContract.getTreeDocumentId(tree), "") { uri, rel, size ->
                if (kind != ImportKind.MODS || rel.lowercase().endsWith(".pnach")) {
                    files += uri to rel
                    total += size
                }
            }
            if (files.isEmpty()) error(if (kind == ImportKind.MODS) "No .pnach files in that folder." else "That folder is empty.")
            needSpace(total)
            val name = displayName(root) ?: "import"
            val staging = freshStaging(kind)
            val base = File(staging, safe(name))
            var done = 0L
            files.forEachIndexed { i, (uri, rel) ->
                currentCoroutineContext().ensureActive()
                val out = File(base, rel)
                out.parentFile?.mkdirs()
                context.contentResolver.openInputStream(uri)?.use { input ->
                    out.outputStream().use { o -> done = copy(input, o, done) { onTick(Tick(it, total, i, files.size)) } }
                } ?: error("Could not read $rel")
                onTick(Tick(done, total, i + 1, files.size))
            }
            place(kind, staging, base, name)
        }

    fun importZip(kind: ImportKind, zip: Uri, onTick: (Tick) -> Unit, onDone: (Boolean, String) -> Unit) =
        run(onDone) {
            val size = context.contentResolver.openAssetFileDescriptor(zip, "r")?.use { it.length } ?: -1L
            val name = displayName(zip)?.removeSuffix(".zip")?.removeSuffix(".ZIP") ?: "import"
            if (kind == ImportKind.TEXTURES) {
                // PCSX2 reads <root>/<serial>.zip as a pack: copied in as is.
                needSpace(size)
                val staging = freshStaging(kind)
                val out = File(staging, "${settings.serial}.zip")
                context.contentResolver.openInputStream(zip)?.use { input ->
                    out.outputStream().use { o -> copy(input, o, 0) { onTick(Tick(it, size, 0, 1)) } }
                } ?: error("Could not read the zip")
                val dest = File(settings.filesDir, "textures").apply { mkdirs() }
                replaceTextures(dest) { if (!out.renameTo(File(dest, "${settings.serial}.zip"))) error("Could not move the pack into place") }
                staging.deleteRecursively()
                settings.set("textures", "textures")
                return@run "Texture pack imported"
            }
            needSpace(size * 2)
            val staging = freshStaging(kind)
            val base = File(staging, safe(name))
            var done = 0L
            var count = 0
            context.contentResolver.openInputStream(zip)?.use { raw ->
                ZipInputStream(raw.buffered()).use { z ->
                    while (true) {
                        currentCoroutineContext().ensureActive()
                        val e = z.nextEntry ?: break
                        val rel = e.name
                        if (e.isDirectory || rel.contains("..") || rel.startsWith("/") || rel.contains(':')) continue
                        if (kind == ImportKind.MODS && !rel.lowercase().endsWith(".pnach")) continue
                        val out = File(base, rel)
                        out.parentFile?.mkdirs()
                        out.outputStream().use { o -> done = copy(z, o, done) { onTick(Tick(it, size, count, 0)) } }
                        count++
                    }
                }
            } ?: error("Could not read the zip")
            if (count == 0) error(if (kind == ImportKind.MODS) "No .pnach files in that zip." else "That zip is empty.")
            place(kind, staging, base, name)
        }

    // ---- internals ----

    private fun run(onDone: (Boolean, String) -> Unit, body: suspend CoroutineScope.() -> String) {
        job?.cancel()
        job = scope.launch {
            val result = runCatching { withContext(Dispatchers.IO) { body() } }
            val staging = File(settings.filesDir, "import-tmp")
            if (result.isFailure) withContext(Dispatchers.IO) { staging.deleteRecursively() }
            val e = result.exceptionOrNull()
            when {
                e is kotlinx.coroutines.CancellationException -> onDone(false, "Import cancelled")
                e != null -> onDone(false, e.message ?: "Import failed")
                else -> onDone(true, result.getOrThrow())
            }
        }
    }

    private suspend fun walk(tree: Uri, docId: String, prefix: String, onFile: (Uri, String, Long) -> Unit) {
        currentCoroutineContext().ensureActive()
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId)
        val cols = arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE)
        val dirs = mutableListOf<Pair<String, String>>()
        context.contentResolver.query(children, cols, null, null, null)?.use { c ->
            while (c.moveToNext()) {
                val id = c.getString(0)
                val name = safe(c.getString(1) ?: continue)
                if (c.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) dirs += id to "$prefix$name/"
                else onFile(DocumentsContract.buildDocumentUriUsingTree(tree, id), "$prefix$name", c.getLong(3))
            }
        }
        for ((id, p) in dirs) walk(tree, id, p, onFile)
    }

    private suspend fun copy(input: InputStream, out: java.io.OutputStream, start: Long, onBytes: (Long) -> Unit): Long {
        var done = start
        var lastTick = 0L
        while (true) {
            currentCoroutineContext().ensureActive()
            val n = input.read(buffer)
            if (n < 0) break
            out.write(buffer, 0, n)
            done += n
            val now = System.nanoTime()
            if (now - lastTick > 100_000_000L) { // ~10 updates a second
                lastTick = now
                onBytes(done)
            }
        }
        return done
    }

    private fun needSpace(bytes: Long) {
        if (bytes <= 0) return
        val free = StatFs(settings.filesDir.path).availableBytes
        if (free < bytes + (64L shl 20))
            error("Not enough space: needs ${gb(bytes)}, ${gb(free)} free.")
    }

    private fun freshStaging(kind: ImportKind): File =
        File(settings.filesDir, "import-tmp/${kind.name.lowercase()}").apply { deleteRecursively(); mkdirs() }

    /** Moves a finished copy into place; returns the status note. */
    private fun place(kind: ImportKind, staging: File, base: File, name: String): String {
        val files = settings.filesDir
        try {
            when (kind) {
                ImportKind.TEXTURES -> {
                    // The pack: a SLPM-65428 folder at depth <= 2, else a replacements folder (wrapped), else the folder.
                    val pack = findDir(base, settings.serial, 2)
                        ?: findDir(base, "replacements", 2)?.let { r ->
                            File(staging, "wrap/${settings.serial}").also { it.mkdirs(); r.renameTo(File(it, "replacements")) }
                        }
                        ?: base
                    val dest = File(files, "textures").apply { mkdirs() }
                    replaceTextures(dest) { if (!pack.renameTo(File(dest, settings.serial))) error("Could not move the pack into place") }
                    settings.set("textures", "textures")
                    return "Texture pack imported"
                }
                ImportKind.MODS -> {
                    val dest = File(files, "mods/${settings.serial}").apply { mkdirs() }
                    var n = 0
                    base.walkTopDown().filter { it.isFile && it.name.lowercase().endsWith(".pnach") }.forEach {
                        it.copyTo(File(dest, it.name), overwrite = true)
                        n++
                    }
                    settings.set("mods", "mods")
                    return "$n mod file(s) imported"
                }
                ImportKind.DRIVER -> {
                    val meta = base.walkTopDown().maxDepth(2).firstOrNull { it.name == "meta.json" }
                        ?: error("No meta.json in that driver package.")
                    val folder = meta.parentFile!!
                    // Internal storage only (security queue C2): the staging folder is external, so copy across.
                    val dest = File(settings.driversDir, safe(name))
                    dest.deleteRecursively()
                    dest.parentFile?.mkdirs()
                    if (!folder.renameTo(dest) && !folder.copyRecursively(dest, overwrite = true))
                        error("Could not move the driver into place")
                    return "Driver added: choose it in the list, then restart the app"
                }
            }
        } finally {
            staging.deleteRecursively()
        }
    }

    // The new pack is in place before the old one goes: the old one moves aside, then is deleted.
    private fun replaceTextures(dest: File, moveIn: () -> Unit) {
        val old = listOf(File(dest, settings.serial), File(dest, "${settings.serial}.zip")).filter { it.exists() }
        val aside = File(dest, ".replaced-${System.currentTimeMillis()}").apply { mkdirs() }
        old.forEach { it.renameTo(File(aside, it.name)) }
        try {
            moveIn()
        } catch (e: Exception) {
            aside.listFiles()?.forEach { it.renameTo(File(dest, it.name)) } // put the old pack back
            aside.deleteRecursively()
            throw e
        }
        aside.deleteRecursively()
    }

    private fun findDir(root: File, name: String, depth: Int): File? =
        root.walkTopDown().maxDepth(depth).firstOrNull { it.isDirectory && it.name.equals(name, ignoreCase = true) }

    private fun displayName(uri: Uri): String? = runCatching {
        context.contentResolver.query(uri, arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null)
            ?.use { if (it.moveToFirst()) it.getString(0) else null }
    }.getOrNull()

    // Letters, digits, ".", "_", "-" only: the game rejects any other driver name (gpu_driver.cpp IsValidDriverName),
    // e.g. "turnip T30 (1)" was silently replaced by the system driver (2026-10-26). DriverNames.kt holds the rule.
    private fun safe(name: String) = safeDriverName(name)
}

fun gb(bytes: Long): String = when {
    bytes >= 1L shl 30 -> String.format("%.1f GB", bytes / (1L shl 30).toDouble())
    bytes >= 1L shl 20 -> String.format("%.0f MB", bytes / (1L shl 20).toDouble())
    bytes >= 1024 -> String.format("%.0f KB", bytes / 1024.0)
    else -> "$bytes bytes"
}
