// android_menu: the in-game menu on Android is Kotlin + Compose (native/android/kotlin, OutbreakActivity.showMenu: a
// dialog window over the game; plan decision A14). The native side decides when it opens (F1 / Esc / Android Back,
// the Back button, F1 / Esc: obui::MenuTrigger) and holds the game while it is open; Kotlin reports how it closed.
//
// Also here: the JNI functions the Kotlin launcher (LauncherActivity, before the game) calls through
// com.outbreakmodern.app.NativeBridge: the ini editor, the disc check and the card reader of native/ob_ui/core, so
// Android and Windows share one implementation of each; and the touch controls' pad (core/touch_pad.h: touchPad,
// physicalPadReads, requestMenu; docs/plans/2026-10-26-touch-controls.md). NativeBridge loads libps2EntryRunner.so with
// System.loadLibrary, so these are found by their Java_* names.
#pragma once

#include <jni.h>

namespace obui::android
{
	// Main thread (the runner's): asks OutbreakActivity to show the menu (on its UI thread). False when the activity
	// has no showMenu (an APK without the Kotlin side): the caller then keeps its own menu.
	bool showMenu();
	// True from showMenu until Kotlin reports the menu closed.
	bool menuOpen();

	// How the menu last closed; each read clears it. quit: the player chose Quit game. inputChanged: input.ini was
	// written (reload the bindings). settingsChanged: [ui] settings that apply live were written (vsync, show_fps,
	// volume: read them again).
	struct MenuClose
	{
		bool closed = false, quit = false, inputChanged = false, settingsChanged = false;
	};
	MenuClose takeMenuClose();

	// True once after the touch controls asked for the menu (NativeBridge.requestMenu: the overlay's Menu > Settings);
	// main thread, read by hostFrame, which opens it as the Back button does.
	bool takeMenuRequest();
	void requestMenuFromTouch(); // any thread (JNI)

	// Internal (android_picker.cpp init): finds showMenu on the activity class and registers nativeMenuClosed.
	bool InitMenu(JNIEnv* env, jclass activityClass);
} // namespace obui::android
