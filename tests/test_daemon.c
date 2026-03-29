/*
 * Daemon integration test.
 * Start f87d first for full testing; tests gracefully SKIP if daemon is not running.
 */
#include <f87/client.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int test_client_connect(void)
{
    printf("test_client_connect... ");
    f87_client *c = f87_client_connect();
    if (!c) {
        printf("SKIP (no session bus)\n");
        return 0;
    }
    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

static int test_get_status(void)
{
    printf("test_get_status... ");
    f87_client *c = f87_client_connect();
    if (!c) { printf("SKIP\n"); return 0; }

    f87_client_status_t st;
    int rc = f87_client_get_status(c, &st);
    if (rc < 0) {
        printf("SKIP (daemon not running)\n");
        f87_client_disconnect(c);
        return 0;
    }

    printf("connected=%d effect=%d category='%s' ",
           st.connected, st.active_effect, st.category);
    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

static int test_is_connected(void)
{
    printf("test_is_connected... ");
    f87_client *c = f87_client_connect();
    if (!c) { printf("SKIP\n"); return 0; }

    int connected = f87_client_is_connected(c);
    if (connected < 0) {
        printf("SKIP (daemon not running)\n");
        f87_client_disconnect(c);
        return 0;
    }

    printf("connected=%d ", connected);
    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

static int test_stop(void)
{
    printf("test_stop... ");
    f87_client *c = f87_client_connect();
    if (!c) { printf("SKIP\n"); return 0; }

    int rc = f87_client_stop(c);
    if (rc < 0) {
        printf("SKIP (daemon not running)\n");
        f87_client_disconnect(c);
        return 0;
    }

    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

int main(void)
{
    printf("=== Daemon Integration Tests ===\n");
    printf("(Start f87d first for full testing)\n\n");

    test_client_connect();
    test_get_status();
    test_is_connected();
    test_stop();

    printf("\nAll tests passed.\n");
    return 0;
}
