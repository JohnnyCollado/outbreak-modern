// gpu_driver: pure part of the Android GPU driver choice (gpu_driver.h). No Vulkan, no Android, no exceptions.
#include "gpu_driver.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace obgpu
{
	namespace
	{
		char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

		bool EqualsNoCase(std::string_view a, std::string_view b)
		{
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (Lower(a[i]) != Lower(b[i]))
					return false;
			return true;
		}

		std::string_view Trim(std::string_view s)
		{
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
				s.remove_prefix(1);
			while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
				s.remove_suffix(1);
			return s;
		}

		bool IsPlainFileName(std::string_view s)
		{
			if (s.empty() || s == "." || s == "..")
				return false;
			return s.find('/') == std::string_view::npos && s.find('\\') == std::string_view::npos;
		}

		// Minimal JSON reader: one object; string values (with escapes), numbers, true/false/null; nested values skipped.
		class Json
		{
		public:
			explicit Json(std::string_view t) : m_t(t) {}

			bool Object(DriverMeta& m, std::string& error)
			{
				Ws();
				if (m_i < m_t.size() && static_cast<unsigned char>(m_t[m_i]) == 0xEF && m_t.substr(m_i, 3) == "\xEF\xBB\xBF")
					m_i += 3; // UTF-8 BOM
				Ws();
				if (!Eat('{'))
					return Fail(error, "not a JSON object");
				Ws();
				if (Eat('}'))
					return End(error);
				for (;;)
				{
					Ws();
					std::string key;
					if (!String(key))
						return Fail(error, "expected a key");
					Ws();
					if (!Eat(':'))
						return Fail(error, "expected ':'");
					Ws();
					if (m_i < m_t.size() && m_t[m_i] == '"')
					{
						std::string v;
						if (!String(v))
							return Fail(error, "bad string");
						StringField(m, key, v);
					}
					else if (m_i < m_t.size() && (m_t[m_i] == '-' || (m_t[m_i] >= '0' && m_t[m_i] <= '9')))
					{
						double v = 0;
						if (!Number(v))
							return Fail(error, "bad number");
						NumberField(m, key, v);
					}
					else if (!Skip(0))
						return Fail(error, "bad value");
					Ws();
					if (Eat(','))
						continue;
					if (Eat('}'))
						return End(error);
					return Fail(error, "expected ',' or '}'");
				}
			}

		private:
			std::string_view m_t;
			size_t m_i = 0;

			bool Fail(std::string& error, const char* what)
			{
				char b[96];
				std::snprintf(b, sizeof(b), "meta.json: %s at byte %zu", what, m_i);
				error = b;
				return false;
			}
			bool End(std::string& error)
			{
				Ws();
				return m_i == m_t.size() ? true : Fail(error, "text after the object");
			}
			void Ws()
			{
				while (m_i < m_t.size() && (m_t[m_i] == ' ' || m_t[m_i] == '\t' || m_t[m_i] == '\r' || m_t[m_i] == '\n'))
					++m_i;
			}
			bool Eat(char c)
			{
				if (m_i < m_t.size() && m_t[m_i] == c)
				{
					++m_i;
					return true;
				}
				return false;
			}
			static int Hex(char c)
			{
				if (c >= '0' && c <= '9')
					return c - '0';
				if (c >= 'a' && c <= 'f')
					return c - 'a' + 10;
				if (c >= 'A' && c <= 'F')
					return c - 'A' + 10;
				return -1;
			}
			static void Utf8(std::string& out, uint32_t cp)
			{
				if (cp < 0x80)
					out += static_cast<char>(cp);
				else if (cp < 0x800)
				{
					out += static_cast<char>(0xC0 | (cp >> 6));
					out += static_cast<char>(0x80 | (cp & 0x3F));
				}
				else
				{
					out += static_cast<char>(0xE0 | (cp >> 12));
					out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
					out += static_cast<char>(0x80 | (cp & 0x3F));
				}
			}
			bool String(std::string& out)
			{
				if (!Eat('"'))
					return false;
				while (m_i < m_t.size())
				{
					const char c = m_t[m_i++];
					if (c == '"')
						return true;
					if (static_cast<unsigned char>(c) < 0x20)
						return false;
					if (c != '\\')
					{
						out += c;
						continue;
					}
					if (m_i >= m_t.size())
						return false;
					const char e = m_t[m_i++];
					switch (e)
					{
						case '"': out += '"'; break;
						case '\\': out += '\\'; break;
						case '/': out += '/'; break;
						case 'b': out += '\b'; break;
						case 'f': out += '\f'; break;
						case 'n': out += '\n'; break;
						case 'r': out += '\r'; break;
						case 't': out += '\t'; break;
						case 'u':
						{
							if (m_i + 4 > m_t.size())
								return false;
							uint32_t cp = 0;
							for (int k = 0; k < 4; ++k)
							{
								const int h = Hex(m_t[m_i++]);
								if (h < 0)
									return false;
								cp = (cp << 4) | static_cast<uint32_t>(h);
							}
							Utf8(out, cp); // surrogate pairs come out as two 3-byte sequences: display only
							break;
						}
						default: return false;
					}
				}
				return false;
			}
			bool Number(double& v)
			{
				const size_t start = m_i;
				if (m_t[m_i] == '-')
					++m_i;
				while (m_i < m_t.size() && ((m_t[m_i] >= '0' && m_t[m_i] <= '9') || m_t[m_i] == '.' || m_t[m_i] == 'e' ||
										   m_t[m_i] == 'E' || m_t[m_i] == '+' || m_t[m_i] == '-'))
					++m_i;
				const std::string s(m_t.substr(start, m_i - start));
				char* end = nullptr;
				v = std::strtod(s.c_str(), &end);
				return end && *end == '\0' && !s.empty() && s != "-";
			}
			// Skips one value (literal, array or object); depth guards against hostile nesting.
			bool Skip(int depth)
			{
				if (depth > 32 || m_i >= m_t.size())
					return false;
				const char c = m_t[m_i];
				if (c == '"')
				{
					std::string s;
					return String(s);
				}
				if (c == '-' || (c >= '0' && c <= '9'))
				{
					double v;
					return Number(v);
				}
				for (std::string_view lit : {std::string_view("true"), std::string_view("false"), std::string_view("null")})
				{
					if (m_t.substr(m_i, lit.size()) == lit)
					{
						m_i += lit.size();
						return true;
					}
				}
				if (c != '[' && c != '{')
					return false;
				const bool obj = c == '{';
				const char close = obj ? '}' : ']';
				++m_i;
				Ws();
				if (Eat(close))
					return true;
				for (;;)
				{
					Ws();
					if (obj)
					{
						std::string k;
						if (!String(k))
							return false;
						Ws();
						if (!Eat(':'))
							return false;
						Ws();
					}
					if (!Skip(depth + 1))
						return false;
					Ws();
					if (Eat(','))
						continue;
					return Eat(close);
				}
			}
			static void StringField(DriverMeta& m, const std::string& k, const std::string& v)
			{
				if (k == "name")
					m.name = v;
				else if (k == "description")
					m.description = v;
				else if (k == "author")
					m.author = v;
				else if (k == "packageVersion")
					m.packageVersion = v;
				else if (k == "vendor")
					m.vendor = v;
				else if (k == "driverVersion")
					m.driverVersion = v;
				else if (k == "libraryName")
					m.libraryName = v;
			}
			static void NumberField(DriverMeta& m, const std::string& k, double v)
			{
				const int i = (v > -1e9 && v < 1e9) ? static_cast<int>(v) : 0;
				if (k == "schemaVersion")
					m.schemaVersion = i;
				else if (k == "minApi")
					m.minApi = i;
				else if (k == "packageVersion" || k == "driverVersion")
				{
					char b[32];
					std::snprintf(b, sizeof(b), "%g", v);
					(k == "packageVersion" ? m.packageVersion : m.driverVersion) = b;
				}
			}
		};
	} // namespace

	bool IsValidDriverName(std::string_view name)
	{
		if (name.empty() || name.size() > 64 || name.front() == '.' || EqualsNoCase(name, "system"))
			return false;
		for (char c : name)
		{
			const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
							c == '_' || c == '-';
			if (!ok)
				return false;
		}
		return true;
	}

	DriverChoice ParseGpuDriverSetting(std::string_view value)
	{
		DriverChoice c;
		value = Trim(value);
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
			value = Trim(value.substr(1, value.size() - 2));
		if (value.empty() || EqualsNoCase(value, "system"))
			return c;
		if (!IsValidDriverName(value))
		{
			c.error = "gpu_driver '" + std::string(value) +
					  "' is not a folder name (letters, digits, '.', '_', '-'; up to 64; no path)";
			return c;
		}
		c.system = false;
		c.name = std::string(value);
		return c;
	}

	bool ParseDriverMeta(std::string_view json, DriverMeta& meta, std::string& error)
	{
		meta = DriverMeta{};
		error.clear();
		Json j(json);
		return j.Object(meta, error);
	}

	std::string PickDriverLibrary(const DriverMeta& meta, const std::vector<std::string>& files, std::string& error)
	{
		error.clear();
		auto has = [&](std::string_view f) { return std::find(files.begin(), files.end(), f) != files.end(); };
		if (!meta.libraryName.empty())
		{
			if (!IsPlainFileName(meta.libraryName))
			{
				error = "meta.json libraryName '" + meta.libraryName + "' is not a file name";
				return {};
			}
			if (!has(meta.libraryName))
			{
				error = "meta.json names " + meta.libraryName + ", not in the folder";
				return {};
			}
			return meta.libraryName;
		}
		std::string found;
		int n = 0;
		for (const std::string& f : files)
		{
			if (f.size() > 10 && f.compare(0, 7, "vulkan.") == 0 && f.compare(f.size() - 3, 3, ".so") == 0)
			{
				found = f;
				++n;
			}
		}
		if (n == 1)
			return found;
		error = n == 0 ? "no libraryName in meta.json and no vulkan.*.so in the folder"
					   : "no libraryName in meta.json and several vulkan.*.so in the folder";
		return {};
	}

	std::string MinApiWarning(const CustomDriverCheck& c)
	{
		if (c.meta.minApi <= 0 || c.device_api <= 0 || c.device_api >= c.meta.minApi)
			return {};
		char b[128];
		std::snprintf(b, sizeof(b), "driver made for Android API %d, device has %d: loading it anyway (it may have bugs)",
			c.meta.minApi, c.device_api);
		return b;
	}

	std::string CustomDriverBlocker(const CustomDriverCheck& c, std::string& library)
	{
		library.clear();
		if (!c.folder_found)
			return "no folder drivers/<name> in the app's files dir";
		if (!c.meta_found)
			return "no meta.json in the driver folder";
		if (!c.meta_ok)
			return c.meta_error.empty() ? std::string("meta.json unreadable") : c.meta_error;
		std::string error;
		library = PickDriverLibrary(c.meta, c.files, error);
		if (library.empty())
			return error;
		const bool adreno = c.system_vendor_id == kVendorQualcomm || (c.system_vendor_id == 0 && c.kgsl_present);
		if (!adreno)
		{
			library.clear();
			char b[96];
			std::snprintf(b, sizeof(b), "GPU is not Adreno (vendor %s); custom drivers load on Adreno only",
				VendorName(c.system_vendor_id).c_str());
			return b;
		}
		// A newer minApi than the device is no longer a blocker (developer, 2026-10-26): firmware-extracted packages
		// often state their own phone's Android version and may still work; MinApiWarning logs it, the launcher warns,
		// and a driver that then fails still falls back to the system one.
		if (!c.native_lib_dir_ok)
		{
			library.clear();
			return "the APK's native libraries are not extracted (adrenotools needs nativeLibraryDir)";
		}
		if (!c.hooks_present)
		{
			library.clear();
			return "libadrenotools hooks (libhook_impl.so, libmain_hook.so) missing from the APK";
		}
		return {};
	}

	std::string FallbackLine(std::string_view name, std::string_view reason)
	{
		return "[gpu] custom driver " + std::string(name) + " unavailable (" + std::string(reason) + "); using system driver";
	}

	std::string VendorName(uint32_t id)
	{
		switch (id)
		{
			case 0x5143: return "Qualcomm";
			case 0x13B5: return "ARM";
			case 0x1010: return "ImgTec";
			case 0x144D: return "Samsung";
			case 0x10DE: return "NVIDIA";
			case 0x1002: return "AMD";
			case 0x8086: return "Intel";
			case 0x10005: return "Mesa";
			default:
			{
				char b[16];
				std::snprintf(b, sizeof(b), "0x%04X", id);
				return b;
			}
		}
	}

	std::string FormatDriverVersion(uint32_t vendor_id, uint32_t v)
	{
		char b[64];
		if (vendor_id == 0x10DE)
			std::snprintf(b, sizeof(b), "%u.%u.%u.%u (0x%08X)", v >> 22, (v >> 14) & 0xFF, (v >> 6) & 0xFF, v & 0x3F, v);
		else
			std::snprintf(b, sizeof(b), "%u.%u.%u (0x%08X)", v >> 22, (v >> 12) & 0x3FF, v & 0xFFF, v);
		return b;
	}

	std::string DriverLine(bool custom, std::string_view name, const DeviceInfo& d)
	{
		std::string s = "[gpu] driver=";
		s += custom ? "custom:" + std::string(name) : std::string("system");
		char b[32];
		std::snprintf(b, sizeof(b), "(0x%04X)", d.vendor_id);
		s += " vendor=" + VendorName(d.vendor_id) + b;
		s += " deviceName=" + (d.device_name.empty() ? std::string("?") : d.device_name);
		s += " driverVersion=" + FormatDriverVersion(d.vendor_id, d.driver_version);
		if (!d.driver_name.empty() || !d.driver_info.empty())
			s += " (" + d.driver_name + (d.driver_name.empty() || d.driver_info.empty() ? "" : " ") + d.driver_info + ")";
		std::snprintf(b, sizeof(b), " api=%u.%u.%u", d.api_version >> 22, (d.api_version >> 12) & 0x3FF, d.api_version & 0xFFF);
		s += b;
		return s;
	}

	SelfReadChoice ParseSelfReadSetting(std::string_view value)
	{
		size_t b = 0, e = value.size();
		while (b < e && std::isspace(static_cast<unsigned char>(value[b])))
			++b;
		while (e > b && std::isspace(static_cast<unsigned char>(value[e - 1])))
			--e;
		std::string v;
		for (char c : value.substr(b, e - b))
			v += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		SelfReadChoice r;
		if (v.empty() || v == "auto")
			r.mode = SelfRead::Auto;
		else if (v == "copy")
			r.mode = SelfRead::Copy;
		else if (v == "barrier")
			r.mode = SelfRead::Barrier;
		else if (v == "fetch")
			r.mode = SelfRead::Fetch;
		else
			r.error = "gs_self_read=" + std::string(value) + " is not auto, copy, barrier or fetch";
		return r;
	}

	bool IsQualcommProprietary(const DeviceInfo& d)
	{
		if (d.vendor_id != kVendorQualcomm)
			return false;
		if (d.driver_name.empty())
			return true;
		std::string n;
		for (char c : d.driver_name)
			n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return n.find("qualcomm") != std::string::npos && n.find("mesa") == std::string::npos &&
			   n.find("turnip") == std::string::npos;
	}

	bool IsTurnipAdreno8(const DeviceInfo& d)
	{
		if (d.vendor_id != kVendorQualcomm)
			return false;
		std::string driver, device;
		for (char c : d.driver_name)
			driver += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		for (char c : d.device_name)
			device += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (driver.find("turnip") == std::string::npos && driver.find("mesa") == std::string::npos)
			return false;
		// "Adreno (TM) 830": the first digit run after "adreno" is the model; 8xx is the 8 Elite generation.
		const size_t at = device.find("adreno");
		if (at == std::string::npos)
			return false;
		const size_t digit = device.find_first_of("0123456789", at);
		return digit != std::string::npos && device[digit] == '8' && digit + 2 < device.size() &&
			   std::isdigit(static_cast<unsigned char>(device[digit + 1])) && std::isdigit(static_cast<unsigned char>(device[digit + 2]));
	}

	SelfReadDecision DecideSelfRead(SelfRead setting, const DeviceInfo* device)
	{
		SelfReadDecision d;
		SelfRead road = setting;
		if (setting == SelfRead::Auto)
		{
			const bool qualcomm = device && IsQualcommProprietary(*device);
			const bool turnip8 = device && !qualcomm && IsTurnipAdreno8(*device);
			road = qualcomm ? SelfRead::Copy : turnip8 ? SelfRead::Barrier : SelfRead::Fetch;
			d.why = qualcomm ? "auto: Qualcomm's driver" : turnip8 ? "auto: Turnip on Adreno 8xx" : "auto";
		}
		else
			d.why = "outbreak.ini";
		switch (road)
		{
			case SelfRead::Copy:
				d.override_texture_barriers = 0;
				d.disable_framebuffer_fetch = true;
				d.road = "copy";
				break;
			case SelfRead::Barrier:
				d.override_texture_barriers = 1;
				d.disable_framebuffer_fetch = true;
				d.road = "barrier";
				break;
			default:
				d.override_texture_barriers = -1;
				d.disable_framebuffer_fetch = false;
				d.road = "fetch";
				break;
		}
		return d;
	}

	std::string SelfReadLine(const SelfReadDecision& d)
	{
		return "[gpu] self-read=" + d.road + " (" + d.why + ")";
	}

	GameGsFixes GameGsFixesFor(std::string_view serial)
	{
		GameGsFixes f;
		if (serial == "SLPM-65428")
			f.limit_24bit_depth = 1;
		return f;
	}

	std::string FormatStamps(std::vector<FileStamp> files)
	{
		std::sort(files.begin(), files.end(), [](const FileStamp& a, const FileStamp& b) { return a.name < b.name; });
		std::string s;
		char b[64];
		for (const FileStamp& f : files)
		{
			std::snprintf(b, sizeof(b), " %llu %lld\n", static_cast<unsigned long long>(f.size), static_cast<long long>(f.mtime));
			s += f.name + b;
		}
		return s;
	}
} // namespace obgpu
