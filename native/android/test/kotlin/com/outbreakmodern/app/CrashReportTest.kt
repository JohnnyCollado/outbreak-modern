package com.outbreakmodern.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.zip.ZipInputStream

// The Android crash report (report entry 71): what the app gathered, in one zip; empty parts are left out.
class CrashReportTest {
    private fun names(bytes: ByteArray): Map<String, String> {
        val found = LinkedHashMap<String, String>()
        ZipInputStream(ByteArrayInputStream(bytes)).use { z ->
            while (true) {
                val e = z.nextEntry ?: break
                found[e.name] = z.readBytes().decodeToString()
            }
        }
        return found
    }

    @Test
    fun partsGoInTheZipAndEmptyOnesDoNot() {
        val out = ByteArrayOutputStream()
        val written = CrashReport.write(out, listOf("logcat.txt" to "line".encodeToByteArray(), "empty.txt" to ByteArray(0)))
        val zip = names(out.toByteArray())
        assertEquals(1, written)
        assertEquals("line", zip["logcat.txt"])
        assertFalse(zip.containsKey("empty.txt"))
    }

    @Test
    fun nothingGatheredWritesNoEntries() {
        val out = ByteArrayOutputStream()
        assertEquals(0, CrashReport.write(out, listOf("empty.txt" to ByteArray(0))))
        assertTrue(names(out.toByteArray()).isEmpty())
    }

    @Test
    fun aPartIsCutToItsLimit() {
        val out = ByteArrayOutputStream()
        CrashReport.write(out, listOf("big.txt" to ByteArray(CrashReport.MAX_PART + 5) { 65 }))
        assertEquals(CrashReport.MAX_PART, names(out.toByteArray())["big.txt"]?.length)
    }
}
