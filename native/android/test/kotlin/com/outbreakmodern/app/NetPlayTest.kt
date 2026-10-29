package com.outbreakmodern.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

// The Network page's rules (NetPlay.kt; docs/plans/2026-10-28-local-play.md): what the game is told for local play and
// for a fan server, and the nearby-host rows from the native search. Everything that ends up in the game's environment
// is checked strictly first: a ';' or '=' smuggled into a value would add a server name or a remap of its own.
class NetPlayTest {
    // ---- addresses ----
    @Test
    fun ipv4IsAStrictDottedQuad() {
        assertTrue(isIpv4("192.168.0.76"))
        assertTrue(isIpv4("0.0.0.0"))
        for (bad in listOf("", "192.168.0", "192.168.0.256", "192.168.0.1.5", "1..2.3", "a.b.c.d", " 1.2.3.4", "1.2.3.4;",
            "0192.168.0.1", "1.2.3.4=5.6.7.8", "::1"))
            assertFalse(bad, isIpv4(bad))
    }

    @Test
    fun localMeansPrivateOrLinkLocal() {
        for (ok in listOf("10.0.0.2", "172.16.0.1", "172.31.255.254", "192.168.1.20", "169.254.3.4"))
            assertTrue(ok, isLocalIpv4(ok))
        for (bad in listOf("172.15.0.1", "172.32.0.1", "8.8.8.8", "100.64.0.1", "224.0.0.1", "255.255.255.255", "127.0.0.1", "x"))
            assertFalse(bad, isLocalIpv4(bad))
    }

    @Test
    fun broadcastFromAddressAndPrefix() {
        assertEquals("192.168.0.255", broadcastAddress("192.168.0.76", 24))
        assertEquals("10.255.255.255", broadcastAddress("10.1.2.3", 8))
        assertEquals("172.16.3.255", broadcastAddress("172.16.2.9", 23))
        assertNull(broadcastAddress("192.168.0.76", 0))
        assertNull(broadcastAddress("192.168.0.76", 33))
        assertNull(broadcastAddress("nope", 24))
    }

    // ---- nearby hosts (NativeBridge.searchHosts rows: "address\tname\twebPort\tlobbyPort\tplayers") ----
    @Test
    fun readsAHostRow() {
        assertEquals(FoundHost("192.168.0.50", "Odin", 18443, 8300, 2), parseFoundHost("192.168.0.50\tOdin\t18443\t8300\t2"))
    }

    @Test
    fun refusesBadHostRows() {
        for (bad in listOf(
            "8.8.8.8\tOdin\t443\t8300\t1",          // not on the local network
            "192.168.0.50\tOdin\t0\t8300\t1",       // port out of range
            "192.168.0.50\tOdin\t70000\t8300\t1",
            "192.168.0.50\tOdin\t443\t8300",         // missing field
            "192.168.0.50\tOdin\t443\t8300\t1\tx",   // extra field
            "192.168.0.50\tOdin\tabc\t8300\t1",
            "192.168.0.50\tOdin\t443\t8300\t-1",
        )) assertNull(bad, parseFoundHost(bad))
    }

    @Test
    fun hostNamesShowPrintableTextOnly() {
        assertEquals("Odin", parseFoundHost("192.168.0.50\tOd\u0007in‮\t443\t8300\t1")?.name)
        assertEquals("Host", parseFoundHost("192.168.0.50\t\u0001\t443\t8300\t1")?.name)
    }

    // ---- the game's network settings ----
    @Test
    fun netcnfDhcpAndManual() {
        assertEquals("dhcp", netcnfValue(NetConnection.DHCP))
        assertEquals("static:192.168.0.80,255.255.255.0,192.168.0.1,192.168.0.1",
            netcnfValue(NetConnection(false, "192.168.0.80", "255.255.255.0", "192.168.0.1", "192.168.0.1", "")))
        assertEquals("static:192.168.0.80,255.255.255.0,192.168.0.1,1.1.1.1,8.8.8.8",
            netcnfValue(NetConnection(false, "192.168.0.80", "255.255.255.0", "192.168.0.1", "1.1.1.1", "8.8.8.8")))
    }

    @Test
    fun manualSettingsMustAllBeAddresses() {
        assertNull(netcnfValue(NetConnection(false, "", "255.255.255.0", "192.168.0.1", "192.168.0.1", "")))
        assertNull(netcnfValue(NetConnection(false, "192.168.0.80,1.2.3.4", "255.255.255.0", "192.168.0.1", "192.168.0.1", "")))
        assertNull(netcnfValue(NetConnection(false, "192.168.0.80", "255.255.255.0", "192.168.0.1", "192.168.0.1", "x")))
    }

    @Test
    fun joiningALocalHostOnAHighPortRemapsTheLogin() {
        assertEquals(
            listOf(
                "PS2X_DEV9" to "pcsx2",
                "PS2X_NET_IF" to "wlan0",
                "PS2X_NET_HOSTS" to "www01.kddi-mmbb.jp=192.168.0.50;gate1.jp.dnas.playstation.org=192.168.0.50",
                "PS2X_NET_REMAP" to "192.168.0.50:443=192.168.0.50:18443",
                "PS2X_NETCNF" to "dhcp",
            ),
            gameNetEnv("192.168.0.50", 18443, "wlan0", NetConnection.DHCP),
        )
    }

    @Test
    fun aServerOn443NeedsNoRemap() {
        val env = gameNetEnv("203.0.113.9", 443, "wlan0", NetConnection.DHCP)!!
        assertFalse(env.any { it.first == "PS2X_NET_REMAP" })
        assertEquals("www01.kddi-mmbb.jp=203.0.113.9;gate1.jp.dnas.playstation.org=203.0.113.9",
            env.first { it.first == "PS2X_NET_HOSTS" }.second)
    }

    @Test
    fun noInterfaceMeansAutomatic() {
        assertFalse(gameNetEnv("192.168.0.50", 443, "", NetConnection.DHCP)!!.any { it.first == "PS2X_NET_IF" })
    }

    @Test
    fun refusesAnythingThatCouldInjectSettings() {
        assertNull(gameNetEnv("192.168.0.50;evil=1.2.3.4", 443, "wlan0", NetConnection.DHCP))
        assertNull(gameNetEnv("example.com", 443, "wlan0", NetConnection.DHCP))
        assertNull(gameNetEnv("192.168.0.50", 0, "wlan0", NetConnection.DHCP))
        assertNull(gameNetEnv("192.168.0.50", 65536, "wlan0", NetConnection.DHCP))
        assertNull(gameNetEnv("192.168.0.50", 443, "wlan0;x", NetConnection.DHCP))
        assertNull(gameNetEnv("192.168.0.50", 443, "a-very-long-interface-name", NetConnection.DHCP))
        assertNull(gameNetEnv("192.168.0.50", 443, "wlan0", NetConnection(false, "1.2.3", "", "", "", "")))
    }

    // ---- this device's own host name ----
    @Test
    fun hostNameIsShortAndPrintable() {
        assertEquals("Johnny's Odin", cleanHostName("  Johnny's Odin  "))
        assertEquals("Host", cleanHostName("\u0000\u0007"))
        assertEquals(24, cleanHostName("x".repeat(40)).length)
    }

    // ---- the saved choices (outbreak.ini [ui]) → the game's environment at start ----
    private fun ui(vararg kv: Pair<String, String>): (String) -> String = { key -> kv.toMap()[key] ?: "" }

    @Test
    fun offOrUnsetTellsTheGameNothing() {
        assertEquals(emptyList<Pair<String, String>>(), chosenNetEnv(ui(), "wlan0"))
        assertEquals(emptyList<Pair<String, String>>(), chosenNetEnv(ui("net_mode" to "off", "net_server" to "203.0.113.9"), "wlan0"))
    }

    @Test
    fun localPlayJoinsTheChosenHost() {
        assertEquals(gameNetEnv("192.168.0.50", 18443, "wlan0", NetConnection.DHCP),
            chosenNetEnv(ui("net_mode" to "local", "net_join" to "192.168.0.50:18443"), "wlan0"))
    }

    @Test
    fun localPlayOnlyJoinsLocalAddresses() {
        assertEquals(emptyList<Pair<String, String>>(), chosenNetEnv(ui("net_mode" to "local", "net_join" to "203.0.113.9:443"), "wlan0"))
        assertEquals(emptyList<Pair<String, String>>(), chosenNetEnv(ui("net_mode" to "local", "net_join" to "192.168.0.50"), "wlan0"))
        assertEquals(emptyList<Pair<String, String>>(), chosenNetEnv(ui("net_mode" to "local"), "wlan0"))
    }

    // A fan server is given by its DNS address, as PCSX2 players set DNS1 (obsrv: 208.72.237.31 answers the login
    // name with .15 and DNAS with another server; report entry 65). The game asks that DNS for its names itself.
    @Test
    fun aFanServerIsTheGamesDnsServer() {
        assertEquals(
            listOf("PS2X_DEV9" to "pcsx2", "PS2X_NET_IF" to "wlan0", "PS2X_NET_DNS" to "208.72.237.31", "PS2X_NETCNF" to "dhcp"),
            chosenNetEnv(ui("net_mode" to "server", "net_server" to "208.72.237.31"), "wlan0"))
        assertEquals(emptyList<Pair<String, String>>(), chosenNetEnv(ui("net_mode" to "server", "net_server" to "bad;x"), "wlan0"))
    }

    @Test
    fun manualConnectionIsPassedOnWithTheFanDns() {
        val env = chosenNetEnv(ui("net_mode" to "server", "net_server" to "208.72.237.31", "net_connection" to "manual",
            "net_address" to "192.168.0.80", "net_netmask" to "255.255.255.0", "net_gateway" to "192.168.0.1",
            "net_dns1" to "192.168.0.1"), "wlan0")
        assertEquals("static:192.168.0.80,255.255.255.0,192.168.0.1,208.72.237.31,192.168.0.1",
            env.first { it.first == "PS2X_NETCNF" }.second)
    }

    @Test
    fun manualSettingsForLocalPlayKeepTheirOwnDns() {
        val env = chosenNetEnv(ui("net_mode" to "local", "net_join" to "192.168.0.50:443", "net_connection" to "manual",
            "net_address" to "192.168.0.80", "net_netmask" to "255.255.255.0", "net_gateway" to "192.168.0.1",
            "net_dns1" to "192.168.0.1"), "wlan0")
        assertEquals("static:192.168.0.80,255.255.255.0,192.168.0.1,192.168.0.1", env.first { it.first == "PS2X_NETCNF" }.second)
    }

    @Test
    fun brokenManualSettingsMeanNoOnlinePlay() {
        assertEquals(emptyList<Pair<String, String>>(), chosenNetEnv(ui("net_mode" to "server", "net_server" to "203.0.113.9",
            "net_connection" to "manual", "net_address" to "192.168.0.80"), "wlan0"))
    }

    // ---- which interface reaches the host (reports/2026-10-26-online-play.md entry 63: a phone hosting a hotspot has
    // its own Wi-Fi on wlan0 and the hotspot on wlan2; the Odin joined the hotspot) ----
    private val oneplus = listOf(LocalLink("wlan0", "192.168.11.164", 20), LocalLink("wlan2", "10.98.58.71", 24))

    @Test
    fun theInterfaceOnTheHostsSubnetIsUsed() {
        assertEquals("wlan2", interfaceFor("10.98.58.174", oneplus, "wlan0"))
        assertEquals("wlan0", interfaceFor("192.168.8.20", oneplus, "wlan0"))
    }

    @Test
    fun noSubnetMatchKeepsTheDefault() {
        assertEquals("wlan0", interfaceFor("203.0.113.9", oneplus, "wlan0"))
        assertEquals("", interfaceFor("10.98.58.174", emptyList(), ""))
        assertEquals("wlan0", interfaceFor("bad", oneplus, "wlan0"))
    }

    @Test
    fun theJoinTargetIsTheHostAddress() {
        assertEquals("10.98.58.174", joinTarget(ui("net_mode" to "local", "net_join" to "10.98.58.174:18443")))
        assertEquals("203.0.113.9", joinTarget(ui("net_mode" to "server", "net_server" to "203.0.113.9")))
        assertEquals("", joinTarget(ui("net_mode" to "off")))
    }

    // ---- net.env: the developer's file applies only while the Network page has online play off ----
    @Test
    fun thePageWinsOverNetEnv() {
        val page = listOf("PS2X_DEV9" to "pcsx2", "PS2X_NET_HOSTS" to "www01.kddi-mmbb.jp=10.98.58.174")
        val dev = listOf("PS2X_NET_HOSTS" to "www01.kddi-mmbb.jp=192.168.0.76", "PS2X_SAVES" to "/x")
        assertEquals(page + ("PS2X_SAVES" to "/x"), gameEnv(page, dev))
        assertEquals(dev, gameEnv(emptyList(), dev))
    }

    @Test
    fun netEnvMayStillTurnOnTheTraceButNotRedirect() {
        val page = listOf("PS2X_DEV9" to "pcsx2", "PS2X_NET_HOSTS" to "www01.kddi-mmbb.jp=10.98.58.174")
        val dev = listOf("PS2X_NET_TRACE" to "1", "PS2X_NET_REMAP" to "10.98.58.174:443=1.2.3.4:443",
            "PS2X_NET_IF" to "rmnet0", "PS2X_NETCNF" to "dhcp", "PS2X_DEV9" to "off")
        assertEquals(page + ("PS2X_NET_TRACE" to "1"), gameEnv(page, dev))
    }
}
