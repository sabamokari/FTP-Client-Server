#include "client.h"

using namespace client;

int main(int argc, char* argv[]) {
    char *host, *port, *tport;

    if (argc < 3) {
        printf("usage: ./client [server ip] [port] [tport]\n");
    }

    // host = "127.0.0.1";
    // port = argv[1];
    // tport = argv[2];

    host = argv[1];
    port = argv[2];
    tport = argv[3];

    Client *client = new Client(host, port, tport);

    delete client;
    return 0;
}
