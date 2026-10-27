package com.outbreakmodern.app

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.os.Process

/**
 * Restarts the app (a new graphics driver loads only in a fresh process: libadrenotools hands PCSX2 its Vulkan
 * library once per process). Runs in its own process (":restart" in the manifest): the app's process ends, this one
 * starts the launcher in a new process, then ends too. The ProcessPhoenix pattern.
 */
class RestartActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Process.killProcess(intent.getIntExtra(EXTRA_PID, -1)) // the app's process, if it is still there
        startActivity(Intent(this, LauncherActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        finish()
        Runtime.getRuntime().exit(0)
    }

    companion object {
        private const val EXTRA_PID = "pid"

        fun restart(from: Activity) {
            from.startActivity(Intent(from, RestartActivity::class.java).putExtra(EXTRA_PID, Process.myPid())
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            from.finishAffinity()
            Runtime.getRuntime().exit(0)
        }

    }
}
