package com.outbreakmodern.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

// Zip import (ZipExtract.kt; security queue C6): a byte budget stops a small zip that unpacks to gigabytes before it
// fills the device, and entries that would land outside the target folder are skipped.
class ZipExtractTest {
    private fun zip(vararg entries: Pair<String, ByteArray>): ByteArray {
        val bytes = ByteArrayOutputStream()
        ZipOutputStream(bytes).use { z ->
            for ((name, data) in entries) {
                z.putNextEntry(ZipEntry(name))
                z.write(data)
                z.closeEntry()
            }
        }
        return bytes.toByteArray()
    }

    private fun <T> inTemp(body: (File) -> T): T {
        val dir = Files.createTempDirectory("zip").toFile()
        try {
            return body(dir)
        } finally {
            dir.deleteRecursively()
        }
    }

    @Test
    fun extractsANormalZip() = inTemp { dir ->
        val data = zip("a.pnach" to "patch=1".toByteArray(), "sub/b.txt" to ByteArray(300) { 7 })
        val count = extractZip(ByteArrayInputStream(data), dir, budget = 1_000_000) { true }
        assertEquals(2, count)
        assertEquals("patch=1", File(dir, "a.pnach").readText())
        assertEquals(300L, File(dir, "sub/b.txt").length())
    }

    @Test
    fun stopsAZipBombAtTheBudget() = inTemp { dir ->
        val data = zip("zeros.bin" to ByteArray(10_000_000)) // 10 MB of zeros: a few KB compressed
        assertTrue(data.size < 100_000)
        try {
            extractZip(ByteArrayInputStream(data), dir, budget = 1_000_000) { true }
            fail("a zip unpacking past its budget must stop")
        } catch (e: IllegalStateException) {
            assertTrue(e.message!!.contains("too large"))
        }
        assertTrue(File(dir, "zeros.bin").length() <= 1_000_000)
    }

    @Test
    fun skipsEntriesOutsideTheFolder() = inTemp { dir ->
        val inside = File(dir, "inside").apply { mkdirs() }
        val data = zip("../evil.txt" to "x".toByteArray(), "/abs.txt" to "x".toByteArray(),
            "c:evil.txt" to "x".toByteArray(), "ok.txt" to "y".toByteArray())
        assertEquals(1, extractZip(ByteArrayInputStream(data), inside, budget = 1_000_000) { true })
        assertFalse(File(dir, "evil.txt").exists())
        assertTrue(File(inside, "ok.txt").isFile)
    }

    @Test
    fun keepsOnlyAcceptedEntries() = inTemp { dir ->
        val data = zip("mod.pnach" to "p".toByteArray(), "readme.txt" to "r".toByteArray())
        assertEquals(1, extractZip(ByteArrayInputStream(data), dir, budget = 1_000_000) { it.endsWith(".pnach") })
        assertFalse(File(dir, "readme.txt").exists())
    }

    // Security queue 2, F7: empty entries cost something, or a small zip could create millions of files.
    @Test
    fun manyEmptyFilesUseUpTheBudget() = inTemp { dir ->
        val entries = Array(2000) { "e$it.txt" to ByteArray(0) }
        try {
            extractZip(ByteArrayInputStream(zip(*entries)), dir, budget = 100_000) { true }
            fail("a zip of many empty files must stop")
        } catch (e: IllegalStateException) {
            assertTrue(e.message!!.contains("too large"))
        }
        assertTrue(dir.listFiles()!!.size < 100)
    }

    // F2: a driver is native code; its import is staged in internal storage, others stay beside their destination.
    @Test
    fun driversAreStagedInternally() {
        val int = File("internal")
        val ext = File("external")
        assertEquals(File(int, "import-tmp/driver"), stagingDir(ImportKind.DRIVER, int, ext))
        assertEquals(File(ext, "import-tmp/mods"), stagingDir(ImportKind.MODS, int, ext))
    }

    @Test
    fun theBudgetIsTheSmallerOfFreeSpaceAndAHundredTimesTheZip() {
        assertEquals(100_000L, zipBudget(zipBytes = 1_000, freeBytes = 10_000_000))
        assertEquals(5_000L, zipBudget(zipBytes = 1_000, freeBytes = 5_000))
    }
}
