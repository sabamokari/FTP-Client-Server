#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "netutil.h"
#include "server.h"

using namespace server;

Server::Server(char *port, char *tport, int threadCt) {
  // Init vars
  this->port = port;
  this->tport = tport;
  this->thread_ct = threadCt;
  this->cwd = this->get_path();

  // Init mutex conditions
  pthread_cond_init(&job_cond, NULL);

  // Init mutex locks
  pthread_mutex_init(&job_mtx, NULL);
  pthread_mutex_init(&read_mtx, NULL);
  pthread_mutex_init(&write_mtx, NULL);

  // Initialize pfds to max connections
  this->pfds = (struct pollfd *)malloc(sizeof(*this->pfds) * max_conn);
  this->term_pfds =
      (struct pollfd *)malloc(sizeof(*this->term_pfds) * max_tconn);

  // Setup sockets
  this->setup_socket(this->listener_sock, this->port, this->server);
  this->setup_socket(this->terminate_sock, this->tport, this->term);

  // Launch all threads
  this->thread_launcher();
}

Server::~Server() {
  // Free memory that stores pfds
  free(this->pfds);
  free(this->term_pfds);

  // Destroy mutex
  pthread_cond_destroy(&job_cond);
  pthread_mutex_destroy(&job_mtx);

  // TODO Destroy threads
}

// TODO Allow multiple heads
std::string Server::get_path() {
  getcwd(this->current_dir, sizeof(this->current_dir));
  if (this->current_dir == NULL) {
    fprintf(stderr, "Error storing CWD");
    return nullptr;
  }

  return std::string(current_dir);
}

int Server::get(int sockfd, const std::string &file, pid_t pid) {
  pthread_mutex_lock(&read_mtx);
  std::ifstream f(file, std::ios::binary | std::ios::out);
  int status;

  if (f.is_open()) {
    if (pid) {
      status = send_file(sockfd, f, pid);
    } else {
      status = send_file(sockfd, f);
    }

    if (status < 0) {
      perror("Could not send file");
      return -1;
    } else if (pid && status == pid) {
      // Premature termination
      fprintf(stdout, "Job %d terminated\n", pid);
      f.close();
      pthread_mutex_unlock(&read_mtx);
      return pid;
    }

    f.close();
  }
  pthread_mutex_unlock(&read_mtx);
  return 0;
}

int Server::put(int sockfd, const std::string &file, pid_t pid) {
  pthread_mutex_lock(&write_mtx);
  std::ofstream f(file, std::ios::binary | std::ios::in | std::ios::trunc);
  int status;

  if (f.is_open()) {
    if (pid) {
      status = receive_file(sockfd, f, pid);
    } else {
      status = receive_file(sockfd, f);
    }

    if (status < 0) {
      perror("Could not receive file");
      return status;
    } else if (pid && status == pid) {
      // Premature termination
      fprintf(stdout, "Job %d terminated\n", pid);
      f.close();
      std::remove(file.c_str());
      pthread_mutex_unlock(&write_mtx);
      return pid;
    }

    f.close();
  }

  pthread_mutex_unlock(&write_mtx);
  return 0;
}

int Server::del(int sockfd, const std::string &file) {
  std::string file_path;
  int fd, result;

  file_path = get_path() + "/" + file;
  fd = open(file_path.c_str(), O_RDONLY);
  if (fd < 0) {
    perror("File does not exist");
    return fd;
  }
  close(fd);

  result = unlink(file_path.c_str());
  if (result < 0) {
    perror("Error deleting file");
    return result;
  }

  return 0;
}

int Server::cd(int sockfd, const std::string &path) {
  size_t last_slash;
  std::string new_path;
  int result;

  // Cases for ".." or "~" are here
  if (path == "..") {
    // Extract parent path from current path
    last_slash = get_path().find_last_of('/');
    new_path = get_path().substr(0, last_slash);
  } else if (path == "~") {
    // Get home directory and set to new path
    new_path = getenv("HOME");
  } else {
    // Append new path to current path
    new_path = get_path() + "/" + path;
  }

  // Change directory
  result = chdir(new_path.c_str());
  if (result < 0) {
    perror("Error changing directory");
    return result;
  }

  // Update current path
  get_path() = new_path;
  return 0;
}

int Server::mk_dir(int sockfd, const std::string &dirname) {
  size_t dirlen;
  int make;

  make = mkdir(dirname.c_str(), 0700);
  if (make < 0) {
    perror("Failed to create a new directory");
    return make;
  }

  return 0;
}

int Server::ls(int sockfd) {
  DIR *dir;
  struct dirent *dp;
  std::string data, current_entry;
  int status;

  // Open directory stream
  dir = opendir(current_dir);
  if (dir == NULL) {
    fprintf(stderr, "Error opening directory");
    return -1;
  }

  // Read entire in directory and send to client
  while ((dp = readdir(dir)) != NULL) {
    current_entry = dp->d_name;
    data += current_entry;
    data += "\n";
  }
  status = send_data(sockfd, data);
  if (status < 0) {
    perror("Could not send data");
    return -1;
  }

  closedir(dir);
  return 0;
}

int Server::pwd(int sockfd) {
  int status;
  std::string current_dir;

  // Update current working directory
  current_dir = this->get_path();
  if (current_dir.empty()) {
    fprintf(stderr, "Failed to get current working directory");
    return -1;
  }

  status = send_data(sockfd, current_dir);
  if (status < 0) {
    perror("Failed to send current working directory");
    return -1;
  }

  return 0;
}

void Server::handle_job(Content &job) {
  std::vector<std::string> args;
  int fd, pid, status;
  std::string tmp;
  bool bg = false;

  fd = job.fd;
  args = split_string(job.data);
  if (args.size() > 2 && args[2] == "&") {
    bg = true;
  }

// Tell compiler to ignore "enumeration values not handled"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
  switch (find_code(args[0])) {
  case CC_GET:
    if (!bg) {
      get(fd, args[1]);
    } else {
      pid = generate_command_id();

      job.pid = pid;
      tmp = std::to_string(pid);

      status = send_data(fd, tmp, tmp.length(), PIDINFO);
      if (status < 0) {
        perror("Failed to send PID");
        block = false;
        break;
      }

      get(fd, args[1], pid);
    }

    // Unblock
    block = false;
    break;
  case CC_PUT:
    if (!bg) {
      put(fd, args[1]);
    } else {
      pid = generate_command_id();

      job.pid = pid;
      tmp = std::to_string(pid);

      status = send_data(fd, tmp, tmp.length(), PIDINFO);
      if (status < 0) {
        perror("Failed to send PID");
        block = false;
        break;
      }

      put(fd, args[1], pid);
    }

    // Unblock
    block = false;
    break;
  case CC_LS:
    ls(fd);
    break;
  case CC_DEL:
    del(fd, args[1]);
    break;
  case CC_PWD:
    pwd(fd);
    break;
  case CC_CD:
    cd(fd, args[1]);
    break;
  case CC_MKDIR:
    mk_dir(fd, args[1]);
    break;
  default:
    // Do nothing
    break;
  }
}

void Server::thread_launcher() {
  pthread_t producer;
  pthread_t interruptor;
  pthread_t consumer[this->thread_ct];
  pthread_attr_t attr;

  // default attributes
  pthread_attr_init(&attr);

  // init producer thread
  pthread_create(&producer, &attr, listener, this);

  // init terminator thread
  pthread_create(&interruptor, &attr, terminator, this);

  // init all consumer threads
  // I want to pool threads to decrease runtime
  for (int i = 0; i < this->thread_ct; ++i) {
    pthread_create(&consumer[i], &attr, handler, this);
  }

  // join producer
  pthread_join(producer, NULL);
}

void Server::setup_socket(int &sockfd, char *port, struct addrinfo *&addr) {
  int rc;

  // Set hints to empty
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  // hints.ai_flags = INADDR_ANY;

  // Retreive address info
  rc = getaddrinfo(NULL, port, &hints, &addr);
  if (rc < 0) {
    fprintf(stderr, "Error retreiving address info %s\n", gai_strerror(rc));
    exit(1);
  }

  // Create socket
  sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  if (sockfd < 0) {
    perror("Failed to create socket");
    exit(1);
  }

  // Allows reuse of a socket
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)1, sizeof(int));

  // Bind socket to sockfd
  rc = bind(sockfd, addr->ai_addr, addr->ai_addrlen);
  if (rc < 0) {
    perror("Error binding to socket");
    exit(1);
  }

  // No longer needed
  freeaddrinfo(addr);
}

void Server::pop_job(Content &p) {
  Content pack;
  int rc;

  pthread_mutex_lock(&this->job_mtx);

  while (this->jobs.empty()) {
    rc = pthread_cond_wait(&this->job_cond, &this->job_mtx);
  }

  pack = this->jobs.front();
  this->jobs.pop_front();
  p = pack;

  pthread_mutex_unlock(&this->job_mtx);
}

void Server::add_job(Content &p) {
  pthread_mutex_lock(&this->job_mtx);

  this->jobs.push_back(p);

  pthread_mutex_unlock(&this->job_mtx);

  pthread_cond_signal(&this->job_cond);
}

void Server::pfds_add(struct pollfd *pfds[], int newconn, int &sockctr) {
  (*pfds)[sockctr].fd = newconn;
  (*pfds)[sockctr].events = POLLIN;

  sockctr++;
}

void Server::pfds_rm(struct pollfd pfds[], int i, int &sockctr) {
  pfds[i] = pfds[sockctr - 1];

  sockctr--;
}

void Server::pfds_close_by_fd(struct pollfd pfds[], int i, int &sockctr) {
  int loc;

  loc = this->pfds_find_loc(pfds, sockctr, i);
  this->pfds_close(pfds, loc, sockctr);
}

void Server::pfds_close(struct pollfd pfds[], int i, int &sockctr) {
  close(pfds[i].fd);
  this->pfds_rm(pfds, i, sockctr);
}

int Server::pfds_find_loc(struct pollfd pfds[], int fd, int sockctr) {
  for (int i = 0; i < sockctr; i++) {
    if (pfds[i].fd == fd) {
      return i;
    }
  }

  return -1;
}

int Server::generate_command_id() {
  // Generate a new id by adding 1 to the previous one
  pthread_mutex_lock(&this->job_mtx);
  next_command_id++;
  pthread_mutex_unlock(&this->job_mtx);

  return next_command_id;
}

void *Server::handler(void *arg) {
  Server *s = (Server *)arg;
  Content job;

  while (1) {
    // Pop new job
    s->pop_job(job);

    // Handle job
    s->handle_job(job);
  }
}

void *Server::listener(void *arg) {
  Server *s = (Server *)arg;

  int rc, poll_ct, status, newconn;
  char inc_addr[INET_ADDRSTRLEN];
  std::vector<std::string> args;
  Content job;

  rc = listen(s->listener_sock, 10);
  if (rc < 0) {
    perror("Server: Error listening on socket");
    exit(1);
  }

  // Add listener to pfds set
  s->pfds[0].fd = s->listener_sock;
  s->pfds[0].events = POLLIN;
  conn_ct = 1;

  printf("Server: listening on port %s\n", s->port);

  while (1) {
    if (!block) {
      poll_ct = poll(s->pfds, conn_ct, -1);
      if (poll_ct == -1) {
        perror("Server: Polling Error");
        exit(1);
      }

      // Check all connections
      for (int i = 0; i < conn_ct; i++) {
        // Check if any connections are ready to read
        if (s->pfds[i].revents & POLLIN) {

          // Handle new incoming connection
          if (s->pfds[i].fd == s->listener_sock) {

            // Accept connection
            s->cli_len = sizeof(s->client);
            newconn = accept(s->listener_sock, (struct sockaddr *)&s->client,
                             &s->cli_len);

            // If connection count is at max, throw error, else, allow
            // connection
            if (newconn < 0 || conn_ct == max_conn) {
              perror("Server: accept error");
            } else {
              s->pfds_add(&s->pfds, newconn, conn_ct);

              inet_ntop(s->client.ss_family,
                        &((struct sockaddr_in *)&s->client)->sin_addr, inc_addr,
                        sizeof(inc_addr));
              printf("Server: Connection received from %s in socket #%d\n",
                     inc_addr, newconn);
            }
          } else {
            // General Client
            status = receive_data(s->pfds[i].fd, job);
            s->conn = s->pfds[i].fd;

            if (status <= 0) {
              // Error
              if (status == 0) {
                printf("Server: Connection %d closed\n", s->pfds[i].fd);
              } else {
                perror("Server: Connection error");
              }

              // Close connection
              s->pfds_close(s->pfds, i, conn_ct);
            } else {
              // Working with data
              if (job.msg == COMMAND) {
                job.fd = s->pfds[i].fd;

                s->add_job(job);
                args = split_string(job.data);

                if (args[0] == "get" || args[0] == "put") {
                  block = true;
                }
              }
            }
          }
        }
      }
    }
  }

  exit(0);
}

void *Server::terminator(void *arg) {
  Server *s = (Server *) arg;

  int rc, pollct, status, newconn, pid;
  std::vector<std::string> args;
  char inc_addr[INET_ADDRSTRLEN];
  Content job;

  rc = listen(s->terminate_sock, 10);
  if (rc < 0) {
    perror("Terminator: Error listening on socket");
    exit(1);
  }

  // Add listener to pfds set
  s->term_pfds[0].fd = s->terminate_sock;
  s->term_pfds[0].events = POLLIN;
  tconn_ct = 1;

  printf("Terminator: Listening on port %s\n", s->tport);

  while (1) {
    pollct = poll(s->term_pfds, tconn_ct, -1);

    if (tconn_ct == -1) {
      perror("Terminator: Polling Error");
      exit(1);
    }

    // Check all connections
    for (int i = 0; i < tconn_ct; i++) {

      // Check if any connections are ready to read
      if (s->term_pfds[i].revents & POLLIN) {

        // Handle new incoming connection
        if (s->term_pfds[i].fd == s->terminate_sock) {

          // Accept connection
          s->cli_len = sizeof(s->client);
          newconn = accept(s->terminate_sock, (struct sockaddr *)&s->client,
                           &s->cli_len);

          // If connection count is at max, throw error, else, allow connection
          if (newconn < 0 || tconn_ct == max_tconn) {
            perror("Terminator: accept error");
          } else {
            s->pfds_add(&s->term_pfds, newconn, tconn_ct);

            inet_ntop(s->client.ss_family,
                      &((struct sockaddr_in *)&s->client)->sin_addr, inc_addr,
                      sizeof(inc_addr));
            printf("Terminator: Connection received from %s in socket #%d\n",
                   inc_addr, newconn);
          }
        } else {
          // Not listener
          status = receive_data(s->term_pfds[i].fd, job);
          s->tconn = s->term_pfds[i].fd;

          if (status <= 0) {
            // Error
            if (status == 0) {
              printf("Terminator: Connection %d closed\n", s->term_pfds[i].fd);
            } else {
              perror("Terminator: : connection error");
            }

            close(s->term_pfds[i].fd);
            s->pfds_rm(s->term_pfds, i, tconn_ct);
          } else {
            // Work with terminate command
            if (job.msg == TERMINATE) {
              args = split_string(job.data);
              pid = std::stoi(args[1]);
              printf("Added %d to termination queue.\n", pid);

              term_targets.push_back(pid);

              // e.g. send errors
              // find some way to check status of stopping
              std::string placeholder = "PID %d terminated successfully\n";
            }
          }
        }
      }
    }
  }

  exit(0);
}
