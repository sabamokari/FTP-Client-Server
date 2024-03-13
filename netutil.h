#ifndef NETUTIL_H_
#define NETUTIL_H_

#include <string>
#include <fstream>
#include <vector>
#include <queue>
#include <unistd.h>
#include <sys/types.h>

const int NET_BUF_SIZE = 1024;

namespace netutil {
    enum Message {
        SUCCESS,                    // Command ran successfully
        COMMAND,                    // Let endpoint know this is a command
        TERMINATE,                  // Let endpoint know this means to terminate something
        PIDINFO,                    // Let endpoint know this is info for pid
        GENERR,                     // General Error - "catch all error"
        TERMERR,                    // Termination Error
    };

    struct Content {
        int fd;                     // Socket Descriptor to communicate with
        int tfd;                    // Term Socket Descriptor to communicate with
        std::string data;           // Data contained within the message
        size_t length;              // Total length of the message
        Message msg;                // This is how we pass messages from the host to client
        int pid;                    // Stores job pid
    };

    enum Command_Code {
        CC_GET,
        CC_PUT,
        CC_DEL,
        CC_LS,
        CC_CD,
        CC_PWD,
        CC_MKDIR,
    };

    // Vector containing termination targets
    static std::vector<pid_t> term_targets;

    // UTILITIES
    Command_Code find_code(const std::string &);
    std::vector<std::string> split_string(const std::string &);

    // NETWORKING
    int send_file(int, std::ifstream &, pid_t pid = NULL);

    // This is solely for backwards compatability
    int send_data(int, const std::string &);

    // SUCCESS is the default message
    int send_data(int, const std::string &, int, Message msg = SUCCESS);
    int receive_file(int, std::ofstream &, pid_t pid = NULL);
    int receive_data(int, Content &);
}

#endif
