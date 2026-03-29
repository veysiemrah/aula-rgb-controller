/*
 * f87d -- D-Bus daemon for Aula F87 keyboard RGB control
 *
 * Owns all USB access and effect execution. Clients (GUI, CLI)
 * communicate via D-Bus session bus (org.f87.Control).
 */

#include <systemd/sd-bus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static volatile sig_atomic_t g_quit = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_quit = 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    sd_bus *bus = NULL;
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "f87d: session bus: %s\n", strerror(-r));
        return EXIT_FAILURE;
    }

    r = sd_bus_request_name(bus, "org.f87.Control", 0);
    if (r < 0) {
        fprintf(stderr, "f87d: bus name: %s\n", strerror(-r));
        sd_bus_unref(bus);
        return EXIT_FAILURE;
    }

    printf("f87d: running on session bus\n");

    while (!g_quit) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) {
            fprintf(stderr, "f87d: bus process error: %s\n", strerror(-r));
            break;
        }
        if (r > 0)
            continue;

        r = sd_bus_wait(bus, 1000000); /* 1 second */
        if (r < 0 && r != -EINTR) {
            fprintf(stderr, "f87d: bus wait error: %s\n", strerror(-r));
            break;
        }
    }

    printf("f87d: shutting down\n");
    sd_bus_release_name(bus, "org.f87.Control");
    sd_bus_unref(bus);
    return EXIT_SUCCESS;
}
