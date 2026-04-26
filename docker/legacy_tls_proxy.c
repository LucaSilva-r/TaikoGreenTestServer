#include <arpa/inet.h>
#include <errno.h>
#include <gnutls/gnutls.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define BACKEND_PORT 18080
#define BUFFER_SIZE 16384

static gnutls_certificate_credentials_t xcred;
static const char *priority = "NONE:+VERS-TLS1.0:+RSA:+3DES-CBC:+ARCFOUR-128:+SHA1:+SIGN-RSA-SHA1:+COMP-NULL";

struct client_args {
    int fd;
    int listen_port;
};

static int send_all_plain(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static int send_all_tls(gnutls_session_t session, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = gnutls_record_send(session, buf + off, len - off);
        if (n < 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static int connect_backend(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BACKEND_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void *handle_client(void *argp) {
    struct client_args *args = (struct client_args *)argp;
    int client_fd = args->fd;
    int listen_port = args->listen_port;
    free(args);

    int backend_fd = -1;
    gnutls_session_t session;
    char buffer[BUFFER_SIZE];

    gnutls_init(&session, GNUTLS_SERVER);
    gnutls_priority_set_direct(session, priority, NULL);
    gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, xcred);
    gnutls_transport_set_int(session, client_fd);

    int ret = gnutls_handshake(session);
    if (ret < 0) {
        fprintf(stderr, "TLS handshake failed on %d: %s\n", listen_port, gnutls_strerror(ret));
        goto done;
    }

    fprintf(stderr, "TLS connected on %d: %s\n", listen_port, gnutls_session_get_desc(session));

    backend_fd = connect_backend();
    if (backend_fd < 0) {
        perror("connect backend");
        goto done;
    }

    int client_open = 1;
    int backend_open = 1;

    while (backend_open) {
        fd_set rfds;
        FD_ZERO(&rfds);
        if (client_open) FD_SET(client_fd, &rfds);
        FD_SET(backend_fd, &rfds);
        int max_fd = client_fd > backend_fd ? client_fd : backend_fd;

        ret = select(max_fd + 1, &rfds, NULL, NULL, NULL);
        if (ret <= 0) break;

        if (client_open && (FD_ISSET(client_fd, &rfds) || gnutls_record_check_pending(session) > 0)) {
            ssize_t n = gnutls_record_recv(session, buffer, sizeof(buffer));
            if (n == 0) {
                client_open = 0;
                shutdown(backend_fd, SHUT_WR);
                continue;
            }
            if (n < 0) break;
            if (send_all_plain(backend_fd, buffer, (size_t)n) != 0) break;
        }

        if (FD_ISSET(backend_fd, &rfds)) {
            ssize_t n = recv(backend_fd, buffer, sizeof(buffer), 0);
            if (n == 0) {
                backend_open = 0;
                break;
            }
            if (n < 0) break;
            if (send_all_tls(session, buffer, (size_t)n) != 0) break;
        }
    }

done:
    if (backend_fd >= 0) close(backend_fd);
    gnutls_bye(session, GNUTLS_SHUT_WR);
    gnutls_deinit(session);
    close(client_fd);
    return NULL;
}

static int listen_on(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 64) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    gnutls_global_init();
    gnutls_certificate_allocate_credentials(&xcred);

    int ret = gnutls_certificate_set_x509_key_file(
        xcred, "/app/Certificates/cert.crt", "/app/Certificates/cert.key", GNUTLS_X509_FMT_PEM);
    if (ret < 0) {
        fprintf(stderr, "certificate load failed: %s\n", gnutls_strerror(ret));
        return 1;
    }

    int ports[] = {10122, 54430, 54431};
    int fds[3];
    for (size_t i = 0; i < 3; i++) {
        fds[i] = listen_on(ports[i]);
        if (fds[i] < 0) {
            perror("listen");
            return 1;
        }
        fprintf(stderr, "Legacy TLS proxy listening on %d -> 127.0.0.1:%d\n", ports[i], BACKEND_PORT);
    }

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int max_fd = 0;
        for (size_t i = 0; i < 3; i++) {
            FD_SET(fds[i], &rfds);
            if (fds[i] > max_fd) max_fd = fds[i];
        }

        if (select(max_fd + 1, &rfds, NULL, NULL, NULL) <= 0) continue;

        for (size_t i = 0; i < 3; i++) {
            if (!FD_ISSET(fds[i], &rfds)) continue;
            int client_fd = accept(fds[i], NULL, NULL);
            if (client_fd < 0) continue;

            struct client_args *args = calloc(1, sizeof(*args));
            args->fd = client_fd;
            args->listen_port = ports[i];

            pthread_t thread;
            if (pthread_create(&thread, NULL, handle_client, args) == 0) {
                pthread_detach(thread);
            } else {
                close(client_fd);
                free(args);
            }
        }
    }
}
