package com.outbreakmodern.app

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import java.io.File
import java.io.OutputStream
import java.util.concurrent.TimeUnit
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * The Save crash report row (report entry 71): this app's own log lines and Android's record of how its last runs
 * ended, in one zip that the player chooses where to put. Nothing is uploaded. Android's log is a ring: on a chatty
 * phone the oldest lines of a crash may be gone, while the exit records stay.
 */
object CrashReport {
    const val MAX_PART = 16 * 1024 * 1024

    /** Writes the non-empty parts, each cut to [MAX_PART]. Returns how many were written. */
    fun write(out: OutputStream, parts: List<Pair<String, ByteArray>>): Int {
        var written = 0
        ZipOutputStream(out).use { zip ->
            for ((name, data) in parts) {
                if (data.isEmpty()) continue
                zip.putNextEntry(ZipEntry(name))
                zip.write(data, 0, minOf(data.size, MAX_PART))
                zip.closeEntry()
                written++
            }
        }
        return written
    }

    fun gather(context: Context): List<Pair<String, ByteArray>> {
        val parts = ArrayList<Pair<String, ByteArray>>()
        parts += "device.txt" to "${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})\n".encodeToByteArray()
        parts += "logcat.txt" to logcat(context)
        File(context.filesDir, "gpu-last-run.txt").takeIf { it.isFile }?.let { parts += it.name to it.readBytes() }
        parts += exits(context)
        return parts
    }

    /** An app may read its own lines of the log without any permission; the output goes to a file so the pipe can't block. */
    private fun logcat(context: Context): ByteArray {
        val file = File(context.cacheDir, "crash-logcat.txt")
        return runCatching {
            val process = ProcessBuilder("logcat", "-d", "-v", "threadtime")
                .redirectErrorStream(true).redirectOutput(file).start()
            if (!process.waitFor(20, TimeUnit.SECONDS)) process.destroy()
            file.readBytes()
        }.getOrDefault(ByteArray(0)).also { file.delete() }
    }

    private fun exits(context: Context): List<Pair<String, ByteArray>> {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return emptyList()
        val manager = context.getSystemService(ActivityManager::class.java) ?: return emptyList()
        val found = runCatching { manager.getHistoricalProcessExitReasons(null, 0, 5) }.getOrDefault(emptyList())
        val parts = ArrayList<Pair<String, ByteArray>>()
        val summary = StringBuilder()
        found.forEachIndexed { i, e ->
            summary.append("${e.timestamp} reason=${e.reason} status=${e.status} ${e.description.orEmpty()}\n")
            // A native crash's record (a tombstone) is readable from Android 12.
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && e.reason == ApplicationExitInfo.REASON_CRASH_NATIVE) {
                runCatching { e.traceInputStream?.use { it.readBytes() } }.getOrNull()?.let { parts += "tombstone-$i.pb" to it }
            }
        }
        parts += "exits.txt" to summary.toString().encodeToByteArray()
        return parts
    }
}
