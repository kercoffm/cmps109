// $Id: cix-server.cpp,v 1.7 2014-07-25 12:12:51-07 - - $

#include <iostream>
#include <sstream>
#include <fstream>
using namespace std;

#include <libgen.h>

#include "cix_protocol.h"
#include "logstream.h"
#include "signal_action.h"
#include "sockets.h"
#include "util.h"

logstream log (cout);

void reply_get(accepted_socket& client_sock, cix_header& header) {
    // collect characters into buffer with ifstream::read(),
    // close buffer, and send down socket
    ifstream fs(header.cix_filename);
    stringstream sstr;
    if (not fs.good()) {
        log << "get: " << header.cix_filename << ": no such file" 
            << endl;
        header.cix_command = CIX_NAK;
        send_packet (client_sock, &header, sizeof header);
    }
    sstr << fs.rdbuf();
    string data = sstr.str();
    header.cix_command = CIX_FILE;
    header.cix_nbytes = data.size();
    memset(header.cix_filename, 0, CIX_FILENAME_SIZE);
    log << "sending header " << header << endl;
    send_packet (client_sock, &header, sizeof header);
    send_packet (client_sock, data.c_str(), data.size());
    log << "sent " << data.size() << " bytes" << endl;
}

void reply_ls (accepted_socket& client_sock, cix_header& header) {
    FILE* ls_pipe = popen ("ls -l", "r");
    if (ls_pipe == NULL) {
        log << "ls -l: popen failed: " << strerror (errno) << endl;
        header.cix_command = CIX_NAK;
        header.cix_nbytes = errno;
        send_packet (client_sock, &header, sizeof header);
        return;
    }
    string ls_output;
    char buffer[0x1000];
    for (;;) {
        char* rc = fgets (buffer, sizeof buffer, ls_pipe);
        if (rc == nullptr) break;
        ls_output.append (buffer);
    }
    header.cix_command = CIX_LSOUT;
    header.cix_nbytes = ls_output.size();
    memset (header.cix_filename, 0, CIX_FILENAME_SIZE);
    log << "sending header " << header << endl;
    send_packet (client_sock, &header, sizeof header);
    send_packet (client_sock, ls_output.c_str(), ls_output.size());
    log << "sent " << ls_output.size() << " bytes" << endl;
}

void reply_put(accepted_socket& client_sock, cix_header& header) {
    // Figure out size of file, write parts of file as received 
    // from the socket with ostream::write().
    ofstream fs(header.cix_filename);
    string put_output;
    if (not fs.good()) {
        log << "put: cannot open file for writing";
        header.cix_command = CIX_NAK;
        send_packet (client_sock, &header, sizeof header);
        return;
    }
    char buffer[header.cix_nbytes + 1];
    recv_packet (client_sock, buffer, header.cix_nbytes);
    log << "received " << header.cix_nbytes << " bytes" << endl;
    fs.write(buffer, header.cix_nbytes);
    fs.close();
    header.cix_command = CIX_ACK;
    header.cix_nbytes = 0;
    log << "sending header " << header << endl;
    send_packet (client_sock, &header, sizeof header);
}

void reply_rm(accepted_socket& client_sock, cix_header& header) {
    // Delete file with unlink(2).
    int retval = unlink(header.cix_filename);
    if (retval != 0) {
        log << "rm: unlink failed: " << strerror (errno) << endl;
        header.cix_command = CIX_NAK;
        header.cix_nbytes = errno;
        send_packet (client_sock, &header, sizeof header);
        return;
    }
    header.cix_command = CIX_ACK;
    header.cix_nbytes = 0;
    send_packet (client_sock, &header, sizeof header);
}


bool SIGINT_throw_cix_exit = false;
void signal_handler (int signal) {
    log << "signal_handler: caught " << strsignal (signal) << endl;
    switch (signal) {
        case SIGINT: case SIGTERM: SIGINT_throw_cix_exit = true; break;
        default: break;
    }
}

int main (int argc, char** argv) {
    log.execname (basename (argv[0]));
    log << "starting" << endl;
    vector<string> args (&argv[1], &argv[argc]);
    signal_action (SIGINT, signal_handler);
    signal_action (SIGTERM, signal_handler);
    int client_fd = args.size() == 0 ? -1 : stoi (args[0]);
    log << "starting client_fd " << client_fd << endl;
    try {
        accepted_socket client_sock (client_fd);
        log << "connected to " << to_string (client_sock) << endl;
        for (;;) {
            if (SIGINT_throw_cix_exit) throw cix_exit();
            cix_header header;
            recv_packet (client_sock, &header, sizeof header);
            log << "received header " << header << endl;
            switch (header.cix_command) {
                case CIX_GET:
                    reply_get (client_sock, header);
                    break;
                case CIX_LS:
                    reply_ls (client_sock, header);
                    break;
                    // Insert code here
                case CIX_PUT:
                    reply_put (client_sock, header);
                    break;
                case CIX_RM:
                    reply_rm (client_sock, header);
                    break;
                default:
                    log << "invalid header from client" << endl;
                    log << "cix_nbytes = " 
                        << header.cix_nbytes << endl;
                    log << "cix_command = " 
                        << header.cix_command << endl;
                    log << "cix_filename = " 
                        << header.cix_filename << endl;
                    break;
            }
        }
    }catch (socket_error& error) {
        log << error.what() << endl;
    }catch (cix_exit& error) {
        log << "caught cix_exit" << endl;
    }
    log << "finishing" << endl;
    return 0;
}

