package com.outbreakmodern.app

/**
 * The developer's net.env in the app's files folder: KEY=VALUE lines passed to the game's environment before it starts,
 * so online play can be tried on Android before the Network settings page exists (reports/2026-10-26-online-play.md
 * entry 53). Only PS2X_ names are taken; '#' starts a comment line; spaces around the key and value are dropped.
 */
fun parseDevEnv(text: String): List<Pair<String, String>> =
    text.lineSequence()
        .map { it.trim() }
        .filter { it.isNotEmpty() && !it.startsWith("#") }
        .mapNotNull { line ->
            val eq = line.indexOf('=')
            if (eq <= 0) return@mapNotNull null
            val key = line.substring(0, eq).trim()
            if (!key.startsWith("PS2X_")) null else key to line.substring(eq + 1).trim()
        }
        .toList()
