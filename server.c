/*
 * nanoweb - A nano HTTP web server made for learning sockets.
 * Copyright (C) 2013 Jan Segre
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/sendfile.h>
#endif
#include <netinet/in.h>

#define LISTEN_PORT 5001
#define LISTEN_ADDR INADDR_ANY
#define BUFFER_LEN (1<<22)
#define MAX_CONN_QUEUE 1024
#define MAX_FILENAME 1024
#define PATH_TO_SERVE "./static/"

int process_connection(int sockfd, const char* path);
int process_request(int sockfd, const char* path);
int server(const int port, const char* path);

int main(int argc, char* argv[]) {
  int opt;
  int port = LISTEN_PORT;
  char *path = PATH_TO_SERVE;

  while ((opt = getopt(argc, argv, "l:s:")) != -1) {
    switch (opt) {
      case 'l':
        port = atoi(optarg);
        break;
      case 's':
        path = optarg;
        break;
      default:
        fprintf(stderr, "Usage: %s [-l port] [-s path]\n", argv[0]);
        return 1;
    }
  }

  return server(port, path);
}

int server(const int port, const char* path) {
  int sockfd;
  struct sockaddr_in serv_addr;
  int optval;

  // try to open a socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("ERROR opening socket");
    return 1;
  }

  // set some useful options, as reusability and keepalive
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  // initialize the server address
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = LISTEN_ADDR;
  serv_addr.sin_port = htons(port);

  // bind to the address
  if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    perror("ERROR on binding");
    return 1;
  }

  // start listening
  listen(sockfd, MAX_CONN_QUEUE);

  // serial processing of conections
  //TODO spawn many threads for concurrent connections
  for (;;) {
    int n;
    n = process_connection(sockfd, path);
    if (n) goto error;
  }

  close(sockfd);
  return 0;

error:
  close(sockfd);
  perror("ERROR unexpected error occurred");
  return 1;
}

int process_connection(int sockfd, const char* path) {
  int newsockfd, n;
  struct sockaddr cli_addr;
  socklen_t cli_addr_len;

  cli_addr_len = sizeof(cli_addr);

  // single accept
  newsockfd = accept(sockfd, &cli_addr, &cli_addr_len);
  if (newsockfd < 0) goto error;

  // process multiple packets
  //for (;;) {
  //  n = process_request(newsockfd);
  //  if (n) {
  //    break;
  //  }
  //}

  // process a single packet
  n = process_request(newsockfd, path);
  if (n < 0) goto error;
  if (n > 0) goto warning;

  close(newsockfd);
  return 0;

warning:
  fprintf(stderr, "WARN something bad happened processing the request");
  close(newsockfd);
  return 0;

error:
  close(newsockfd);
  perror("ERROR processing the connection");
  return 1;
}

int process_request(int sockfd, const char* path) {
  int fd, n;
  struct stat st;
  off_t offset;
#ifdef __linux__
  ssize_t sent;
#endif
  char buffer[BUFFER_LEN],
       verb[32],
       httpver[32],
       contentlen[32],
       filename[MAX_FILENAME],
       *url;

  // copy the given path to our filename
  strcpy(filename, path);

  // the url var is so we can write the requested path
  // to it and filename will be filled
  url = filename + strlen(filename);

  // initialize fd so it always has a value
  fd = -1;

  // receive data
  n = read(sockfd, buffer, BUFFER_LEN);
  if (n < 0) goto error;
  // clear last cell of the buffer, so it marks a termination
  buffer[n] = 0;

  // parse the request
  sscanf(buffer, "%s %s %s", verb, url, httpver);
  if (strcmp("GET", verb) != 0) {
    write(sockfd, "HTTP/1.0 400 Method Not Supported\n\n", 35);
    printf("requested method %s not supported\n", verb);
    goto error;
  }

  //TODO check version (httpver)

  // local log
  printf("%s %s %s ... ", verb, url, httpver);

  // try to open the requested file
  fd = open(filename, O_RDONLY, S_IREAD);
  if (fd < 0) {
    write(sockfd, "HTTP/1.0 404 Not Found\n\n", 24);
    printf("not found\n");
    goto error;
  }

  // check the file size
  stat(filename, &st);
  sprintf(contentlen, "Content-Length: %lli\n", (long long)st.st_size);

  // write headers
  write(sockfd, "HTTP/1.0 200 OK\n", 16);
  write(sockfd, "Server: nanoweb-0.0.1\n", 22);
  write(sockfd, contentlen, strlen(contentlen));
  write(sockfd, "\n", 1);

  // send the requested file
  offset = 0;
#if defined(__linux__)
  sent = 0;
  do {
    sent = sendfile(sockfd, fd, &offset, st.st_size - sent);
    offset += sent;
    if (sent < 0) goto error;
  } while (sent > 0);
#elif defined(__APPLE__)
  do {
    n = sendfile(fd, sockfd, offset, &offset, 0, 0);
    if (n < 0) goto error;
  } while (offset != 0);
#else
#error "Your platform is not supported."
#endif

  if (close(fd) < 0) goto error_close;
  printf("ok\n");
  return 0;

error:
  if (fd > 0) if (close(fd) < 0) goto error_close;
  perror("ERROR processing the request");
  return 1;

error_close:
  perror("ERROR closing file");
  return 1;
}
