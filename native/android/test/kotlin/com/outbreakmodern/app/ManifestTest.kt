package com.outbreakmodern.app

import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

// Online play opens real sockets (PCSX2's DEV9 Sockets adapter): without the INTERNET permission Android refuses every
// socket() with EPERM, "DEV9: TCP: Failed to open socket. Error: 1" (reports/2026-10-26-online-play.md entry 58).
// Gradle runs unit tests in the module folder (native/android).
class ManifestTest {
    private val manifest = File("AndroidManifest.xml").readText()

    @Test
    fun mayOpenNetworkSockets() {
        assertTrue(manifest.contains("android.permission.INTERNET"))
    }
}
