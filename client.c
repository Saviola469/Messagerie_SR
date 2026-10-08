#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#define BUFSIZE 1024


int write_full(int fd, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int rc = write(fd, buf + sent, len - sent);
        if (rc <= 0)
            return -1;
        sent += rc;
    }
    return sent;
}


int get_response(int sock, char *buf, int size) {
    int len = 0;
    while (len < size - 1) {
        int rc = read(sock, buf + len, 1);
        if (rc <= 0)
            return -1;
        if (buf[len] == '\n') {
            buf[len + 1] = '\0';
            return len + 1;
        }
        len++;
    }
    buf[len] = '\0';
    return len;
}


int connect_server(const char *host, const char *port) {
    struct addrinfo hints, *res, *rp;
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // IPv4 ou IPv6
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0)
            continue;

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;  // succès

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    return sock;
}


void speak_to_server(int sock) {
    char sendbuf[BUFSIZE];
    char recvbuf[BUFSIZE];

    while (1) {
        printf(">>> ");
        fflush(stdout);

        if (fgets(sendbuf, BUFSIZE, stdin) == NULL)
            break;

        if (strcmp(sendbuf, "q\n") == 0 || strcmp(sendbuf, "quit\n") == 0)
            break;

        if (write_full(sock, sendbuf, strlen(sendbuf)) < 0) {
            perror("write");
            break;
        }

        if (get_response(sock, recvbuf, BUFSIZE) < 0) {
            printf("Connexion fermée par le serveur\n");
            break;
        }

        printf("%s", recvbuf);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s adresse port\n", argv[0]);
        exit(1);
    }

    int sock = connect_server(argv[1], argv[2]);
    if (sock < 0) {
        fprintf(stderr, "Impossible de se connecter au serveur\n");
        exit(1);
    }

    speak_to_server(sock);
    close(sock);

    return 0;
}