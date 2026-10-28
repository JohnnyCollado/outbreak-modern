package com.outbreakmodern.app

import java.io.File

/** The game's rule for a driver folder name (gpu_driver.cpp IsValidDriverName). */
fun isValidDriverName(name: String): Boolean =
    name.isNotEmpty() && name.length <= 64 && !name.startsWith(".") && !name.equals("system", ignoreCase = true) &&
        name.all { it in 'a'..'z' || it in 'A'..'Z' || it in '0'..'9' || it == '.' || it == '_' || it == '-' }

/** A name the game accepts: anything else becomes '_' (the importer names folders with it). */
fun safeDriverName(name: String): String =
    name.trim().replace(Regex("[^A-Za-z0-9._-]"), "_").trimStart('.').take(64)
        .let { if (it.isEmpty() || it.equals("system", ignoreCase = true)) "import" else it }

/**
 * Renames driver folders whose names the game rejects (imported before the importer's fix: "turnip T30 (1)" on the
 * Odin 3, report entry 53) to valid names; returns the gpu_driver setting to keep, renamed when it named such a folder.
 */
fun repairDriverFolders(driversDir: File, current: String): String {
    var kept = current
    driversDir.listFiles()?.filter { it.isDirectory && !isValidDriverName(it.name) }?.forEach { d ->
        val dest = File(driversDir, safeDriverName(d.name))
        if (!dest.exists() && d.renameTo(dest) && kept == d.name)
            kept = dest.name
    }
    return kept
}
