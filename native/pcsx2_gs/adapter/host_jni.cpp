// host_jni: the Android launcher's calls into the built-in host and the nearby-game search (docs/plans/
// 2026-10-28-local-play.md). The server code is native/ob_server (AGPL-3.0, combined with this GPL-3.0 program as its
// LICENSE.md allows). Called from the launcher's process (HostService, the Network page); one host and one search at
// a time per process.
#include "host.h"
#include "safe_text.h" // text to NewStringUTF (security queue C5)

#include <android/log.h>
#include <jni.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
	std::mutex g_mutex;
	std::unique_ptr<ob::server::LocalHost> g_host;
	std::unique_ptr<ob::server::BeaconListener> g_search;

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

	jobjectArray Strings(JNIEnv* env, const std::vector<std::string>& items)
	{
		jobjectArray out = env->NewObjectArray(static_cast<jsize>(items.size()), env->FindClass("java/lang/String"), nullptr);
		for (size_t i = 0; i < items.size(); i++)
		{
			jstring s = env->NewStringUTF(obui::javaSafeText(items[i]).c_str());
			env->SetObjectArrayElement(out, static_cast<jsize>(i), s);
			env->DeleteLocalRef(s);
		}
		return out;
	}

	void Log(const std::string& line)
	{
		__android_log_write(ANDROID_LOG_INFO, "ob_host", line.c_str());
	}
}

// Referenced from the runner's start-up (hook.cpp) so the linker keeps this file's JNI functions.
extern "C" void ObHostJniKeep() {}

extern "C" {

// hostStart(name, address, prefix, webPort, broadcast): "" when hosting, else why not. address = this device's Wi-Fi
// IPv4 (the server listens there only) and prefix its network's length (only that subnet may join: security queue
// S10); broadcast = the Wi-Fi subnet's broadcast address ("" = 255.255.255.255 only).
JNIEXPORT jstring JNICALL Java_com_outbreakmodern_app_NativeBridge_hostStart(JNIEnv* env, jclass, jstring name,
	jstring address, jint prefix, jint webPort, jstring broadcast)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	ob::server::HostOptions options;
	options.name = Str(env, name);
	options.address = Str(env, address);
	options.webPort = static_cast<uint16_t>(webPort);
	options.prefix = prefix >= 1 && prefix <= 32 ? static_cast<int>(prefix) : 0;
	const std::string subnet = Str(env, broadcast);
	if (!subnet.empty() && subnet != "255.255.255.255")
		options.beaconTargets.push_back(subnet);
	options.log = Log;
	if (!g_host)
		g_host = std::make_unique<ob::server::LocalHost>();
	std::string error;
	if (!g_host->start(options, &error))
	{
		Log("[host] not started: " + error);
		return env->NewStringUTF(error.empty() ? "could not start" : obui::javaSafeText(error).c_str());
	}
	return env->NewStringUTF("");
}

JNIEXPORT void JNICALL Java_com_outbreakmodern_app_NativeBridge_hostStop(JNIEnv*, jclass)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_host)
	{
		g_host->stop();
		Log("[host] stopped");
	}
}

JNIEXPORT jboolean JNICALL Java_com_outbreakmodern_app_NativeBridge_hostRunning(JNIEnv*, jclass)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_host && g_host->running();
}

// The addresses in the host's lobby now.
JNIEXPORT jobjectArray JNICALL Java_com_outbreakmodern_app_NativeBridge_hostPlayers(JNIEnv* env, jclass)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return Strings(env, g_host && g_host->running() ? g_host->players() : std::vector<std::string>{});
}

JNIEXPORT void JNICALL Java_com_outbreakmodern_app_NativeBridge_hostRemove(JNIEnv* env, jclass, jstring address)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_host)
		g_host->removePlayer(Str(env, address));
}

// searchStart(): "" when listening for nearby hosts, else why not.
JNIEXPORT jstring JNICALL Java_com_outbreakmodern_app_NativeBridge_searchStart(JNIEnv* env, jclass)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_search)
		g_search = std::make_unique<ob::server::BeaconListener>();
	std::string error;
	if (!g_search->start(ob::server::kBeaconPort, &error))
		return env->NewStringUTF(error.empty() ? "could not listen" : obui::javaSafeText(error).c_str());
	return env->NewStringUTF("");
}

// The hosts heard in the last 3 seconds: "address\tname\twebPort\tlobbyPort\tplayers" each.
JNIEXPORT jobjectArray JNICALL Java_com_outbreakmodern_app_NativeBridge_searchHosts(JNIEnv* env, jclass)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	std::vector<std::string> rows;
	if (g_search)
		for (const ob::server::FoundHost& h : g_search->hosts())
			rows.push_back(h.address + "\t" + h.info.name + "\t" + std::to_string(h.info.webPort) + "\t" +
						   std::to_string(h.info.lobbyPort) + "\t" + std::to_string(h.info.players));
	return Strings(env, rows);
}

JNIEXPORT void JNICALL Java_com_outbreakmodern_app_NativeBridge_searchStop(JNIEnv*, jclass)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_search)
		g_search->stop();
}

} // extern "C"
