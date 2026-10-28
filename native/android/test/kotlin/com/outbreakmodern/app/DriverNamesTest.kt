package com.outbreakmodern.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

// Driver folder names (DriverNames.kt) against the game's rule (gpu_driver.cpp IsValidDriverName): a folder imported
// before the importer's fix kept a space ("turnip_mrpurple_T30-toasted.adpkg _1_" on the Odin 3, 2026-10-28) and the
// game silently used the system driver. Run by tools/ob/android.sh package (gradlew testDebugUnitTest).
class DriverNamesTest {
    @Test
    fun matchesTheGamesRule() {
        assertTrue(isValidDriverName("turnip_T30-toasted.adpkg_1_"))
        assertFalse(isValidDriverName("turnip T30 (1)"))
        assertFalse(isValidDriverName(".hidden"))
        assertFalse(isValidDriverName("System"))
        assertFalse(isValidDriverName("x".repeat(65)))
    }

    @Test
    fun repairsAFolderAndTheSettingThatNamesIt() {
        val dir = Files.createTempDirectory("drivers").toFile()
        try {
            File(dir, "turnip_mrpurple_T30-toasted.adpkg _1_").mkdirs()
            File(dir, "ok_driver").mkdirs()
            val current = repairDriverFolders(dir, "turnip_mrpurple_T30-toasted.adpkg _1_")
            assertEquals("turnip_mrpurple_T30-toasted.adpkg__1_", current)
            assertEquals(listOf("ok_driver", "turnip_mrpurple_T30-toasted.adpkg__1_"),
                dir.listFiles()!!.map { it.name }.sorted())
        } finally {
            dir.deleteRecursively()
        }
    }

    @Test
    fun leavesTheSettingWhenItNamesAnotherFolder() {
        val dir = Files.createTempDirectory("drivers").toFile()
        try {
            File(dir, "bad name").mkdirs()
            assertEquals("system", repairDriverFolders(dir, "system"))
            assertTrue(File(dir, "bad_name").isDirectory)
        } finally {
            dir.deleteRecursively()
        }
    }
}
