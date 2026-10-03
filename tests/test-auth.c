/* Check conversation failures and secret cleanup with native sanitizers. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int allocation_count, fail_at;
static char *secrets[8];
static char *test_strdup(const char *text) {
    if (++allocation_count == fail_at) {
        return NULL;
    }
    char *copy = strdup(text);
    assert(allocation_count <= 8);
    secrets[allocation_count - 1] = copy;
    return copy;
}
static void test_free(void *pointer) {
    for (int i = 0; i < 8; i++) {
        if (pointer && pointer == secrets[i]) {
            assert(((char *)pointer)[0] == '\0');
            secrets[i] = NULL;
        }
    }
    free(pointer);
}
#define strdup test_strdup
#define free test_free
#include "../auth.c"
#undef strdup
#undef free

#ifndef AUTH_TEST_SANITIZED
static unsigned long locked_kib(void) {
    FILE *status = fopen("/proc/self/status", "r");
    assert(status);
    char line[256];
    unsigned long locked = 0;
    while (fgets(line, sizeof(line), status)) {
        if (sscanf(line, "VmLck: %lu", &locked) == 1) {
            break;
        }
    }
    fclose(status);
    return locked;
}
#endif
static void result(auth_channel_t channel, bool success) {
}
static void notice(const char *message) {
}

int main(void) {
    auth_watch(EV_DEFAULT, result, notice);
    const struct pam_message info = {PAM_TEXT_INFO, "Scan\nnow"};
    const struct pam_message prompt = {PAM_PROMPT_ECHO_OFF, "Password"};
    const struct pam_message bad = {999, "Invalid style"};
    const struct pam_message *messages[] = {&info, &prompt, &prompt};
    struct pam_response *responses = NULL;
    strcpy(attempts[AUTH_PASSWORD].password, "test-secret");
    assert(conversation(3, messages, &responses, &attempts[AUTH_PASSWORD]) == PAM_SUCCESS);
    assert(responses && !responses[0].resp);
    assert(strcmp(responses[1].resp, "test-secret") == 0);
    free_responses(responses, 3);
    responses = NULL;
    allocation_count = 0;
    fail_at = 2;
    assert(conversation(3, messages, &responses, &attempts[AUTH_PASSWORD]) == PAM_CONV_ERR);
    assert(!responses && !secrets[0]);
    fail_at = 0;
    assert(conversation(3, messages, &responses, &attempts[AUTH_FINGERPRINT]) == PAM_CONV_ERR);
    assert(!responses && strcmp(latest_notice, "Scan now") == 0);
    messages[1] = &bad;
    assert(conversation(2, messages, &responses, &attempts[AUTH_PASSWORD]) == PAM_CONV_ERR);
    assert(!responses);
    assert(conversation(0, messages, &responses, &attempts[AUTH_PASSWORD]) == PAM_CONV_ERR);
    assert(conversation(-1, messages, &responses, &attempts[AUTH_PASSWORD]) == PAM_CONV_ERR);
    assert(conversation(PAM_MAX_NUM_MSG + 1, messages, &responses, &attempts[AUTH_PASSWORD]) == PAM_CONV_ERR);
#ifndef AUTH_TEST_SANITIZED
    /* ASan intercepts mlock without locking pages. Check the kernel behavior
     * in a separate unsanitized build of this test. */
    assert(auth_lock_memory());
    unsigned long locked = locked_kib();
    assert(locked > 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        assert(locked_kib() == 0);
        assert(auth_lock_memory());
        assert(locked_kib() == locked);
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && status == 0);
#endif
    auth_close();
    ev_loop_destroy(EV_DEFAULT);
    puts("PASS: PAM responses are erased on success/error; invalid conversations fail");
    return 0;
}
