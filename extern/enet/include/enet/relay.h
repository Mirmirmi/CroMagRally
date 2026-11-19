/**
 @file  relay.h
 @brief Cro-Mag Rally WebSocket room relay & virtual loopback ENet socket hooks
*/
#ifndef __ENET_RELAY_H__
#define __ENET_RELAY_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ENET_RELAY_PROTOCOL_VERSION      5
#define ENET_RELAY_SYNTHETIC_HOST_IP     0x7F0001FEu /* 127.0.1.254 host byte order */
#define ENET_RELAY_SYNTHETIC_CLIENT_BASE 0x7F000100u /* 127.0.1.1 .. 127.0.1.7 host byte order */
#define ENET_RELAY_PROBE_MAGIC           0x434D5250u /* 'CMRP' */
#define ENET_RELAY_ACK_MAGIC             0x434D5241u /* 'CMRA' */
#define ENET_RELAY_MAX_CANDIDATES        16

typedef struct _ENetRelayLanCandidate
{
   enet_uint32 ipv4NetworkOrder;
   enet_uint16 portHostOrder;
} ENetRelayLanCandidate;

ENET_API void         enet_relay_set_url (const char * wsUrl);
ENET_API const char * enet_relay_get_url (void);

ENET_API int  enet_relay_host_start (const ENetRelayLanCandidate * candidates, size_t candidateCount);
ENET_API void enet_relay_host_poll (void);
ENET_API int  enet_relay_host_get_room_code (char * outCodeBuf, size_t bufSize);
ENET_API int  enet_relay_host_pop_disconnected_client (enet_uint8 * outClientId);

ENET_API void enet_relay_set_active_room_code (const char * roomCode);
ENET_API int  enet_relay_check_probe_room_code (const enet_uint8 * code4);

ENET_API int  enet_relay_client_join (const char * roomCode);
ENET_API int  enet_relay_client_poll_join (enet_uint8 * outClientId,
                                           ENetRelayLanCandidate * outCandidates,
                                           size_t maxCandidates,
                                           size_t * outCandidateCount,
                                           int * outRejectReason);
ENET_API int  enet_relay_client_is_connected (void);

ENET_API void enet_relay_shutdown (void);

ENET_API int  enet_relay_try_send (const ENetAddress * address, const ENetBuffer * buffers, size_t bufferCount);
ENET_API int  enet_relay_try_receive (ENetAddress * address, ENetBuffer * buffers, size_t bufferCount);

#ifdef __cplusplus
}
#endif

#endif /* __ENET_RELAY_H__ */
