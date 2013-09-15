/*
 * nanows - A nano HTTP web server made for learning sockets.
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

// the __linux__ macro is a subset of __unix__
#if !(defined(__unix__) || defined(__APPLE__))
#error "Your platform is not supported."
#endif
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
#define PATH_TO_SERVE "./"

int server(const int port, const char* path);
int process_connection(int sockfd, const char* path);
int process_request(int sockfd, const char* path);
int serve_file(int sockfd, char* filepath, int serve);

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
  printf("Listening on port %i.\n", port);
  for (;;) {
    int n;
    n = process_connection(sockfd, path);
    if (n < 0) goto error;
  }

  close(sockfd);
  return 0;

error:
  close(sockfd);
  perror("ERROR unexpected error occurred");
  return 1;
}

int process_connection(int sockfd, const char* path) {
  int newsockfd;
  struct sockaddr cli_addr;
  socklen_t cli_addr_len;

  cli_addr_len = sizeof(cli_addr);

  // single accept
  newsockfd = accept(sockfd, &cli_addr, &cli_addr_len);
  if (newsockfd < 0) goto error;

  // process multiple packets
  //for (;process_request(newsockfd, path) == 0;);

  // process a single packet
  if (process_request(newsockfd, path) != 0) goto error;

  close(newsockfd);
  return 0;

error:
  close(newsockfd);
  perror("ERROR processing the connection");
  return 1;
}

int process_request(int sockfd, const char* path) {
  int n;
  char buffer[BUFFER_LEN], verb[10], httpver[10], filepath[MAX_FILENAME], *url;

  // copy the given path to our filename
  strcpy(filepath, path);

  // the url var is so we can write the requested path
  // to it and filename will be filled
  url = filepath + strlen(filepath);

  // receive data
  n = read(sockfd, buffer, BUFFER_LEN);
  if (n < 0) goto error;
  // clear last cell of the buffer, so it marks a termination
  buffer[n] = 0;

  // parse the request
  sscanf(buffer, "%s %s %s", verb, url, httpver);

  // local log
  printf("%s %s %s ... ", verb, url, httpver);

  // check HTTP version and method
  if (strcmp("HTTP/1.1", httpver) != 0 && strcmp("HTTP/1.0", httpver) != 0) {
    write(sockfd, "HTTP/1.0 505 HTTP Version Not Supported\r\n\r\n", 43);
    fprintf(stderr, "version %s not supported, aborting.\n", httpver);
    return 0;
  } else if (strcmp("GET", verb) == 0) {
    // make a get request
    return serve_file(sockfd, filepath, 1);
  } else if (strcmp("HEAD", verb) == 0) {
    // make a get request
    return serve_file(sockfd, filepath, 0);
  } else {
    write(sockfd, "HTTP/1.0 400 Method Not Supported\r\n\r\n", 37);
    fprintf(stderr, "requested method %s not supported.\n", verb);
    return 0;
  }

error:
  perror("ERROR processing the request");
  return 1;
}

int serve_file(int sockfd, char* filepath, int serve) {
  int fd;
  struct stat st;
  char contentlen[32];

  // check if it's a dir
  stat(filepath, &st);
  if (st.st_mode & S_IFDIR) {
    char* path_append = filepath + strlen(filepath);
    sprintf(path_append, "/index.html");
    stat(filepath, &st);
  }

  // check if it's a file, if not it doesn't exist
  if (!(st.st_mode & S_IFREG)) {
    write(sockfd, "HTTP/1.0 404 Not Found\r\n\r\n", 26);
    printf("not found\n");
    fprintf(stderr, "ERROR ");
    perror(filepath);
    return 0;
  }

  // try to open the requested file
  fd = open(filepath, O_RDONLY, S_IREAD);
  if (fd < 0) {
    write(sockfd, "HTTP/1.0 403 Forbidden\r\n\r\n", 26);
    printf("forbidden\n");
    fprintf(stderr, "ERROR ");
    perror(filepath);
    return 0;
  }

  // write headers
  write(sockfd, "HTTP/1.0 200 OK\r\n", 17);
  write(sockfd, "Server: nanows-0.0.1\r\n", 22);
  sprintf(contentlen, "Content-Length: %lli\r\n", (long long)st.st_size);
  if (serve)
    write(sockfd, contentlen, strlen(contentlen));
  write(sockfd, "\r\n", 2);

  // send the requested file
  if (serve) {
    off_t offset = 0;
    ssize_t n = 0;
    // linux and bsd have different signatures,
    // which yield slightly different behaviour
#if defined(__linux__)
    do {
      n = sendfile(sockfd, fd, &offset, st.st_size - n);
      offset += n;
      if (n < 0) goto error;
    } while (n > 0);
#elif defined(__APPLE__) || defined(__unix__)
    do {
      n = sendfile(fd, sockfd, offset, &offset, 0, 0);
      if (n < 0) goto error;
    } while (offset != 0);
#endif
  }

  if (close(fd) < 0) goto error_close;
  printf("ok\n");
  return 0;

error:
  if (fd > 0) if (close(fd) < 0) goto error_close;
  perror("ERROR serving file");
  return 1;

error_close:
  perror("ERROR closing file");
  return 2;
}
