// SPDX-License-Identifier: AGPL-3.0-or-later
#include "web.h"

#include <cstdio>

namespace ob::server
{
	namespace
	{
		constexpr const char* kSitePath = "/00000002/";

		std::string siteUrl(const WebConfig& c, const char* page)
		{
			return "https://" + c.siteHost + kSitePath + page;
		}

		// The game-start page: the marker the game's browser looks for (CRS-GAME-START), the EUC-JP charset line, and
		// the CSV it reads: result, session number, start-game URL (the lobby list), exit URL. Every field quoted and
		// followed by a comma, the last one too.
		std::string gameStartPage(const WebConfig& c, uint32_t session)
		{
			char id[16];
			std::snprintf(id, sizeof(id), "%08u", session);
			return "<html><head><!--CRS-game-start--><META HTTP-EQUIV=Content-Type CONTENT=text/html;CHARSET=EUC-JP>"
				   "</head><!--result--><!--connection id--><!--start the game url--><!--exit game url-->"
				   "<!--<CSV>\"OK\",\"" + std::string(id) + "\",\"" + siteUrl(c, "enterareas.html") + "\",\"" +
				   siteUrl(c, "CRS-top.jsp") + "\",</CSV>--></html>";
		}

		// The lobby list (CRS-LBS-INFO-GET): "OK", then per lobby six fields: address:port, connections, maximum,
		// three info fields. One lobby: ours. Info 1 copies the reference's form "<code>,<NAME>,<n>" (meaning of the
		// code unknown, research section 2); info 2 and 3 are the game's own markup.
		std::string lobbyListPage(const WebConfig& c)
		{
			const std::string address = c.lobbyHost + ":" + std::to_string(c.lobbyPort);
			return "<html><head><!--CRS-lbs-info-get--><META HTTP-EQUIV=Content-Type CONTENT=text/html;CHARSET=UTF-8>"
				   "</head><!--Results--><!--LBS domain name + port number--><!--connection number-->"
				   "<!--Maximum number of connections--><!--Additional Information 1--><!--Additional Information 2-->"
				   "<!--Additional Information 3--><!--<CSV>\"OK\",\n\"" + address + "\",\n\"0\",\n\"999\",\n"
				   "\"0ad601082008,LOCAL,2\",\n\"<BODY><SIZE=4>LOCAL<END>\",\n\"<BODY><SIZE=4>LOCAL<END>\",\n</CSV>--></html>";
		}
	} // namespace

	WebResponse webRespond(const WebConfig& config, const std::string& path, const std::string& clientAddress)
	{
		const std::string page = path.substr(0, path.find('?'));
		WebResponse r;
		if (page == std::string(kSitePath) + "CRS-top.jsp")
		{
			// Quick login: straight on to the game-start page with a fresh session (meta refresh, as the reference's
			// own CRS-top.jsp does; 1 second as there).
			const uint32_t session = config.mintSession ? config.mintSession(clientAddress) : 0;
			char id[16];
			std::snprintf(id, sizeof(id), "%08u", session);
			r.body = "<html><head><!--CRS-top--><meta http-equiv=\"Refresh\" content=\"1; url=startsession.php?sessid=" +
					 std::string(id) + "\"></head></html>";
		}
		else if (page == std::string(kSitePath) + "startsession.php")
		{
			const size_t at = path.find("sessid=");
			uint32_t session = 0;
			if (at != std::string::npos)
				for (size_t i = at + 7; i < path.size() && path[i] >= '0' && path[i] <= '9' && i < at + 15; i++)
					session = session * 10 + static_cast<uint32_t>(path[i] - '0');
			r.body = gameStartPage(config, session);
		}
		else if (page == std::string(kSitePath) + "enterareas.html")
			r.body = lobbyListPage(config);
		else
		{
			r.status = 404;
			r.body = "<html><body>not found</body></html>";
		}
		return r;
	}

	std::string webSerialize(const WebResponse& response)
	{
		const char* reason = response.status == 200 ? "OK" : response.status == 404 ? "Not Found" : "Error";
		return "HTTP/1.0 " + std::to_string(response.status) + " " + reason + "\r\nContent-Type: " +
			   response.contentType + "\r\nContent-Length: " + std::to_string(response.body.size()) +
			   "\r\nConnection: close\r\n\r\n" + response.body;
	}

	std::string webRequestPath(const std::string& request)
	{
		const size_t lineEnd = request.find("\r\n");
		const std::string line = request.substr(0, lineEnd);
		const size_t a = line.find(' ');
		if (a == std::string::npos)
			return {};
		const size_t b = line.find(' ', a + 1);
		const std::string method = line.substr(0, a);
		if (method != "GET" && method != "POST")
			return {};
		std::string path = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
		// An absolute URL (a proxy-style request) keeps only its path.
		if (path.rfind("http", 0) == 0)
		{
			const size_t slash = path.find('/', path.find("://") + 3);
			path = slash == std::string::npos ? "/" : path.substr(slash);
		}
		return path;
	}
}
