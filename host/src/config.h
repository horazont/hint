#include "private_config.h"

#if !defined(CONFIG_XMPP_JID) || !defined(CONFIG_XMPP_PASSWORD)
#error "Please define CONFIG_XMPP_JID and CONFIG_XMPP_PASSWORD as strings"
#endif

#if !defined(CONFIG_XMPP_PING_PEER)
#error "Please define CONFIG_XMPP_PING_PEER as string"
// this might in future become optional
#endif

#if !defined(CONFIG_COMM_DEVFILE)
#error "Please define CONFIG_COMM_DEVFILE as string"
#endif

#ifndef CONFIG_COMM_BAUDRATE
#define CONFIG_COMM_BAUDRATE (115200)
#endif
