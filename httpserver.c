#include "asgn2_helper_funcs.h"
#include "connection.h"
#include "debug.h"
#include "response.h"
#include "request.h"
#include "queue.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/file.h>

#define OPTIONS "t:"

queue_t *threads_pool;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    //char buf[BUFSIZE + 1];
    char *oper;
    char *uri;
    uint16_t status_code;
    char *rid;
} audit;

void handle_connection(int);

bool check_dir(char *file);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);
void send_audit_log(conn_t *conn, const Response_t *res);
void *work_thread();

int main(int argc, char **argv) {
    // cited from practica/asgn4-starter/httpserver.c
    if (argc < 2) {
        // warnx("wrong arguments: %s port_num", argv[0]);
        // fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int threads;
    int port;
    if (argc > 2) {
        int opt = 0;
        while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
            switch (opt) {
            case 't':
                threads = strtoul(optarg, NULL, 10);
                port = strtoul(argv[3], NULL, 10);
                // fprintf(stderr, "threads = %d, port = %d\n", threads, port);
                break;
            default:
                threads = 4;
                port = strtoul(argv[1], NULL, 10);
                // fprintf(stderr, "threads = %d, port = %d\n", threads, port);
                break;
            }
        }
    } else {
        threads = 4;
        port = strtoul(argv[1], NULL, 10);
        // fprintf(stderr, "threads = %d, port = %d\n", threads, port);
    }

    // if (argc > 2) {
    //     threads = atoi(argv[2]);
    // }
    threads_pool = queue_new(threads);
    pthread_t thread[threads];

    for (int i = 0; i < threads; i++) {
        pthread_create(&thread[i], NULL, work_thread, NULL);
        //pthread_join(thread[i], NULL);
    }

    // char *endptr = NULL;
    // size_t port = (size_t) strtoull(argv[1], &endptr, 10);
    // if (endptr && *endptr != '\0') {
    //     // warnx("invalid port number: %s", argv[1]);
    //     return EXIT_FAILURE;
    // }

    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    listener_init(&sock, port);

    // dispatcher thread
    while (1) {
        uintptr_t connfd = listener_accept(&sock); // accept a connection
        queue_push(threads_pool, (void *) connfd); // push connfd to pool
    }

    queue_delete(&threads_pool);
    return 0;
}

void *work_thread() {
    while (1) {
        uintptr_t connfd;
        queue_pop(threads_pool, (void **) &connfd);
        handle_connection(connfd);
        close(connfd);
    }
}

// cited from practica/asgn4-starter/httpserver.c
void handle_connection(int connfd) {
    conn_t *conn = conn_new(connfd);
    const Response_t *res = conn_parse(conn);
    if (res != NULL) {
        conn_send_response(conn, res);
    } else {
        // debug("%s", conn_str(conn));
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_GET) {
            handle_get(conn);
            // fprintf(stderr, "handle get\n");
        } else if (req == &REQUEST_PUT) {
            // fprintf(stderr, "handle put\n");
            handle_put(conn);
        } else {
            // fprintf(stderr, "unsupported\n");
            handle_unsupported(conn);
        }
    }

    conn_delete(&conn);
}

bool check_dir(char *file) {
    struct stat path = { 0 };
    stat(file, &path);
    if (S_ISDIR(path.st_mode)) {
        return true; // is dir
    }
    return false; // is not dir
}

void handle_get(conn_t *conn) {

    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;

    pthread_mutex_lock(&mutex);
    int infile = open(uri, O_RDONLY);
    int tmp_errno = errno;

    flock(infile, LOCK_SH);
    pthread_mutex_unlock(&mutex);
    if (infile < 0) {
        // printf("open file error with errno = %d\n", errno);
        if (tmp_errno == EACCES) {
            res = &RESPONSE_FORBIDDEN;
            //conn_send_response(conn, res);
            goto out;
        } else if (tmp_errno == ENOENT || tmp_errno == EISDIR) {
            res = &RESPONSE_NOT_FOUND;
            //conn_send_response(conn, res);
            goto out;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            //conn_send_response(conn, res);
            goto out;
        }
    } else if (check_dir(uri)) {
        res = &RESPONSE_FORBIDDEN;
        // conn_send_response(conn, res);
        goto out;
    }

    struct stat f;
    fstat(infile, &f);
    int content_length = f.st_size;
    // res = &RESPONSE_OK;
    res = conn_send_file(conn, infile, content_length);
    if (res == NULL) {
        res = &RESPONSE_OK;
    }
    send_audit_log(conn, res);

    close(infile);
    return;

//close(infile);
out:
    conn_send_response(conn, res);
    send_audit_log(conn, res);
}

void handle_unsupported(conn_t *conn) {
    // debug("handling unsupported request");
    const Response_t *res = NULL;
    res = &RESPONSE_NOT_IMPLEMENTED;

    // send responses
    //pthread_mutex_lock(&mutex);
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
    send_audit_log(conn, res);
    //pthread_mutex_unlock(&mutex);
}

void handle_put(conn_t *conn) {

    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;

    pthread_mutex_lock(&mutex);
    bool existed = access(uri, F_OK) == 0;

    int fd = open(uri, O_CREAT | O_WRONLY, 0600);
    flock(fd, LOCK_EX);
    ftruncate(fd, 0);
    pthread_mutex_unlock(&mutex);

    //int fd = open(uri, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        // debug("%s: %d", uri, errno);
        if (errno == EACCES || errno == EISDIR || errno == ENOENT) {
            res = &RESPONSE_FORBIDDEN;
            goto out;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            goto out;
        }
    }
    //ftruncate(fd, 0);
    res = conn_recv_file(conn, fd);

    //flock(fd, LOCK_UN);
    // close(fd);

    if (res == NULL && existed) {
        res = &RESPONSE_OK;
    } else if (res == NULL && !existed) {
        res = &RESPONSE_CREATED;
    }
    conn_send_response(conn, res);
    send_audit_log(conn, res);

    close(fd);
    return;

out:
    conn_send_response(conn, res);
    send_audit_log(conn, res);
}

void send_audit_log(conn_t *conn, const Response_t *res) {
    audit log;
    const Request_t *req = conn_get_request(conn);
    if (req == &REQUEST_GET) {
        log.oper = "GET";
    } else if (req == &REQUEST_PUT) {
        log.oper = "PUT";
    } else {
        log.oper = "";
    }
    // log.oper = conn_get_request(conn);
    log.uri = conn_get_uri(conn);
    log.status_code = response_get_code(res);
    log.rid = conn_get_header(conn, "Request-Id");
    int rid;
    // sscanf(log.rid, "%*s %d", &rid);
    if (log.rid != NULL) {
        rid = atoi(log.rid);
    } else {
        rid = 0;
    }

    //sprintf(log.buf, "%s,/%s,%d,%d\n", log.oper, log.uri, log.status_code, rid);
    fprintf(stderr, "%s,/%s,%d,%d\n", log.oper, log.uri, log.status_code, rid);
}
