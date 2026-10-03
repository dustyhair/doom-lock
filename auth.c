#include "config.h"
#include <pthread.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "auth.h"

typedef struct {
    pam_handle_t *handle;
    pthread_t thread;
    ev_async completed;
    bool pending;
    int result;
    char password[AUTH_PASSWORD_SIZE];
} auth_attempt_t;

static auth_attempt_t attempts[AUTH_CHANNEL_COUNT];
static struct ev_loop *event_loop;
static auth_result_fn result_callback;
static auth_notice_fn notice_callback;
static pthread_mutex_t notice_mutex = PTHREAD_MUTEX_INITIALIZER;
static char latest_notice[AUTH_NOTICE_SIZE];
static ev_async notice_updates;

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

static void *authenticate_worker(void *context) {
    auth_attempt_t *attempt = context;
    attempt->result = pam_authenticate(attempt->handle, 0);
    if (attempt->result == PAM_SUCCESS) {
        pam_setcred(attempt->handle, PAM_REFRESH_CRED);
    }
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
    for (int i = 0; i < AUTH_CHANNEL_COUNT; i++) {
        auth_attempt_t *attempt = &attempts[i];
        struct pam_conv conv = {conversation, attempt};
        int result = i == AUTH_FINGERPRINT
                         ? pam_start("i3lock", username, &conv, &attempt->handle)
                         : pam_start_confdir("i3lock-password", username, &conv, confdir, &attempt->handle);
        if (result == PAM_SUCCESS) {
            result = pam_set_item(attempt->handle, PAM_TTY, display);
        }
        attempt->result = result;
        if (result != PAM_SUCCESS) {
            fprintf(stderr, "PAM initialization failed: %s\n", pam_strerror(attempt->handle, result));
            auth_close();
            return false;
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
    erase(attempt->password, sizeof(attempt->password));
    if (length) {
        memcpy(attempt->password, password, length);
    }
    attempt->pending = true;
    if (pthread_create(&attempt->thread, NULL, authenticate_worker, attempt) == 0) {
        return true;
    }
    attempt->pending = false;
    erase(attempt->password, sizeof(attempt->password));
    return false;
}

void auth_close(void) {
    for (int i = 0; i < AUTH_CHANNEL_COUNT; i++) {
        auth_attempt_t *attempt = &attempts[i];
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
}
