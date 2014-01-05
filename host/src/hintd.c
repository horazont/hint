#include <signal.h>
#include <pthread.h>
#include <unistd.h>

#include "common/comm.h"

#include "config.h"
#include "xmppintf.h"
#include "comm.h"
#include "broker.h"
#include "utils.h"
#include "lpcdisplay.h"

#include "weather.h"
#include "array.h"

#define PERIODIC_SLEEP (100)

static volatile bool terminated = false;

void sigterm(int signum)
{
    terminated = true;
    fprintf(stderr, "SIGTERM / SIGINT\n");
}

int main(int argc, char *argv[])
{
    struct xmpp_t xmpp;
    xmppintf_init(
        &xmpp,
        CONFIG_XMPP_JID,
        CONFIG_XMPP_PASSWORD,
        CONFIG_XMPP_PING_PEER,
        CONFIG_XMPP_WEATHER_PEER,
        CONFIG_XMPP_DEPARTURE_PEER);

    struct comm_t comm;
    comm_init(
        &comm,
        CONFIG_COMM_DEVFILE,
        CONFIG_COMM_BAUDRATE);

    struct broker_t broker;
    broker_init(&broker, &comm, &xmpp);

    lpcd_set_brightness(&comm, 0x0FFF);

    signal(SIGTERM, &sigterm);
    signal(SIGINT, &sigterm);

    while (1) {
        if (sleep(PERIODIC_SLEEP) < PERIODIC_SLEEP) {
            if (terminated) {
                break;
            }
        }
    }

    broker_free(&broker);
    comm_free(&comm);
    xmppintf_free(&xmpp);

    return 0;
}
