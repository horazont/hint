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

int main(int argc, char *argv[])
{
    struct xmpp_t xmpp;
    xmppintf_init(
        &xmpp,
        CONFIG_XMPP_JID,
        CONFIG_XMPP_PASSWORD,
        CONFIG_XMPP_PING_PEER,
        CONFIG_XMPP_WEATHER_PEER);

    struct comm_t comm;
    comm_init(
        &comm,
        CONFIG_COMM_DEVFILE,
        CONFIG_COMM_BAUDRATE);

    struct broker_t broker;
    broker_init(&broker, &comm, &xmpp);

    lpcd_set_brightness(&comm, 0x0FFF);

    while (1) {
        sleep(1);
    }

    return 0;
}
