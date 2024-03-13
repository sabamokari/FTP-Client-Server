#ifndef CLIENT_H_
#define CLIENT_H_

#include <arpa/inet.h>
#include <netdb.h>
#include <string>
#include <vector>

#include "netutil.h"

using namespace netutil;

namespace client {
class Client {
    public:
        Client(char *, char *, char *);
        ~Client();
        // TODO Destructor - Destroy all lingering threads
    private:
        int sockfd, tsockfd;
        char *port, *tport;
        struct addrinfo *server, *terminator, hints;

        struct Thread {
            std::string command;
            std::string file;
            Command_Code command_type;
            int command_id;
        } thread_data;

        std::vector<int> pids;

        static void *thread_functions(void *);
        static void *prompt_user(void *);

        void handle_command(const std::string &, int);
        void setup_conn(const char *, const char *, struct addrinfo *&, int &);
        void thread_launcher();
    };
}

#endif
