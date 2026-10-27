package com.outbreakmodern.app

/**
 * The C++ settings core (native/ob_ui/core) for the Kotlin side: the comment-keeping ini editor, the 3-step disc
 * check and the PS2 memory card reader, so Android and Windows share one tested implementation of each
 * (native/ob_ui/android/android_menu.cpp). The functions live in libps2EntryRunner.so; loading it here only maps it
 * (the runner's main() starts later, with the game activity).
 */
object NativeBridge {
    init {
        System.loadLibrary("ps2EntryRunner")
    }

    @JvmStatic external fun iniGet(path: String, section: String, key: String, inlineComments: Boolean): String?
    @JvmStatic external fun iniSet(path: String, section: String, key: String, value: String, inlineComments: Boolean): Boolean
    @JvmStatic external fun iniRemove(path: String, section: String, key: String, inlineComments: Boolean): Boolean

    /** {verdict, stepsPassed, message, serial, crc, version}; verdict 0 = verified. */
    @JvmStatic external fun checkDisc(path: String): Array<String>

    /** {present, formatted, sizeBytes, error, saves...}. */
    @JvmStatic external fun readCard(path: String): Array<String>

    /** "" when backed up, else the reason. */
    @JvmStatic external fun backupCard(path: String, keep: Int): String

    // Widescreen mods (native/ob_ui/core/widescreen.h): any .pnach/.pnach.disabled carrying the gameplay widescreen
    // patch or gsaspectratio=16:9, plus ob-widescreen.pnach. Hidden from Mods; Display > Aspect ratio switches them.
    @JvmStatic external fun isWidescreenModFile(path: String): Boolean
    @JvmStatic external fun widescreenEnabled(dir: String): Boolean
    /** 16:9 enables one (the player's own first) or writes ob-widescreen.pnach; 4:3 disables all. "" = done. */
    @JvmStatic external fun setWidescreen(dir: String, on: Boolean): String
}
