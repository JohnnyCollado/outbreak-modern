package com.outbreakmodern.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

// The save folder the player must choose before Play (SaveCards.kt): the game plays on a working copy in the app's
// internal storage, copied in from the chosen folder at Play and back out while it runs (developer, 2026-10-28).
// Run by tools/ob/android.sh package (gradlew testDebugUnitTest).
class SaveCardsTest {
    /** The chosen folder as a plain directory; [broken] = the folder can't be reached (permission gone). */
    private class DirFolder(val dir: File, var broken: Boolean = false) : CardFolder {
        override fun has(name: String) = !broken && File(dir, name).isFile
        override fun copyIn(name: String, to: File): Boolean {
            if (broken) return false
            File(dir, name).copyTo(to, overwrite = true)
            return true
        }
        override fun copyOut(from: File, name: String): Boolean {
            if (broken) return false
            from.copyTo(File(dir, name), overwrite = true)
            return true
        }
    }

    private fun tmp(): File = Files.createTempDirectory("savecards").toFile()
    private fun File.card(name: String = "Mcd001.ps2") = File(this, name)

    @Test
    fun theFoldersCardReplacesTheWorkingCopy() {
        val work = tmp(); val folder = tmp()
        work.card().writeText("old")
        folder.card().writeText("folder")
        assertEquals("", syncCardsIn(work, DirFolder(folder), null))
        assertEquals("folder", work.card().readText())
    }

    @Test
    fun anUnsyncedWorkingCopyIsWrittenOutNotReplaced() {
        val work = tmp(); val folder = tmp()
        work.card().writeText("played")
        markCardsUnsynced(work)
        folder.card().writeText("older")
        assertEquals("", syncCardsIn(work, DirFolder(folder), null))
        assertEquals("played", work.card().readText())
        assertEquals("played", folder.card().readText())
        assertFalse(cardsUnsynced(work))
    }

    @Test
    fun aNewFolderReceivesTheExistingWorkingCard() {
        val work = tmp(); val folder = tmp()
        work.card().writeText("mine")
        assertEquals("", syncCardsIn(work, DirFolder(folder), null))
        assertEquals("mine", folder.card().readText())
    }

    @Test
    fun theOldDefaultFoldersCardMovesIn() {
        val work = tmp(); val folder = tmp(); val legacy = tmp()
        legacy.card().writeText("legacy")
        legacy.card("Mcd002.ps2").writeText("legacy2")
        assertEquals("", syncCardsIn(work, DirFolder(folder), legacy))
        assertEquals("legacy", work.card().readText())
        assertEquals("legacy", folder.card().readText())
        assertEquals("legacy2", folder.card("Mcd002.ps2").readText())
        assertTrue(legacy.card().isFile) // the old card stays where it was
    }

    @Test
    fun nothingAnywhereCreatesNothing() {
        val work = tmp(); val folder = tmp()
        assertEquals("", syncCardsIn(work, DirFolder(folder), tmp()))
        assertFalse(work.card().exists())
        assertFalse(folder.card().exists())
    }

    @Test
    fun anUnreachableFolderStopsPlayAndKeepsTheWorkingCopy() {
        val work = tmp(); val folder = tmp()
        work.card().writeText("played")
        markCardsUnsynced(work)
        assertTrue(syncCardsIn(work, DirFolder(folder, broken = true), null).isNotEmpty())
        assertEquals("played", work.card().readText())
        assertTrue(cardsUnsynced(work))
    }

    @Test
    fun syncOutWritesEveryCardAndClearsTheMark() {
        val work = tmp(); val folder = tmp()
        work.card().writeText("a")
        work.card("Mcd002.ps2").writeText("b")
        markCardsUnsynced(work)
        assertEquals("", syncCardsOut(work, DirFolder(folder)))
        assertEquals("a", folder.card().readText())
        assertEquals("b", folder.card("Mcd002.ps2").readText())
        assertFalse(cardsUnsynced(work))
    }

    @Test
    fun aFailedSyncOutStaysMarked() {
        val work = tmp(); val folder = tmp()
        work.card().writeText("a")
        markCardsUnsynced(work)
        assertTrue(syncCardsOut(work, DirFolder(folder, broken = true)).isNotEmpty())
        assertTrue(cardsUnsynced(work))
    }

    @Test
    fun aChangeDuringTheCopyStaysMarked() {
        val work = tmp(); val folder = tmp()
        work.card().writeText("a")
        markCardsUnsynced(work)
        val changing = object : CardFolder by DirFolder(folder) {
            override fun copyOut(from: File, name: String): Boolean {
                from.copyTo(File(folder, name), overwrite = true)
                markCardsUnsynced(work) // the game saved again meanwhile
                return true
            }
        }
        assertEquals("", syncCardsOut(work, changing))
        assertTrue(cardsUnsynced(work))
    }

    @Test
    fun playNeedsTheDiscThenTheSaveFolder() {
        assertEquals("Choose your game disc first", playBlocker(discVerified = false, discMessage = "", hasSaveFolder = true))
        assertEquals("Bad checksum", playBlocker(discVerified = false, discMessage = "Bad checksum", hasSaveFolder = false))
        assertEquals("Choose a save folder for your memory cards first", playBlocker(true, "", hasSaveFolder = false))
        assertNull(playBlocker(true, "", hasSaveFolder = true))
    }
}
