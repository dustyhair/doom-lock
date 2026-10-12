/* Reproduce retry resource exhaustion without host PAM or a fingerprint reader. */
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <security/pam_appl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

typedef struct {
    int descriptor;
    bool password;
    struct pam_conv conversation;
} fake_handle_t;
static unsigned starts, ends, callbacks;
static bool expected_success;
static int start_error, item_error;

int pam_start(const char *service, const char *user, const struct pam_conv *conv,
              pam_handle_t **handle) {
    assert(strcmp(user, "test-user") == 0);
    if (start_error) {
        *handle = NULL;
        return start_error;
    }
    fake_handle_t *fake = calloc(1, sizeof(*fake));
    assert(fake);
    fake->descriptor = -1;
    fake->conversation = *conv;
    *handle = (pam_handle_t *)fake;
    starts++;
    return PAM_SUCCESS;
}
int pam_start_confdir(const char *service, const char *user, const struct pam_conv *conv,
                      const char *confdir, pam_handle_t **handle) {
    assert(strcmp(service, "i3lock-password") == 0);
    assert(strcmp(confdir, "/test/pam") == 0);
    int result = pam_start(service, user, conv, handle);
    if (result == PAM_SUCCESS) {
        ((fake_handle_t *)*handle)->password = true;
    }
    return result;
}
int pam_set_item(pam_handle_t *handle, int item, const void *value) {
    assert(item == PAM_TTY && strcmp(value, ":test") == 0);
    return item_error;
}
int pam_setcred(pam_handle_t *handle, int flags) {
    assert(flags == PAM_REFRESH_CRED);
    return PAM_SUCCESS;
}
const char *pam_strerror(pam_handle_t *handle, int error) {
    return "simulated initialization failure";
}
int pam_end(pam_handle_t *handle, int status) {
    fake_handle_t *fake = (fake_handle_t *)handle;
    if (fake->descriptor >= 0) {
        assert(close(fake->descriptor) == 0);
    }
    free(fake);
    ends++;
    return PAM_SUCCESS;
}
int pam_authenticate(pam_handle_t *handle, int flags) {
    fake_handle_t *fake = (fake_handle_t *)handle;
    /* Model a module that overwrites its previous descriptor on each call
     * and only releases the current one at transaction cleanup. */
    fake->descriptor = open("/dev/null", O_RDONLY);
    assert(fake->descriptor >= 0);
    if (!fake->password) {
        return PAM_AUTH_ERR;
    }
    const struct pam_message prompt = {PAM_PROMPT_ECHO_OFF, "Password"};
    const struct pam_message *message = &prompt;
    struct pam_response *response = NULL;
    assert(fake->conversation.conv(1, &message, &response,
                                  fake->conversation.appdata_ptr) == PAM_SUCCESS);
    bool success = strcmp(response->resp, "correct") == 0;
    explicit_bzero(response->resp, strlen(response->resp));
    free(response->resp);
    free(response);
    return success ? PAM_SUCCESS : PAM_AUTH_ERR;
}

#include "../auth.c"

static unsigned descriptor_count(void) {
    DIR *directory = opendir("/proc/self/fd");
    assert(directory);
    unsigned count = 0;
    while (readdir(directory)) {
        count++;
    }
    closedir(directory);
    return count;
}
static void result(auth_channel_t channel, bool success) {
    assert(success == expected_success);
    assert(!auth_pending(channel));
    assert(!attempts[channel].handle);
    for (unsigned i = 0; i < AUTH_PASSWORD_SIZE; i++) {
        assert(attempts[channel].password[i] == 0);
    }
    callbacks++;
}
static void notice(const char *message) {}

int main(void) {
    struct rlimit limit;
    assert(getrlimit(RLIMIT_NOFILE, &limit) == 0);
    limit.rlim_cur = 64;
    assert(setrlimit(RLIMIT_NOFILE, &limit) == 0);
    assert(auth_init("test-user", "/test/pam", ":test"));
    auth_watch(EV_DEFAULT, result, notice);
    unsigned baseline = descriptor_count();
    for (unsigned i = 0; i < 2048; i++) {
        expected_success = i % 3 == 2;
        auth_channel_t channel = i % 3 == 0 ? AUTH_FINGERPRINT : AUTH_PASSWORD;
        assert(auth_submit(channel, expected_success ? "correct" : "wrong"));
        assert(!auth_submit(channel, "duplicate"));
        while (auth_pending(channel)) {
            ev_run(EV_DEFAULT, EVRUN_ONCE);
        }
        assert(descriptor_count() == baseline);
    }
    assert(callbacks == 2048 && starts == ends);
    start_error = PAM_SYSTEM_ERR;
    assert(!auth_submit(AUTH_PASSWORD, "correct"));
    assert(!auth_pending(AUTH_PASSWORD));
    start_error = 0;
    item_error = PAM_SYSTEM_ERR;
    assert(!auth_submit(AUTH_PASSWORD, "correct"));
    assert(!attempts[AUTH_PASSWORD].handle && starts == ends);
    item_error = 0;
    expected_success = true;
    assert(auth_submit(AUTH_PASSWORD, "correct"));
    while (auth_pending(AUTH_PASSWORD)) {
        ev_run(EV_DEFAULT, EVRUN_ONCE);
    }
    auth_close();
    assert(starts == ends && descriptor_count() == baseline);
    ev_loop_destroy(EV_DEFAULT);
    puts("PASS: 2048 retries under a 64-descriptor limit; password succeeds after failures");
    return 0;
}
