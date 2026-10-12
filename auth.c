#include "config.h"
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "auth.h"

typedef struct {
    pam_handle_t *handle;
    pthread_t thread;
    ev_async completed;
    bool pending;
    int result;
    pid_t child;
    bool cancelled;
    char password[AUTH_PASSWORD_SIZE];
} auth_attempt_t;

static auth_attempt_t attempts[AUTH_CHANNEL_COUNT];
static struct ev_loop *event_loop;
static auth_result_fn result_callback;
static auth_notice_fn notice_callback;
static pthread_mutex_t notice_mutex = PTHREAD_MUTEX_INITIALIZER;
static char latest_notice[AUTH_NOTICE_SIZE];
static ev_async notice_updates;
static char *auth_username, *auth_confdir, *auth_display;
static pthread_mutex_t process_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool fingerprint_helper;
extern char **environ;

#define FINGERPRINT_SOCKET 3
#ifndef FINGERPRINT_TIMEOUT
#define FINGERPRINT_TIMEOUT 120
#endif
typedef struct {
    unsigned result;
    int status;
    char notice[AUTH_NOTICE_SIZE];
} fingerprint_message_t;
static int conversation(int count, const struct pam_message **messages,
                        struct pam_response **output, void *context);

static bool start_transaction(auth_attempt_t *attempt) {
    struct pam_conv conv = {conversation, attempt};
    int result = attempt == &attempts[AUTH_FINGERPRINT]
                     ? pam_start("i3lock", auth_username, &conv, &attempt->handle)
                     : pam_start_confdir("i3lock-password", auth_username, &conv,
                                         auth_confdir, &attempt->handle);
    if (result == PAM_SUCCESS) {
        result = pam_set_item(attempt->handle, PAM_TTY, auth_display);
    }
    attempt->result = result;
    if (result == PAM_SUCCESS) {
        return true;
    }
    fprintf(stderr, "PAM initialization failed: %s\n", pam_strerror(attempt->handle, result));
    if (attempt->handle) {
        pam_end(attempt->handle, result);
        attempt->handle = NULL;
    }
    return false;
}

static void erase(void *memory, size_t size) {
#ifdef HAVE_EXPLICIT_BZERO
    explicit_bzero(memory, size);
#else
    volatile unsigned char *bytes = memory;
    while (size--) {
        *bytes++ = 0;
    }
#endif
}

static void free_responses(struct pam_response *responses, int count) {
    if (!responses) {
        return;
    }
    for (int i = 0; i < count; i++) {
        if (responses[i].resp) {
            erase(responses[i].resp, strlen(responses[i].resp));
            free(responses[i].resp);
        }
    }
    free(responses);
}

static void post_notice(const char *message) {
    if (fingerprint_helper) {
        fingerprint_message_t update = {0};
        snprintf(update.notice, sizeof(update.notice), "%s", message);
        (void)send(FINGERPRINT_SOCKET, &update, sizeof(update), MSG_NOSIGNAL);
        return;
    }
    pthread_mutex_lock(&notice_mutex);
    size_t i = 0;
    for (; i < sizeof(latest_notice) - 1 && message[i]; i++) {
        unsigned char character = message[i];
        latest_notice[i] = character >= 32 && character < 127 ? character : ' ';
    }
    latest_notice[i] = '\0';
    pthread_mutex_unlock(&notice_mutex);
    ev_async_send(event_loop, &notice_updates);
}

static int conversation(int count, const struct pam_message **messages,
                        struct pam_response **output, void *context) {
    auth_attempt_t *attempt = context;
    *output = NULL;
    if (count <= 0 || count > PAM_MAX_NUM_MSG || !messages) {
        return PAM_CONV_ERR;
    }
    struct pam_response *responses = calloc(count, sizeof(*responses));
    if (!responses) {
        return PAM_BUF_ERR;
    }
    for (int i = 0; i < count; i++) {
        if (!messages[i]) {
            goto invalid;
        }
        switch (messages[i]->msg_style) {
            case PAM_TEXT_INFO:
            case PAM_ERROR_MSG:
                if (attempt == &attempts[AUTH_FINGERPRINT] && messages[i]->msg) {
                    post_notice(messages[i]->msg);
                }
                break;
            case PAM_PROMPT_ECHO_OFF:
                /* A scan must never fall back to a blank password or borrow
                 * the password being entered on the event-loop thread. */
                if (attempt == &attempts[AUTH_FINGERPRINT]) {
                    goto invalid;
                }
                responses[i].resp = strdup(attempt->password);
                if (!responses[i].resp) {
                    goto invalid;
                }
                break;
            case PAM_PROMPT_ECHO_ON:
                responses[i].resp = strdup("");
                if (!responses[i].resp) {
                    goto invalid;
                }
                break;
            default:
                goto invalid;
        }
    }
    *output = responses;
    return PAM_SUCCESS;
invalid:
    free_responses(responses, count);
    return PAM_CONV_ERR;
}

/* Exec replaces the locker's address space and closes inherited descriptors.
 * A fresh process also contains resources that PAM modules never clean up. */
int auth_fingerprint_worker(const char *username, const char *display, const char *parent_pid) {
    char *end;
    long parent = strtol(parent_pid, &end, 10);
    if (!parent_pid[0] || *end || parent != getppid() ||
        prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || parent != getppid()) {
        return EXIT_FAILURE;
    }
    int type;
    socklen_t size = sizeof(type);
    if (getsockopt(FINGERPRINT_SOCKET, SOL_SOCKET, SO_TYPE, &type, &size) != 0 ||
        type != SOCK_SEQPACKET || fcntl(FINGERPRINT_SOCKET, F_SETFD, FD_CLOEXEC) != 0) {
        return EXIT_FAILURE;
    }
    fingerprint_helper = true;
    auth_username = (char *)username;
    auth_display = display[0] ? (char *)display : NULL;
    auth_attempt_t *attempt = &attempts[AUTH_FINGERPRINT];
    if (start_transaction(attempt)) {
        attempt->result = pam_authenticate(attempt->handle, 0);
        if (attempt->result == PAM_SUCCESS) {
            pam_setcred(attempt->handle, PAM_REFRESH_CRED);
        }
        pam_end(attempt->handle, attempt->result);
    }
    fingerprint_message_t result = {.result = true, .status = attempt->result};
    return send(FINGERPRINT_SOCKET, &result, sizeof(result), MSG_NOSIGNAL) == (ssize_t)sizeof(result)
               ? EXIT_SUCCESS : EXIT_FAILURE;
}

static double monotonic_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

static int fingerprint_scan(auth_attempt_t *attempt) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
        return PAM_SYSTEM_ERR;
    }
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        close(sockets[0]);
        close(sockets[1]);
        return PAM_SYSTEM_ERR;
    }
    error = posix_spawn_file_actions_adddup2(&actions, sockets[1], FINGERPRINT_SOCKET);
    if (!error) {
        error = posix_spawn_file_actions_addclosefrom_np(&actions, FINGERPRINT_SOCKET + 1);
    }
    char parent_pid[32];
    snprintf(parent_pid, sizeof(parent_pid), "%ld", (long)getpid());
    char *arguments[] = {"i3lock-doom", "--fingerprint-worker", auth_username,
                         auth_display ? auth_display : "", parent_pid, NULL};
    pid_t child = 0;
    if (!error) {
        error = posix_spawn(&child, "/proc/self/exe", &actions, NULL, arguments, environ);
    }
    posix_spawn_file_actions_destroy(&actions);
    close(sockets[1]);
    if (error) {
        close(sockets[0]);
        return PAM_SYSTEM_ERR;
    }
    pthread_mutex_lock(&process_mutex);
    attempt->child = child;
    if (attempt->cancelled) {
        kill(child, SIGKILL);
    }
    pthread_mutex_unlock(&process_mutex);
    double deadline = monotonic_seconds() + FINGERPRINT_TIMEOUT;
    int result = PAM_SYSTEM_ERR;
    bool received_result = false;
    for (;;) {
        double remaining = deadline - monotonic_seconds();
        if (remaining <= 0) {
            break;
        }
        struct pollfd descriptor = {.fd = sockets[0], .events = POLLIN};
        int ready = poll(&descriptor, 1, (int)(remaining * 1000) + 1);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            break;
        }
        fingerprint_message_t message;
        ssize_t count = recv(sockets[0], &message, sizeof(message), MSG_TRUNC);
        if (count == 0) {
            break;
        }
        if (count != (ssize_t)sizeof(message) || message.result > 1 || received_result) {
            received_result = false;
            break;
        }
        if (message.result) {
            received_result = true;
            result = message.status;
        } else {
            message.notice[sizeof(message.notice) - 1] = '\0';
            post_notice(message.notice);
        }
    }
    close(sockets[0]);
    pthread_mutex_lock(&process_mutex);
    /* A result is trusted only after the helper has exited successfully.
     * Missing/malformed results and timeouts must never unlock the screen. */
    if (!received_result || monotonic_seconds() >= deadline) {
        kill(child, SIGKILL);
        result = PAM_SYSTEM_ERR;
    }
    int status;
    pid_t reaped;
    do {
        reaped = waitpid(child, &status, 0);
    } while (reaped < 0 && errno == EINTR);
    attempt->child = 0;
    bool cancelled = attempt->cancelled;
    pthread_mutex_unlock(&process_mutex);
    return reaped == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 && !cancelled
               ? result : PAM_SYSTEM_ERR;
}

static void *authenticate_worker(void *context) {
    auth_attempt_t *attempt = context;
    if (attempt == &attempts[AUTH_FINGERPRINT]) {
        attempt->result = fingerprint_scan(attempt);
        ev_async_send(event_loop, &attempt->completed);
        return NULL;
    }
    attempt->result = pam_authenticate(attempt->handle, 0);
    if (attempt->result == PAM_SUCCESS) {
        pam_setcred(attempt->handle, PAM_REFRESH_CRED);
    }
    /* Modules keep resources in transaction data until pam_end. Reusing a
     * handle across scans can leak those resources on every retry. */
    pam_end(attempt->handle, attempt->result);
    attempt->handle = NULL;
    erase(attempt->password, sizeof(attempt->password));
    ev_async_send(event_loop, &attempt->completed);
    return NULL;
}

static void completed(EV_P_ ev_async *watcher, int events) {
    auth_attempt_t *attempt = watcher->data;
    pthread_join(attempt->thread, NULL);
    attempt->pending = false;
    result_callback((auth_channel_t)(attempt - attempts), attempt->result == PAM_SUCCESS);
}

static void notice_ready(EV_P_ ev_async *watcher, int events) {
    if (!attempts[AUTH_FINGERPRINT].pending) {
        return;
    }
    char message[AUTH_NOTICE_SIZE];
    pthread_mutex_lock(&notice_mutex);
    memcpy(message, latest_notice, sizeof(message));
    pthread_mutex_unlock(&notice_mutex);
    notice_callback(message);
}

bool auth_lock_memory(void) {
    for (int i = 0; i < AUTH_CHANNEL_COUNT; i++) {
        if (mlock(attempts[i].password, sizeof(attempts[i].password)) != 0) {
            perror("Could not lock authentication snapshot in memory");
            return false;
        }
    }
    return true;
}

bool auth_init(const char *username, const char *confdir, const char *display) {
    if (!confdir || !confdir[0]) {
        fprintf(stderr, "Missing password PAM configuration\n");
        return false;
    }
    if (!auth_lock_memory()) {
        auth_close();
        return false;
    }
    auth_username = strdup(username);
    auth_confdir = strdup(confdir);
    auth_display = display ? strdup(display) : NULL;
    if (!auth_username || !auth_confdir || (display && !auth_display)) {
        auth_close();
        return false;
    }
    for (int i = 0; i < AUTH_CHANNEL_COUNT; i++) {
        auth_attempt_t *attempt = &attempts[i];
        if (!start_transaction(attempt)) {
            auth_close();
            return false;
        }
        if (i == AUTH_FINGERPRINT) {
            pam_end(attempt->handle, PAM_SUCCESS);
            attempt->handle = NULL;
        }
    }
    return true;
}

void auth_watch(struct ev_loop *loop, auth_result_fn result, auth_notice_fn notice) {
    event_loop = loop;
    result_callback = result;
    notice_callback = notice;
    for (int i = 0; i < AUTH_CHANNEL_COUNT; i++) {
        ev_async_init(&attempts[i].completed, completed);
        attempts[i].completed.data = &attempts[i];
        ev_async_start(loop, &attempts[i].completed);
    }
    ev_async_init(&notice_updates, notice_ready);
    ev_async_start(loop, &notice_updates);
}

bool auth_pending(auth_channel_t channel) {
    return attempts[channel].pending;
}

bool auth_submit(auth_channel_t channel, const char *password) {
    auth_attempt_t *attempt = &attempts[channel];
    if (!event_loop || attempt->pending) {
        return false;
    }
    size_t length = channel == AUTH_PASSWORD ? strlen(password) : 0;
    if (length >= sizeof(attempt->password)) {
        return false;
    }
    if (channel == AUTH_PASSWORD && !attempt->handle && !start_transaction(attempt)) {
        return false;
    }
    erase(attempt->password, sizeof(attempt->password));
    if (length) {
        memcpy(attempt->password, password, length);
    }
    attempt->pending = true;
    attempt->cancelled = false;
    if (pthread_create(&attempt->thread, NULL, authenticate_worker, attempt) == 0) {
        return true;
    }
    attempt->pending = false;
    if (attempt->handle) {
        pam_end(attempt->handle, PAM_SYSTEM_ERR);
    }
    attempt->handle = NULL;
    erase(attempt->password, sizeof(attempt->password));
    return false;
}

void auth_close(void) {
    for (int i = 0; i < AUTH_CHANNEL_COUNT; i++) {
        auth_attempt_t *attempt = &attempts[i];
        if (i == AUTH_FINGERPRINT && attempt->pending) {
            pthread_mutex_lock(&process_mutex);
            attempt->cancelled = true;
            if (attempt->child > 0) {
                kill(attempt->child, SIGKILL);
            }
            pthread_mutex_unlock(&process_mutex);
            pthread_join(attempt->thread, NULL);
            attempt->pending = false;
        }
        if (attempt->pending) {
            continue;
        }
        if (event_loop) {
            ev_async_stop(event_loop, &attempt->completed);
        }
        if (attempt->handle) {
            pam_end(attempt->handle, attempt->result);
        }
        attempt->handle = NULL;
        erase(attempt->password, sizeof(attempt->password));
        munlock(attempt->password, sizeof(attempt->password));
    }
    free(auth_username);
    free(auth_confdir);
    free(auth_display);
    auth_username = auth_confdir = auth_display = NULL;
}
