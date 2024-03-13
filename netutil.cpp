#include <algorithm>
#include <exception>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>

#include "netutil.h"

using namespace netutil;

Command_Code netutil::find_code(const std::string &input) {
  if (input == "get")
    return CC_GET;
  if (input == "put")
    return CC_PUT;
  if (input == "delete")
    return CC_DEL;
  if (input == "ls")
    return CC_LS;
  if (input == "cd")
    return CC_CD;
  if (input == "pwd")
    return CC_PWD;
  if (input == "mkdir")
    return CC_MKDIR;
  return (Command_Code)NULL; // Return NULL if command not found
}

int netutil::send_file(int sockfd, std::ifstream &file, pid_t pid) {
  int bytes_written, size, length, total_sent;
  std::vector<pid_t>::iterator iter;
  std::string tmp;

  if (pid) {
    size = 1000;
  } else {
    size = NET_BUF_SIZE;
  }

  char buf[size];

  // Get length of file
  file.seekg(0, file.end);
  length = file.tellg();
  file.seekg(0, file.beg);

  // Put this into a string to send
  bytes_written = send(sockfd, &length, sizeof(length), 0);
  if (bytes_written < 0) {
    perror("Failed to send file length");
    return -1;
  }

  total_sent = 0;
  while (!file.eof()) {
    file.read(buf, size);
    bytes_written = send(sockfd, buf, file.gcount(), 0);
    if (bytes_written < 0) {
      perror("Failed to send file");
      return -1;
    }

    total_sent += bytes_written;

    // If pid == true, this will execute after every 1000 bytes
    if (pid) {
      // If there is a PID to terminate
      if (term_targets.size() > 0) {
        // Find pid in the term_targets vector
        iter = std::find(term_targets.begin(), term_targets.end(), pid);

        // If pid is in term targets
        if (iter != term_targets.end()) {
          // Remove it
          term_targets.erase(iter);

          // Break out of the function
          return pid;
        }
      }
    }
  }

  return 0;
}

int netutil::send_data(int sockfd, const std::string &data) {
  send_data(sockfd, data, data.length());
  return 0;
}

int netutil::send_data(int sockfd, const std::string &data, int length, Message msg) {
  int bytes_written, offset;
  std::string tmp;

  tmp = "";
  tmp.append(std::to_string(length));
  tmp.append(" ");
  tmp.append(std::to_string(msg));
  tmp.append(" ");
  tmp.append(data);

  char buf[tmp.length() + 1];

  // Copy to buffer then send, no loop required!
  memset(buf, 0, sizeof(buf)); // just in case
  strcpy(buf, tmp.c_str());
  bytes_written = send(sockfd, buf, strlen(buf), 0);

  return bytes_written;
}

int netutil::receive_file(int sockfd, std::ofstream &file, pid_t pid) {
  int bytes_written, size, output_length, total_bytes;
  std::vector<pid_t>::iterator iter;

  if (pid) {
    size = 1000;
  } else {
    size = NET_BUF_SIZE;
  }

  char buf[size];

  // Get output length of the file
  bytes_written = recv(sockfd, &output_length, sizeof(output_length), 0);
  if (bytes_written < 0) {
    perror("Failed to receive file size");
    return -1;
  }

  total_bytes = 0;
  while (total_bytes < output_length) {
    memset(buf, 0, sizeof(buf));
    bytes_written = recv(sockfd, buf, size, 0);
    if (bytes_written < 0) {
      perror("Error retreiving file data");
      return -1;
    }

    file.write(buf, bytes_written);
    total_bytes += bytes_written;

    // EOF and less than 1000 bytes
    if (bytes_written < size)
      break;

    // If pid == true, this will execute after every 1000 bytes
    if (pid) {
      // If there is a PID to terminate
      if (term_targets.size() > 0) {
        // Find pid in the term_targets vector
        iter = std::find(term_targets.begin(), term_targets.end(), pid);

        // If pid is in term targets
        if (iter != term_targets.end()) {
          // Remove it
          term_targets.erase(iter);
          printf("Cancelling %d\n", pid);

          // Break out of the function
          return pid;
        }
      }
    }
  }

  return 0;
}

int netutil::receive_data(int sockfd, Content &ctx) {
  int bytes_read, bytes_written, length, offset;
  Message msg;
  char buf[NET_BUF_SIZE];
  std::string tmp;
  std::vector<std::string> split;
  Content content;
  tmp = "";

  // Initial read
  memset(buf, 0, sizeof(buf));
  bytes_read = recv(sockfd, buf, sizeof(buf), 0);
  if (bytes_read < 0) {
    perror("Failed to receive data");
    return bytes_read;
  } else if (bytes_read == 0) {
    // Connection closed
    return bytes_read;
  }

  // Split the string
  split = split_string(buf);

  if (split.size() < 2) {
    return 1;
  }

  // Get length and code
  length = stoi(split[0]);
  msg = (Message)stoi(split[1]);
  offset = split[0].length() + split[1].length() + 2;

  tmp.append(buf + offset, length);
  bytes_written = tmp.length();

  while (bytes_written < length) {
    // Zero buffer every time
    memset(buf, 0, sizeof(buf));
    bytes_read = recv(sockfd, buf, sizeof(buf), 0);
    if (bytes_read < 0) {
      perror("Failed to receive data");
      return bytes_read;
    } else if (bytes_read == 0) {
      // Connection closed
      return bytes_read;
    }

    tmp.append(buf);
    bytes_written += bytes_read;
  }

  // Populate content
  content.length = length;
  content.msg = msg;
  content.data = tmp;

  ctx = content;

  return bytes_written;
}

std::vector<std::string> netutil::split_string(const std::string &input) {
  std::vector<std::string> output;
  std::istringstream splitter(input);
  std::string tmp;

  while (getline(splitter, tmp, ' ')) {
    output.push_back(tmp);
  }

  return output;
}
