package com.outbreakmodern.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Build
import android.os.IBinder
import java.net.Inet4Address
import java.net.NetworkInterface

/**
 * The built-in host (docs/plans/2026-10-28-local-play.md): ob_server and its beacon (NativeBridge.hostStart) in the
 * launcher's process, kept alive by this foreground service while the game runs in ":game". Started only by the
 * player's Host button; never restarted by the system (START_NOT_STICKY): hosting is off unless the player turned it
 * on. The notification's Stop ends it; so does quitting the app. Not exported (the manifest).
 */
class HostService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action != ACTION_START) {
            stopHosting()
            return START_NOT_STICKY
        }
        val name = cleanHostName(intent.getStringExtra(EXTRA_NAME) ?: "")
        val address = intent.getStringExtra(EXTRA_ADDRESS) ?: ""
        val broadcast = intent.getStringExtra(EXTRA_BROADCAST) ?: ""
        val prefix = intent.getIntExtra(EXTRA_PREFIX, 0)
        // Foreground first (required within seconds of the start), then the server; on failure the service ends.
        val type = if (Build.VERSION.SDK_INT >= 34) ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE else 0
        startForeground(NOTIFICATION_ID, notification(name, address), type)
        val err = if (isLocalIpv4(address)) NativeBridge.hostStart(name, address, prefix, WEB_PORT, broadcast)
        else "no Wi-Fi address"
        lastError = err
        if (err.isNotEmpty()) stopHosting()
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        NativeBridge.hostStop()
        super.onDestroy()
    }

    private fun stopHosting() {
        NativeBridge.hostStop()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun notification(name: String, address: String): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Hosting", NotificationManager.IMPORTANCE_LOW))
        val stop = PendingIntent.getService(this, 0, Intent(this, HostService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentTitle("Hosting \"$name\"")
            .setContentText("Players on this Wi-Fi can join ($address)")
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null, "Stop hosting", stop).build())
            .build()
    }

    companion object {
        /** The login port: Android apps cannot listen below 1024, so joiners remap 443 to it (gameNetEnv). */
        const val WEB_PORT = 18443
        private const val ACTION_START = "com.outbreakmodern.app.HOST_START"
        private const val ACTION_STOP = "com.outbreakmodern.app.HOST_STOP"
        private const val EXTRA_NAME = "name"
        private const val EXTRA_ADDRESS = "address"
        private const val EXTRA_BROADCAST = "broadcast"
        private const val EXTRA_PREFIX = "prefix"
        private const val CHANNEL = "host"
        private const val NOTIFICATION_ID = 18443
        /** Why the last start failed ("" = it did not). */
        @Volatile var lastError = ""

        fun start(context: Context, name: String, link: LocalLink) {
            lastError = ""
            context.startForegroundService(Intent(context, HostService::class.java).setAction(ACTION_START)
                .putExtra(EXTRA_NAME, name).putExtra(EXTRA_ADDRESS, link.address)
                .putExtra(EXTRA_BROADCAST, broadcastAddress(link.address, link.prefix) ?: "")
                .putExtra(EXTRA_PREFIX, link.prefix))
        }

        fun stop(context: Context) {
            context.startService(Intent(context, HostService::class.java).setAction(ACTION_STOP))
        }
    }
}

/**
 * The Wi-Fi (or Ethernet) link with a local IPv4 address: the connected network first, then this device's own hotspot
 * (which ConnectivityManager does not list). Mobile data interfaces are never used, even with a private address.
 */
fun localLink(context: Context): LocalLink? {
    val cm = context.getSystemService(ConnectivityManager::class.java)
    @Suppress("DEPRECATION")
    for (n in listOfNotNull(cm.activeNetwork) + cm.allNetworks) {
        val caps = cm.getNetworkCapabilities(n) ?: continue
        if (!caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) && !caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) continue
        val lp = cm.getLinkProperties(n) ?: continue
        val la = lp.linkAddresses.firstOrNull { it.address is Inet4Address && isLocalIpv4(it.address.hostAddress ?: "") } ?: continue
        return LocalLink(lp.interfaceName ?: "", la.address.hostAddress!!, la.prefixLength)
    }
    return runCatching {
        NetworkInterface.getNetworkInterfaces().toList()
            .filter { it.isUp && !it.isLoopback && HOTSPOT_NAMES.any { p -> it.name.startsWith(p) } }
            .firstNotNullOfOrNull { ni ->
                ni.interfaceAddresses.firstOrNull { it.address is Inet4Address && isLocalIpv4(it.address.hostAddress ?: "") }
                    ?.let { LocalLink(ni.name, it.address.hostAddress!!, it.networkPrefixLength.toInt()) }
            }
    }.getOrNull()
}

private val HOTSPOT_NAMES = listOf("wlan", "swlan", "ap", "softap", "eth")

/** Every interface that is up with a local IPv4 address (the Wi-Fi, this device's own hotspot, Ethernet). */
fun localLinks(): List<LocalLink> = runCatching {
    NetworkInterface.getNetworkInterfaces().toList().filter { it.isUp && !it.isLoopback }.flatMap { ni ->
        ni.interfaceAddresses.filter { it.address is Inet4Address && isLocalIpv4(it.address.hostAddress ?: "") }
            .map { LocalLink(ni.name, it.address.hostAddress!!, it.networkPrefixLength.toInt()) }
    }
}.getOrDefault(emptyList())
