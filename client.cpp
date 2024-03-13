#include <cstdio>
#include <iostream>
#include <netdb.h>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "client.h"
#include "netutil.h"

using namespace client;

Client::Client(char *host, char *port, char *tport) {
    this->port = port;
    this->tport = tport;

    setup_conn(host, port, this->server, this->sockfd);
    setup_conn(host, tport, this->terminator, this->tsockfd);

    this->thread_launcher();
}

Client::~Client() {
    if (this->server != NULL) {
        freeaddrinfo(server);
    }

    if (this->terminator != NULL) {
        freeaddrinfo(terminator);
    }
}

void Client::setup_conn(const char *host, const char *port, struct addrinfo *&addr, int &sockfd) {
    int rc;

    // Set hints to empty
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = IPPROTO_TCP;

    // Get address info
    rc = getaddrinfo(host, port, &hints, &addr);
    if (rc < 0) {
        fprintf(stderr, "Error getting address information %s\n", gai_strerror(rc));
        exit(1);
    }

    sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sockfd < 0) {
        perror("Failed to create TCP socket");
        exit(1);
    }

    rc = connect(sockfd, addr->ai_addr, addr->ai_addrlen);
    if (rc < 0) {
        perror("Could not set host");
        exit(1);
    }

    // No longer needed
    freeaddrinfo(addr);
}

void Client::handle_command(const std::string &command, int con) {
    std::vector<std::string> args, tmpdata;
    Content tmp;
    std::ifstream ifile;
    std::ofstream ofile;
    int fd, status;

    args = split_string(command);

    if (args[0] == "quit") {
        close(con);
        close(con);
        exit(0);
    }

    // Tell compiler to ignore "enumeration values not handled"
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
    switch(find_code(args[0])) {
        case CC_GET:
            ofile.open(args[1], std::ios::binary | std::ios::in | std::ios::trunc );

            status = receive_file(con, ofile);
            if (status < 0) {
                fprintf(stderr, "Error receiveing file %s\n", args[1].c_str());
                break;
            }

            ofile.close();
            break;
        case CC_PUT:
            fd = open(args[1].c_str(), O_RDONLY);
            if (fd < 0) {
                perror("Could not open file");
                break;
            }
            close(fd);

            ifile.open(args[1], std::ios::binary | std::ios::out);

            status = send_file(con, ifile);
            if (status < 0) {
                fprintf(stderr, "Error sending file %s\n", args[1].c_str());
                break;
            }

            ifile.close();
            break;
        case CC_DEL:
            // Do Nothing
            break;
        case CC_LS:
            receive_data(con, tmp);
            printf("%s\n", tmp.data.c_str());
            break;
        case CC_CD:
            // Do Nothing
            break;
        case CC_PWD:
            receive_data(con, tmp);
            printf("%s\n", tmp.data.c_str());
            break;
        case CC_MKDIR:
            // Do Nothing
            break;
        default:
            printf("Commands: \n get <file>, delete <file>, put <file>, ");
            printf("cd <dir>, ls, pwd, mkdir <dir, quit\n");
            break;
    }
}

void Client::thread_launcher() {
    pthread_t main;
    pthread_attr_t attr;

    // Default attributes
    pthread_attr_init(&attr);

    // Init main thread - Used to wait for commands from user
    pthread_create(&main, &attr, prompt_user, this);

    // Join main thread
    pthread_join(main, NULL);
}

void *Client::prompt_user(void *arg) {
    Client *client = (Client *) arg;
    int sockfd, length, status;
    std::string command;
    std::vector<std::string> parse_command, args, tmpdata;
    Content tmp;
    pid_t pid;
    pthread_t worker;
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    sockfd = client->sockfd;
    while (1) {
        printf("myftp> ");
        getline(std::cin, command);

        // Parse command to check for "&"
        parse_command = split_string(command);
        length = parse_command.size();

        if (parse_command[length - 1] == "&") {
            printf("Running command in background.\n");
            send_data(sockfd, command, command.length(), COMMAND);

            status = receive_data(client->sockfd, tmp);
            if (status < 0 || tmp.msg != PIDINFO) {
                fprintf(stderr, "Error receiving PID for file %s\n", args[1].c_str());
                break;
            }

            tmpdata = split_string(tmp.data);
            pid = std::stoi(tmpdata[0]);
            client->pids.push_back(pid);
            printf("PID: %d\n", pid);

            client->thread_data.command = command;
            client->thread_data.command_type = find_code(parse_command[0]);
            client->thread_data.command_id = pid;

            pthread_create(&worker, &attr, thread_functions, client);
            pthread_detach(worker);
        } else if (parse_command[0] == "terminate") {
            // Command given is terminate <pid>
            pid = stoi(parse_command[1]);
            status = send_data(client->tsockfd, command, command.length(), TERMINATE);

            // Add pid to termination targets
            term_targets.push_back(pid);
        } else {
            send_data(sockfd, command, command.length(), COMMAND);
            client->handle_command(command, sockfd);
        }
    }
}

void *Client::thread_functions(void *arg) {
    Client *client = (Client *) arg;
    struct Thread params = client->thread_data;
    std::vector<std::string> args, tmpdata;
    Content tmp;
    std::ifstream ifile;
    std::ofstream ofile;
    int fd, status;

    args = split_string(params.command);
    params.file = args[1];

    // Tell compiler to ignore "enumeration values not handled"
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
    switch(params.command_type) {
        case CC_GET:
            ofile.open(args[1], std::ios::binary | std::ios::in | std::ios::trunc);

            status = receive_file(client->sockfd, ofile, params.command_id);
            if (status < 0) {
                fprintf(stderr, "Error receiveing file %s\n", args[1].c_str());
                break;
            } else if (status == params.command_id) {
                // Handle premature termination
                fprintf(stdout, "Command terminated");
                ofile.close();
                std::remove(args[1].c_str());
                // client->pids.pop_back();
                break;
            }

            // client->pids.pop_back();
            ofile.close();
            break;
        case CC_PUT:
            fd = open(params.file.c_str(), O_RDONLY);
            if (fd < 0) {
                perror("Could not open file");
                break;
            }
            close(fd);

            ifile.open(args[1], std::ios::binary | std::ios::out);

            status = send_file(client->sockfd, ifile, params.command_id);
            if (status < 0) {
                fprintf(stderr, "Error sending file %s\n", args[1].c_str());
                break;
            } else if (status == params.command_id) {
                // Premature exit
                // client->pids.pop_back();
                ifile.close();
                break;
            }

            // Remove pid from back (last added)
            // client->pids.pop_back();
            ifile.close();
            break;
    }
}
