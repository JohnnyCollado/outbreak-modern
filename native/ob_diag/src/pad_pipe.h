#ifndef OB_DIAG_PAD_PIPE_H
#define OB_DIAG_PAD_PIPE_H

// Named pipe server for the live mock controller: \\.\pipe\<name>, local clients only
// (PIPE_REJECT_REMOTE_CLIENTS), access for the current user only (protected DACL with the user's SID), one
// instance = one client at a time. Each received line goes to ObPadControl::execute; its reply is written back
// with a '\n'. Windows only: elsewhere start() fails.

#include <string>

class ObPadControl;

// Creates the pipe (on the calling thread, so a name already in use fails here) and starts the server thread,
// which lives until the process exits. False with a message on failure.
bool obStartPadPipe(const std::string &name, ObPadControl &control, std::string &error);

#endif
