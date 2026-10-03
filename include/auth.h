#ifndef DOOM_AUTH_H
#define DOOM_AUTH_H
#include <stdbool.h>
#include <ev.h>

#define AUTH_PASSWORD_SIZE 512
#define AUTH_NOTICE_SIZE 160

typedef enum {
    AUTH_FINGERPRINT,
    AUTH_PASSWORD,
    AUTH_CHANNEL_COUNT
} auth_channel_t;
typedef void (*auth_result_fn)(auth_channel_t channel, bool success);
typedef void (*auth_notice_fn)(const char *message);

/* Linux PAM backend. Call init before grabs, watch before submitting workers.
 * Re-lock snapshots in the daemon child before submitting its first worker.
 * Submit/pending and both callbacks run on the event-loop thread. Workers own
 * separate PAM handles and locked snapshots; they never touch X11 or UI state. */
bool auth_init(const char *username, const char *confdir, const char *display);
bool auth_lock_memory(void);
void auth_watch(struct ev_loop *loop, auth_result_fn result, auth_notice_fn notice);
bool auth_submit(auth_channel_t channel, const char *password);
bool auth_pending(auth_channel_t channel);
/* At process exit, release idle handles. Pending workers end with the process. */
void auth_close(void);
#endif
