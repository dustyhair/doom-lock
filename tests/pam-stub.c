/* Test only. Loaded with LD_PRELOAD exclusively on the harness's Xvfb display. */
#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
static unsigned fingerprint_scans;

static int notify(struct pam_conv conversation, const char *text, int style) {
    const struct pam_message message = {style, text};
    const struct pam_message *messages[] = {&message};
    struct pam_response *response = NULL;
    int result = conversation.conv(1, messages, &response, conversation.appdata_ptr);
    free(response);
    return result;
}

typedef struct {
    struct pam_conv conversation;
    int password_only;
} test_handle;
static void trace_event(const char *event) {
    FILE *trace = fopen(getenv("DOOM_TEST_TRACE"), "a");
    fprintf(trace, "%s\n", event);
    fclose(trace);
}
int pam_start(const char *service, const char *user, const struct pam_conv *conv, pam_handle_t **handle) {
    test_handle *test = calloc(1, sizeof(*test));
    test->conversation = *conv;
    *handle = (pam_handle_t *)test;
    return PAM_SUCCESS;
}
int pam_start_confdir(const char *service, const char *user, const struct pam_conv *conv,
                      const char *confdir, pam_handle_t **handle) {
    int result = pam_start(service, user, conv, handle);
    ((test_handle *)*handle)->password_only = 1;
    return result;
}
int pam_set_item(pam_handle_t *handle, int item, const void *value) { return PAM_SUCCESS; }
int pam_setcred(pam_handle_t *handle, int flags) { return PAM_SUCCESS; }
int pam_end(pam_handle_t *handle, int status) { free(handle); return PAM_SUCCESS; }
int pam_authenticate(pam_handle_t *handle, int flags) {
    test_handle *test = (test_handle *)handle;
    struct pam_conv conversation = test->conversation;
    const struct pam_message notice = {PAM_TEXT_INFO, "Test fingerprint scan"};
    const struct pam_message prompt = {PAM_PROMPT_ECHO_OFF, "Password"};
    const struct pam_message *messages[] = {&notice, &prompt};
    struct pam_response *responses = NULL;
    if (!test->password_only) {
        /* Linux must keep password snapshots locked in the daemon child. */
        FILE *status = fopen("/proc/self/status", "r");
        char line[256];
        unsigned long locked = 0;
        if (status) {
            while (fgets(line, sizeof(line), status))
                if (sscanf(line, "VmLck: %lu", &locked) == 1) break;
            fclose(status);
        }
        if (!locked) trace_event("password-memory-not-locked");
        trace_event("empty"); /* Fingerprint worker started. */
        fingerprint_scans++;
        if (notify(conversation, "Place your right index finger on the reader", PAM_TEXT_INFO) != PAM_SUCCESS)
            return PAM_AUTH_ERR;
        const char *mode = getenv("DOOM_TEST_MODE");
        if (strcmp(mode, "fingerprint-restart") == 0 ||
            strcmp(mode, "fingerprint-retry-during-failure") == 0 ||
            (strcmp(mode, "fingerprint-late-success") == 0 && fingerprint_scans < 3)) {
            usleep(250000);
            notify(conversation, "Fingerprint scan did not match", PAM_ERROR_MSG);
            if (strcmp(mode, "fingerprint-restart") == 0) {
                struct pam_response *fallback = NULL;
                if (conversation.conv(2, messages, &fallback, conversation.appdata_ptr) != PAM_CONV_ERR)
                    trace_event("unsafe-fingerprint-password-fallback");
                if (fallback) {
                    free(fallback[0].resp);
                    free(fallback[1].resp);
                    free(fallback);
                }
            }
            trace_event("scan-timeout");
            return PAM_AUTH_ERR;
        }
        int fingerprint = strcmp(getenv("DOOM_TEST_MODE"), "fingerprint") == 0 ||
                          strcmp(getenv("DOOM_TEST_MODE"), "fingerprint-during-failure") == 0 ||
                          strcmp(mode, "fingerprint-late-success") == 0;
        usleep(fingerprint ? 1000000 : 10000000);
        if (!fingerprint) return PAM_AUTH_ERR;
        if (conversation.conv(1, messages, &responses, conversation.appdata_ptr) != PAM_SUCCESS)
            return PAM_AUTH_ERR;
        free(responses);
        return PAM_SUCCESS;
    }
    if (conversation.conv(2, messages, &responses, conversation.appdata_ptr) != PAM_SUCCESS)
        return PAM_AUTH_ERR;
    char *password = responses[1].resp;
    int empty = password[0] == '\0';
    int matches = strcmp(password, "doom-test") == 0;
    explicit_bzero(password, strlen(password));
    free(password);
    free(responses);
    trace_event(empty ? "empty-password" : matches ? "match" : "wrong");
    int queued = strcmp(getenv("DOOM_TEST_MODE"), "queued-edit") == 0 ||
                 strcmp(getenv("DOOM_TEST_MODE"), "queued-after-failure") == 0;
    usleep(queued ? 1000000 : 250000);
    if (!matches && (strcmp(getenv("DOOM_TEST_MODE"), "queued-after-failure") == 0 ||
                     strcmp(getenv("DOOM_TEST_MODE"), "failure") == 0 ||
                     strcmp(getenv("DOOM_TEST_MODE"), "fingerprint-during-failure") == 0 ||
                     strcmp(getenv("DOOM_TEST_MODE"), "fingerprint-retry-during-failure") == 0))
        trace_event("denied");
    return matches ? PAM_SUCCESS : PAM_AUTH_ERR;
}
