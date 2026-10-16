#pragma once
#include <cstring>
namespace sticks3 {
// Public observation never grants role/provisioning or transmit permission.
// Match complete commands: role mutations and suffixes fall through to the
// existing unprovisioned guard. Handlers are bounded, on-demand status only.
template<class Status, class Peers, class Mesh>
bool read_only_query(const char* command, Status status, Peers peers, Mesh mesh) {
 if(!std::strcmp(command,"gm status")){status();return true;}
 if(!std::strcmp(command,"gm peers")){peers();return true;}
 if(!std::strcmp(command,"mesh status")){mesh();return true;}
 return false;
}
}
