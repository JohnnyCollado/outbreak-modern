// Link-only stand-ins for the network adapters PCSX2's net.cpp names but this build leaves out: PCAP (pcap_io.cpp,
// needs Npcap/libpcap) and, on Windows, TAP (Win32/tap-win32.cpp, needs a TAP driver). Only the Sockets adapter is used;
// these never initialise, so GetNetAdapter() deletes them and reports no adapter if ever asked for one.
#include "Config.h"
#include "DEV9/pcap_io.h"
#ifdef _WIN32
#include "DEV9/Win32/tap.h"
#endif

// EmuFolders::Settings: where a relative HDD image path would be found (DEV9.cpp GetHDDPath); no HDD here.
std::string EmuFolders::Settings;

PCAPAdapter::PCAPAdapter() {}
bool PCAPAdapter::blocks() { return false; }
bool PCAPAdapter::isInitialised() { return false; }
bool PCAPAdapter::recv(NetPacket*) { return false; }
bool PCAPAdapter::send(NetPacket*) { return false; }
void PCAPAdapter::reloadSettings() {}
PCAPAdapter::~PCAPAdapter() {}
std::vector<AdapterEntry> PCAPAdapter::GetAdapters() { return {}; }
AdapterOptions PCAPAdapter::GetAdapterOptions() { return AdapterOptions::None; }

#ifdef _WIN32
TAPAdapter::TAPAdapter() {}
bool TAPAdapter::blocks() { return false; }
bool TAPAdapter::isInitialised() { return false; }
bool TAPAdapter::recv(NetPacket*) { return false; }
bool TAPAdapter::send(NetPacket*) { return false; }
void TAPAdapter::reloadSettings() {}
void TAPAdapter::close() {}
TAPAdapter::~TAPAdapter() {}
std::vector<AdapterEntry> TAPAdapter::GetAdapters() { return {}; }
AdapterOptions TAPAdapter::GetAdapterOptions() { return AdapterOptions::None; }
#endif
