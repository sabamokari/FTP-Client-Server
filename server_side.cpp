#include "server.h"

using namespace server;

// This is the main class for the server
int main(int argc, char* argv[]) {
    int threadCt;
    char *port, *tport;

    if (argc < 2) {
        printf("./server [nport] [tport]\n");
        exit(1);
    }

    port = argv[1];
    tport = argv[2];
    threadCt = 2;

    Server *s = new Server(port, tport, threadCt);

    delete s;
    return 0;
}
