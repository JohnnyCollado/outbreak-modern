package com.outbreakmodern.app

import org.junit.Assert.assertEquals
import org.junit.Test

// The developer's net.env (DevEnv.kt): PS2X_ settings for the game on Android until the Network page exists
// (reports/2026-10-26-online-play.md entry 53). Run by tools/ob/android.sh package (gradlew testDebugUnitTest).
class DevEnvTest {
    @Test
    fun readsPs2xLines() {
        val text = "# online test\nPS2X_DEV9=pcsx2\n\n  PS2X_NET_HOSTS = www01.kddi-mmbb.jp=192.168.0.76 \r\n"
        assertEquals(
            listOf("PS2X_DEV9" to "pcsx2", "PS2X_NET_HOSTS" to "www01.kddi-mmbb.jp=192.168.0.76"),
            parseDevEnv(text),
        )
    }

    @Test
    fun keepsEqualsSignsInTheValue() {
        assertEquals(listOf("PS2X_NET_REMAP" to "192.0.2.2=127.0.0.1"), parseDevEnv("PS2X_NET_REMAP=192.0.2.2=127.0.0.1"))
    }

    @Test
    fun ignoresOtherNamesAndBrokenLines() {
        assertEquals(emptyList<Pair<String, String>>(), parseDevEnv("LD_PRELOAD=/x.so\nPATH=/bin\nPS2X_DEV9\n=pcsx2\n"))
    }
}
