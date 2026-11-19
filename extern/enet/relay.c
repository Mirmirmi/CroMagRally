/**
 @file  relay.c
 @brief Cro-Mag Rally WebSocket room relay client (Emscripten & Native)
*/

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#define ENET_BUILDING_LIB 1
#include "enet/enet.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELAY_DEFAULT_URL      "wss://cromagrally.bridgewirelr.com/ws"
#define RELAY_OP_REGISTER_HOST 0x01
#define RELAY_OP_ROOM_ASSIGNED 0x02
#define RELAY_OP_JOIN_ROOM     0x03
#define RELAY_OP_JOIN_ACCEPTED 0x04
#define RELAY_OP_JOIN_REJECTED 0x05
#define RELAY_OP_RELAY_PACKET  0x06
#define RELAY_OP_PEER_DISC     0x07
#define RELAY_OP_PING          0x08
#define RELAY_OP_PONG          0x09

static char sRelayUrlOverride[256] = "";
static int  sRelayUrlDisabled = 0;
static char sActiveRoomCode[5] = "";

void enet_relay_set_url(const char *wsUrl)
{
   if (!wsUrl)
   {
      sRelayUrlDisabled = 0;
      sRelayUrlOverride[0] = '\0';
      return;
   }
   if (!wsUrl[0] || strcmp(wsUrl, "none") == 0 || strcmp(wsUrl, "off") == 0)
   {
      sRelayUrlDisabled = 1;
      sRelayUrlOverride[0] = '\0';
      return;
   }
   sRelayUrlDisabled = 0;
   snprintf(sRelayUrlOverride, sizeof(sRelayUrlOverride), "%s", wsUrl);
}

const char *enet_relay_get_url(void)
{
   if (sRelayUrlDisabled)
      return "";
   if (sRelayUrlOverride[0])
      return sRelayUrlOverride;
#ifndef __EMSCRIPTEN__
   const char *env = getenv("CROMAG_RELAY_URL");
   if (env)
   {
      if (!env[0] || strcmp(env, "none") == 0 || strcmp(env, "off") == 0)
         return "";
      return env;
   }
#endif
   return RELAY_DEFAULT_URL;
}

void enet_relay_set_active_room_code(const char *roomCode)
{
   memset(sActiveRoomCode, 0, sizeof(sActiveRoomCode));
   if (!roomCode)
      return;
   for (int i = 0; i < 4 && roomCode[i]; i++)
      sActiveRoomCode[i] = (char)toupper((unsigned char)roomCode[i]);
}

int enet_relay_check_probe_room_code(const enet_uint8 *code4)
{
   if (!code4 || strlen(sActiveRoomCode) != 4)
      return 0;
   for (int i = 0; i < 4; i++)
   {
      if ((char)toupper(code4[i]) != sActiveRoomCode[i])
         return 0;
   }
   return 1;
}

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

EM_JS(void, js_relay_close, (void), {
   const st = window.__croMagRelay;
   if (st) {
      if (st.ws) {
         try {
            st.ws.onopen = null;
            st.ws.onmessage = null;
            st.ws.onerror = null;
            st.ws.onclose = null;
            if (st.ws.readyState === 0 || st.ws.readyState === 1) {
               st.ws.close();
            }
         } catch (e) {}
      }
      if (st.wakeLock) {
         try { st.wakeLock.release(); } catch (e) {}
         st.wakeLock = null;
      }
   }
   window.__croMagRelay = null;
});

EM_JS(int, js_relay_open, (const char *overrideUrlPtr, int isHost, const char *codePtr), {
   const overrideUrl = UTF8ToString(overrideUrlPtr);
   const codeStr = codePtr ? UTF8ToString(codePtr).toUpperCase() : '';
   let url = overrideUrl;
   if (!url) {
      if (window.__croMagRelayUrl) {
         url = window.__croMagRelayUrl;
      } else {
         const params = new URLSearchParams(window.location.search || '');
         const qp = params.get('relay');
         if (qp) {
            url = qp;
         } else if (window.location && window.location.hostname === 'cromagrally.bridgewirelr.com') {
            const proto = (window.location.protocol === 'https:') ? 'wss://' : 'ws://';
            url = proto + window.location.host + '/ws';
         } else {
            url = 'wss://cromagrally.bridgewirelr.com/ws';
         }
      }
   }

   if (window.__croMagRelay) {
      if (window.__croMagRelay.ws) {
         try { window.__croMagRelay.ws.close(); } catch (e) {}
      }
      if (window.__croMagRelay.wakeLock) {
         try { window.__croMagRelay.wakeLock.release(); } catch (e) {}
         window.__croMagRelay.wakeLock = null;
      }
   }

   const state = {
      isHost: !!isHost,
      ws: null,
      hostRoomState: 0, /* 0=pending, 1=assigned, -1=failed */
      roomCode: '',
      joinState: 0,
      rejectReason: 0,
      clientId: 0,
      connected: false,
      rxQueue: [],      /* { clientId: number, data: Uint8Array } */
      discQueue: [],    /* number[] */
      candidates: [],
      wakeLock: null
   };
   window.__croMagRelay = state;

   function markDisconnected() {
      if (window.__croMagRelay !== state) return;
      if (state.isHost) {
         state.hostRoomState = -1;
         if (state.wakeLock) {
            try { state.wakeLock.release(); } catch (e) {}
            state.wakeLock = null;
         }
      } else if (state.joinState === 0) {
         state.joinState = -1;
         state.rejectReason = 4;
      }
      state.connected = false;
   }

   let ws;
   try {
      ws = new WebSocket(url);
   } catch (e) {
      if (state.isHost) {
         state.hostRoomState = -1;
      } else {
         state.joinState = -1;
         state.rejectReason = 4;
      }
      return -1;
   }
   ws.binaryType = 'arraybuffer';
   state.ws = ws;

   if (state.isHost && typeof navigator !== 'undefined' && navigator.wakeLock &&
       typeof navigator.wakeLock.request === 'function') {
      navigator.wakeLock.request('screen').then(function (sentinel) {
         if (window.__croMagRelay === state) {
            state.wakeLock = sentinel;
         } else {
            try { sentinel.release(); } catch (e) {}
         }
      }).catch(function () {});
   }

   ws.onopen = function () {
      if (window.__croMagRelay !== state) return;
      if (state.isHost) {
         /* OP_REGISTER_HOST (0x01), protocol version 5 LE, 0 LAN candidates */
         ws.send(new Uint8Array([0x01, 5, 0, 0]));
      } else {
         const frame = new Uint8Array(7);
         frame[0] = 0x03; /* JoinRoom */
         frame[1] = 5;    /* version lo */
         frame[2] = 0;    /* version hi */
         for (let i = 0; i < 4; i++) {
            frame[3 + i] = i < codeStr.length ? codeStr.charCodeAt(i) : 0;
         }
         ws.send(frame);
      }
   };

   ws.onmessage = function (ev) {
      if (window.__croMagRelay !== state) return;
      if (!(ev.data instanceof ArrayBuffer)) return;
      const u8 = new Uint8Array(ev.data);
      if (u8.length < 1) return;
      const op = u8[0];
      if (op === 0x08) {
         try { ws.send(new Uint8Array([0x09])); } catch (e) {}
      } else if (state.isHost && op === 0x02 && u8.length >= 5) {
         state.roomCode = String.fromCharCode(u8[1], u8[2], u8[3], u8[4]);
         state.hostRoomState = 1;
         state.connected = true;
      } else if (!state.isHost && op === 0x04 && u8.length >= 7) {
         state.joinState = 1;
         state.clientId = u8[5];
         state.connected = true;
         const count = Math.min(u8[6], Math.floor((u8.length - 7) / 6));
         state.candidates = [];
         for (let i = 0; i < count; i++) {
            const off = 7 + i * 6;
            const ip = ((u8[off] << 24) | (u8[off + 1] << 16) | (u8[off + 2] << 8) | u8[off + 3]) >>> 0;
            const port = u8[off + 4] | (u8[off + 5] << 8);
            state.candidates.push([ip, port]);
         }
      } else if (op === 0x05) {
         if (state.isHost) {
            state.hostRoomState = -1;
         } else {
            state.joinState = -1;
            state.rejectReason = u8.length >= 2 ? u8[1] : 4;
         }
         state.connected = false;
      } else if (op === 0x06 && u8.length >= 3) {
         if (state.rxQueue.length < 512) {
            state.rxQueue.push({ clientId: u8[1], data: u8.slice(2) });
         }
      } else if (state.isHost && op === 0x07 && u8.length >= 2) {
         if (state.discQueue.length < 64) {
            state.discQueue.push(u8[1]);
         }
      }
   };

   ws.onerror = markDisconnected;
   ws.onclose = markDisconnected;

   return 0;
});

EM_JS(int, js_relay_host_get_room_code, (char *outCodeBuf, int bufSize), {
   const st = window.__croMagRelay;
   if (!st || !st.isHost) return -1;
   if (st.hostRoomState === 1 && st.roomCode && st.roomCode.length === 4) {
      if (outCodeBuf && bufSize > 0) {
         stringToUTF8(st.roomCode, outCodeBuf, bufSize);
      }
      return 1;
   }
   if (st.hostRoomState < 0) return -1;
   return 0;
});

EM_JS(int, js_relay_host_pop_disc, (void), {
   const st = window.__croMagRelay;
   if (!st || !st.isHost || !st.discQueue || st.discQueue.length === 0) return 0;
   return st.discQueue.shift() | 0;
});

EM_JS(int, js_relay_poll_join, (int *outClientId, int *outRejectReason), {
   const st = window.__croMagRelay;
   if (!st) {
      HEAP32[outRejectReason >> 2] = 4;
      return -1;
   }
   if (st.joinState === 1) {
      HEAP32[outClientId >> 2] = st.clientId;
      return 1;
   }
   if (st.joinState < 0) {
      HEAP32[outRejectReason >> 2] = st.rejectReason || 4;
      return -1;
   }
   return 0;
});

EM_JS(int, js_relay_is_connected, (void), {
   const st = window.__croMagRelay;
   return (st && st.connected && st.ws && st.ws.readyState === 1) ? 1 : 0;
});

EM_JS(int, js_relay_send_packet, (const uint8_t *framePtr, int frameLen), {
   const st = window.__croMagRelay;
   if (!st || !st.ws || st.ws.readyState !== 1) return -1;
   try {
      st.ws.send(HEAPU8.subarray(framePtr, framePtr + frameLen));
      return frameLen - 2;
   } catch (e) {
      return -1;
   }
});

EM_JS(int, js_relay_pop_packet, (uint8_t *dstPtr, int dstCap, int *outClientId, int *outIsHost), {
   const st = window.__croMagRelay;
   if (!st || st.rxQueue.length === 0) return 0;
   const item = st.rxQueue.shift();
   const pkt = item.data;
   if (pkt.length > dstCap) return -2;
   HEAPU8.set(pkt, dstPtr);
   if (outClientId) HEAP32[outClientId >> 2] = item.clientId | 0;
   if (outIsHost) HEAP32[outIsHost >> 2] = st.isHost ? 1 : 0;
   return pkt.length;
});

int enet_relay_host_start(const ENetRelayLanCandidate *candidates, size_t candidateCount)
{
   (void)candidates;
   (void)candidateCount;
   if (sRelayUrlDisabled)
      return -1;
   js_relay_close();
   return js_relay_open(sRelayUrlOverride, 1, "");
}

void enet_relay_host_poll(void)
{
}

int enet_relay_host_get_room_code(char *outCodeBuf, size_t bufSize)
{
   return js_relay_host_get_room_code(outCodeBuf, (int)bufSize);
}

int enet_relay_host_pop_disconnected_client(enet_uint8 *outClientId)
{
   int cid = js_relay_host_pop_disc();
   if (cid > 0)
   {
      if (outClientId)
         *outClientId = (enet_uint8)cid;
      return 1;
   }
   return 0;
}

int enet_relay_client_join(const char *roomCode)
{
   if (!roomCode || strlen(roomCode) != 4)
      return -1;
   js_relay_close();
   return js_relay_open(sRelayUrlOverride, 0, roomCode);
}

int enet_relay_client_poll_join(enet_uint8 *outClientId,
                                ENetRelayLanCandidate *outCandidates,
                                size_t maxCandidates,
                                size_t *outCandidateCount,
                                int *outRejectReason)
{
   (void)outCandidates;
   (void)maxCandidates;
   if (outCandidateCount)
      *outCandidateCount = 0;

   int cid = 0;
   int reason = 0;
   int res = js_relay_poll_join(&cid, &reason);
   if (res == 1 && outClientId)
      *outClientId = (enet_uint8)cid;
   if (res < 0 && outRejectReason)
      *outRejectReason = reason;
   return res;
}

int enet_relay_client_is_connected(void)
{
   return js_relay_is_connected();
}

void enet_relay_shutdown(void)
{
   sActiveRoomCode[0] = '\0';
   js_relay_close();
}

int enet_relay_try_send(const ENetAddress *address, const ENetBuffer *buffers, size_t bufferCount)
{
   if (!address || !buffers || bufferCount == 0)
      return -2;

   enet_uint32 hostOrder = ENET_NET_TO_HOST_32(address->host);
   enet_uint8 targetId = 0;
   if (hostOrder == ENET_RELAY_SYNTHETIC_HOST_IP)
   {
      targetId = 0;
   }
   else if ((hostOrder & 0xFFFFFF00u) == ENET_RELAY_SYNTHETIC_CLIENT_BASE &&
            (hostOrder & 0xFFu) >= 1u && (hostOrder & 0xFFu) <= 7u)
   {
      targetId = (enet_uint8)(hostOrder & 0xFFu);
   }
   else
   {
      return -2;
   }

   enet_uint8 frame[2 + 2048];
   frame[0] = RELAY_OP_RELAY_PACKET;
   frame[1] = targetId;
   size_t total = 0;
   for (size_t i = 0; i < bufferCount; i++)
   {
      if (buffers[i].dataLength == 0)
         continue;
      if (total + buffers[i].dataLength > 2048)
         return -1;
      memcpy(frame + 2 + total, buffers[i].data, buffers[i].dataLength);
      total += buffers[i].dataLength;
   }
   if (total == 0)
      return 0;

   int sent = js_relay_send_packet(frame, (int)(2 + total));
   return (sent < 0) ? 0 : sent;
}

int enet_relay_try_receive(ENetAddress *address, ENetBuffer *buffers, size_t bufferCount)
{
   if (!buffers || bufferCount == 0 || !buffers[0].data || buffers[0].dataLength == 0)
      return 0;

   int cid = 0;
   int isHost = 0;
   int r = js_relay_pop_packet((uint8_t *)buffers[0].data, (int)buffers[0].dataLength, &cid, &isHost);
   if (r > 0 && address)
   {
      address->port = 29400;
      if (isHost)
         address->host = ENET_HOST_TO_NET_32(ENET_RELAY_SYNTHETIC_CLIENT_BASE | (enet_uint32)(cid & 0xFF));
      else
         address->host = ENET_HOST_TO_NET_32(ENET_RELAY_SYNTHETIC_HOST_IP);
   }
   return r;
}

#else /* !__EMSCRIPTEN__ */

#ifdef _WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   typedef SOCKET RelaySock;
   #define RELAY_INVALID_SOCK INVALID_SOCKET
   #define RelayCloseSock(s)  closesocket(s)
   #ifndef MSG_NOSIGNAL
      #define MSG_NOSIGNAL 0
   #endif
#else
   #include <errno.h>
   #include <fcntl.h>
   #include <netdb.h>
   #include <netinet/in.h>
   #include <netinet/tcp.h>
   #include <sys/select.h>
   #include <sys/socket.h>
   #include <sys/types.h>
   #include <unistd.h>
   typedef int RelaySock;
   #define RELAY_INVALID_SOCK (-1)
   #define RelayCloseSock(s)  close(s)
   #ifndef MSG_NOSIGNAL
      #define MSG_NOSIGNAL 0
   #endif
#endif

#if defined(__APPLE__)
   #include <CoreFoundation/CoreFoundation.h>
   #include <Security/SecureTransport.h>
   #include <Security/Security.h>
#endif

typedef enum
{
   RELAY_STATE_IDLE = 0,
   RELAY_STATE_TCP_CONNECTING,
   RELAY_STATE_TLS_HANDSHAKE,
   RELAY_STATE_WS_UPGRADE,
   RELAY_STATE_OPEN,
   RELAY_STATE_FAILED
} RelayConnState;

typedef struct
{
   enet_uint8  clientId;
   size_t      len;
   enet_uint8  data[2048];
} RelayQueuedPacket;

#define RELAY_TX_CAP       65536
#define RELAY_RX_CAP       65536
#define RELAY_PKT_CAP      256
#define RELAY_DISC_CAP     32
#define RELAY_PING_MS      15000u

static RelayConnState sConnState = RELAY_STATE_IDLE;
static RelaySock      sSock = RELAY_INVALID_SOCK;
static int            sIsHostMode = 0;
static int            sUseTls = 0;
static char           sParsedHost[128] = "";
static char           sParsedHostHdr[144] = "";
static char           sParsedPath[128] = "/ws";
static enet_uint16    sParsedPort = 80;

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static SSLContextRef  sTlsCtx = NULL;
static int            sTlsHasBufferedWrite = 0;
#pragma clang diagnostic pop
#endif

static enet_uint8     sTxBuf[RELAY_TX_CAP];
static size_t         sTxLen = 0;
static enet_uint8     sRxBuf[RELAY_RX_CAP];
static size_t         sRxLen = 0;

static enet_uint8     sFragBuf[8192];
static size_t         sFragLen = 0;
static int            sInFrag = 0;

static enet_uint8     sPendingInitMsg[256];
static size_t         sPendingInitLen = 0;
static enet_uint32    sLastPingSentMs = 0;
static enet_uint32    sMaskPrng = 0xA5A55A5Au;

/* Host state */
static int            sHostRoomState = 0; /* 0=pending, 1=assigned, -1=failed */
static char           sHostRoomCode[5] = "";
static enet_uint8     sDiscQueue[RELAY_DISC_CAP];
static size_t         sDiscHead = 0;
static size_t         sDiscCount = 0;

/* Client state */
static int            sClientJoinState = 0; /* 0=pending, 1=accepted, -1=rejected */
static int            sClientRejectReason = 0;
static enet_uint8     sClientAssignedId = 0;
static ENetRelayLanCandidate sClientCands[ENET_RELAY_MAX_CANDIDATES];
static size_t         sClientCandCount = 0;

/* Incoming packet queue */
static RelayQueuedPacket sPktQueue[RELAY_PKT_CAP];
static size_t            sPktHead = 0;
static size_t            sPktCount = 0;

static int RelayLastSockErrWouldBlock(void)
{
#ifdef _WIN32
   int e = WSAGetLastError();
   return (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS);
#else
   return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS);
#endif
}

static void RelaySetNonBlocking(RelaySock sock)
{
#ifdef _WIN32
   u_long mode = 1;
   ioctlsocket(sock, FIONBIO, &mode);
#else
   int flags = fcntl(sock, F_GETFL, 0);
   if (flags >= 0)
      fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

static enet_uint32 RelayNextRand(void)
{
   enet_uint32 x = sMaskPrng ^ enet_time_get();
   x ^= x << 13;
   x ^= x >> 17;
   x ^= x << 5;
   if (x == 0) x = 0x13579BDFu;
   sMaskPrng = x;
   return x;
}

static void Base64Encode16(const enet_uint8 in[16], char out[25])
{
   static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   int o = 0;
   for (int i = 0; i < 16; i += 3)
   {
      enet_uint32 b0 = in[i];
      enet_uint32 b1 = (i + 1 < 16) ? in[i + 1] : 0;
      enet_uint32 b2 = (i + 2 < 16) ? in[i + 2] : 0;
      enet_uint32 v = (b0 << 16) | (b1 << 8) | b2;
      out[o++] = tbl[(v >> 18) & 0x3F];
      out[o++] = tbl[(v >> 12) & 0x3F];
      out[o++] = (i + 1 < 16) ? tbl[(v >> 6) & 0x3F] : '=';
      out[o++] = (i + 2 < 16) ? tbl[v & 0x3F] : '=';
   }
   out[o] = '\0';
}

static int ParseRelayUrl(const char *url)
{
   if (!url || !url[0])
      return -1;

   int tls = 0;
   enet_uint16 port = 80;
   const char *p = url;
   if (strncmp(p, "wss://", 6) == 0)
   {
      tls = 1;
      port = 443;
      p += 6;
   }
   else if (strncmp(p, "ws://", 5) == 0)
   {
      tls = 0;
      port = 80;
      p += 5;
   }
   else
   {
      return -1;
   }

   const char *slash = strchr(p, '/');
   size_t hostPortLen = slash ? (size_t)(slash - p) : strlen(p);
   if (hostPortLen == 0 || hostPortLen >= sizeof(sParsedHostHdr))
      return -1;

   char hostPort[144];
   memcpy(hostPort, p, hostPortLen);
   hostPort[hostPortLen] = '\0';

   if (slash && slash[0])
      snprintf(sParsedPath, sizeof(sParsedPath), "%s", slash);
   else
      snprintf(sParsedPath, sizeof(sParsedPath), "/");

   char *colon = strrchr(hostPort, ':');
   if (colon && strchr(hostPort, ']') == NULL)
   {
      *colon = '\0';
      int parsed = atoi(colon + 1);
      if (parsed <= 0 || parsed > 65535)
         return -1;
      port = (enet_uint16)parsed;
   }

   snprintf(sParsedHost, sizeof(sParsedHost), "%s", hostPort);
   if ((tls && port == 443) || (!tls && port == 80))
      snprintf(sParsedHostHdr, sizeof(sParsedHostHdr), "%s", sParsedHost);
   else
      snprintf(sParsedHostHdr, sizeof(sParsedHostHdr), "%s:%u", sParsedHost, (unsigned)port);

#if !defined(__APPLE__)
   /* Non-Apple builds fall back to ws:// (port 80) when TLS is unavailable */
   if (tls)
   {
      tls = 0;
      if (port == 443)
      {
         port = 80;
         snprintf(sParsedHostHdr, sizeof(sParsedHostHdr), "%s", sParsedHost);
      }
   }
#endif

   sUseTls = tls;
   sParsedPort = port;
   return 0;
}

static void CloseRelayTransport(int failed)
{
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
   if (sTlsCtx)
   {
      SSLClose(sTlsCtx);
      CFRelease(sTlsCtx);
      sTlsCtx = NULL;
   }
   sTlsHasBufferedWrite = 0;
#pragma clang diagnostic pop
#endif

   if (sSock != RELAY_INVALID_SOCK)
   {
      RelayCloseSock(sSock);
      sSock = RELAY_INVALID_SOCK;
   }

   sTxLen = 0;
   sRxLen = 0;
   sFragLen = 0;
   sInFrag = 0;
   sConnState = failed ? RELAY_STATE_FAILED : RELAY_STATE_IDLE;

   if (failed)
   {
      if (sIsHostMode)
         sHostRoomState = -1;
      if (!sIsHostMode && sClientJoinState == 0)
      {
         sClientJoinState = -1;
         if (sClientRejectReason == 0)
            sClientRejectReason = 4;
      }
   }
}

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static OSStatus RelayTlsReadFunc(SSLConnectionRef connection, void *data, size_t *dataLength)
{
   RelaySock sock = (RelaySock)(intptr_t)connection;
   size_t requested = *dataLength;
   size_t total = 0;
   enet_uint8 *dst = (enet_uint8 *)data;

   while (total < requested)
   {
      ssize_t n = recv(sock, dst + total, requested - total, 0);
      if (n > 0)
      {
         total += (size_t)n;
      }
      else if (n == 0)
      {
         *dataLength = total;
         return errSSLClosedGraceful;
      }
      else
      {
         if (errno == EINTR)
            continue;
         if (errno == EAGAIN || errno == EWOULDBLOCK)
         {
            *dataLength = total;
            return errSSLWouldBlock;
         }
         *dataLength = total;
         return errSSLClosedAbort;
      }
   }
   *dataLength = total;
   return noErr;
}

static OSStatus RelayTlsWriteFunc(SSLConnectionRef connection, const void *data, size_t *dataLength)
{
   RelaySock sock = (RelaySock)(intptr_t)connection;
   size_t requested = *dataLength;
   size_t total = 0;
   const enet_uint8 *src = (const enet_uint8 *)data;

   while (total < requested)
   {
      ssize_t n = send(sock, src + total, requested - total, MSG_NOSIGNAL);
      if (n > 0)
      {
         total += (size_t)n;
      }
      else if (n == 0)
      {
         *dataLength = total;
         return errSSLClosedAbort;
      }
      else
      {
         if (errno == EINTR)
            continue;
         if (errno == EAGAIN || errno == EWOULDBLOCK)
         {
            *dataLength = total;
            return errSSLWouldBlock;
         }
         *dataLength = total;
         return errSSLClosedAbort;
      }
   }
   *dataLength = total;
   return noErr;
}

static int BeginTlsHandshake(void)
{
   sTlsCtx = SSLCreateContext(kCFAllocatorDefault, kSSLClientSide, kSSLStreamType);
   if (!sTlsCtx)
      return -1;
   if (SSLSetIOFuncs(sTlsCtx, RelayTlsReadFunc, RelayTlsWriteFunc) != noErr ||
       SSLSetConnection(sTlsCtx, (SSLConnectionRef)(intptr_t)sSock) != noErr ||
       SSLSetPeerDomainName(sTlsCtx, sParsedHost, strlen(sParsedHost)) != noErr ||
       SSLSetProtocolVersionMin(sTlsCtx, kTLSProtocol12) != noErr)
   {
      CFRelease(sTlsCtx);
      sTlsCtx = NULL;
      return -1;
   }
   sTlsHasBufferedWrite = 0;
   sConnState = RELAY_STATE_TLS_HANDSHAKE;
   return 0;
}

#pragma clang diagnostic pop
#endif

static int QueueRawTx(const enet_uint8 *data, size_t len)
{
   if (sTxLen + len > sizeof(sTxBuf))
      return -1;
   memcpy(sTxBuf + sTxLen, data, len);
   sTxLen += len;
   return 0;
}

static int FlushTx(void)
{
   if (sSock == RELAY_INVALID_SOCK)
      return -1;

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
   if (sUseTls && sTlsCtx)
   {
      while (sTxLen > 0 || sTlsHasBufferedWrite)
      {
         size_t processed = 0;
         OSStatus st = SSLWrite(sTlsCtx, sTxLen > 0 ? sTxBuf : NULL, sTxLen, &processed);
         if (processed > 0 && processed <= sTxLen)
         {
            if (processed < sTxLen)
               memmove(sTxBuf, sTxBuf + processed, sTxLen - processed);
            sTxLen -= processed;
         }
         if (st == noErr)
         {
            sTlsHasBufferedWrite = 0;
         }
         else if (st == errSSLWouldBlock)
         {
            sTlsHasBufferedWrite = 1;
            break;
         }
         else
         {
            return -1;
         }
      }
      return 0;
   }
#pragma clang diagnostic pop
#endif

   while (sTxLen > 0)
   {
      int n = (int)send(sSock, (const char *)sTxBuf,
#ifdef _WIN32
                        (int)sTxLen,
#else
                        sTxLen,
#endif
                        MSG_NOSIGNAL);
      if (n > 0)
      {
         size_t sent = (size_t)n;
         if (sent < sTxLen)
            memmove(sTxBuf, sTxBuf + sent, sTxLen - sent);
         sTxLen -= sent;
      }
      else if (n < 0 && RelayLastSockErrWouldBlock())
      {
         break;
      }
      else
      {
         return -1;
      }
   }
   return 0;
}

static int PumpRx(void)
{
   if (sSock == RELAY_INVALID_SOCK)
      return -1;

   while (sRxLen < sizeof(sRxBuf))
   {
      size_t space = sizeof(sRxBuf) - sRxLen;
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      if (sUseTls && sTlsCtx)
      {
         size_t processed = 0;
         OSStatus st = SSLRead(sTlsCtx, sRxBuf + sRxLen, space, &processed);
         if (processed > 0)
            sRxLen += processed;
         if (st == noErr)
         {
            if (processed == 0)
               break;
            continue;
         }
         if (st == errSSLWouldBlock)
            break;
         return -1;
      }
#pragma clang diagnostic pop
#endif

      int n = (int)recv(sSock, (char *)(sRxBuf + sRxLen),
#ifdef _WIN32
                        (int)space,
#else
                        space,
#endif
                        0);
      if (n > 0)
      {
         sRxLen += (size_t)n;
      }
      else if (n == 0)
      {
         return -1;
      }
      else if (RelayLastSockErrWouldBlock())
      {
         break;
      }
      else
      {
         return -1;
      }
   }
   return 0;
}

static int QueueWsFrame(enet_uint8 opcode, const enet_uint8 *payload, size_t len)
{
   if (len > 65535)
      return -1;

   enet_uint8 hdr[8];
   size_t hdrLen = 0;
   hdr[0] = (enet_uint8)(0x80u | (opcode & 0x0Fu));
   if (len <= 125)
   {
      hdr[1] = (enet_uint8)(0x80u | len);
      hdrLen = 2;
   }
   else
   {
      hdr[1] = (enet_uint8)(0x80u | 126u);
      hdr[2] = (enet_uint8)((len >> 8) & 0xFFu);
      hdr[3] = (enet_uint8)(len & 0xFFu);
      hdrLen = 4;
   }

   enet_uint32 maskVal = RelayNextRand();
   enet_uint8 mask[4];
   mask[0] = (enet_uint8)(maskVal & 0xFFu);
   mask[1] = (enet_uint8)((maskVal >> 8) & 0xFFu);
   mask[2] = (enet_uint8)((maskVal >> 16) & 0xFFu);
   mask[3] = (enet_uint8)((maskVal >> 24) & 0xFFu);

   if (sTxLen + hdrLen + 4 + len > sizeof(sTxBuf))
      return -1;

   memcpy(sTxBuf + sTxLen, hdr, hdrLen);
   sTxLen += hdrLen;
   memcpy(sTxBuf + sTxLen, mask, 4);
   sTxLen += 4;
   for (size_t i = 0; i < len; i++)
      sTxBuf[sTxLen + i] = (enet_uint8)(payload[i] ^ mask[i & 3u]);
   sTxLen += len;
   return 0;
}

static void SendWsUpgradeRequest(void)
{
   enet_uint8 nonce[16];
   for (int i = 0; i < 16; i += 4)
   {
      enet_uint32 r = RelayNextRand();
      nonce[i + 0] = (enet_uint8)(r & 0xFFu);
      nonce[i + 1] = (enet_uint8)((r >> 8) & 0xFFu);
      nonce[i + 2] = (enet_uint8)((r >> 16) & 0xFFu);
      nonce[i + 3] = (enet_uint8)((r >> 24) & 0xFFu);
   }
   char keyB64[25];
   Base64Encode16(nonce, keyB64);

   char req[512];
   int reqLen = snprintf(req, sizeof(req),
                         "GET %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "Upgrade: websocket\r\n"
                         "Connection: Upgrade\r\n"
                         "Sec-WebSocket-Key: %s\r\n"
                         "Sec-WebSocket-Version: 13\r\n"
                         "\r\n",
                         sParsedPath, sParsedHostHdr, keyB64);
   if (reqLen > 0 && (size_t)reqLen < sizeof(req))
   {
      QueueRawTx((const enet_uint8 *)req, (size_t)reqLen);
      FlushTx();
   }
   sConnState = RELAY_STATE_WS_UPGRADE;
}

static void HandleRelayAppMessage(const enet_uint8 *data, size_t len)
{
   if (len == 0)
      return;

   enet_uint8 op = data[0];
   switch (op)
   {
      case RELAY_OP_ROOM_ASSIGNED:
         if (sIsHostMode && len >= 5)
         {
            memcpy(sHostRoomCode, data + 1, 4);
            sHostRoomCode[4] = '\0';
            sHostRoomState = 1;
            enet_relay_set_active_room_code(sHostRoomCode);
         }
         break;

      case RELAY_OP_JOIN_ACCEPTED:
         if (!sIsHostMode && len >= 7)
         {
            sClientAssignedId = data[5];
            size_t count = data[6];
            size_t avail = (len - 7) / 6;
            if (count > avail) count = avail;
            if (count > ENET_RELAY_MAX_CANDIDATES) count = ENET_RELAY_MAX_CANDIDATES;
            sClientCandCount = count;
            for (size_t i = 0; i < count; i++)
            {
               const enet_uint8 *p = data + 7 + i * 6;
               memcpy(&sClientCands[i].ipv4NetworkOrder, p, 4);
               sClientCands[i].portHostOrder = (enet_uint16)(p[4] | ((enet_uint16)p[5] << 8));
            }
            sClientJoinState = 1;
         }
         break;

      case RELAY_OP_JOIN_REJECTED:
         if (!sIsHostMode)
         {
            sClientRejectReason = (len >= 2) ? (int)data[1] : 4;
            sClientJoinState = -1;
         }
         break;

      case RELAY_OP_RELAY_PACKET:
         if (len >= 3)
         {
            size_t payloadLen = len - 2;
            if (payloadLen <= 2048 && sPktCount < RELAY_PKT_CAP)
            {
               size_t tail = (sPktHead + sPktCount) % RELAY_PKT_CAP;
               sPktQueue[tail].clientId = data[1];
               sPktQueue[tail].len = payloadLen;
               memcpy(sPktQueue[tail].data, data + 2, payloadLen);
               sPktCount++;
            }
         }
         break;

      case RELAY_OP_PEER_DISC:
         if (sIsHostMode && len >= 2 && sDiscCount < RELAY_DISC_CAP)
         {
            size_t tail = (sDiscHead + sDiscCount) % RELAY_DISC_CAP;
            sDiscQueue[tail] = data[1];
            sDiscCount++;
         }
         break;

      case RELAY_OP_PING:
      {
         enet_uint8 pong = RELAY_OP_PONG;
         QueueWsFrame(0x2, &pong, 1);
         FlushTx();
         break;
      }

      case RELAY_OP_PONG:
      default:
         break;
   }
}

static int ProcessOpenWsFrames(void)
{
   while (sRxLen >= 2)
   {
      enet_uint8 b0 = sRxBuf[0];
      enet_uint8 b1 = sRxBuf[1];
      int fin = (b0 & 0x80u) != 0;
      int opcode = b0 & 0x0Fu;
      int masked = (b1 & 0x80u) != 0;
      uint64_t plen = (uint64_t)(b1 & 0x7Fu);
      size_t hdrLen = 2;

      if (plen == 126)
      {
         if (sRxLen < 4)
            break;
         plen = ((uint64_t)sRxBuf[2] << 8) | (uint64_t)sRxBuf[3];
         hdrLen = 4;
      }
      else if (plen == 127)
      {
         if (sRxLen < 10)
            break;
         plen = 0;
         for (int i = 0; i < 8; i++)
            plen = (plen << 8) | (uint64_t)sRxBuf[2 + i];
         hdrLen = 10;
      }

      if (masked)
         hdrLen += 4;

      if (plen > sizeof(sFragBuf))
         return -1;

      if (sRxLen < hdrLen + (size_t)plen)
         break;

      const enet_uint8 *maskKey = masked ? (sRxBuf + hdrLen - 4) : NULL;
      enet_uint8 *payload = sRxBuf + hdrLen;
      if (masked && maskKey)
      {
         for (size_t i = 0; i < (size_t)plen; i++)
            payload[i] ^= maskKey[i & 3u];
      }

      if (opcode == 0x8) /* Close */
      {
         return -1;
      }
      else if (opcode == 0x9) /* WS Ping */
      {
         size_t echoLen = ((size_t)plen <= 125) ? (size_t)plen : 125;
         QueueWsFrame(0xA, payload, echoLen);
         FlushTx();
      }
      else if (opcode == 0xA) /* WS Pong */
      {
      }
      else if (opcode == 0x2 || opcode == 0x0)
      {
         if (opcode == 0x2)
         {
            sFragLen = 0;
            sInFrag = 1;
         }
         if (sInFrag)
         {
            if (sFragLen + (size_t)plen > sizeof(sFragBuf))
               return -1;
            if (plen > 0)
               memcpy(sFragBuf + sFragLen, payload, (size_t)plen);
            sFragLen += (size_t)plen;
            if (fin)
            {
               HandleRelayAppMessage(sFragBuf, sFragLen);
               sFragLen = 0;
               sInFrag = 0;
            }
         }
      }

      size_t consumed = hdrLen + (size_t)plen;
      if (consumed < sRxLen)
         memmove(sRxBuf, sRxBuf + consumed, sRxLen - consumed);
      sRxLen -= consumed;
   }
   return 0;
}

static void enet_relay_poll_internal(void)
{
   if (sConnState == RELAY_STATE_IDLE || sConnState == RELAY_STATE_FAILED || sSock == RELAY_INVALID_SOCK)
      return;

   if (sConnState == RELAY_STATE_TCP_CONNECTING)
   {
      fd_set writeSet, errSet;
      struct timeval tv;
      FD_ZERO(&writeSet);
      FD_ZERO(&errSet);
      FD_SET(sSock, &writeSet);
      FD_SET(sSock, &errSet);
      tv.tv_sec = 0;
      tv.tv_usec = 0;

#ifdef _WIN32
      int sel = select(0, NULL, &writeSet, &errSet, &tv);
#else
      int sel = select(sSock + 1, NULL, &writeSet, &errSet, &tv);
#endif
      if (sel <= 0)
         return;

      int soErr = 0;
      socklen_t len = (socklen_t)sizeof(soErr);
      if (getsockopt(sSock, SOL_SOCKET, SO_ERROR, (char *)&soErr, &len) != 0 || soErr != 0)
      {
         CloseRelayTransport(1);
         return;
      }

#if defined(__APPLE__)
      if (sUseTls)
      {
         if (BeginTlsHandshake() != 0)
         {
            CloseRelayTransport(1);
            return;
         }
      }
      else
#endif
      {
         SendWsUpgradeRequest();
      }
   }

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
   if (sConnState == RELAY_STATE_TLS_HANDSHAKE)
   {
      OSStatus st = SSLHandshake(sTlsCtx);
      if (st == noErr)
      {
         SendWsUpgradeRequest();
      }
      else if (st == errSSLWouldBlock || st == errSSLPeerAuthCompleted)
      {
         return;
      }
      else
      {
         CloseRelayTransport(1);
         return;
      }
   }
#pragma clang diagnostic pop
#endif

   if (sConnState == RELAY_STATE_WS_UPGRADE)
   {
      if (FlushTx() != 0 || PumpRx() != 0)
      {
         CloseRelayTransport(1);
         return;
      }

      /* Look for \r\n\r\n */
      size_t hdrEnd = 0;
      for (size_t i = 0; i + 3 < sRxLen; i++)
      {
         if (sRxBuf[i] == '\r' && sRxBuf[i + 1] == '\n' &&
             sRxBuf[i + 2] == '\r' && sRxBuf[i + 3] == '\n')
         {
            hdrEnd = i + 4;
            break;
         }
      }
      if (hdrEnd == 0)
         return;

      /* Verify HTTP 101 Switching Protocols */
      int ok = 0;
      if (hdrEnd >= 12 && strncmp((const char *)sRxBuf, "HTTP/1.", 7) == 0)
      {
         const char *sp = (const char *)memchr(sRxBuf, ' ', hdrEnd);
         if (sp && strncmp(sp + 1, "101", 3) == 0)
            ok = 1;
      }
      if (!ok)
      {
         CloseRelayTransport(1);
         return;
      }

      if (hdrEnd < sRxLen)
         memmove(sRxBuf, sRxBuf + hdrEnd, sRxLen - hdrEnd);
      sRxLen -= hdrEnd;

      sConnState = RELAY_STATE_OPEN;
      sLastPingSentMs = enet_time_get();
      if (sPendingInitLen > 0)
      {
         QueueWsFrame(0x2, sPendingInitMsg, sPendingInitLen);
         sPendingInitLen = 0;
      }
      if (FlushTx() != 0)
      {
         CloseRelayTransport(1);
         return;
      }
   }

   if (sConnState == RELAY_STATE_OPEN)
   {
      enet_uint32 now = enet_time_get();
      if ((now - sLastPingSentMs) >= RELAY_PING_MS)
      {
         sLastPingSentMs = now;
         enet_uint8 ping = RELAY_OP_PING;
         QueueWsFrame(0x2, &ping, 1);
      }

      if (FlushTx() != 0 || PumpRx() != 0 || ProcessOpenWsFrames() != 0)
      {
         CloseRelayTransport(1);
         return;
      }
   }
}

static int StartRelayConnection(int isHostMode, const enet_uint8 *initMsg, size_t initLen)
{
   enet_relay_shutdown();

   if (initLen > sizeof(sPendingInitMsg))
      return -1;
   if (ParseRelayUrl(enet_relay_get_url()) != 0)
   {
      sConnState = RELAY_STATE_FAILED;
      if (isHostMode) sHostRoomState = -1;
      else { sClientJoinState = -1; sClientRejectReason = 4; }
      return -1;
   }

   sIsHostMode = isHostMode;
   memcpy(sPendingInitMsg, initMsg, initLen);
   sPendingInitLen = initLen;

   char portStr[16];
   snprintf(portStr, sizeof(portStr), "%u", (unsigned)sParsedPort);

   struct addrinfo hints;
   struct addrinfo *res = NULL;
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;

   if (getaddrinfo(sParsedHost, portStr, &hints, &res) != 0 || !res)
   {
      sConnState = RELAY_STATE_FAILED;
      if (isHostMode) sHostRoomState = -1;
      else { sClientJoinState = -1; sClientRejectReason = 4; }
      return -1;
   }

   RelaySock sock = RELAY_INVALID_SOCK;
   int connectedImmediate = 0;
   for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next)
   {
      RelaySock cand = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (cand == RELAY_INVALID_SOCK)
         continue;

      int yes = 1;
      setsockopt(cand, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
      setsockopt(cand, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&yes, sizeof(yes));
#endif
      RelaySetNonBlocking(cand);

      int rc = connect(cand, rp->ai_addr, (int)rp->ai_addrlen);
      if (rc == 0)
      {
         sock = cand;
         connectedImmediate = 1;
         break;
      }
      if (RelayLastSockErrWouldBlock())
      {
         sock = cand;
         connectedImmediate = 0;
         break;
      }
      RelayCloseSock(cand);
   }
   freeaddrinfo(res);

   if (sock == RELAY_INVALID_SOCK)
   {
      sConnState = RELAY_STATE_FAILED;
      if (isHostMode) sHostRoomState = -1;
      else { sClientJoinState = -1; sClientRejectReason = 4; }
      return -1;
   }

   sSock = sock;
   if (connectedImmediate)
   {
#if defined(__APPLE__)
      if (sUseTls)
      {
         if (BeginTlsHandshake() != 0)
         {
            CloseRelayTransport(1);
            return -1;
         }
      }
      else
#endif
      {
         SendWsUpgradeRequest();
      }
   }
   else
   {
      sConnState = RELAY_STATE_TCP_CONNECTING;
   }

   enet_relay_poll_internal();
   return 0;
}

int enet_relay_host_start(const ENetRelayLanCandidate *candidates, size_t candidateCount)
{
   size_t count = candidateCount;
   if (count > ENET_RELAY_MAX_CANDIDATES)
      count = ENET_RELAY_MAX_CANDIDATES;

   enet_uint8 msg[4 + ENET_RELAY_MAX_CANDIDATES * 6];
   msg[0] = RELAY_OP_REGISTER_HOST;
   msg[1] = (enet_uint8)(ENET_RELAY_PROTOCOL_VERSION & 0xFFu);
   msg[2] = (enet_uint8)((ENET_RELAY_PROTOCOL_VERSION >> 8) & 0xFFu);
   msg[3] = (enet_uint8)count;
   for (size_t i = 0; i < count; i++)
   {
      size_t off = 4 + i * 6;
      memcpy(msg + off, &candidates[i].ipv4NetworkOrder, 4);
      msg[off + 4] = (enet_uint8)(candidates[i].portHostOrder & 0xFFu);
      msg[off + 5] = (enet_uint8)((candidates[i].portHostOrder >> 8) & 0xFFu);
   }

   return StartRelayConnection(1, msg, 4 + count * 6);
}

void enet_relay_host_poll(void)
{
   if (sIsHostMode)
      enet_relay_poll_internal();
}

int enet_relay_host_get_room_code(char *outCodeBuf, size_t bufSize)
{
   if (sIsHostMode)
      enet_relay_poll_internal();

   if (sHostRoomState == 1)
   {
      if (outCodeBuf && bufSize > 0)
         snprintf(outCodeBuf, bufSize, "%s", sHostRoomCode);
      return 1;
   }
   if (sHostRoomState < 0)
      return -1;
   return 0;
}

int enet_relay_host_pop_disconnected_client(enet_uint8 *outClientId)
{
   if (sDiscCount == 0)
      return 0;
   enet_uint8 cid = sDiscQueue[sDiscHead];
   sDiscHead = (sDiscHead + 1) % RELAY_DISC_CAP;
   sDiscCount--;
   if (outClientId)
      *outClientId = cid;
   return 1;
}

int enet_relay_client_join(const char *roomCode)
{
   if (!roomCode || strlen(roomCode) != 4)
      return -1;

   enet_uint8 msg[7];
   msg[0] = RELAY_OP_JOIN_ROOM;
   msg[1] = (enet_uint8)(ENET_RELAY_PROTOCOL_VERSION & 0xFFu);
   msg[2] = (enet_uint8)((ENET_RELAY_PROTOCOL_VERSION >> 8) & 0xFFu);
   for (int i = 0; i < 4; i++)
      msg[3 + i] = (enet_uint8)toupper((unsigned char)roomCode[i]);

   return StartRelayConnection(0, msg, sizeof(msg));
}

int enet_relay_client_poll_join(enet_uint8 *outClientId,
                                ENetRelayLanCandidate *outCandidates,
                                size_t maxCandidates,
                                size_t *outCandidateCount,
                                int *outRejectReason)
{
   if (!sIsHostMode)
      enet_relay_poll_internal();

   if (sClientJoinState == 1)
   {
      if (outClientId)
         *outClientId = sClientAssignedId;
      size_t copyCount = sClientCandCount;
      if (copyCount > maxCandidates)
         copyCount = maxCandidates;
      if (outCandidates && copyCount > 0)
         memcpy(outCandidates, sClientCands, copyCount * sizeof(ENetRelayLanCandidate));
      if (outCandidateCount)
         *outCandidateCount = copyCount;
      return 1;
   }
   if (sClientJoinState < 0)
   {
      if (outRejectReason)
         *outRejectReason = sClientRejectReason ? sClientRejectReason : 4;
      if (outCandidateCount)
         *outCandidateCount = 0;
      return -1;
   }
   return 0;
}

int enet_relay_client_is_connected(void)
{
   if (!sIsHostMode)
      enet_relay_poll_internal();
   return (!sIsHostMode && sConnState == RELAY_STATE_OPEN && sClientJoinState == 1) ? 1 : 0;
}

void enet_relay_shutdown(void)
{
   CloseRelayTransport(0);
   sIsHostMode = 0;
   sPendingInitLen = 0;
   sHostRoomState = 0;
   sHostRoomCode[0] = '\0';
   sActiveRoomCode[0] = '\0';
   sDiscHead = 0;
   sDiscCount = 0;
   sClientJoinState = 0;
   sClientRejectReason = 0;
   sClientAssignedId = 0;
   sClientCandCount = 0;
   sPktHead = 0;
   sPktCount = 0;
}

int enet_relay_try_send(const ENetAddress *address, const ENetBuffer *buffers, size_t bufferCount)
{
   if (!address || !buffers || bufferCount == 0)
      return -2;

   enet_uint32 hostOrder = ENET_NET_TO_HOST_32(address->host);
   enet_uint8 targetId = 0;
   if (hostOrder == ENET_RELAY_SYNTHETIC_HOST_IP)
   {
      targetId = 0;
   }
   else if ((hostOrder & 0xFFFFFF00u) == ENET_RELAY_SYNTHETIC_CLIENT_BASE &&
            (hostOrder & 0xFFu) >= 1u && (hostOrder & 0xFFu) <= 7u)
   {
      targetId = (enet_uint8)(hostOrder & 0xFFu);
   }
   else
   {
      return -2;
   }

   if (sConnState != RELAY_STATE_OPEN)
      return 0;

   enet_uint8 frame[2 + 2048];
   frame[0] = RELAY_OP_RELAY_PACKET;
   frame[1] = targetId;
   size_t total = 0;
   for (size_t i = 0; i < bufferCount; i++)
   {
      if (buffers[i].dataLength == 0)
         continue;
      if (total + buffers[i].dataLength > 2048)
         return 0;
      memcpy(frame + 2 + total, buffers[i].data, buffers[i].dataLength);
      total += buffers[i].dataLength;
   }
   if (total == 0)
      return 0;

   if (QueueWsFrame(0x2, frame, 2 + total) != 0)
      return 0;
   if (FlushTx() != 0)
   {
      CloseRelayTransport(1);
      return 0;
   }
   return (int)total;
}

int enet_relay_try_receive(ENetAddress *address, ENetBuffer *buffers, size_t bufferCount)
{
   enet_relay_poll_internal();

   if (sPktCount == 0 || !buffers || bufferCount == 0 || !buffers[0].data || buffers[0].dataLength == 0)
      return 0;

   const RelayQueuedPacket *pkt = &sPktQueue[sPktHead];
   if (pkt->len > buffers[0].dataLength)
   {
      sPktHead = (sPktHead + 1) % RELAY_PKT_CAP;
      sPktCount--;
      return -2;
   }

   size_t outLen = pkt->len;
   enet_uint8 cid = pkt->clientId;
   memcpy(buffers[0].data, pkt->data, outLen);
   sPktHead = (sPktHead + 1) % RELAY_PKT_CAP;
   sPktCount--;

   if (address)
   {
      address->port = 29400;
      if (sIsHostMode)
         address->host = ENET_HOST_TO_NET_32(ENET_RELAY_SYNTHETIC_CLIENT_BASE | (enet_uint32)cid);
      else
         address->host = ENET_HOST_TO_NET_32(ENET_RELAY_SYNTHETIC_HOST_IP);
   }
   return (int)outLen;
}

#endif /* __EMSCRIPTEN__ */
