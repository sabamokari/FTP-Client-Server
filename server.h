#ifndef SERVER_H_
#define SERVER_H_

#include <map>
#include <deque>
#include <string>
#include <semaphore.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <atomic>
#include <sys/types.h>
#include <signal.h>

#include "netutil.h"

using namespace netutil;

namespace server {
    static int max_conn = 5;
    static int conn_ct = 0;

    static int max_tconn = 5;
    static int tconn_ct = 0;

    // Poll blocking
    static bool block = false;

    // Storing command ids
    static int next_command_id = 2;

    class Server {
        public:
            Server(char *, char *, int);
            ~Server();

            // FTP Functions
            std::string get_path();
            int get(int, const std::string &, pid_t pid = NULL);
            int put(int, const std::string &, pid_t pid = NULL);
            int del(int, const std::string &);
            int cd(int, const std::string &);
            int mk_dir(int, const std::string &);
            int ls(int);
            int pwd(int);
            int quit(int);

            // Getters
            int get_port();

        private:
            // Sockets
            int conn, tconn, listener_sock, terminate_sock, thread_ct;
            char *port, *tport;

            // Poll fd struct
            struct pollfd *pfds;
            struct pollfd *term_pfds;

            // Mutex
            pthread_mutex_t job_mtx, read_mtx, write_mtx;
            pthread_cond_t job_cond;

            // Storing current working directory
            std::string cwd;
            char current_dir[1024];

            // Client-Server Addr Information
            // server stores the server information
            // term stores terminate information
            // Hints helps addrinfos genrate
            // client stores the client's information
            // cli_len stores the length of the client struct
            struct addrinfo *server, *term, hints;
            struct sockaddr_storage client;
            socklen_t cli_len;

            // Storing jobs
            std::deque<Content> jobs;


            // Static functions (to interact with threads and sockets)
            static void *handler(void *);
            static void *listener(void *);
            static void *terminator(void *);

            // Working with packets
            void add_job(Content &);
            void pop_job(Content &);

            // PFDS Storage
            void pfds_add(struct pollfd *[], int, int &);
            void pfds_rm(struct pollfd [], int, int &);
            void pfds_close(struct pollfd [], int, int &);
            void pfds_close_by_fd(struct pollfd [], int, int &);
            int pfds_find_loc(struct pollfd [], int, int);

            // Handdling jobs and threads
            void handle_job(struct Content &);
            void thread_launcher();
            void setup_socket(int &, int, struct sockaddr_in &);
            void setup_socket(int &, char *, struct addrinfo *&);

            // Tracking and working with pids
            int generate_command_id();
            pid_t get_pid(int);
            void set_pid(int, pid_t);

            // Handling termination
            void handle_terminate(int);
            bool should_terminate(int);
    };
}

#endif
