package com.outbreakmodern.app

import android.os.Handler
import android.os.Looper
import android.os.StatFs
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.concurrent.ConcurrentHashMap

/**
 * What is installed in the app's storage and how much space it takes (developer request 2026-10-26): sizes are
 * measured on a background coroutine (a 3 GB texture folder has thousands of files), cached per path until
 * [invalidate] (after an import or a delete), and [onChange] asks the UI to read them again.
 */
class Installed(private val scope: CoroutineScope, private val onChange: () -> Unit) {
    data class Size(val bytes: Long, val files: Int)

    private val sizes = ConcurrentHashMap<String, Size>()
    private val measuring = ConcurrentHashMap.newKeySet<String>()
    private val main = Handler(Looper.getMainLooper())

    /** The size of [f] (a file or a whole folder), or null while it is being measured. */
    fun size(f: File): Size? {
        sizes[f.path]?.let { return it }
        if (measuring.add(f.path))
            scope.launch(Dispatchers.IO) {
                val s = measure(f)
                sizes[f.path] = s
                measuring.remove(f.path)
                main.post(onChange)
            }
        return null
    }

    /** "3.2 GB, 5,678 files" / "Measuring…". */
    fun describe(f: File, withFiles: Boolean = true): String = size(f)?.let {
        if (withFiles && f.isDirectory) "${gb(it.bytes)}, ${"%,d".format(it.files)} files" else gb(it.bytes)
    } ?: "Measuring…"

    fun bytesOrNull(f: File): Long? = size(f)?.bytes

    fun invalidate() {
        sizes.clear()
        main.post(onChange)
    }

    /** Deletes [targets] (files or folders) on a background coroutine, then calls [done] on the main thread. */
    fun delete(targets: List<File>, done: (Boolean) -> Unit) {
        scope.launch {
            val ok = withContext(Dispatchers.IO) { targets.all { !it.exists() || it.deleteRecursively() } }
            invalidate()
            done(ok)
        }
    }

    companion object {
        fun measure(f: File): Size {
            if (f.isFile) return Size(f.length(), 1)
            var bytes = 0L
            var files = 0
            f.walkTopDown().forEach { if (it.isFile) { bytes += it.length(); files++ } }
            return Size(bytes, files)
        }

        fun freeBytes(dir: File): Long = runCatching { StatFs(dir.path).availableBytes }.getOrDefault(0L)
    }
}
