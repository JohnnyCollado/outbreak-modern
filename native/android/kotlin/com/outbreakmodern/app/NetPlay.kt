package com.outbreakmodern.app

/**
 * The Network page's rules (docs/plans/2026-10-28-local-play.md), Android-free so the JVM tests reach them
 * (NetPlayTest.kt). What the game is told comes from here only, and every value is checked strictly first: the game's
 * settings are ';'- and '='-separated lists (PS2X_NET_HOSTS, PS2X_NET_REMAP), so an unchecked value could add entries
 * of its own.
 */

/** The game's two server names (login and DNAS check), both sent to the chosen server. */
val GAME_SERVER_NAMES = listOf("www01.kddi-mmbb.jp", "gate1.jp.dnas.playstation.org")

/** Strict dotted quad: four parts 0-255, digits only, no leading zeros. */
fun isIpv4(s: String): Boolean = ipv4Bytes(s) != null

private fun ipv4Bytes(s: String): IntArray? {
    val parts = s.split('.')
    if (parts.size != 4) return null
    return IntArray(4) { i ->
        val p = parts[i]
        if (p.isEmpty() || p.length > 3 || !p.all { it in '0'..'9' } || (p.length > 1 && p[0] == '0')) return null
        p.toInt().takeIf { it <= 255 } ?: return null
    }
}

/** On the local network: 10/8, 172.16/12, 192.168/16 or 169.254/16 (not loopback: another device is never 127.x). */
fun isLocalIpv4(s: String): Boolean {
    val b = ipv4Bytes(s) ?: return false
    return b[0] == 10 || (b[0] == 172 && b[1] in 16..31) || (b[0] == 192 && b[1] == 168) || (b[0] == 169 && b[1] == 254)
}

/** The subnet's broadcast address, where the host's beacon goes; null for a bad address or prefix. */
fun broadcastAddress(address: String, prefix: Int): String? {
    val b = ipv4Bytes(address) ?: return null
    if (prefix !in 1..32) return null
    val ip = b.fold(0L) { acc, x -> (acc shl 8) or x.toLong() }
    val mask = (0xFFFFFFFFL shl (32 - prefix)) and 0xFFFFFFFFL
    val bc = (ip and mask) or (mask.inv() and 0xFFFFFFFFL)
    return (3 downTo 0).joinToString(".") { ((bc shr (it * 8)) and 0xFF).toString() }
}

/** As the server's cleanHostName (native/ob_server/src/beacon.cpp): printable ASCII, trimmed, 24 at most, "Host" if empty. */
fun cleanHostName(name: String): String {
    val printable = name.map { if (it == '\t') ' ' else it }.filter { it in ' '..'~' }.joinToString("").trim()
    return if (printable.isEmpty()) "Host" else printable.take(24).trimEnd()
}

/** A game heard nearby (NativeBridge.searchHosts). */
data class FoundHost(val address: String, val name: String, val webPort: Int, val lobbyPort: Int, val players: Int)

/** One "address\tname\twebPort\tlobbyPort\tplayers" row; null unless every field is sound and the address is local. */
fun parseFoundHost(row: String): FoundHost? {
    val f = row.split('\t')
    if (f.size != 5 || !isLocalIpv4(f[0])) return null
    val web = port(f[2]) ?: return null
    val lobby = port(f[3]) ?: return null
    val players = f[4].takeIf { it.length in 1..3 && it.all { c -> c in '0'..'9' } }?.toInt() ?: return null
    return FoundHost(f[0], cleanHostName(f[1]), web, lobby, players)
}

private fun port(s: String): Int? =
    s.takeIf { it.length in 1..5 && it.all { c -> c in '0'..'9' } }?.toInt()?.takeIf { it in 1..65535 }

/** The game's own network settings (what its Network Settings screen would hold): DHCP, or typed in by the player. */
data class NetConnection(
    val dhcp: Boolean, val address: String, val netmask: String, val gateway: String, val dns1: String, val dns2: String,
) {
    companion object {
        val DHCP = NetConnection(true, "", "", "", "", "")
    }
}

/** PS2X_NETCNF (PS2Recomp patch 0097): "dhcp" or "static:address,netmask,gateway,dns1[,dns2]"; null if a field is bad. */
fun netcnfValue(c: NetConnection): String? {
    if (c.dhcp) return "dhcp"
    val required = listOf(c.address, c.netmask, c.gateway, c.dns1)
    if (!required.all(::isIpv4) || (c.dns2.isNotEmpty() && !isIpv4(c.dns2))) return null
    return "static:" + (required + listOfNotNull(c.dns2.takeIf { it.isNotEmpty() })).joinToString(",")
}

private val INTERFACE_NAME = Regex("[A-Za-z0-9_.]{1,15}")

/**
 * The game's environment for playing on [server] (an IPv4 address; a local host or a fan server), whose login answers
 * on [webPort]: the game's server names → [server], the login's port 443 → [webPort] when it differs (an Android host
 * cannot listen below 1024), the network [iface] ("" = automatic) and the game's settings. Null if anything is unsound.
 */
fun gameNetEnv(server: String, webPort: Int, iface: String, connection: NetConnection): List<Pair<String, String>>? {
    if (!isIpv4(server) || webPort !in 1..65535) return null
    if (iface.isNotEmpty() && !INTERFACE_NAME.matches(iface)) return null
    val netcnf = netcnfValue(connection) ?: return null
    val env = mutableListOf("PS2X_DEV9" to "pcsx2")
    if (iface.isNotEmpty()) env += "PS2X_NET_IF" to iface
    env += "PS2X_NET_HOSTS" to GAME_SERVER_NAMES.joinToString(";") { "$it=$server" }
    if (webPort != 443) env += "PS2X_NET_REMAP" to "$server:443=$server:$webPort"
    env += "PS2X_NETCNF" to netcnf
    return env
}

/**
 * The game's environment from the Network page's saved choices ([ui] reads outbreak.ini's [ui] keys, "" when unset):
 * net_mode off | local (net_join = "address:webPort" of a host on the local network) | server (net_server = a fan
 * server's IPv4, login on 443); net_connection dhcp | manual (net_address, net_netmask, net_gateway, net_dns1,
 * net_dns2). Empty when online play is off or anything saved is unsound: the game then plays offline.
 */
fun chosenNetEnv(ui: (String) -> String, iface: String): List<Pair<String, String>> {
    val connection = if (ui("net_connection") == "manual")
        NetConnection(false, ui("net_address"), ui("net_netmask"), ui("net_gateway"), ui("net_dns1"), ui("net_dns2"))
    else NetConnection.DHCP
    val env = when (ui("net_mode")) {
        "local" -> {
            val join = ui("net_join")
            val colon = join.lastIndexOf(':')
            if (colon <= 0) null else {
                val address = join.substring(0, colon)
                val web = port(join.substring(colon + 1))
                if (!isLocalIpv4(address) || web == null) null else gameNetEnv(address, web, iface, connection)
            }
        }
        "server" -> gameNetEnv(ui("net_server"), 443, iface, connection)
        else -> null
    }
    return env ?: emptyList()
}

/** This device on the local network: the interface, its IPv4 address and prefix length. */
data class LocalLink(val iface: String, val address: String, val prefix: Int)

private fun ipv4Value(s: String): Long? = ipv4Bytes(s)?.fold(0L) { acc, x -> (acc shl 8) or x.toLong() }

/**
 * The interface whose subnet holds [target] (the host or server), else [default]. A phone sharing a hotspot has two
 * Wi-Fi interfaces (its own Wi-Fi on wlan0, the hotspot on wlan2): a guest on the hotspot is reached on the hotspot's
 * (reports/2026-10-26-online-play.md entry 63).
 */
fun interfaceFor(target: String, links: List<LocalLink>, default: String): String {
    val t = ipv4Value(target) ?: return default
    return links.firstOrNull { link ->
        val a = ipv4Value(link.address) ?: return@firstOrNull false
        if (link.prefix !in 1..32) return@firstOrNull false
        val mask = (0xFFFFFFFFL shl (32 - link.prefix)) and 0xFFFFFFFFL
        (a and mask) == (t and mask)
    }?.iface ?: default
}

/** The address the game will connect to from the saved choices ("" when online play is off). */
fun joinTarget(ui: (String) -> String): String = when (ui("net_mode")) {
    "local" -> ui("net_join").substringBeforeLast(':', "")
    "server" -> ui("net_server")
    else -> ""
}

/** The settings that decide where the game connects: with online play on, only the Network page sets them. */
private val NETWORK_KEYS = setOf("PS2X_DEV9", "PS2X_NET_IF", "PS2X_NET_HOSTS", "PS2X_NET_REMAP", "PS2X_NETCNF")

/**
 * The game's environment: with online play on, the Network page's settings plus the developer's net.env lines that do
 * not decide where the game connects (PS2X_NET_TRACE, entry 64); with it off, net.env as written. A leftover net.env
 * from testing pointed both phones at the PC while the page said to join the Odin (entry 63).
 */
fun gameEnv(page: List<Pair<String, String>>, dev: List<Pair<String, String>>): List<Pair<String, String>> =
    if (page.isEmpty()) dev else page + dev.filter { it.first !in NETWORK_KEYS }
