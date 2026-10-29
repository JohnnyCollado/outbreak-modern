// SPDX-License-Identifier: AGPL-3.0-or-later
// Command numbers and answer layouts: research/2026-10-27-lobby-protocol.md section 4 and the lobby study it cites.
#include "lobby.h"

#include "datafile.h"
#include "net.h"
#include "protocol.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <list>
#include <mutex>

namespace ob::server
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr int kAreas = 2, kRooms = 10, kSlots = 20;
		constexpr int kAfterGameArea = 51;
		constexpr size_t kStatsSize = 0xD0;

		// Commands the minimal path uses (protocol spec section 4).
		enum : uint16_t
		{
			CONNCHECK = 0x6001, LEAVELOBBY = 0x6002, LOGOUT = 0x6006, CHECKRND = 0x600E,
			LOGIN = 0x6101, CHECKVERSION = 0x6103, ENDLOGIN = 0x6104, IDHNPAIRS = 0x6131, HNSELECT = 0x6132,
			POSTGAMEINFO = 0x6138, RANKINGS = 0x6145, MOTD = 0x614C, CHARSELECT = 0x6190, UNK61A0 = 0x61A0,
			UNK61A1 = 0x61A1,
			HEARTBEAT = 0x6202, AREACOUNT = 0x6203, AREANAME = 0x6204, AREAPLAYERCNT = 0x6205, AREASTATUS = 0x6206,
			AREASELECT = 0x6207, EXITAREA = 0x6209, AREADESCRIPT = 0x620A,
			ROOMSCOUNT = 0x6301, ROOMNAME = 0x6302, ROOMPLAYERCNT = 0x6303, ROOMSTATUS = 0x6304, ENTERROOM = 0x6305,
			UNK6308 = 0x6308,
			SLOTCOUNT = 0x6401, SLOTTITLE = 0x6402, SLOTPLRSTATUS = 0x6403, SLOTSTATUS = 0x6404, SLOTPWDPROT = 0x6405,
			JOINGAME = 0x6406, CREATESLOT = 0x6407, EXITSLOTLIST = 0x6408, SLOTTIMER = 0x6409, PLAYERSTATS = 0x640A,
			SLOTATTRIB2 = 0x640B, UNK6412 = 0x6412,
			CANCELSLOT = 0x6501, LEAVESLOT = 0x6502, PLAYERSTATBC = 0x6503, UNK6504 = 0x6504, CANCELSLOTBC = 0x6505,
			PLAYEROK = 0x6506, STARTGAME = 0x6508, SCENESELECT = 0x6509, SLOTSCENTYPE = 0x650A,
			UNK6601 = 0x6601, UNK6602 = 0x6602, RULESCOUNT = 0x6603, RULEDESCRIPT = 0x6604, RULEATTRIB = 0x6605,
			RULEVALUE = 0x6606, RULEATTCOUNT = 0x6607, ATTRDESCRIPT = 0x6608, SLOTNAME = 0x6609, SLOTPASSWD = 0x660A,
			SETRULE = 0x660B, UNK660C = 0x660C, ATTRATTRIB = 0x660E,
			CHATIN = 0x6701, CHATOUT = 0x6702, EVENTDAT = 0x670D, EVENTDATBC = 0x670E,
			INFOSIZES = 0x6881, INFOCHUNK = 0x6882,
			GETREADY = 0x6910, PLAYERCOUNT = 0x6911, PLAYERNUMBER = 0x6912, PLAYERSTAT = 0x6913, GAMEDIFF = 0x6914,
			GAMESESSION = 0x6915, GSINFO = 0x6916, PLAYERSCORE = 0x6917,
		};

		struct Rule
		{
			const char* name;
			std::vector<const char*> choices;
		};
		// Values are choice indices. The fan server's defaults {2, 2, 3, 0} (Slot below) = four players, ten minutes,
		// "very hard", friendly fire off; the host's SETRULE answers replace them.
		const Rule kRules[4] = {
			{"number of players", {"two players", "three players", "four players"}},
			{"wait limit", {"three minutes", "five minutes", "ten minutes", "fifteen minutes", "thirty minutes"}},
			{"difficulty level", {"easy", "normal", "hard", "very hard"}},
			{"friendly fire", {"off", "on"}},
		};
		const int kWaitMinutes[5] = {3, 5, 10, 15, 30};
		// Caps on strings passed on to other players (security review S5): handles are 6 characters
		// (research/2026-10-27-lobby-protocol.md); the others are generous guesses well above the game's own use.
		constexpr size_t kMaxHandle = 6, kMaxNickname = 32, kMaxChat = 128, kMaxSlotName = 32, kMaxPassword = 16,
						 kMaxEvent = 8192;
		const char* kAreaNames[kAreas] = {"East Town", "West Town"};
		const char* kAreaDescriptions[kAreas] = {"<BODY><SIZE=3>standard rules<END>", "<BODY><SIZE=3>individual games<END>"};

		struct Slot
		{
			std::string name = "(free)";
			uint8_t status = 1; // 0 off, 1 free, 2 being created, 3 open, 4 full/started
			std::string password;
			uint8_t type = 0, scenario = 0;
			uint8_t rules[4] = {2, 2, 3, 0};
			Clock::time_point deadline{};
			std::string host;
			uint32_t game = 0;

			int maxPlayers() const { return std::clamp(rules[0] + 2, 2, 4); }
			void reset() { *this = Slot(); }
		};

		struct Client
		{
			net::Socket socket;
			std::string address;
			LobbyStream in;
			std::vector<uint8_t> out;
			bool loggedIn = false, closing = false, alive = true;
			std::string account;
			Handle handle;
			std::vector<uint8_t> stats = std::vector<uint8_t>(kStatsSize, 0);
			int area = 0, room = 0, slot = 0, playerNumber = 0;
			bool host = false;
			uint32_t game = 0;
			Clock::time_point connectedAt = Clock::now(), closingSince{};
		};

		void putHandle(std::vector<uint8_t>& out, const Handle& h)
		{
			putString(out, h.id);
			putString(out, h.nickname);
		}

		void putStats(std::vector<uint8_t>& out, const Client& c)
		{
			putU16(out, static_cast<uint16_t>(kStatsSize));
			out.insert(out.end(), c.stats.begin(), c.stats.end());
		}
	} // namespace

	struct LobbyServer::Impl
	{
		Accounts* accounts = nullptr;
		LobbyConfig config;
		ConnectionGuard guard;
		// Shared with the host's UI thread: who is connected, and who the host removed (handled in the loop).
		mutable std::mutex sharedMutex;
		std::vector<std::string> sharedAddresses;
		std::vector<std::string> pendingRemove;
		const std::vector<uint8_t> dataFile = loginDataFile(); // 6881/6882 (datafile.h)
		net::Socket listener = net::kInvalid;
		std::list<Client> clients;
		Slot slots[kAreas][kRooms][kSlots];
		uint16_t nextPid = 0;
		uint32_t lastGame = 1; // the first game number is 2, as the fan server's
		Clock::time_point nextHeartbeat, nextConnCheck;
		LobbyServer* owner = nullptr;

		void log(const std::string& line) const
		{
			if (config.log)
				config.log("[lobby] " + line);
		}

		uint16_t pid()
		{
			if (++nextPid == 0)
				nextPid = 1;
			return nextPid;
		}

		void send(Client& c, Kind kind, uint16_t cmd, uint16_t packetId, std::vector<uint8_t> payload, bool error = false)
		{
			Packet p;
			p.who = static_cast<uint8_t>(Who::Server);
			p.kind = static_cast<uint8_t>(kind);
			p.cmd = cmd;
			p.pid = packetId;
			p.error = error;
			p.payload = std::move(payload);
			if (config.trace)
				trace(c, "->", p);
			const std::vector<uint8_t> bytes = encode(p);
			if (c.out.size() + bytes.size() > config.maxOutBytes)
			{
				c.out.clear();
				log(c.address + ": not reading (pending output over the cap), disconnected");
				dropClient(c);
				return;
			}
			c.out.insert(c.out.end(), bytes.begin(), bytes.end());
		}
		// --trace: one line per packet in or out (kind, command, packet id, size, first bytes).
		void trace(const Client& c, const char* dir, const Packet& p)
		{
			char t[64];
			std::snprintf(t, sizeof(t), " %s k%u 0x%04X pid %u, %zu bytes:", dir, p.kind, p.cmd, p.pid, p.payload.size());
			std::string line = c.address + t;
			for (size_t i = 0; i < std::min<size_t>(p.payload.size(), 16); i++)
			{
				std::snprintf(t, sizeof(t), " %02X", p.payload[i]);
				line += t;
			}
			log(line);
		}
		void answer(Client& c, const Packet& q, std::vector<uint8_t> payload = {}) { send(c, Kind::Tell, q.cmd, q.pid, std::move(payload)); }
		void answerError(Client& c, const Packet& q, const std::string& message)
		{
			std::vector<uint8_t> p;
			putString(p, message);
			send(c, Kind::Tell, q.cmd, q.pid, std::move(p), true);
		}
		template <class Pred>
		void broadcast(Pred who, uint16_t cmd, const std::vector<uint8_t>& payload)
		{
			const uint16_t id = pid();
			for (Client& c : clients)
				if (c.loggedIn && !c.closing && who(c))
					send(c, Kind::Broadcast, cmd, id, payload);
		}

		Slot* slotOf(int area, int room, int slot)
		{
			if (area < 1 || area > kAreas || room < 1 || room > kRooms || slot < 1 || slot > kSlots)
				return nullptr;
			return &slots[area - 1][room - 1][slot - 1];
		}
		int playersIn(int area, int room, int slot) const
		{
			return static_cast<int>(std::count_if(clients.begin(), clients.end(), [&](const Client& c) {
				return c.loggedIn && !c.closing && c.area == area && c.room == room && c.slot == slot;
			}));
		}

		// ---- what the slot list shows (also broadcast to the room when it changes) ----
		std::vector<uint8_t> slotPlayerStatus(int area, int room, int nr)
		{
			const Slot* s = slotOf(area, room, nr);
			const uint8_t n = static_cast<uint8_t>(playersIn(area, room, nr));
			std::vector<uint8_t> p;
			putU16(p, static_cast<uint16_t>(nr));
			p.insert(p.end(), {0, n, 0, 0, 0, static_cast<uint8_t>(s ? s->maxPlayers() : 4), 0, n});
			return p;
		}
		std::vector<uint8_t> slotStatus(int area, int room, int nr)
		{
			const Slot* s = slotOf(area, room, nr);
			std::vector<uint8_t> p;
			putU16(p, static_cast<uint16_t>(nr));
			p.push_back(s ? s->status : 0);
			return p;
		}
		std::vector<uint8_t> slotTitle(int area, int room, int nr)
		{
			const Slot* s = slotOf(area, room, nr);
			std::vector<uint8_t> p;
			putU16(p, static_cast<uint16_t>(nr));
			putString(p, s ? s->name : "(free)");
			return p;
		}
		std::vector<uint8_t> slotAttrib2(int area, int room, int nr)
		{
			const Slot* s = slotOf(area, room, nr);
			std::vector<uint8_t> p;
			putU16(p, static_cast<uint16_t>(nr));
			p.insert(p.end(), {0, static_cast<uint8_t>(s ? s->maxPlayers() : 4), 0, 4, 0, 1, 0, 4, 0, 1});
			return p;
		}
		std::vector<uint8_t> slotPassword(int area, int room, int nr)
		{
			const Slot* s = slotOf(area, room, nr);
			std::vector<uint8_t> p;
			putU16(p, static_cast<uint16_t>(nr));
			p.push_back(s && !s->password.empty() ? 1 : 0);
			return p;
		}
		std::vector<uint8_t> slotScene(int area, int room, int nr)
		{
			const Slot* s = slotOf(area, room, nr);
			std::vector<uint8_t> p;
			putU16(p, static_cast<uint16_t>(nr));
			p.insert(p.end(), {0, static_cast<uint8_t>(s ? s->type : 0), 0, static_cast<uint8_t>(s ? s->scenario : 0)});
			return p;
		}
		void broadcastSlot(int area, int room, int nr)
		{
			auto inRoom = [&](const Client& c) { return c.area == area && c.room == room; };
			broadcast(inRoom, SLOTPLRSTATUS, slotPlayerStatus(area, room, nr));
			broadcast(inRoom, SLOTSTATUS, slotStatus(area, room, nr));
			broadcast(inRoom, SLOTATTRIB2, slotAttrib2(area, room, nr));
		}
		std::vector<uint8_t> areaPlayerCount(int nr)
		{
			uint16_t inLobby = 0, inRooms = 0, afterGame = 0;
			for (const Client& c : clients)
				if (c.loggedIn && !c.closing)
				{
					if (c.area == nr && c.room == 0)
						inLobby++;
					else if (c.area == nr)
						inRooms++;
					else if (c.area == kAfterGameArea)
						afterGame++;
				}
			std::vector<uint8_t> p;
			putU16(p, static_cast<uint16_t>(nr));
			putU16(p, inLobby);
			putU16(p, inRooms);
			p.insert(p.end(), {0xFF, 0xFF});
			putU16(p, afterGame);
			return p;
		}
		std::vector<uint8_t> roomPlayerCount(int area, int nr)
		{
			uint16_t inRoom = 0;
			for (const Client& c : clients)
				if (c.loggedIn && !c.closing && c.area == area && c.room == nr)
					inRoom++;
			std::vector<uint8_t> p;
			putU16(p, static_cast<uint16_t>(nr));
			putU16(p, inRoom);
			p.insert(p.end(), {0, 0, 0xFF, 0xFF, 0, 0});
			return p;
		}
		std::vector<uint8_t> preGameStat(const Client& c)
		{
			std::vector<uint8_t> p = {static_cast<uint8_t>(c.playerNumber), 1};
			putHandle(p, c.handle);
			putStats(p, c);
			p.insert(p.end(), {0, 0, 6});
			return p;
		}

		// ---- leaving a slot (cancel, leave, disconnect) ----
		void leaveSlot(Client& c)
		{
			Slot* s = slotOf(c.area, c.room, c.slot);
			if (!s)
			{
				c.slot = 0;
				return;
			}
			const int area = c.area, room = c.room, nr = c.slot;
			auto inSlot = [&](const Client& o) { return &o != &c && o.area == area && o.room == room && o.slot == nr; };
			std::vector<uint8_t> leave;
			putString(leave, c.handle.id);
			broadcast(inSlot, LEAVESLOT, leave);
			if (c.host)
			{
				std::vector<uint8_t> msg;
				putString(msg, "<LF=6><BODY><CENTER>host cancelled game<END>");
				broadcast(inSlot, CANCELSLOTBC, msg);
				for (Client& o : clients)
					if (inSlot(o))
						o.slot = 0, o.playerNumber = 0;
				s->reset();
			}
			c.slot = 0;
			c.host = false;
			c.playerNumber = 0;
			if (!c.host && s->status != 1)
			{
				const int left = playersIn(area, room, nr);
				if (left == 0)
					s->reset();
				else if (s->status == 4 && s->game == 0)
					s->status = 3;
			}
			broadcastSlot(area, room, nr);
			auto inRoom = [&](const Client& o) { return o.area == area && o.room == room; };
			broadcast(inRoom, SLOTTITLE, slotTitle(area, room, nr));
			broadcast(inRoom, SLOTPWDPROT, slotPassword(area, room, nr));
			broadcast(inRoom, SLOTSCENTYPE, slotScene(area, room, nr));
		}

		// ---- the start: one game number for the slot, GETREADY to its players ----
		void getReady(int area, int room, int nr)
		{
			Slot* s = slotOf(area, room, nr);
			if (!s || s->status == 1)
				return;
			if (s->game == 0)
				s->game = ++lastGame;
			for (Client& c : clients)
				if (c.loggedIn && c.area == area && c.room == room && c.slot == nr)
				{
					c.game = s->game;
					accounts->setGameNumber(c.account, s->game);
				}
			s->status = 4;
			broadcast([&](const Client& c) { return c.area == area && c.room == room; }, SLOTSTATUS, slotStatus(area, room, nr));
			broadcast([&](const Client& c) { return c.area == area && c.room == room && c.slot == nr; }, GETREADY, {});
			owner->m_games++;
			log("game " + std::to_string(s->game) + " starts: area " + std::to_string(area) + " room " +
				std::to_string(room) + " slot " + std::to_string(nr) + ", " + std::to_string(playersIn(area, room, nr)) +
				" player(s)");
		}

		void handle(Client& c, const Packet& q);
		void handleLoggedIn(Client& c, const Packet& q);
		void tick();
		void dropClient(Client& c);
	};

	void LobbyServer::Impl::dropClient(Client& c)
	{
		if (!c.closing)
			c.closingSince = Clock::now();
		if (c.loggedIn)
		{
			leaveSlot(c);
			const int area = c.area;
			c.closing = true;
			if (area >= 1 && area <= kAreas)
				broadcast([&](const Client& o) { return o.area == 0 || (o.area == area && o.room == 0); }, AREAPLAYERCNT,
					areaPlayerCount(area));
		}
		c.closing = true;
	}

	void LobbyServer::Impl::handle(Client& c, const Packet& q)
	{
		if (q.who != static_cast<uint8_t>(Who::Client))
			return;
		if (!c.loggedIn)
		{
			// The answer to our LOGIN question: the session number as 10 digits after a 2-byte field.
			if (q.cmd != LOGIN || q.kind != static_cast<uint8_t>(Kind::Tell) || q.payload.size() < 12)
				return;
			const auto session = decodeSessionDigits(q.payload.data() + 2, q.pid);
			const auto account = session ? accounts->accountForSession(*session) : std::nullopt;
			if (!account || (config.sessionsByAddress && *account != c.address))
			{
				log(c.address + (account ? ": another device's session, disconnected" : ": unknown session, disconnected"));
				c.closing = true;
				return;
			}
			for (Client& o : clients) // one connection per account
				if (&o != &c && o.loggedIn && o.account == *account)
					dropClient(o);
			c.loggedIn = true;
			c.account = *account;
			c.game = accounts->gameNumber(c.account);
			if (c.game > 0)
				c.area = kAfterGameArea;
			owner->m_logins++;
			log(c.address + ": logged in as " + c.account);
			send(c, Kind::Query, CHECKVERSION, pid(), {});
			return;
		}
		handleLoggedIn(c, q);
	}

	void LobbyServer::Impl::handleLoggedIn(Client& c, const Packet& q)
	{
		const std::vector<uint8_t>& p = q.payload;
		const uint8_t key = static_cast<uint8_t>(q.pid & 0xFF);
		const int nr = getU16(p, 0);
		auto byte = [&](size_t i) { return i < p.size() ? p[i] : uint8_t(0); };
		if (config.trace)
			trace(c, "<-", q);

		if (q.kind == static_cast<uint8_t>(Kind::Tell))
		{
			switch (q.cmd)
			{
			case CHECKVERSION:
			{
				// [?][?][?][length:2][sum:2][version...]: logged once; never answered with the code patch (6121-6125).
				std::string version;
				const size_t length = getU16(p, 3);
				if (length >= 2 && 7 + length - 2 <= p.size())
				{
					version.assign(p.begin() + 7, p.begin() + 7 + static_cast<std::ptrdiff_t>(length - 2));
					scramble(reinterpret_cast<uint8_t*>(version.data()), version.size(), key);
				}
				log(c.address + ": game version '" + version + "'");
				std::vector<uint8_t> list;
				const std::vector<Handle> handles = accounts->handles(c.account);
				list.push_back(static_cast<uint8_t>(handles.size()));
				for (const Handle& h : handles)
				{
					putHandle(list, h);
					list.insert(list.end(), {0, 0});
				}
				send(c, Kind::Broadcast, IDHNPAIRS, pid(), std::move(list));
				break;
			}
			case CONNCHECK:
				c.alive = true;
				break;
			default:
				break; // POSTGAMEINFO and others: nothing to do
			}
			return;
		}

		if (q.kind == static_cast<uint8_t>(Kind::Broadcast))
		{
			if (q.cmd == STARTGAME && c.host)
				getReady(c.area, c.room, c.slot);
			else if (q.cmd == CHATIN)
			{
				const auto text = readScrambledString(p, 0, key, nullptr, kMaxChat);
				if (!text)
					return; // over the cap: not passed on
				std::vector<uint8_t> out;
				putHandle(out, c.handle);
				putString(out, text.value_or(""));
				out.insert(out.end(), {0, 0, 0, 0, 0xFF});
				const int area = c.area, room = c.room, slot = c.slot;
				broadcast([&](const Client& o) { return o.area == area && o.room == room && o.slot == slot; }, CHATOUT, out);
			}
			return;
		}

		// Questions: every one gets an answer with its packet id.
		switch (q.cmd)
		{
		case UNK61A0: answer(c, q, {0, 0, 0x07, 0x08, 0, 0, 0x02, 0x58}); break;
		case CHECKRND:
		{
			const uint8_t first = p.size() > 4 ? static_cast<uint8_t>(p[4] ^ scrambleKey(0, key)) : 0;
			answer(c, q, {0, 1, first});
			break;
		}
		case UNK61A1: answer(c, q, {0, 0, 0x03, 0x84, 0, 0, 0x07, 0x08, 0, 0}); break;
		case HNSELECT:
		{
			size_t next = 0;
			const auto id = readScrambledString(p, 0, key, &next, kMaxHandle);
			const auto nick = id ? readScrambledString(p, next, key, nullptr, kMaxNickname) : std::nullopt;
			const auto h = (id && nick) ? accounts->selectHandle(c.account, *id, *nick) : std::nullopt;
			if (!h)
			{
				answerError(c, q, "<LF=6><BODY><CENTER>not possible<END>");
				break;
			}
			c.handle = *h;
			std::vector<uint8_t> a;
			putString(a, h->id);
			answer(c, q, a);
			if (c.game > 0)
				send(c, Kind::Query, POSTGAMEINFO, pid(), {});
			send(c, Kind::Broadcast, ENDLOGIN, pid(), {});
			log(c.address + ": handle " + h->id);
			break;
		}
		case MOTD:
		{
			std::vector<uint8_t> a = {static_cast<uint8_t>(config.messageOfTheDay.empty() ? 0 : 1)};
			putString(a, config.messageOfTheDay);
			answer(c, q, a);
			break;
		}
		case CHARSELECT:
			if (p.size() >= 4 + kStatsSize)
			{
				c.stats.assign(p.begin() + 4, p.begin() + 4 + kStatsSize);
				scramble(c.stats.data(), c.stats.size(), key);
			}
			answer(c, q);
			break;
		// The login data file (datafile.h): its first piece is the room rules' scenario list (report entries 42-44).
		case INFOSIZES: answer(c, q, infoSizesAnswer(dataFile)); break;
		case INFOCHUNK:
		{
			std::string raw;
			for (size_t i = 0; i < std::min<size_t>(p.size(), 12); i++)
			{
				char t[4];
				std::snprintf(t, sizeof(t), "%02X", p[i]);
				raw += t;
			}
			log(c.address + ": data file request " + raw + " (" + std::to_string(p.size()) + " bytes)");
			answer(c, q, infoChunkAnswer(dataFile, p));
			break;
		}
		case RANKINGS:
		{
			std::vector<uint8_t> a(207, 0); // the reference sends test values; layout unknown (spec section 7)
			a[0] = byte(1);
			answer(c, q, a);
			break;
		}
		case AREACOUNT: { std::vector<uint8_t> a; putU16(a, kAreas); answer(c, q, a); break; }
		case AREANAME:
		case AREADESCRIPT:
		{
			std::vector<uint8_t> a;
			putU16(a, static_cast<uint16_t>(nr));
			const int i = std::clamp(nr, 1, kAreas) - 1;
			putString(a, q.cmd == AREANAME ? kAreaNames[i] : kAreaDescriptions[i]);
			answer(c, q, a);
			break;
		}
		case AREASTATUS: { std::vector<uint8_t> a; putU16(a, static_cast<uint16_t>(nr)); a.push_back(nr >= 1 && nr <= kAreas ? 3 : 0); answer(c, q, a); break; }
		case AREAPLAYERCNT: answer(c, q, areaPlayerCount(nr)); break;
		case AREASELECT:
		{
			if (nr >= 1 && nr <= kAreas)
				c.area = nr, c.room = 0, c.slot = 0;
			std::vector<uint8_t> a;
			putU16(a, static_cast<uint16_t>(nr));
			answer(c, q, a);
			broadcast([&](const Client& o) { return o.area == 0 || (o.area == nr && o.room == 0); }, AREAPLAYERCNT, areaPlayerCount(nr));
			break;
		}
		case EXITAREA: c.area = 0, c.room = 0; answer(c, q); break;
		case ROOMSCOUNT: { std::vector<uint8_t> a; putU16(a, kRooms); answer(c, q, a); break; }
		case ROOMNAME:
		{
			std::vector<uint8_t> a;
			putU16(a, static_cast<uint16_t>(nr));
			putString(a, nr == 10 ? std::string("RA") : "R" + std::to_string(nr));
			answer(c, q, a);
			break;
		}
		case ROOMSTATUS: { std::vector<uint8_t> a; putU16(a, static_cast<uint16_t>(nr)); a.push_back(3); answer(c, q, a); break; }
		case ROOMPLAYERCNT: answer(c, q, roomPlayerCount(c.area, nr)); break;
		case UNK6308: answer(c, q, {byte(0), byte(1), 0, 2, 0x81, 0x40}); break;
		case ENTERROOM:
		{
			if (nr >= 1 && nr <= kRooms)
				c.room = nr, c.slot = 0;
			std::vector<uint8_t> a;
			putU16(a, static_cast<uint16_t>(nr));
			answer(c, q, a);
			const int area = c.area;
			broadcast([&](const Client& o) { return o.area == area && o.room == 0; }, ROOMPLAYERCNT, roomPlayerCount(area, nr));
			break;
		}
		case EXITSLOTLIST:
		{
			const int area = c.area, room = c.room;
			leaveSlot(c);
			c.room = 0;
			answer(c, q);
			broadcast([&](const Client& o) { return o.area == area && o.room == 0; }, ROOMPLAYERCNT, roomPlayerCount(area, room));
			break;
		}
		case SLOTCOUNT: { std::vector<uint8_t> a; putU16(a, kSlots); answer(c, q, a); break; }
		case SLOTTITLE: answer(c, q, slotTitle(c.area, c.room, nr)); break;
		case SLOTSTATUS: answer(c, q, slotStatus(c.area, c.room, nr)); break;
		case SLOTPLRSTATUS: answer(c, q, slotPlayerStatus(c.area, c.room, nr)); break;
		case SLOTATTRIB2: answer(c, q, slotAttrib2(c.area, c.room, nr)); break;
		case SLOTPWDPROT: answer(c, q, slotPassword(c.area, c.room, nr)); break;
		case SLOTSCENTYPE: answer(c, q, slotScene(c.area, c.room, nr)); break;
		case PLAYERSTATS:
		{
			std::vector<uint8_t> a;
			putU16(a, static_cast<uint16_t>(nr));
			std::vector<const Client*> in;
			for (const Client& o : clients)
				if (o.loggedIn && !o.closing && o.area == c.area && o.room == c.room && o.slot == nr)
					in.push_back(&o);
			a.push_back(3);
			a.push_back(static_cast<uint8_t>(in.size()));
			for (const Client* o : in)
			{
				putHandle(a, o->handle);
				putStats(a, *o);
			}
			answer(c, q, a);
			break;
		}
		case RULESCOUNT: answer(c, q, {4}); break;
		case RULEATTCOUNT: answer(c, q, {byte(2), static_cast<uint8_t>(kRules[std::min<int>(byte(2), 3)].choices.size())}); break;
		case RULEDESCRIPT: { std::vector<uint8_t> a = {byte(2)}; putString(a, kRules[std::min<int>(byte(2), 3)].name); answer(c, q, a); break; }
		case RULEVALUE:
		{
			const Slot* s = slotOf(c.area, c.room, nr);
			answer(c, q, {byte(2), s ? s->rules[std::min<int>(byte(2), 3)] : uint8_t(0)});
			break;
		}
		case RULEATTRIB: answer(c, q, {byte(2), 1}); break;
		case ATTRDESCRIPT:
		{
			const Rule& r = kRules[std::min<int>(byte(2), 3)];
			std::vector<uint8_t> a = {byte(2), byte(3)};
			putString(a, byte(3) < r.choices.size() ? r.choices[byte(3)] : "");
			answer(c, q, a);
			break;
		}
		case ATTRATTRIB: answer(c, q, {byte(2), byte(3), 0}); break;
		case UNK6601:
		case UNK6602: { std::vector<uint8_t> a = {1}; putU16(a, static_cast<uint16_t>(nr)); answer(c, q, a); break; }
		case CREATESLOT:
		{
			Slot* s = slotOf(c.area, c.room, nr);
			if (!s || s->status != 1)
			{
				answerError(c, q, "<LF=6><BODY><CENTER>not possible<END>");
				break;
			}
			leaveSlot(c);
			c.slot = nr;
			c.host = true;
			c.playerNumber = 1;
			s->status = 2;
			s->host = c.account;
			answer(c, q, {0, static_cast<uint8_t>(nr)});
			broadcastSlot(c.area, c.room, nr);
			log(c.address + ": creating game slot " + std::to_string(nr));
			break;
		}
		case SCENESELECT:
		{
			if (Slot* s = slotOf(c.area, c.room, c.slot))
				s->type = byte(1), s->scenario = byte(3);
			answer(c, q, {0, static_cast<uint8_t>(c.slot), 0, byte(1), 0, byte(3)});
			log(c.address + ": scenario type " + std::to_string(byte(1)) + ", scenario " + std::to_string(byte(3)) +
				" (slot " + std::to_string(c.slot) + ")");
			break;
		}
		case SLOTNAME:
		{
			if (Slot* s = slotOf(c.area, c.room, c.slot))
				s->name = readScrambledString(p, 0, key, nullptr, kMaxSlotName).value_or(s->name);
			answer(c, q);
			const int area = c.area, room = c.room, slot = c.slot;
			broadcast([&](const Client& o) { return o.area == area && o.room == room; }, SLOTTITLE, slotTitle(area, room, slot));
			break;
		}
		case SLOTPASSWD:
			if (Slot* s = slotOf(c.area, c.room, c.slot))
				s->password = readScrambledString(p, 0, key, nullptr, kMaxPassword).value_or("");
			answer(c, q);
			break;
		case SETRULE:
			if (Slot* s = slotOf(c.area, c.room, c.slot); s && byte(0) < 4)
				s->rules[byte(0)] = byte(1);
			answer(c, q, {1});
			break;
		case UNK660C: answer(c, q); break;
		case UNK6412: answer(c, q, {0, byte(1), 0, 0, 0, 0}); break;
		case UNK6504:
		{
			answer(c, q, {byte(0)});
			Slot* s = slotOf(c.area, c.room, c.slot);
			if (c.host && s)
			{
				s->status = 3; // open to join
				s->deadline = Clock::now() + std::chrono::minutes(kWaitMinutes[std::min<int>(s->rules[1], 4)]);
				const int area = c.area, room = c.room, slot = c.slot;
				auto inRoom = [&](const Client& o) { return o.area == area && o.room == room; };
				broadcastSlot(area, room, slot);
				broadcast(inRoom, SLOTPWDPROT, slotPassword(area, room, slot));
				broadcast(inRoom, SLOTSCENTYPE, slotScene(area, room, slot));
				broadcast([&](const Client& o) { return o.area == area && o.room == room && o.slot == slot; }, PLAYEROK,
					{0, static_cast<uint8_t>(c.playerNumber), 0, 0});
				log(c.address + ": opened game slot " + std::to_string(slot) + " '" + s->name + "'");
			}
			break;
		}
		case SLOTTIMER:
		{
			const Slot* s = slotOf(c.area, c.room, c.slot);
			int seconds = 0;
			if (s && s->status == 3)
				seconds = std::max<int>(0, static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
												s->deadline - Clock::now()).count()));
			std::vector<uint8_t> a = {0, static_cast<uint8_t>(c.slot)};
			putU16(a, static_cast<uint16_t>(seconds));
			answer(c, q, a);
			if (s && s->status == 3 && seconds == 0)
				getReady(c.area, c.room, c.slot);
			break;
		}
		case JOINGAME:
		{
			Slot* s = slotOf(c.area, c.room, nr);
			std::string password;
			if (p.size() >= 6)
			{
				const size_t length = getU16(p, 2);
				if (length >= 2 && 6 + length - 2 <= p.size())
				{
					password.assign(p.begin() + 6, p.begin() + 6 + static_cast<std::ptrdiff_t>(length - 2));
					scramble(reinterpret_cast<uint8_t*>(password.data()), password.size(), key);
				}
			}
			if (!s || s->status == 4)
				answerError(c, q, "<LF=6><BODY><CENTER>game is full<END>");
			else if (s->status != 3)
				answerError(c, q, "<LF=6><BODY><CENTER>not possible<END>");
			else if (!s->password.empty() && password != s->password)
				answerError(c, q, "<LF=6><BODY><CENTER>wrong password<END>");
			else
			{
				leaveSlot(c);
				bool used[5] = {};
				for (const Client& o : clients)
					if (o.loggedIn && o.area == c.area && o.room == c.room && o.slot == nr)
						used[std::clamp(o.playerNumber, 0, 4)] = true;
				int number = 2;
				while (number <= 4 && used[number])
					number++;
				c.slot = nr;
				c.playerNumber = number;
				if (playersIn(c.area, c.room, nr) >= s->maxPlayers())
					s->status = 4;
				broadcastSlot(c.area, c.room, nr);
				std::vector<uint8_t> joined;
				putHandle(joined, c.handle);
				putStats(joined, c);
				const int area = c.area, room = c.room;
				for (Client& o : clients) // to the slot, with the joiner's packet id
					if (o.loggedIn && !o.closing && &o != &c && o.area == area && o.room == room && o.slot == nr)
						send(o, Kind::Broadcast, PLAYERSTATBC, q.pid, joined);
				answer(c, q, {0, static_cast<uint8_t>(nr)});
				log(c.address + ": joined game slot " + std::to_string(nr) + " as player " + std::to_string(number));
			}
			break;
		}
		case CANCELSLOT: leaveSlot(c); answer(c, q); break;
		case PLAYERCOUNT: answer(c, q, {static_cast<uint8_t>(playersIn(c.area, c.room, c.slot))}); break;
		case PLAYERNUMBER: answer(c, q, {static_cast<uint8_t>(c.playerNumber)}); break;
		case PLAYERSTAT:
		{
			const Client* who = nullptr;
			for (const Client& o : clients)
				if (o.loggedIn && !o.closing && o.area == c.area && o.room == c.room && o.slot == c.slot &&
					o.playerNumber == byte(0))
					who = &o;
			answer(c, q, who ? preGameStat(*who) : std::vector<uint8_t>{byte(0), 0});
			break;
		}
		case GAMEDIFF:
		{
			const Slot* s = slotOf(c.area, c.room, c.slot);
			std::vector<uint8_t> a = {0, 0x10, 1, s ? s->rules[2] : uint8_t(1), s ? s->rules[3] : uint8_t(0)};
			a.resize(18, 0);
			answer(c, q, a);
			break;
		}
		case GAMESESSION:
		{
			char digits[16];
			std::snprintf(digits, sizeof(digits), "%015u", c.game);
			std::vector<uint8_t> a = {0, 0x0F};
			a.insert(a.end(), digits, digits + 15);
			a.insert(a.end(), {0, 0});
			answer(c, q, a);
			break;
		}
		case GSINFO:
		{
			std::vector<uint8_t> a = {0, 4};
			a.insert(a.end(), config.relayAddress.begin(), config.relayAddress.end());
			a.insert(a.end(), {0, 2});
			putU16(a, config.relayPort);
			a.insert(a.end(), {0, 0, 0x1E, 0});
			answer(c, q, a);
			break;
		}
		case PLAYERSCORE:
		{
			const Slot* s = slotOf(c.area, c.room, c.slot);
			std::vector<uint8_t> a = {byte(0)};
			putU16(a, s ? s->scenario : 0);
			a.resize(23, 0);
			answer(c, q, a);
			break;
		}
		case LEAVELOBBY:
		{
			answer(c, q);
			const int area = c.area, room = c.room, slot = c.slot;
			c.area = 0, c.room = 0, c.slot = 0, c.playerNumber = 0, c.host = false;
			if (Slot* s = slotOf(area, room, slot); s && playersIn(area, room, slot) == 0)
			{
				s->reset();
				broadcastSlot(area, room, slot);
			}
			break;
		}
		case EVENTDAT:
		{
			// A message to one player by handle (the host's game sends one to a player who has just joined; the
			// joiner waits for it, report entry 51): two scrambled strings, the recipient's handle and the event.
			// Answered with the recipient's handle; passed on as EVENTDATBC [sender's handle][event], both decoded, to
			// that player only (Bioserver1 Packet.getEvenData + sendEventDat).
			size_t next = 0;
			const auto to = readScrambledString(p, 0, key, &next, kMaxHandle);
			const auto event = to ? readScrambledString(p, next, key, nullptr, kMaxEvent) : std::nullopt;
			if (!to || !event)
			{
				answerError(c, q, "<LF=6><BODY><CENTER>not possible<END>");
				break;
			}
			std::vector<uint8_t> a;
			putString(a, *to);
			answer(c, q, a);
			std::vector<uint8_t> out;
			putString(out, c.handle.id);
			putString(out, *event);
			for (Client& o : clients)
				if (o.loggedIn && !o.closing && &o != &c && o.handle.id == *to)
					send(o, Kind::Broadcast, EVENTDATBC, pid(), out);
			break;
		}
		case LOGOUT:
			accounts->setGameNumber(c.account, 0);
			answer(c, q);
			dropClient(c);
			break;
		default:
			log(c.address + ": unhandled question 0x" + [&] {
				char t[8];
				std::snprintf(t, sizeof(t), "%04X", q.cmd);
				return std::string(t);
			}() + " (" + std::to_string(p.size()) + " bytes): answered empty");
			answer(c, q);
			break;
		}
	}

	void LobbyServer::Impl::tick()
	{
		const auto now = Clock::now();
		if (now >= nextHeartbeat)
		{
			nextHeartbeat = now + std::chrono::seconds(config.heartbeatSeconds);
			broadcast([](const Client&) { return true; }, HEARTBEAT, {0, 2, 0, 1, 0x03, 0xE7, 0, 1});
		}
		if (now >= nextConnCheck)
		{
			nextConnCheck = now + std::chrono::seconds(2 * config.heartbeatSeconds);
			for (Client& c : clients)
			{
				if (!c.loggedIn || c.closing || c.area == kAfterGameArea)
					continue;
				if (!c.alive)
				{
					log(c.address + ": no answer to the connection check, dropped");
					dropClient(c);
					continue;
				}
				c.alive = false;
				send(c, Kind::Query, CONNCHECK, pid(), {});
			}
		}
	}

	LobbyServer::LobbyServer() = default;
	LobbyServer::~LobbyServer() { stop(); }

	bool LobbyServer::start(Accounts& accounts, const LobbyConfig& config, const std::string& address, uint16_t port,
		std::string* error)
	{
		std::string localError;
		if (!error)
			error = &localError;
		stop();
		m_impl = std::make_unique<Impl>();
		m_impl->accounts = &accounts;
		m_impl->config = config;
		m_impl->guard.setLimits(config.limits);
		m_impl->owner = this;
		m_impl->listener = net::listenTcp(address, port, &m_port, error);
		if (m_impl->listener == net::kInvalid)
		{
			m_impl.reset();
			return false;
		}
		m_impl->nextHeartbeat = Clock::now() + std::chrono::seconds(config.heartbeatSeconds);
		m_impl->nextConnCheck = Clock::now() + std::chrono::seconds(2 * config.heartbeatSeconds);
		m_running = true;
		m_thread = std::thread([this] {
			Impl& s = *m_impl;
			std::vector<uint8_t> buffer(8192);
			while (m_running)
			{
				std::vector<net::PollEntry> entries;
				entries.push_back({s.listener});
				for (Client& c : s.clients)
					entries.push_back({c.socket, !c.out.empty()});
				net::poll(entries, 100);
				if (entries[0].readable)
				{
					std::string peer;
					for (net::Socket a; (a = net::acceptTcp(s.listener, &peer)) != net::kInvalid;)
					{
						// Local network only, and capped (guard.h): anything else is closed before a byte is read.
						if (!s.guard.admit(peer))
						{
							net::closeSocket(a);
							if (s.config.log)
								s.config.log("[lobby] " + peer + ": refused (not local, removed, or over the limit)");
							continue;
						}
						Client& c = s.clients.emplace_back();
						c.socket = a;
						c.address = peer;
						// LOGIN question: 28 37 (the fan server's constant; the game answers with the session digits)
						s.send(c, Kind::Query, LOGIN, s.pid(), {0x28, 0x37});
					}
				}
				size_t i = 1;
				for (Client& c : s.clients)
				{
					if (i >= entries.size())
						break;
					const net::PollEntry& e = entries[i++];
					if (e.failed)
						s.dropClient(c);
					if (c.closing && e.readable)
						net::receiveSome(c.socket, buffer.data(), buffer.size()); // discarded: no busy poll on unread input
					if (!c.closing && e.readable)
					{
						const long n = net::receiveSome(c.socket, buffer.data(), buffer.size());
						if (n < 0)
							s.dropClient(c);
						else if (n > 0)
						{
							c.in.feed(buffer.data(), static_cast<size_t>(n));
							while (auto packet = c.in.next())
								s.handle(c, *packet);
							if (c.in.overflowed())
								s.dropClient(c);
						}
					}
				}
				s.tick();
				const auto loginDeadline = Clock::now() - std::chrono::seconds(s.config.loginSeconds);
				for (Client& c : s.clients)
					if (!c.loggedIn && !c.closing && c.connectedAt < loginDeadline)
					{
						s.log(c.address + ": no login in time, disconnected");
						s.dropClient(c);
						c.out.clear();
					}
				for (Client& c : s.clients)
					while (!c.out.empty())
					{
						const long n = net::sendSome(c.socket, c.out.data(), c.out.size());
						if (n < 0)
						{
							s.dropClient(c);
							c.out.clear();
						}
						if (n <= 0)
							break;
						c.out.erase(c.out.begin(), c.out.begin() + n);
					}
				{
					std::lock_guard<std::mutex> lock(s.sharedMutex);
					for (const std::string& gone : s.pendingRemove)
						for (Client& c : s.clients)
							if (c.address == gone && !c.closing)
							{
								s.dropClient(c);
								c.out.clear();
							}
					s.pendingRemove.clear();
				}
				const auto graceOver = Clock::now() - std::chrono::milliseconds(s.config.closeGraceMs);
				for (auto it = s.clients.begin(); it != s.clients.end();)
					if (it->closing && (it->out.empty() || it->closingSince < graceOver))
					{
						net::closeSocket(it->socket);
						s.guard.release(it->address);
						it = s.clients.erase(it);
					}
					else
						++it;
				{
					std::vector<std::string> now;
					for (const Client& c : s.clients)
						if (!c.closing && std::find(now.begin(), now.end(), c.address) == now.end())
							now.push_back(c.address);
					std::lock_guard<std::mutex> lock(s.sharedMutex);
					s.sharedAddresses = std::move(now);
				}
			}
			for (Client& c : s.clients)
			{
				net::closeSocket(c.socket);
				s.guard.release(c.address);
			}
			net::closeSocket(s.listener);
			std::lock_guard<std::mutex> lock(s.sharedMutex);
			s.sharedAddresses.clear();
		});
		return true;
	}

	std::vector<std::string> LobbyServer::connectedAddresses() const
	{
		if (!m_impl) // not running (security queue S9)
			return {};
		std::lock_guard<std::mutex> lock(m_impl->sharedMutex);
		return m_impl->sharedAddresses;
	}

	void LobbyServer::removeAddress(const std::string& address)
	{
		if (!m_impl)
			return;
		m_impl->guard.remove(address);
		std::lock_guard<std::mutex> lock(m_impl->sharedMutex);
		m_impl->pendingRemove.push_back(address);
	}

	void LobbyServer::stop()
	{
		if (m_running.exchange(false) && m_thread.joinable())
			m_thread.join();
		m_impl.reset();
	}
}
