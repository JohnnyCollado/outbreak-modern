package com.outbreakmodern.app

import java.io.File
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * A zip's files into [base] (security queue C6; docs/plans/2026-10-29-security-queue.md). The unpacked total may not
 * pass [budget] bytes: a small zip that unpacks to gigabytes stops with "too large" instead of filling the device.
 * Entries that would land outside [base] (`..`, absolute, drive letters, or anything that resolves elsewhere) and
 * entries [accept] refuses are skipped. [onBytes] gets the running total. Returns the number of files written; the
 * caller removes [base] on a failure.
 */
fun extractZip(
    input: InputStream, base: File, budget: Long, onBytes: (Long) -> Unit = {}, accept: (String) -> Boolean,
): Int {
    val root = base.canonicalFile
    var total = 0L
    var count = 0
    val buffer = ByteArray(64 * 1024)
    ZipInputStream(input.buffered()).use { z ->
        while (true) {
            val e = z.nextEntry ?: break
            val rel = e.name
            if (e.isDirectory || rel.contains("..") || rel.startsWith("/") || rel.startsWith("\\") || rel.contains(':'))
                continue
            if (!accept(rel)) continue
            val out = File(root, rel).canonicalFile
            if (!out.path.startsWith(root.path + File.separator)) continue
            out.parentFile?.mkdirs()
            out.outputStream().use { o ->
                while (true) {
                    val n = z.read(buffer)
                    if (n < 0) break
                    if (total + n > budget) {
                        o.write(buffer, 0, (budget - total).toInt().coerceAtLeast(0))
                        error("That zip is too large: it unpacks to more than ${budget / (1024 * 1024)} MB.")
                    }
                    o.write(buffer, 0, n)
                    total += n
                    onBytes(total)
                }
            }
            count++
        }
    }
    return count
}

/** The unpacking budget: the smaller of the free space and 100 times the zip's size (no real pack compresses more). */
fun zipBudget(zipBytes: Long, freeBytes: Long): Long = minOf(freeBytes, zipBytes * 100)
