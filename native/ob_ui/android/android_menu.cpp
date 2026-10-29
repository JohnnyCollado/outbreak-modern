// android_menu: see android_menu.h.
#include "android_menu.h"
#include "safe_text.h" // text from files to NewStringUTF (security queue C5)

#include "disc_check.h"
#include "game_screen.h"
#include "ini_file.h"
#include "memory_card.h"
#include "touch_pad.h"
#include "widescreen.h"

#include <android_native_app_glue.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

extern "C" struct android_app* GetAndroidApp(void); // raylib's android platform layer

namespace obui::android
{
	namespace
	{
		JavaVM* s_vm = nullptr;
		jobject s_activity = nullptr; // global ref
		jmethodID s_showMenu = nullptr;
		std::atomic<bool> s_open{false};
		std::atomic<bool> s_menuRequest{false};
		std::mutex s_mu;
		MenuClose s_close;

		// OutbreakActivity.nativeMenuClosed(quit, inputChanged, settingsChanged), on the Java UI thread.
		void JNICALL NativeMenuClosed(JNIEnv*, jobject, jboolean quit, jboolean inputChanged, jboolean settingsChanged)
		{
			{
				std::lock_guard<std::mutex> lk(s_mu);
				s_close.closed = true;
				s_close.quit = quit;
				s_close.inputChanged = s_close.inputChanged || inputChanged;
				s_close.settingsChanged = s_close.settingsChanged || settingsChanged;
			}
			s_open.store(false);
			std::fprintf(stderr, "[ui] menu closed (quit=%d input=%d settings=%d)\n", quit ? 1 : 0, inputChanged ? 1 : 0,
				settingsChanged ? 1 : 0);
			std::fflush(stderr);
		}
	} // namespace

	bool InitMenu(JNIEnv* env, jclass cls)
	{
		installFastUnwind(); // before the game's first thread switch (fast_unwind.cpp)
		s_showMenu = env->GetMethodID(cls, "showMenu", "()V");
		if (env->ExceptionCheck())
		{
			env->ExceptionClear();
			s_showMenu = nullptr;
			return false;
		}
		static const JNINativeMethod kMethods[] = {
			{const_cast<char*>("nativeMenuClosed"), const_cast<char*>("(ZZZ)V"), reinterpret_cast<void*>(&NativeMenuClosed)},
		};
		if (env->RegisterNatives(cls, kMethods, 1) != JNI_OK || env->ExceptionCheck())
		{
			env->ExceptionClear();
			s_showMenu = nullptr;
			return false;
		}
		android_app* app = GetAndroidApp();
		s_vm = app->activity->vm;
		s_activity = env->NewGlobalRef(app->activity->clazz);
		return true;
	}

	bool showMenu()
	{
		if (!s_activity || !s_showMenu)
			return false;
		JNIEnv* env = nullptr;
		bool attached = false;
		if (s_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
		{
			if (s_vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
				return false;
			attached = true;
		}
		s_open.store(true);
		env->CallVoidMethod(s_activity, s_showMenu);
		const bool ok = !env->ExceptionCheck();
		if (!ok)
		{
			env->ExceptionClear();
			s_open.store(false);
		}
		if (attached)
			s_vm->DetachCurrentThread();
		std::fprintf(stderr, "[ui] menu %s (Kotlin)\n", ok ? "open" : "failed to open");
		std::fflush(stderr);
		return ok;
	}

	bool menuOpen() { return s_open.load(); }

	MenuClose takeMenuClose()
	{
		std::lock_guard<std::mutex> lk(s_mu);
		MenuClose c = s_close;
		s_close = MenuClose{};
		return c;
	}

	bool takeMenuRequest() { return s_menuRequest.exchange(false); }
	void requestMenuFromTouch() { s_menuRequest.store(true); }
} // namespace obui::android

// ---- com.outbreakmodern.app.NativeBridge (the Kotlin launcher) ----
namespace
{
	std::string Str(JNIEnv* env, jstring s)
	{
		if (!s)
			return {};
		const char* c = env->GetStringUTFChars(s, nullptr);
		std::string out = c ? c : "";
		if (c)
			env->ReleaseStringUTFChars(s, c);
		return out;
	}

	jobjectArray Strings(JNIEnv* env, const std::vector<std::string>& v)
	{
		jclass str = env->FindClass("java/lang/String");
		jobjectArray arr = env->NewObjectArray(static_cast<jsize>(v.size()), str, nullptr);
		for (size_t i = 0; i < v.size(); i++)
		{
			jstring s = env->NewStringUTF(obui::javaSafeText(v[i]).c_str());
			env->SetObjectArrayElement(arr, static_cast<jsize>(i), s);
			env->DeleteLocalRef(s);
		}
		env->DeleteLocalRef(str);
		return arr;
	}
} // namespace

extern "C" {

// iniGet(path, section, key, inlineComments): the value, or null when the key is not set.
JNIEXPORT jstring JNICALL Java_com_outbreakmodern_app_NativeBridge_iniGet(JNIEnv* env, jclass, jstring path,
	jstring section, jstring key, jboolean inlineComments)
{
	obui::IniFile ini(inlineComments);
	ini.load(std::filesystem::path(Str(env, path)));
	const auto v = ini.get(Str(env, section), Str(env, key));
	return v ? env->NewStringUTF(obui::javaSafeText(*v).c_str()) : nullptr;
}

// iniSet / iniRemove: edit one key in place (comments and other lines kept) and save. False when the save failed.
JNIEXPORT jboolean JNICALL Java_com_outbreakmodern_app_NativeBridge_iniSet(JNIEnv* env, jclass, jstring path,
	jstring section, jstring key, jstring value, jboolean inlineComments)
{
	const std::filesystem::path p(Str(env, path));
	obui::IniFile ini(inlineComments);
	ini.load(p);
	ini.set(Str(env, section), Str(env, key), Str(env, value));
	return ini.save(p);
}

JNIEXPORT jboolean JNICALL Java_com_outbreakmodern_app_NativeBridge_iniRemove(JNIEnv* env, jclass, jstring path,
	jstring section, jstring key, jboolean inlineComments)
{
	const std::filesystem::path p(Str(env, path));
	obui::IniFile ini(inlineComments);
	ini.load(p);
	ini.remove(Str(env, section), Str(env, key));
	return ini.save(p);
}

// checkDisc(path): {verdict (0 Ok, 1 NoFile, 2 NotDiscImage, 3 NoSystemCnf, 4 WrongGame, 5 WrongRelease, 6 ReadError),
// stepsPassed, message, serial, crc (8 hex digits), version}. path may be /proc/self/fd/<n>.
JNIEXPORT jobjectArray JNICALL Java_com_outbreakmodern_app_NativeBridge_checkDisc(JNIEnv* env, jclass, jstring path)
{
	const obui::DiscReport r = obui::checkDisc(std::filesystem::path(Str(env, path)), obui::builtInExpectedElf());
	char crc[16];
	std::snprintf(crc, sizeof(crc), "%08X", r.crc);
	return Strings(env, {std::to_string(static_cast<int>(r.verdict)), std::to_string(r.stepsPassed), r.message, r.serial,
		crc, r.version});
}

// readCard(path): {present 0/1, formatted 0/1, sizeBytes, error, save names...}.
JNIEXPORT jobjectArray JNICALL Java_com_outbreakmodern_app_NativeBridge_readCard(JNIEnv* env, jclass, jstring path)
{
	const obui::CardInfo c = obui::readCard(std::filesystem::path(Str(env, path)));
	std::vector<std::string> v = {c.present ? "1" : "0", c.formatted ? "1" : "0", std::to_string(c.sizeBytes), c.error};
	v.insert(v.end(), c.saves.begin(), c.saves.end());
	return Strings(env, v);
}

// backupCard(path, keep): "" when backed up, else the reason.
JNIEXPORT jstring JNICALL Java_com_outbreakmodern_app_NativeBridge_backupCard(JNIEnv* env, jclass, jstring path, jint keep)
{
	std::string err;
	const bool ok = obui::backupCard(std::filesystem::path(Str(env, path)), keep, &err);
	return env->NewStringUTF(ok ? "" : (err.empty() ? "backup failed" : obui::javaSafeText(err).c_str()));
}

// Widescreen (core/widescreen.h), one rule for both platforms: isWidescreenModFile(path), widescreenEnabled(dir),
// setWidescreen(dir, on) -> "" when done, else the reason. dir = <mods>/<serial>.
JNIEXPORT jboolean JNICALL Java_com_outbreakmodern_app_NativeBridge_isWidescreenModFile(JNIEnv* env, jclass, jstring path)
{
	return obui::isWidescreenModFile(std::filesystem::path(Str(env, path))) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_outbreakmodern_app_NativeBridge_widescreenEnabled(JNIEnv* env, jclass, jstring dir)
{
	return obui::widescreenEnabled(std::filesystem::path(Str(env, dir))) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_outbreakmodern_app_NativeBridge_setWidescreen(JNIEnv* env, jclass, jstring dir, jboolean on)
{
	std::string err;
	const bool ok = obui::setWidescreen(std::filesystem::path(Str(env, dir)), on == JNI_TRUE, &err);
	return env->NewStringUTF(ok ? "" : (err.empty() ? "could not change the aspect ratio" : obui::javaSafeText(err).c_str()));
}

// The touch controls (TouchOverlay, core/touch_pad.h). touchPad(pressed, rx, ry, lx, ly): PS2 button bits held (1 =
// held) and the stick bytes (0x80 = centre), merged into the first pad at the game's next read.
JNIEXPORT void JNICALL Java_com_outbreakmodern_app_NativeBridge_touchPad(JNIEnv*, jclass, jint pressed, jint rx, jint ry,
	jint lx, jint ly)
{
	const uint8_t sticks[4] = {static_cast<uint8_t>(rx), static_cast<uint8_t>(ry), static_cast<uint8_t>(lx),
		static_cast<uint8_t>(ly)};
	obui::touchPad().set(static_cast<uint16_t>(pressed), sticks);
}

// physicalPadReads(): the game's pad reads on which a controller or keyboard was in use (wraps); the overlay hides
// while it rises (setting "Auto").
JNIEXPORT jint JNICALL Java_com_outbreakmodern_app_NativeBridge_physicalPadReads(JNIEnv*, jclass)
{
	return static_cast<jint>(obui::touchPad().physicalReads());
}

// gameScreen(): what the game's screen wants (core/game_screen.h): -1 not known yet (no game RAM), else bit 0 = a menu
// screen (the D-pad layout), bits 8-15 = the game's controller type.
JNIEXPORT jint JNICALL Java_com_outbreakmodern_app_NativeBridge_gameScreen(JNIEnv*, jclass)
{
	const obui::GameScreen g = obui::currentScreen();
	if (!g.known)
		return -1;
	return (g.menu ? 1 : 0) | (static_cast<jint>(g.controlType) << 8);
}

// requestMenu(): open the in-game menu, as the Back button does (the overlay's Menu > Settings).
JNIEXPORT void JNICALL Java_com_outbreakmodern_app_NativeBridge_requestMenu(JNIEnv*, jclass)
{
	obui::android::requestMenuFromTouch();
}

} // extern "C"
