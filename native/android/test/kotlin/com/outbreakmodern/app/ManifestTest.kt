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

    // Local play (docs/plans/2026-10-28-local-play.md): the host runs in a foreground service that only this app can
    // start or stop, and finding the Wi-Fi address needs the network state; no location permission is asked for.
    @Test
    fun hostServiceIsPrivateToTheApp() {
        val service = Regex("<service[^>]*HostService[^>]*>").find(manifest)?.value ?: ""
        assertTrue(service.contains("android:exported=\"false\""))
        assertTrue(service.contains("android:foregroundServiceType=\"specialUse\""))
    }

    @Test
    fun hostingPermissions() {
        for (p in listOf("FOREGROUND_SERVICE", "FOREGROUND_SERVICE_SPECIAL_USE", "ACCESS_NETWORK_STATE", "CHANGE_WIFI_MULTICAST_STATE"))
            assertTrue(p, manifest.contains("android.permission.$p\""))
        assertTrue(!manifest.contains("LOCATION"))
    }
}
