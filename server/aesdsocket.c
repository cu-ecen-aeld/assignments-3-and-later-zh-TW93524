#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define SERVER_PORT 9000
#define BACKLOG 10
#define RECV_CHUNK 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

static volatile sig_atomic_t g_exit_requested = 0;
static volatile sig_atomic_t g_signal_caught = 0;
static int g_listen_fd = -1;

static void handle_signal(int signo)
{
    (void)signo;
    g_exit_requested = 1;
    g_signal_caught = 1;

    if (g_listen_fd != -1) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t rc = send(fd, buf + sent, len - sent, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)rc;
    }

    return 0;
}

static int append_packet_and_reply(int client_fd, const char *packet, size_t packet_len)
{
    int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "Failed to open %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    size_t written = 0;
    while (written < packet_len) {
        ssize_t rc = write(fd, packet + written, packet_len - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "Failed writing to %s: %s", DATA_FILE, strerror(errno));
            close(fd);
            return -1;
        }
        written += (size_t)rc;
    }

    if (close(fd) != 0) {
        syslog(LOG_ERR, "Failed closing %s after write: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    fd = open(DATA_FILE, O_RDONLY);
    if (fd < 0) {
        syslog(LOG_ERR, "Failed to reopen %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    char buffer[RECV_CHUNK];
    for (;;) {
        ssize_t rc = read(fd, buffer, sizeof(buffer));
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "Failed reading %s: %s", DATA_FILE, strerror(errno));
            close(fd);
            return -1;
        }

        if (rc == 0) {
            break;
        }

        if (send_all(client_fd, buffer, (size_t)rc) != 0) {
            syslog(LOG_ERR, "Failed sending response: %s", strerror(errno));
            close(fd);
            return -1;
        }
    }

    if (close(fd) != 0) {
        syslog(LOG_ERR, "Failed closing %s after read: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    return 0;
}

static int process_connection(int client_fd)
{
    char recvbuf[RECV_CHUNK];
    char *packet = NULL;
    size_t packet_len = 0;

    for (;;) {
        ssize_t bytes = recv(client_fd, recvbuf, sizeof(recvbuf), 0);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
            free(packet);
            return -1;
        }

        if (bytes == 0) {
            break;
        }

        size_t start = 0;
        while (start < (size_t)bytes) {
            char *newline = memchr(recvbuf + start, '\n', (size_t)bytes - start);
            size_t chunk_len = newline ? (size_t)(newline - (recvbuf + start) + 1)
                                       : ((size_t)bytes - start);

            char *new_packet = realloc(packet, packet_len + chunk_len);
            if (new_packet == NULL) {
                syslog(LOG_ERR, "realloc failed");
                free(packet);
                return -1;
            }
            packet = new_packet;

            memcpy(packet + packet_len, recvbuf + start, chunk_len);
            packet_len += chunk_len;
            start += chunk_len;

            if (newline != NULL) {
                if (append_packet_and_reply(client_fd, packet, packet_len) != 0) {
                    free(packet);
                    return -1;
                }
                free(packet);
                packet = NULL;
                packet_len = 0;
            }
        }
    }

    free(packet);
    return 0;
}

static int daemonize_process(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        return -1;
    }

    if (chdir("/") != 0) {
        return -1;
    }

    umask(0);

    int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0) {
        return -1;
    }

    if (dup2(devnull, STDIN_FILENO) < 0) {
        close(devnull);
        return -1;
    }
    if (dup2(devnull, STDOUT_FILENO) < 0) {
        close(devnull);
        return -1;
    }
    if (dup2(devnull, STDERR_FILENO) < 0) {
        close(devnull);
        return -1;
    }

    if (devnull > STDERR_FILENO) {
        close(devnull);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int status = 0;
    bool daemon_mode = false;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    } else if (argc > 1) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return -1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
        syslog(LOG_ERR, "sigaction failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    int opt = 1;
    if (setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        closelog();
        return -1;
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERVER_PORT);

    if (bind(g_listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        closelog();
        return -1;
    }

    if (daemon_mode && daemonize_process() != 0) {
        syslog(LOG_ERR, "daemonize failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        closelog();
        return -1;
    }

    if (listen(g_listen_fd, BACKLOG) != 0) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        closelog();
        return -1;
    }

    while (!g_exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (g_exit_requested) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            status = -1;
            break;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL) {
            strncpy(client_ip, "unknown", sizeof(client_ip) - 1);
            client_ip[sizeof(client_ip) - 1] = '\0';
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        if (process_connection(client_fd) != 0) {
            status = -1;
        }

        if (close(client_fd) != 0) {
            syslog(LOG_ERR, "close client failed: %s", strerror(errno));
            status = -1;
        }
        syslog(LOG_INFO, "Closed connection from %s", client_ip);

        if (status != 0) {
            break;
        }
    }

    if (g_signal_caught) {
        syslog(LOG_INFO, "Caught signal, exiting");
    }

    if (g_listen_fd != -1) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    if (unlink(DATA_FILE) != 0 && errno != ENOENT) {
        syslog(LOG_ERR, "Failed to delete %s: %s", DATA_FILE, strerror(errno));
        status = -1;
    }

    closelog();
    return status;
}


