// Run by tools/crash/capture-test.sh: starts the release's crash capture, prints, then ends as asked (report entry 71).
#include "ob_ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
	const char* how = argc > 1 ? argv[1] : "exit";
	obuiStartCrashCapture();
	std::printf("printed on stdout\n");
	std::cerr << "printed on cerr" << std::endl;
	std::set_terminate([] {
		std::cerr << "[terminate] unhandled exception" << std::endl;
		std::abort();
	});
	if (std::strcmp(how, "report") == 0)
	{
		std::string savedTo, error;
		const bool ok = obuiSaveCrashReport(savedTo, error);
		std::fprintf(stderr, "report %d %s%s\n", ok ? 1 : 0, savedTo.c_str(), error.c_str());
	}
	else if (std::strcmp(how, "abort") == 0)
		std::abort();
	else if (std::strcmp(how, "throw") == 0)
		throw std::runtime_error("boom");
	else if (std::strcmp(how, "fault") == 0)
		*static_cast<volatile int*>(nullptr) = 1;
	return 0;
}
