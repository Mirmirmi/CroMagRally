/**
 @file  emscripten.c
 @brief ENet Emscripten WebSocket relay backend
*/
#ifdef __EMSCRIPTEN__

#include <arpa/inet.h>
#include <emscripten.h>
#include <string.h>
#include <time.h>

#define ENET_BUILDING_LIB 1
#include "enet/enet.h"

static enet_uint32 timeBase = 0;

int
enet_initialize (void)
{
    return 0;
}

void
enet_deinitialize (void)
{
    enet_relay_shutdown ();
}

enet_uint32
enet_host_random_seed (void)
{
    return (enet_uint32) time (NULL) ^ (enet_uint32) emscripten_get_now ();
}

enet_uint32
enet_time_get (void)
{
    return (enet_uint32) emscripten_get_now () - timeBase;
}

void
enet_time_set (enet_uint32 newTimeBase)
{
    timeBase = (enet_uint32) emscripten_get_now () - newTimeBase;
}

int
enet_address_set_host_ip (ENetAddress * address, const char * name)
{
    if (! inet_pton (AF_INET, name, & address -> host))
        return -1;
    return 0;
}

int
enet_address_set_host (ENetAddress * address, const char * name)
{
    return enet_address_set_host_ip (address, name);
}

int
enet_address_get_host_ip (const ENetAddress * address, char * name, size_t nameLength)
{
    if (inet_ntop (AF_INET, & address -> host, name, (socklen_t) nameLength) == NULL)
        return -1;
    return 0;
}

int
enet_address_get_host (const ENetAddress * address, char * name, size_t nameLength)
{
    return enet_address_get_host_ip (address, name, nameLength);
}

int
enet_socket_bind (ENetSocket socket, const ENetAddress * address)
{
    (void) socket;
    (void) address;
    return 0;
}

int
enet_socket_get_address (ENetSocket socket, ENetAddress * address)
{
    (void) socket;
    if (address != NULL)
    {
        address -> host = ENET_HOST_ANY;
        address -> port = 0;
    }
    return 0;
}

int
enet_socket_listen (ENetSocket socket, int backlog)
{
    (void) socket;
    (void) backlog;
    return 0;
}

ENetSocket
enet_socket_create (ENetSocketType type)
{
    (void) type;
    return 1;
}

int
enet_socket_set_option (ENetSocket socket, ENetSocketOption option, int value)
{
    (void) socket;
    (void) option;
    (void) value;
    return 0;
}

int
enet_socket_get_option (ENetSocket socket, ENetSocketOption option, int * value)
{
    (void) socket;
    (void) option;
    if (value != NULL)
        * value = 0;
    return 0;
}

int
enet_socket_connect (ENetSocket socket, const ENetAddress * address)
{
    (void) socket;
    (void) address;
    return 0;
}

ENetSocket
enet_socket_accept (ENetSocket socket, ENetAddress * address)
{
    (void) socket;
    (void) address;
    return ENET_SOCKET_NULL;
}

int
enet_socket_shutdown (ENetSocket socket, ENetSocketShutdown how)
{
    (void) socket;
    (void) how;
    return 0;
}

void
enet_socket_destroy (ENetSocket socket)
{
    (void) socket;
}

int
enet_socket_send (ENetSocket socket,
                  const ENetAddress * address,
                  const ENetBuffer * buffers,
                  size_t bufferCount)
{
    (void) socket;
    int r = enet_relay_try_send (address, buffers, bufferCount);
    if (r == -2)
        return -1;
    return r;
}

int
enet_socket_receive (ENetSocket socket,
                     ENetAddress * address,
                     ENetBuffer * buffers,
                     size_t bufferCount)
{
    (void) socket;
    return enet_relay_try_receive (address, buffers, bufferCount);
}

int
enet_socketset_select (ENetSocket maxSocket, ENetSocketSet * readSet, ENetSocketSet * writeSet, enet_uint32 timeout)
{
    (void) maxSocket;
    (void) readSet;
    (void) writeSet;
    (void) timeout;
    return 0;
}

int
enet_socket_wait (ENetSocket socket, enet_uint32 * condition, enet_uint32 timeout)
{
    (void) socket;
    (void) timeout;
    if (condition != NULL)
        * condition = ENET_SOCKET_WAIT_NONE;
    return 0;
}

#endif /* __EMSCRIPTEN__ */
