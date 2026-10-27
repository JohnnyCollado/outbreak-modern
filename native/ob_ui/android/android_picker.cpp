// android_picker: see android_picker.h.
#include "android_picker.h"
#include "android_menu.h"

#include <android_native_app_glue.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <vector>

extern "C" struct android_app* GetAndroidApp(void); // raylib's android platform layer

namespace obui::android
{
	namespace
	{
		JavaVM* s_vm = nullptr;
		jobject s_activity = nullptr;   // global ref
		jmethodID s_pickDocument = nullptr;
		jmethodID s_openDocument = nullptr;
		jclass s_stringClass = nullptr; // global ref
		std::mutex s_mu;
		std::deque<PickResult> s_results;

		void Log(const std::string& line)
		{
			std::fprintf(stderr, "[ui] %s\n", line.c_str());
			std::fflush(stderr);
		}

		// The JNIEnv of this thread, attaching it when needed (attached = true: detach when done).
		JNIEnv* Env(bool& attached)
		{
			attached = false;
			if (!s_vm)
				return nullptr;
			JNIEnv* env = nullptr;
			const jint r = s_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
			if (r == JNI_OK)
				return env;
			if (r == JNI_EDETACHED && s_vm->AttachCurrentThread(&env, nullptr) == JNI_OK)
			{
				attached = true;
				return env;
			}
			return nullptr;
		}

		struct EnvScope
		{
			bool attached = false;
			JNIEnv* env = nullptr;
			EnvScope() : env(Env(attached)) {}
			~EnvScope()
			{
				if (attached)
					s_vm->DetachCurrentThread();
			}
		};

		std::string ToString(JNIEnv* env, jstring s)
		{
			if (!s)
				return {};
			const char* c = env->GetStringUTFChars(s, nullptr);
			std::string out = c ? c : "";
			if (c)
				env->ReleaseStringUTFChars(s, c);
			return out;
		}

		// OutbreakActivity.nativePicked, on the Java UI thread.
		void JNICALL NativePicked(JNIEnv* env, jclass, jint requestCode, jint fd, jstring displayName, jstring uri)
		{
			PickResult r;
			r.kind = static_cast<PickKind>(requestCode);
			r.fd = fd;
			r.cancelled = fd < 0;
			r.displayName = ToString(env, displayName);
			r.uri = ToString(env, uri);
			std::lock_guard<std::mutex> lk(s_mu);
			s_results.push_back(std::move(r));
		}

		bool ClearException(JNIEnv* env)
		{
			if (!env->ExceptionCheck())
				return false;
			env->ExceptionClear();
			return true;
		}

		// Entry names from a zip are untrusted: no absolute path, no "..", no drive letter or backslash tricks.
		bool SafeEntryName(const std::string& name)
		{
			if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos)
				return false;
			size_t start = 0;
			while (start <= name.size())
			{
				size_t end = name.find_first_of("/\\", start);
				if (end == std::string::npos)
					end = name.size();
				if (name.compare(start, end - start, "..") == 0 && end - start == 2)
					return false;
				start = end + 1;
			}
			return true;
		}
	} // namespace

	bool init()
	{
		if (s_activity)
			return true;
		android_app* app = GetAndroidApp();
		if (!app || !app->activity || !app->activity->vm || !app->activity->clazz)
		{
			Log("android picker unavailable: no activity");
			return false;
		}
		s_vm = app->activity->vm;
		EnvScope scope;
		JNIEnv* env = scope.env;
		if (!env)
		{
			Log("android picker unavailable: no JNI environment");
			s_vm = nullptr;
			return false;
		}
		jclass cls = env->GetObjectClass(app->activity->clazz);
		// The in-game menu (android_menu.h): OutbreakActivity.showMenu() and its nativeMenuClosed callback.
		const bool menu = InitMenu(env, cls);
		// The single-file picker of the ImGui launcher (optional: the Kotlin launcher picks on its own since A14).
		s_pickDocument = env->GetMethodID(cls, "pickDocument", "(I[Ljava/lang/String;)V");
		if (ClearException(env))
			s_pickDocument = nullptr;
		s_openDocument = env->GetMethodID(cls, "openDocument", "(Ljava/lang/String;)I");
		if (ClearException(env))
			s_openDocument = nullptr;
		if (s_pickDocument)
		{
			// NativeActivity loads our library with dlopen, so Java's lookup of Java_* symbols is not to be relied on.
			static const JNINativeMethod kMethods[] = {
				{const_cast<char*>("nativePicked"), const_cast<char*>("(IILjava/lang/String;Ljava/lang/String;)V"),
					reinterpret_cast<void*>(&NativePicked)},
			};
			if (env->RegisterNatives(cls, kMethods, 1) != JNI_OK || ClearException(env))
				s_pickDocument = nullptr;
		}
		env->DeleteLocalRef(cls);
		if (!menu && !s_pickDocument && !s_openDocument)
		{
			Log("android bridge unavailable: the activity has no showMenu/pickDocument/openDocument");
			s_vm = nullptr;
			return false;
		}
		jclass str = env->FindClass("java/lang/String");
		s_stringClass = static_cast<jclass>(env->NewGlobalRef(str));
		env->DeleteLocalRef(str);
		s_activity = env->NewGlobalRef(app->activity->clazz);
		Log(std::string("android bridge ready (menu ") + (menu ? "yes" : "no") + ", picker " + (s_pickDocument ? "yes" : "no") + ")");
		return true;
	}

	bool startPick(PickKind kind)
	{
		if (!s_activity || !s_pickDocument)
			return false;
		EnvScope scope;
		JNIEnv* env = scope.env;
		if (!env)
			return false;
		static const char* const kDisc[] = {"application/x-iso9660-image", "application/octet-stream", "*/*"};
		static const char* const kZip[] = {"application/zip", "application/octet-stream"};
		const bool disc = kind == PickKind::Disc;
		const char* const* types = disc ? kDisc : kZip;
		const jsize n = disc ? 3 : 2;
		jobjectArray arr = env->NewObjectArray(n, s_stringClass, nullptr);
		for (jsize i = 0; i < n; i++)
		{
			jstring s = env->NewStringUTF(types[i]);
			env->SetObjectArrayElement(arr, i, s);
			env->DeleteLocalRef(s);
		}
		env->CallVoidMethod(s_activity, s_pickDocument, static_cast<jint>(kind), arr);
		env->DeleteLocalRef(arr);
		if (ClearException(env))
		{
			Log("picker: pickDocument threw");
			return false;
		}
		return true;
	}

	bool pollPick(PickResult& out)
	{
		std::lock_guard<std::mutex> lk(s_mu);
		if (s_results.empty())
			return false;
		out = std::move(s_results.front());
		s_results.pop_front();
		return true;
	}

	int openDocument(const std::string& uri)
	{
		if (!s_activity || !s_openDocument || uri.empty())
			return -1;
		EnvScope scope;
		JNIEnv* env = scope.env;
		if (!env)
			return -1;
		jstring s = env->NewStringUTF(uri.c_str());
		const jint fd = env->CallIntMethod(s_activity, s_openDocument, s);
		env->DeleteLocalRef(s);
		if (ClearException(env))
			return -1;
		return fd;
	}

	std::string fdPath(int fd)
	{
		return "/proc/self/fd/" + std::to_string(fd);
	}

	bool isSeekableFile(int fd)
	{
		struct stat st = {};
		return fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
	}

	bool copyFdToFile(int fd, const std::filesystem::path& dest, std::string* err)
	{
		auto fail = [&](const std::string& why) {
			if (err)
				*err = why;
			return false;
		};
		if (fd < 0)
			return fail("no file");
		std::error_code ec;
		std::filesystem::create_directories(dest.parent_path(), ec);
		const std::filesystem::path tmp = dest.string() + ".tmp";
		std::FILE* out = std::fopen(tmp.c_str(), "wb");
		if (!out)
			return fail("cannot write " + tmp.string() + ": " + std::strerror(errno));
		std::vector<char> buf(1u << 20);
		off_t offset = 0;
		for (;;)
		{
			const ssize_t n = pread(fd, buf.data(), buf.size(), offset);
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				const std::string why = std::string("read failed: ") + std::strerror(errno);
				std::fclose(out);
				std::filesystem::remove(tmp, ec);
				return fail(why);
			}
			if (n == 0)
				break;
			if (std::fwrite(buf.data(), 1, static_cast<size_t>(n), out) != static_cast<size_t>(n))
			{
				std::fclose(out);
				std::filesystem::remove(tmp, ec);
				return fail("write failed (disk full?)");
			}
			offset += n;
		}
		if (std::fclose(out) != 0)
		{
			std::filesystem::remove(tmp, ec);
			return fail("write failed (disk full?)");
		}
		std::filesystem::rename(tmp, dest, ec);
		if (ec)
		{
			std::filesystem::remove(tmp, ec);
			return fail("cannot rename to " + dest.string());
		}
		return true;
	}

	bool extractZip(const std::filesystem::path& zip, const std::filesystem::path& destDir,
		const std::function<bool(const std::string& entryName)>& keep, std::string* err)
	{
		auto fail = [&](const std::string& why) {
			if (err)
				*err = why;
			return false;
		};
		int zerr = 0;
		zip_t* za = zip_open(zip.c_str(), ZIP_RDONLY, &zerr);
		if (!za)
		{
			zip_error_t ze;
			zip_error_init_with_code(&ze, zerr);
			const std::string why = std::string("cannot open the zip: ") + zip_error_strerror(&ze);
			zip_error_fini(&ze);
			return fail(why);
		}
		const zip_int64_t count = zip_get_num_entries(za, 0);
		std::vector<char> buf(1u << 20);
		std::error_code ec;
		for (zip_int64_t i = 0; i < count; i++)
		{
			const char* raw = zip_get_name(za, static_cast<zip_uint64_t>(i), 0);
			const std::string name = raw ? raw : "";
			if (!SafeEntryName(name))
			{
				zip_close(za);
				return fail("unsafe entry name in the zip: " + name);
			}
			if (keep && !keep(name))
				continue;
			const std::filesystem::path out = destDir / std::filesystem::path(name);
			if (name.back() == '/' || name.back() == '\\')
			{
				std::filesystem::create_directories(out, ec);
				continue;
			}
			std::filesystem::create_directories(out.parent_path(), ec);
			zip_file_t* zf = zip_fopen_index(za, static_cast<zip_uint64_t>(i), 0);
			if (!zf)
			{
				zip_close(za);
				return fail("cannot read " + name + " in the zip");
			}
			std::ofstream os(out, std::ios::binary | std::ios::trunc);
			bool ok = static_cast<bool>(os);
			for (zip_int64_t n; ok && (n = zip_fread(zf, buf.data(), buf.size())) != 0;)
			{
				if (n < 0)
					ok = false;
				else
					ok = static_cast<bool>(os.write(buf.data(), n));
			}
			zip_fclose(zf);
			if (!ok)
			{
				zip_close(za);
				return fail("cannot extract " + name + " (disk full?)");
			}
		}
		zip_close(za);
		return true;
	}
} // namespace obui::android
