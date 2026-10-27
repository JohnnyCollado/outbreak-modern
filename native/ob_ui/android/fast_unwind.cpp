// fast_unwind: a quicker library lookup for C++ exception unwinding inside libps2EntryRunner.so (Android).
//
// The EE scheduler hands control back to its dispatcher by throwing (EeDispatcherTransfer: thread switches, waits,
// interrupt handler calls), many times a frame. The unwinder linked into this library (LLVM libunwind) finds the
// unwind tables of every frame it passes through with dl_iterate_phdr, which takes the dynamic linker's lock and walks
// the loaded libraries one by one; on the developer's OnePlus 13 that was 5-12 % of the game thread (reports/
// 2026-10-26-touch-controls.md, struggle 11). Nearly every frame it unwinds is in this library.
//
// This file defines dl_iterate_phdr with hidden visibility, so the unwinder linked into this library calls it instead
// of libc's (nothing outside the library sees it): this library's entry first, from a copy taken once, then libc's
// walk over the others, skipping this one, so every library is still visited once, this one first. A throw-and-catch
// at installation checks the unwinder through it; should that fail, every call goes straight to libc's.
#include "android_menu.h"

#include <dlfcn.h>
#include <link.h>

#include <atomic>
#include <cstdio>
#include <mutex>

namespace
{
	using IterateFn = int (*)(int (*)(dl_phdr_info*, size_t, void*), void*);
	using Callback = int (*)(dl_phdr_info*, size_t, void*);

	std::atomic<IterateFn> s_real{nullptr};
	dl_phdr_info s_self{};
	std::atomic<bool> s_enabled{false}; // set (release) after s_self is filled
	std::once_flag s_once;

	// libc's dl_iterate_phdr (ours is hidden, so dlsym finds libc's); looked up once, safe from any thread.
	IterateFn Real()
	{
		IterateFn f = s_real.load(std::memory_order_acquire);
		if (!f)
		{
			f = reinterpret_cast<IterateFn>(dlsym(RTLD_DEFAULT, "dl_iterate_phdr"));
			s_real.store(f, std::memory_order_release);
		}
		return f;
	}

	// The entry whose loaded segments hold this function: this library.
	int FindSelf(dl_phdr_info* info, size_t, void*)
	{
		const auto here = reinterpret_cast<ElfW(Addr)>(&FindSelf);
		for (ElfW(Half) i = 0; i < info->dlpi_phnum; i++)
		{
			const ElfW(Phdr)& ph = info->dlpi_phdr[i];
			if (ph.p_type == PT_LOAD && here >= info->dlpi_addr + ph.p_vaddr && here < info->dlpi_addr + ph.p_vaddr + ph.p_memsz)
			{
				s_self = *info;
				return 1;
			}
		}
		return 0;
	}

	struct SkipSelf
	{
		Callback callback;
		void* data;
	};

	int CallSkippingSelf(dl_phdr_info* info, size_t size, void* data)
	{
		auto* s = static_cast<SkipSelf*>(data);
		if (info->dlpi_addr == s_self.dlpi_addr && info->dlpi_phdr == s_self.dlpi_phdr)
			return 0;
		return s->callback(info, size, s->data);
	}

	struct SelfTest
	{
		int value;
	};

	[[gnu::noinline]] void Throw(int v) { throw SelfTest{v}; }

	// Once (installFastUnwind); never from dl_iterate_phdr itself, which the self-test's throw calls.
	void Install()
	{
		const IterateFn real = Real();
		if (!real || real(&FindSelf, nullptr) == 0)
		{
			std::fprintf(stderr, "[unwind] fast path off: %s\n", real ? "this library not found" : "no dl_iterate_phdr");
			return;
		}
		s_enabled.store(true, std::memory_order_release);
		bool ok = false;
		try
		{
			Throw(42);
		}
		catch (const SelfTest& t)
		{
			ok = t.value == 42;
		}
		if (!ok)
			s_enabled.store(false);
		std::fprintf(stderr, "[unwind] fast path %s (this library first, %u segments)\n", ok ? "on" : "off: self-test failed",
			static_cast<unsigned>(s_self.dlpi_phnum));
		std::fflush(stderr);
	}
} // namespace

extern "C" __attribute__((visibility("hidden"))) int dl_iterate_phdr(Callback callback, void* data)
{
	const IterateFn real = Real();
	if (!s_enabled.load(std::memory_order_acquire))
		return real ? real(callback, data) : 0;
	dl_phdr_info self = s_self; // a copy: the callback may not keep or change ours
	if (const int r = callback(&self, sizeof(self), data))
		return r;
	SkipSelf skip{callback, data};
	return real(&CallSkippingSelf, &skip);
}

namespace obui::android
{
	void installFastUnwind() { std::call_once(s_once, Install); }
} // namespace obui::android
