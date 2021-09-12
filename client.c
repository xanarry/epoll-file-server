//
// Created by xanarry on 2021/9/11.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include<arpa/inet.h>

#define MAXEVENTS 64


void displayUsage(char *app) {
    fprintf(stderr, "Usage: %s host port GET    remoteFile localFile\n", app);
    fprintf(stderr, "Usage: %s host port POST   localFile\n", app);
    fprintf(stderr, "Usage: %s host port LIST   path\n", app);
    fprintf(stderr, "Usage: %s host port DELETE remoteFile\n", app);
    exit(EXIT_FAILURE);
}

void upper(char *str) {
    if (str == NULL) {
        return;
    }

    for (int i = 0; i < strlen(str); i++) {
        str[i] = toupper(str[i]);
    }
}


int connectToServer(const char *host, const char *port) {
    // Get a fd number for the client socket
    int rc;
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        fprintf(stderr, "ERROR: failed to create client socket\n");
        exit(1);
    }

    // Get address info of the host and port we want to connect to
    struct addrinfo hints;
    struct addrinfo *result;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;  // TCP

    rc = getaddrinfo(host, port, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        exit(1);
    }

    // Connect the client socket to the server
    if (connect(sock_fd, result->ai_addr, result->ai_addrlen) == -1) {
        perror("connect()");
        exit(1);
    }

    // No longer needs address info
    freeaddrinfo(result);

    // If we get to here, connection is successful
    return sock_fd;
}


ssize_t writeNBytes(int outFd, char *buf, ssize_t n) {
    ssize_t hasWrite = 0;
    while (hasWrite < n) {
        ssize_t len = write(outFd, buf + hasWrite, n - hasWrite);
        if (len > 0) {
            hasWrite += len;
        }
        if (len == -1 && errno != EAGAIN) {
            return -1;
        }
    }
    return hasWrite;
}


ssize_t readNBytes(int outFd, char *buf, ssize_t n) {
    ssize_t hasRead = 0;
    while (hasRead < n) {
        ssize_t len = read(outFd, buf + hasRead, n - hasRead);
        if (len > 0) {
            hasRead += len;
        }
        if (len == -1 && errno != EAGAIN) {
            return -1;
        }
    }
    return hasRead;
}


int readLine(int sfd, char *line, int maxLen) {
    if (line == NULL) {
        return 0;
    }

    int i;
    for (i = 0; i < maxLen; i++) {
        if (read(sfd, line + i, 1) == -1) {
            return -1;
        }
        if (line[i] == '\n') {
            break;
        }
    }
    return i;
}

ssize_t iocopy(int dest, int src, ssize_t n) {
    char buffer[4096];
    ssize_t hasGot = 0;
    while (hasGot < n) {
        size_t rdCnt = 4096;
        size_t remain = n - hasGot;
        if (remain < rdCnt) {
            rdCnt = remain;
        }
        ssize_t len = read(src, buffer, rdCnt);

        if (len > 0) {
            hasGot += len;
            ssize_t wrCnt = writeNBytes(dest, buffer, len);
            if (wrCnt != len) {
                return -1;
            }
        }

        if (len == -1 && errno != EAGAIN) {
            return -1;
        }
    }
    return hasGot;
}

int parseStatus(int sfd) {
    char line[1025] = {0};
    int len = readLine(sfd, line, 1024);
    if (len <= 0) {
        return -1;
    }

    if (strncmp(line, "OK", 2) == 0) {
        return 1;
    }
    if (strncmp(line, "ERROR", 5) == 0) {
        return 0;
    } else {
        return -1;
    }
}

void handleList(int sfd, char *path) {
    char req[1024] = {0};
    sprintf(req, "LIST %s\n", path);
    if (writeNBytes(sfd, req, strlen(req)) == -1) {
        perror("write");
        exit(-1);
    }
    shutdown(sfd, SHUT_WR);

    int status = parseStatus(sfd);
    if (status == -1) {
        printf("Bad Response\n");
    } else if (status == 0) {
        char errMsg[1025] = {0};
        readLine(sfd, errMsg, 1024);
        printf("ERROR: %s\n", errMsg);
    } else {
        ssize_t contentLength = 0;
        if (readNBytes(sfd, (char *) &contentLength, sizeof(contentLength)) != sizeof(contentLength)) {
            fprintf(stderr, "Failed to read contentLength\n");
            close(sfd);
            return;
        }
        //printf("contentLength: %ld\n", contentLength);
        if (iocopy(STDOUT_FILENO, sfd, contentLength) == -1) {
            fprintf(stderr, "Failed to copy content from fd%d to %d\n", sfd, STDOUT_FILENO);
        }
    }
}


void handleDelete(int sfd, char *remoteFile) {
    char req[1024] = {0};
    sprintf(req, "DELETE %s\n", remoteFile);
    if (writeNBytes(sfd, req, strlen(req)) == -1) {
        perror("write");
        exit(-1);
    }
    shutdown(sfd, SHUT_WR);

    int status = parseStatus(sfd);
    if (status == -1) {
        printf("Bad Response\n");
    } else if (status == 0) {
        char errMsg[1025] = {0};
        readLine(sfd, errMsg, 1024);
        printf("ERROR: %s\n", errMsg);
    } else {
        printf("OK, %s Deleted Successfully\n", remoteFile);
    }
}


void handleGet(int sfd, char *remoteFile, char *localFile) {
    char req[1024] = {0};
    sprintf(req, "GET %s\n", remoteFile);
    if (writeNBytes(sfd, req, strlen(req)) == -1) {
        perror("write");
        exit(-1);
    }
    shutdown(sfd, SHUT_WR);

    int status = parseStatus(sfd);
    if (status == -1) {
        printf("Bad Response\n");
        return;
    } else if (status == 0) {
        char errMsg[1025] = {0};
        readLine(sfd, errMsg, 1024);
        printf("ERROR: %s\n", errMsg);
        return;
    }

    ssize_t contentLength = 0;
    if (readNBytes(sfd, (char *) &contentLength, sizeof(contentLength)) != sizeof(contentLength)) {
        fprintf(stderr, "Failed to read contentLength\n");
        close(sfd);
        return;
    }

    int outFd = open(localFile, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG);
    if (outFd == -1) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
        return;
    }

    if (iocopy(outFd, sfd, contentLength) != contentLength) {
        fprintf(stderr, "%s %d Error occur in iocopy: %s\n", __FILE__, __LINE__, strerror(errno));
        return;
    }
    printf("save %ld bytes to file %s\n", contentLength, localFile);
}

void handlePost(int sfd, char *localFile, char *remoteFile) {
    int fd = open(localFile, O_RDONLY);
    struct stat st;
    if (fd == -1 || fstat(fd, &st) == -1) {
        char *strerr = strerror(errno);
        fprintf(stderr, "%s: %s", localFile, strerr);
    } else if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "%s is not a regular file", localFile);
    }


    char req[1024] = {0};
    sprintf(req, "POST %s\n", remoteFile);
    ssize_t size = st.st_size;
    if (writeNBytes(sfd, req, strlen(req)) == -1 || writeNBytes(sfd, (char *) &size, sizeof(size)) == -1) {
        perror("write");
        exit(-1);
    }

    iocopy(sfd, fd, st.st_size);
    shutdown(sfd, SHUT_WR);

    int status = parseStatus(sfd);
    if (status == -1) {
        printf("Bad Response\n");
    } else if (status == 0) {
        char errMsg[1025] = {0};
        readLine(sfd, errMsg, 1024);
        printf("ERROR: %s\n", errMsg);
    } else {
        printf("OK\n");
    }
}


int main(int argc, char *argv[]) {
    if (argc <= 4 || (strcmp(argv[3], "GET") == 0 && argc != 6) || (strcmp(argv[3], "POST") == 0 && argc != 6)) {
        displayUsage(argv[0]);
    }

    int sfd = connectToServer(argv[1], argv[2]);
    //puts("Connected");
    upper(argv[3]);

    if (strcmp(argv[3], "GET") == 0) {
        handleGet(sfd, argv[4], argv[5]);
    }

    if (strcmp(argv[3], "POST") == 0) {
        handlePost(sfd, argv[4], argv[5]);
    }

    if (strcmp(argv[3], "LIST") == 0) {
        handleList(sfd, argv[4]);
    }

    if (strcmp(argv[3], "DELETE") == 0) {
        handleDelete(sfd, argv[4]);
    }

}
