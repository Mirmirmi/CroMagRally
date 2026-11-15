/****************************/
/*        NETWORK.C         */
/* (c)2000 Pangea Software  */
/* LAN ENet Rewrite (2026)  */
/****************************/

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <winsock2.h>
	#include <ws2tcpip.h>
	typedef int socklen_t;
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <ifaddrs.h>
	#include <errno.h>
#endif

#include <enet/enet.h>
#include <math.h>
#include <string.h>

#include "game.h"
#include "network.h"

/**********************/
/*     CONSTANTS      */
/**********************/

#define DISCOVERY_PRUNE_MS       3500u
#define DISCOVERY_INTERVAL_MS    450u
#define SNAPSHOT_INTERVAL_MS     13u    /* ~75 Hz max snapshot rate (avoids 60Hz aliasing) */
#define INPUT_SEND_INTERVAL_MS   13u    /* ~75 Hz input send rate */

#define NET_PROJ_SYNC_STATUS_MASK \
	(STATUS_BIT_AIMATCAMERA | STATUS_BIT_KEEPBACKFACES | STATUS_BIT_GLOW | \
	 STATUS_BIT_NOLIGHTING | STATUS_BIT_ROTXZY | STATUS_BIT_NOTEXTUREWRAP | \
	 STATUS_BIT_NOZWRITES | STATUS_BIT_HIDDEN)

#ifndef _WIN32
	typedef int NetUDPSocket;
	#define NET_INVALID_SOCKET (-1)
	#define NetCloseSocket(s) close(s)
#else
	typedef SOCKET NetUDPSocket;
	#define NET_INVALID_SOCKET INVALID_SOCKET
	#define NetCloseSocket(s) closesocket(s)
#endif

/**********************/
/*     GLOBALS        */
/**********************/

Boolean gIsNetworkHost = false;
Boolean gIsNetworkClient = false;
Boolean gNetGameInProgress = false;
Str32   gPlayerNameStrings[MAX_PLAYERS];

/**********************/
/*   STATIC STATE     */
/**********************/

static Boolean  sENetInitialized = false;
static uint16_t sGamePort = NET_DEFAULT_PORT;
static uint16_t sDiscoveryPort = NET_DISCOVERY_PORT;

static ENetHost* sHost = NULL;
static ENetPeer* sServerPeer = NULL;
static ENetPeer* sClientPeers[MAX_PLAYERS]; /* index = playerNum (1..MAX_PLAYERS-1) */

static NetUDPSocket sHostDiscoverySock = NET_INVALID_SOCKET;
static NetUDPSocket sClientDiscoverySock = NET_INVALID_SOCKET;
static uint32_t     sLastBeaconMs = 0;
static uint32_t     sLastProbeMs = 0;

static NetLobbyConnectionState sConnState = kNetConn_Idle;
static char                    sStatusText[128] = "";

static NetLobbyStateMsg        sLobbyState;
static uint16_t                sMyMachineId = 0;
static uint16_t                sNextMachineId = 2;
static Boolean                 sClientMatchStarted = false;
static Boolean                 sClientLevelGo = false;
static uint32_t                sCurrentMatchId = 0;
static uint32_t                sPendingSeed = 1;
static int8_t                  sPendingWhoIsIt = 0;

static Boolean                 sClientLevelPreparedSent[MAX_PLAYERS];

static NetDiscoveredHostInfo   sDiscoveredHosts[NET_MAX_DISCOVERED_HOSTS];
static int                     sNumDiscoveredHosts = 0;

/* Gameplay sync state */
static uint32_t           sSendInputSeq = 0;
static uint32_t           sLastSentInputMs = 0;
static uint32_t           sClientOutNewAccum[MAX_LOCAL_PLAYERS];

static uint32_t           sSendSnapSeq = 0;
static uint32_t           sLastSentSnapMs = 0;
static uint32_t           sLastRecvSnapSeq = 0;
static Boolean            sHasSnapshot = false;
static NetHostSnapshotMsg sCurrSnap;

static uint32_t           sHostInputSeq[MAX_PLAYERS];
static NetClientInputMsg  sHostLatestInput[MAX_PLAYERS];
static uint32_t           sHostPendingNewBits[MAX_PLAYERS];
static Boolean            sHostHasInput[MAX_PLAYERS];

typedef struct NetWeaponTracker
{
	Boolean  active;
	uint32_t netId;
	uint8_t  kind;
	ObjNode* obj;
} NetWeaponTracker;

static NetWeaponTracker sHostWeapons[NET_MAX_PROJECTILES];
static uint32_t         sNextWeaponNetId = 1;
static NetWeaponTracker sClientWeapons[NET_MAX_PROJECTILES];
static Boolean          sMatchLost = false;
static uint32_t         sLastSnapRecvMs = 0;
static Boolean          sClientAnnouncedPOW[MAX_POW_TYPES];
static uint8_t          sClientPrevThrowMask[MAX_PLAYERS];

/**********************/
/*  INTERNAL HELPERS  */
/**********************/

static uint32_t NetNowMs(void)
{
	return (uint32_t)SDL_GetTicks();
}

static void SafeCopyStr(char* dst, size_t dstSize, const char* src)
{
	if (!dst || dstSize == 0)
		return;
	if (!src)
	{
		dst[0] = '\0';
		return;
	}
	SDL_strlcpy(dst, src, dstSize);
}

static Boolean SetSocketNonBlockingAndBroadcast(NetUDPSocket sock, Boolean enableBroadcast)
{
	if (sock == NET_INVALID_SOCKET)
		return false;

	int yes = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#ifdef SO_REUSEPORT
	setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&yes, sizeof(yes));
#endif
	if (enableBroadcast)
	{
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
	}

#ifdef _WIN32
	u_long mode = 1;
	if (ioctlsocket(sock, FIONBIO, &mode) != 0)
		return false;
#else
	int flags = fcntl(sock, F_GETFL, 0);
	if (flags < 0)
		return false;
	if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
		return false;
#endif
	return true;
}

Boolean Net_IsLANAddress(uint32_t hostOrderIPv4)
{
	uint8_t a = (uint8_t)((hostOrderIPv4 >> 24) & 0xFFu);
	uint8_t b = (uint8_t)((hostOrderIPv4 >> 16) & 0xFFu);

	if (a == 127)
		return true; /* Loopback 127.0.0.0/8 */
	if (a == 10)
		return true; /* RFC1918 10.0.0.0/8 */
	if (a == 172 && (b >= 16 && b <= 31))
		return true; /* RFC1918 172.16.0.0/12 */
	if (a == 192 && b == 168)
		return true; /* RFC1918 192.168.0.0/16 */
	if (a == 169 && b == 254)
		return true; /* Link-local 169.254.0.0/16 */

	return false;
}

Boolean Net_ParseLANIPString(const char* ipStr, uint32_t* outHostOrderIPv4)
{
	if (!ipStr || ipStr[0] == '\0')
		return false;

	struct in_addr addr;
	if (inet_pton(AF_INET, ipStr, &addr) != 1)
		return false;

	uint32_t hostIP = ntohl(addr.s_addr);
	if (!Net_IsLANAddress(hostIP))
		return false;

	if (outHostOrderIPv4)
		*outHostOrderIPv4 = hostIP;
	return true;
}

void Net_GetLocalIPString(char* outBuf, size_t bufSize)
{
	if (!outBuf || bufSize == 0)
		return;

	SDL_strlcpy(outBuf, "127.0.0.1", bufSize);

#ifndef _WIN32
	struct ifaddrs* ifaddr = NULL;
	if (getifaddrs(&ifaddr) == 0 && ifaddr != NULL)
	{
		for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
		{
			if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
				continue;

			const struct sockaddr_in* sa = (const struct sockaddr_in*)ifa->ifa_addr;
			uint32_t ip = ntohl(sa->sin_addr.s_addr);
			if ((ip >> 24) == 127)
				continue; /* prefer non-loopback LAN interface if available */
			if (Net_IsLANAddress(ip))
			{
				inet_ntop(AF_INET, &sa->sin_addr, outBuf, (socklen_t)bufSize);
				break;
			}
		}
		freeifaddrs(ifaddr);
		return;
	}
#endif

	/* Fallback UDP getsockname trick (no traffic sent) */
	NetUDPSocket tmpSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (tmpSock != NET_INVALID_SOCKET)
	{
		struct sockaddr_in remoteAddr;
		memset(&remoteAddr, 0, sizeof(remoteAddr));
		remoteAddr.sin_family = AF_INET;
		remoteAddr.sin_port = htons(53);
		inet_pton(AF_INET, "192.168.1.1", &remoteAddr.sin_addr);

		if (connect(tmpSock, (struct sockaddr*)&remoteAddr, sizeof(remoteAddr)) == 0)
		{
			struct sockaddr_in localAddr;
			socklen_t addrLen = sizeof(localAddr);
			if (getsockname(tmpSock, (struct sockaddr*)&localAddr, &addrLen) == 0)
			{
				uint32_t ip = ntohl(localAddr.sin_addr.s_addr);
				if (Net_IsLANAddress(ip) && ip != 0)
				{
					inet_ntop(AF_INET, &localAddr.sin_addr, outBuf, (socklen_t)bufSize);
				}
			}
		}
		NetCloseSocket(tmpSock);
	}
}

static void SendENetPacket(ENetPeer* peer, uint8_t channel, const void* data, size_t size, enet_uint32 flags)
{
	if (!peer || !data || size == 0)
		return;
	ENetPacket* pkt = enet_packet_create(data, size, flags);
	if (pkt)
	{
		if (enet_peer_send(peer, channel, pkt) < 0)
			enet_packet_destroy(pkt);
	}
}

static void BroadcastENetPacketToClients(uint8_t channel, const void* data, size_t size, enet_uint32 flags, int skipPlayer)
{
	if (!sHost)
		return;

	ENetPeer* sentPeers[MAX_PLAYERS];
	int sentCount = 0;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (i == skipPlayer)
			continue;
		ENetPeer* peer = sClientPeers[i];
		if (!peer)
			continue;

		/* Deduplicate when one remote peer owns multiple split-screen slots */
		Boolean alreadySent = false;
		for (int j = 0; j < sentCount; j++)
		{
			if (sentPeers[j] == peer)
			{
				alreadySent = true;
				break;
			}
		}
		if (!alreadySent)
		{
			sentPeers[sentCount++] = peer;
			SendENetPacket(peer, channel, data, size, flags);
		}
	}
}

static void SyncPlayerNamesFromLobby(void)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		SDL_memset(gPlayerNameStrings[i], 0, sizeof(gPlayerNameStrings[i]));
		if (i < sLobbyState.numRealPlayers && sLobbyState.players[i].active)
		{
			SafeCopyStr(gPlayerNameStrings[i], sizeof(gPlayerNameStrings[i]), sLobbyState.players[i].name);
		}
		else
		{
			SDL_snprintf(gPlayerNameStrings[i], sizeof(gPlayerNameStrings[i]), "CPU %d", i + 1);
		}
	}
}

static void ApplyLobbyStateToGlobals(void)
{
	gGameMode = sLobbyState.gameMode;
	gTheAge = sLobbyState.age;
	gTrackNum = sLobbyState.trackNum;
	gGamePrefs.difficulty = sLobbyState.difficulty;
	if (sLobbyState.tagDuration > 0)
		gGamePrefs.tagDuration = sLobbyState.tagDuration;
	gMultiplayerCPUs = (sLobbyState.multiplayerCPUs != 0);
	gNumRealPlayers = sLobbyState.numRealPlayers;
	if (gNumRealPlayers < 1)
		gNumRealPlayers = 1;
	if (gNumRealPlayers > MAX_PLAYERS)
		gNumRealPlayers = MAX_PLAYERS;

	if (gIsNetworkClient && sMyMachineId != 0)
	{
		int found = 0;
		for (int i = 0; i < MAX_PLAYERS && found < MAX_LOCAL_PLAYERS; i++)
		{
			if (sLobbyState.players[i].active && sLobbyState.players[i].machineId == sMyMachineId)
			{
				gLocalPlayerNums[found++] = (short)i;
			}
		}
		if (found > 0)
		{
			gNumLocalPlayers = (short)found;
			gMyNetworkPlayerNum = gLocalPlayerNums[0];
		}
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sLobbyState.players[i].active && sLobbyState.players[i].vehicleSelected)
		{
			gPlayerInfo[i].sex = sLobbyState.players[i].sex;
			gPlayerInfo[i].vehicleType = sLobbyState.players[i].vehicleType;
			gPlayerInfo[i].skin = sLobbyState.players[i].skin;
			gPlayerInfo[i].team = sLobbyState.players[i].team;
		}
	}

	SyncPlayerNamesFromLobby();
}

static void BroadcastLobbyState(void)
{
	if (!sHost || !gIsNetworkHost)
		return;

	sLobbyState.msgType = kNetMsgLobbyState;
	sLobbyState.gameMode = (uint8_t)gGameMode;
	sLobbyState.age = (uint8_t)gTheAge;
	sLobbyState.trackNum = (uint8_t)gTrackNum;
	sLobbyState.difficulty = (uint8_t)gGamePrefs.difficulty;
	sLobbyState.multiplayerCPUs = gMultiplayerCPUs ? 1 : 0;
	sLobbyState.tagDuration = (uint8_t)gGamePrefs.tagDuration;

	/* Recompute active count across all Host + Client seats */
	int activeCount = 0;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sLobbyState.players[i].active)
			activeCount++;
	}
	sLobbyState.numRealPlayers = (uint8_t)activeCount;
	gNumRealPlayers = (short)(activeCount > 0 ? activeCount : 1);
	SyncPlayerNamesFromLobby();

	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &sLobbyState, sizeof(sLobbyState), ENET_PACKET_FLAG_RELIABLE, -1);
}

static void BuildBeaconPacket(NetDiscoveryBeaconPacket* pkt)
{
	memset(pkt, 0, sizeof(*pkt));
	pkt->magic = NET_MAGIC;
	pkt->version = NET_PROTOCOL_VERSION;
	pkt->gamePort = sGamePort;
	pkt->gameMode = sLobbyState.gameMode;
	pkt->age = sLobbyState.age;
	pkt->trackNum = sLobbyState.trackNum;
	pkt->curPlayers = sLobbyState.numRealPlayers;
	pkt->maxPlayers = MAX_PLAYERS;
	pkt->inProgress = sLobbyState.matchStarted;
	SafeCopyStr(pkt->hostName, sizeof(pkt->hostName), sLobbyState.players[0].name);
}

static void PollHostDiscoverySocket(void)
{
	if (sHostDiscoverySock == NET_INVALID_SOCKET)
		return;

	uint32_t now = NetNowMs();
	NetDiscoveryBeaconPacket beacon;
	BuildBeaconPacket(&beacon);

	/* Receive any probes and reply directly (bounded per frame; full buffer avoids beacon truncation) */
	for (int iter = 0; iter < 32; iter++)
	{
		uint8_t rxBuf[256];
		struct sockaddr_in fromAddr;
		socklen_t fromLen = sizeof(fromAddr);
		int n = (int)recvfrom(sHostDiscoverySock, (char*)rxBuf, sizeof(rxBuf), 0,
		                      (struct sockaddr*)&fromAddr, &fromLen);
		if (n <= 0)
			break;

		/* Never reply to packets originating from the discovery port itself (prevents self/host echo loops) */
		if (ntohs(fromAddr.sin_port) == sDiscoveryPort)
			continue;

		if (n == (int)sizeof(NetDiscoveryProbePacket))
		{
			NetDiscoveryProbePacket probe;
			memcpy(&probe, rxBuf, sizeof(probe));
			if (probe.magic == NET_MAGIC && probe.version == NET_PROTOCOL_VERSION)
			{
				uint32_t fromIP = ntohl(fromAddr.sin_addr.s_addr);
				if (Net_IsLANAddress(fromIP))
				{
					sendto(sHostDiscoverySock, (const char*)&beacon, sizeof(beacon), 0,
					       (struct sockaddr*)&fromAddr, fromLen);
				}
			}
		}
	}

	/* Periodic LAN broadcast beacon while lobby is open */
	if (!sLobbyState.matchStarted && (now - sLastBeaconMs) >= DISCOVERY_INTERVAL_MS)
	{
		sLastBeaconMs = now;
		struct sockaddr_in bcastAddr;
		memset(&bcastAddr, 0, sizeof(bcastAddr));
		bcastAddr.sin_family = AF_INET;
		bcastAddr.sin_port = htons(sDiscoveryPort);
		bcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		sendto(sHostDiscoverySock, (const char*)&beacon, sizeof(beacon), 0,
		       (struct sockaddr*)&bcastAddr, sizeof(bcastAddr));

		/* Also ping loopback in case another local test client listens on discoveryPort */
		bcastAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		sendto(sHostDiscoverySock, (const char*)&beacon, sizeof(beacon), 0,
		       (struct sockaddr*)&bcastAddr, sizeof(bcastAddr));
	}
}

static void UpsertDiscoveredHost(uint32_t ipHostOrder, const NetDiscoveryBeaconPacket* beacon)
{
	if (!Net_IsLANAddress(ipHostOrder))
		return;
	if (beacon->magic != NET_MAGIC || beacon->version != NET_PROTOCOL_VERSION)
		return;
	if (beacon->inProgress)
		return;

	uint32_t now = NetNowMs();
	int targetIdx = -1;
	for (int i = 0; i < sNumDiscoveredHosts; i++)
	{
		if (sDiscoveredHosts[i].ipHostOrder == ipHostOrder &&
		    sDiscoveredHosts[i].gamePort == beacon->gamePort)
		{
			targetIdx = i;
			break;
		}
	}

	if (targetIdx < 0)
	{
		if (sNumDiscoveredHosts >= NET_MAX_DISCOVERED_HOSTS)
			return;
		targetIdx = sNumDiscoveredHosts++;
	}

	NetDiscoveredHostInfo* entry = &sDiscoveredHosts[targetIdx];
	entry->ipHostOrder = ipHostOrder;
	struct in_addr addr;
	addr.s_addr = htonl(ipHostOrder);
	inet_ntop(AF_INET, &addr, entry->ipString, sizeof(entry->ipString));
	entry->gamePort = beacon->gamePort;
	entry->gameMode = beacon->gameMode;
	entry->age = beacon->age;
	entry->trackNum = beacon->trackNum;
	entry->curPlayers = beacon->curPlayers;
	entry->maxPlayers = beacon->maxPlayers;
	entry->inProgress = beacon->inProgress;
	entry->lastSeenMs = now;
	SafeCopyStr(entry->hostName, sizeof(entry->hostName), beacon->hostName);
}

static void HandleJoinRequest(ENetPeer* peer, const NetJoinRequestMsg* req)
{
	uint32_t peerIP = ntohl(peer->address.host);
	NetJoinAckMsg ack;
	memset(&ack, 0, sizeof(ack));
	ack.msgType = kNetMsgJoinAck;

	if (!Net_IsLANAddress(peerIP))
	{
		ack.accepted = 0;
		ack.rejectReason = 1;
		SendENetPacket(peer, NET_CHANNEL_RELIABLE, &ack, sizeof(ack), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_disconnect_later(peer, 0);
		return;
	}

	if (req->version != NET_PROTOCOL_VERSION || sLobbyState.matchStarted)
	{
		ack.accepted = 0;
		ack.rejectReason = 2;
		SendENetPacket(peer, NET_CHANNEL_RELIABLE, &ack, sizeof(ack), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_disconnect_later(peer, 0);
		return;
	}

	/* Count active human seats cleanly (always contiguous 0..curActive-1 while in lobby) */
	int curActive = 0;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sLobbyState.players[i].active)
			curActive++;
	}

	int reqCount = (int)req->numLocalPlayers;
	if (reqCount < 1)
		reqCount = 1;
	if (reqCount > MAX_LOCAL_PLAYERS)
		reqCount = MAX_LOCAL_PLAYERS;

	if (curActive + reqCount > MAX_PLAYERS)
	{
		ack.accepted = 0;
		ack.rejectReason = 3; /* server full / not enough free slots */
		SendENetPacket(peer, NET_CHANNEL_RELIABLE, &ack, sizeof(ack), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_disconnect_later(peer, 0);
		return;
	}

	uint16_t machineId = sNextMachineId++;
	const char* baseName = (req->playerName[0] != '\0') ? req->playerName : "PLAYER";

	ack.accepted = 1;
	ack.assignedCount = (uint8_t)reqCount;
	ack.clientMachineId = machineId;

	for (int k = 0; k < reqCount; k++)
	{
		int slot = curActive + k;
		sClientPeers[slot] = peer;
		memset(&sLobbyState.players[slot], 0, sizeof(sLobbyState.players[slot]));
		sLobbyState.players[slot].active = 1;
		sLobbyState.players[slot].team = (uint8_t)(slot & 1);
		sLobbyState.players[slot].machineId = machineId;

		if (k == 0)
		{
			SafeCopyStr(sLobbyState.players[slot].name, sizeof(sLobbyState.players[slot].name), baseName);
		}
		else
		{
			SDL_snprintf(sLobbyState.players[slot].name, sizeof(sLobbyState.players[slot].name),
			             "%.*s P%d", 24, baseName, k + 1);
		}
		ack.assignedSlots[k] = (uint8_t)slot;
	}

	SendENetPacket(peer, NET_CHANNEL_RELIABLE, &ack, sizeof(ack), ENET_PACKET_FLAG_RELIABLE);

	BroadcastLobbyState();
}

static void HandleReliableEventOnClient(const NetReliableEventMsg* ev)
{
	if (!sClientMatchStarted)
		return;

	switch (ev->kind)
	{
		case kNetEvent_POWPickup:
		{
			if (gMasterItemList && *gMasterItemList)
			{
				const TerrainItemEntryType* targetPtr = &(*gMasterItemList)[ev->paramU32];
				Net_ClientHidePOWTrigger(targetPtr, ev->f0);
			}
			if (ev->a >= 0 && ev->a < MAX_PLAYERS && gPlayerInfo[ev->a].onThisMachine)
			{
				PlayEffect(EFFECT_GETPOW);
				if (ev->b >= 0 && ev->b < MAX_POW_TYPES &&
				    !gPlayerInfo[ev->a].isComputer && !sClientAnnouncedPOW[ev->b])
				{
					PlayAnnouncerSound(EFFECT_POW_BONEBOMB + ev->b, false, 0.4f);
					sClientAnnouncedPOW[ev->b] = true;
				}
			}
			break;
		}

		case kNetEvent_Explosion:
		{
			OGLPoint3D pt = { ev->f0, ev->f1, ev->f2 };
			if (ev->c == kNetExplosion_Bomb)
			{
				OGLVector3D zero = {0.0f, 0.0f, 0.0f};
				MakeBombExplosion(NULL, ev->f0, ev->f2, &zero);
			}
			else if (ev->c == kNetExplosion_Snow)
			{
				MakeSnowExplosion(&pt);
			}
			else
			{
				PlayWeaponExplosionFX(ev->c, &pt);
			}
			break;
		}

		case kNetEvent_Sound3D:
		{
			OGLPoint3D pt = { ev->f0, ev->f1, ev->f2 };
			uint32_t rate = (ev->paramU32 != 0) ? ev->paramU32 : NORMAL_CHANNEL_RATE;
			PlayEffect_Parms3D(ev->c, &pt, rate, ev->f3);
			break;
		}

		case kNetEvent_AnnouncerSound:
		{
			if (ev->a < 0 ||
			    (ev->a < MAX_PLAYERS && gPlayerInfo[ev->a].onThisMachine && !gPlayerInfo[ev->a].isComputer))
			{
				PlayAnnouncerSound(ev->c, ev->b != 0, ev->f0);
			}
			break;
		}

		case kNetEvent_LapOrFinish:
		{
			short p = ev->a;
			if (p >= 0 && p < MAX_PLAYERS)
			{
				gPlayerInfo[p].raceComplete = true;
				gPlayerInfo[p].place = ev->b;
				if (gGameMode == GAME_MODE_MULTIPLAYERRACE &&
				    gPlayerInfo[p].onThisMachine &&
				    !gPlayerInfo[p].isComputer)
				{
					ShowFinalPlace(p, -1);
				}
			}
			if (ev->c != 0 && !gTrackCompleted && gGameMode == GAME_MODE_MULTIPLAYERRACE)
			{
				gTrackCompleted = true;
				gTrackCompletedCoolDownTimer = TRACK_COMPLETE_COOLDOWN_TIME;
			}
			break;
		}

		case kNetEvent_FlagEvent:
		{
			gCapturedFlagCount[0] = (Byte)ev->a;
			gCapturedFlagCount[1] = (Byte)ev->b;
			if (ev->c == 1)
				PlayEffect_Parms(EFFECT_REDTEAMWINS, FULL_CHANNEL_VOLUME * 3, FULL_CHANNEL_VOLUME * 2, NORMAL_CHANNEL_RATE);
			else if (ev->c == 2)
				PlayEffect_Parms(EFFECT_GREENTEAMWINS, FULL_CHANNEL_VOLUME * 3, FULL_CHANNEL_VOLUME * 2, NORMAL_CHANNEL_RATE);
			break;
		}

		case kNetEvent_GameOver:
		{
			/* ev->c == 1 => individual elimination, ev->c == 0 => full match over */
			if (ev->c == 1)
			{
				if (ev->a >= 0 && ev->a < MAX_PLAYERS && gPlayerInfo[ev->a].onThisMachine)
					ShowWinLose(ev->a, 0, 0);
			}
			else
			{
				short winner = ev->a;
				gTrackCompleted = true;
				gTrackCompletedCoolDownTimer = TRACK_COMPLETE_COOLDOWN_TIME;

				for (int pane = 0; pane < gNumLocalPlayers && pane < MAX_LOCAL_PLAYERS; pane++)
				{
					short slot = gLocalPlayerNums[pane];
					if (gGameMode == GAME_MODE_CAPTUREFLAG)
					{
						Byte won = (gPlayerInfo[slot].team == (Byte)ev->b) ? 1 : 2;
						ShowWinLose(slot, won, 0);
					}
					else if (winner < 0 || winner >= gNumTotalPlayers)
					{
						ShowWinLose(slot, 2, -1);
					}
					else if (winner == slot)
					{
						ShowWinLose(slot, 1, winner);
					}
					else
					{
						ShowWinLose(slot, 2, winner);
					}
				}
			}
			break;
		}

		case kNetEvent_PlayerLeft:
		{
			short p = ev->a;
			if (p >= 0 && p < MAX_PLAYERS)
			{
				gPlayerInfo[p].isComputer = true;
				sLobbyState.players[p].active = 0;
			}
			break;
		}

		default:
			break;
	}
}

static void CompactLobbySlots(void)
{
	int writeIdx = 0;
	for (int readIdx = 0; readIdx < MAX_PLAYERS; readIdx++)
	{
		if (!sLobbyState.players[readIdx].active)
			continue;
		if (writeIdx != readIdx)
		{
			sLobbyState.players[writeIdx] = sLobbyState.players[readIdx];
			sClientPeers[writeIdx] = sClientPeers[readIdx];
			memset(&sLobbyState.players[readIdx], 0, sizeof(sLobbyState.players[readIdx]));
			sClientPeers[readIdx] = NULL;
		}
		writeIdx++;
	}

	if (gIsNetworkHost && sMyMachineId != 0)
	{
		int found = 0;
		for (int i = 0; i < MAX_PLAYERS && found < MAX_LOCAL_PLAYERS; i++)
		{
			if (sLobbyState.players[i].active && sLobbyState.players[i].machineId == sMyMachineId)
				gLocalPlayerNums[found++] = (short)i;
		}
		if (found > 0)
		{
			gNumLocalPlayers = (short)found;
			gMyNetworkPlayerNum = gLocalPlayerNums[0];
		}
	}
}

static void HandlePeerDisconnectOnHost(ENetPeer* peer)
{
	if (!peer)
		return;

	Boolean removedAny = false;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sClientPeers[i] != peer)
			continue;

		removedAny = true;
		sClientPeers[i] = NULL;
		sLobbyState.players[i].active = 0;
		sHostHasInput[i] = false;
		sHostPendingNewBits[i] = 0;

		if (sLobbyState.matchStarted)
		{
			/* Convert departed remote player to AI during active race/battle */
			gPlayerInfo[i].isComputer = true;
			gPlayerInfo[i].controlBits = 0;
			gPlayerInfo[i].controlBits_New = 0;

			NetReliableEventMsg ev;
			memset(&ev, 0, sizeof(ev));
			ev.msgType = kNetMsgReliableEvent;
			ev.kind = kNetEvent_PlayerLeft;
			ev.a = (int8_t)i;
			ev.b = 1;
			BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
		}
	}

	if (!removedAny)
		return;

	if (!sLobbyState.matchStarted)
	{
		/* Compact remaining active lobby slots so humans remain contiguous 0..numRealPlayers-1 */
		CompactLobbySlots();
		BroadcastLobbyState();
	}
}

static void ApplySnapshotToClientWorld(const NetHostSnapshotMsg* snap)
{
	gWhoIsIt = snap->whoIsIt;
	gCapturedFlagCount[0] = snap->flagCount0;
	gCapturedFlagCount[1] = snap->flagCount1;

	int count = (snap->numPlayers <= MAX_PLAYERS) ? snap->numPlayers : MAX_PLAYERS;
	if (count > 0)
		gNumTotalPlayers = (short)count;
	for (int i = 0; i < count; i++)
	{
		const NetPlayerSnapshot* ps = &snap->players[i];
		PlayerInfoType* pi = &gPlayerInfo[i];

		pi->tagTimer = ps->tagTimer;
		pi->frozenTimer = ps->frozenTimer;
		pi->nitroTimer = ps->nitroTimer;
		pi->stickyTiresTimer = ps->stickyTiresTimer;
		pi->superSuspensionTimer = ps->superSuspensionTimer;
		pi->invisibilityTimer = ps->invisibilityTimer;
		pi->flamingTimer = ps->flamingTimer;
		pi->submarineImmobilized = ps->submarineImmobilized;
		pi->health = (float)ps->health / 100.0f;
		int prevLap = pi->lapNum;
		pi->powType = ps->powType;
		pi->powQuantity = ps->powQuantity;
		pi->lapNum = ps->lapNum;
		pi->place = ps->place;
		pi->steering = ps->steering;

		pi->isIt         = (ps->flags & kPlayerSnapFlag_IsIt) != 0;
		pi->isEliminated = (ps->flags & kPlayerSnapFlag_Eliminated) != 0;
		pi->raceComplete = (ps->flags & kPlayerSnapFlag_RaceComplete) != 0;
		pi->isComputer   = (ps->flags & kPlayerSnapFlag_IsComputer) != 0;
		pi->onWater      = (ps->flags & kPlayerSnapFlag_OnWater) != 0;
		pi->isPlaning    = (ps->flags & kPlayerSnapFlag_IsPlaning) != 0;
		pi->gasPedalDown = (ps->flags & kPlayerSnapFlag_GasDown) != 0;
		pi->braking      = (ps->flags & kPlayerSnapFlag_Braking) != 0;
		pi->wrongWay     = (ps->flags & kPlayerSnapFlag_WrongWay) != 0;
		/* Leave pi->makingSkid under local UpdateTireSkidMarks control so EFFECT_SKID triggers on new skids */

		if (gIsInGame && !pi->raceComplete && pi->onThisMachine && !pi->isComputer &&
		    prevLap >= 0 && ps->lapNum > prevLap && ps->lapNum > 0 && ps->lapNum < gNumLapsThisRace)
		{
			ShowLapNum((short)i);
		}

		uint8_t throwMask = (uint8_t)((ps->flags & (kPlayerSnapFlag_ThrowFwd | kPlayerSnapFlag_ThrowBack)) >> 12);
		if (pi->headObj && pi->headObj->Skeleton)
		{
			if (throwMask != 0 && throwMask != sClientPrevThrowMask[i])
			{
				short throwAnim = (ps->flags & kPlayerSnapFlag_ThrowFwd)
					? PLAYER_ANIM_THROWFORWARD : PLAYER_ANIM_THROWBACKWARD;
				pi->headObj->HEAD_THROW_READY_FLAG = false;
				MorphToSkeletonAnim(pi->headObj->Skeleton, throwAnim, 8.0f);
			}
			sClientPrevThrowMask[i] = throwMask;
		}

		ObjNode* car = pi->objNode;
		if (car)
		{
			if (ps->flags & kPlayerSnapFlag_OnGround)
				car->StatusBits |= STATUS_BIT_ONGROUND;
			else
				car->StatusBits &= ~STATUS_BIT_ONGROUND;

			car->Delta.x = ps->vx;
			car->Delta.y = ps->vy;
			car->Delta.z = ps->vz;
			car->Speed2D = ps->speed2D;

			/* Clear all CarriedFlag refs before re-linking torches below */
			car->CapturedFlag = nil;

			float dx = ps->x - car->Coord.x;
			float dy = ps->y - car->Coord.y;
			float dz = ps->z - car->Coord.z;
			float distSq = dx * dx + dy * dy + dz * dz;
			if (distSq > 700.0f * 700.0f)
			{
				car->Coord.x = ps->x;
				car->Coord.y = ps->y;
				car->Coord.z = ps->z;
				car->Rot.x = ps->rotX;
				car->Rot.y = ps->rotY;
				car->Rot.z = ps->rotZ;
				pi->coord = car->Coord;
			}
		}
	}

	/* Sync CTF Torches */
	int torchCount = (snap->numTorches < gNumTorches) ? snap->numTorches : gNumTorches;
	for (int t = 0; t < torchCount; t++)
	{
		ObjNode* torch = gTorchObjs[t];
		if (!torch)
			continue;
		const NetTorchSnapshot* ts = &snap->torches[t];
		torch->Mode = (signed char)ts->mode;
		torch->PlayerNum = ts->playerNum;
		if (ts->mode == 1 && ts->playerNum >= 0 && ts->playerNum < MAX_PLAYERS && gPlayerInfo[ts->playerNum].objNode)
		{
			gPlayerInfo[ts->playerNum].objNode->CapturedFlag = (Ptr)torch;
		}
		else
		{
			torch->Coord.x = ts->x;
			torch->Coord.y = ts->y;
			torch->Coord.z = ts->z;
			UpdateObjectTransforms(torch);
		}
	}

	/* Sync active projectile / mine / oil visuals */
	Boolean keepTracker[NET_MAX_PROJECTILES];
	memset(keepTracker, 0, sizeof(keepTracker));

	for (int k = 0; k < snap->numProjectiles && k < NET_MAX_PROJECTILES; k++)
	{
		const NetProjectileSnapshot* pr = &snap->projectiles[k];
		int foundIdx = -1;
		for (int s = 0; s < NET_MAX_PROJECTILES; s++)
		{
			if (sClientWeapons[s].active && sClientWeapons[s].netId == pr->netId)
			{
				foundIdx = s;
				break;
			}
		}

		if (foundIdx < 0)
		{
			int freeIdx = -1;
			for (int s = 0; s < NET_MAX_PROJECTILES; s++)
			{
				if (!sClientWeapons[s].active)
				{
					freeIdx = s;
					break;
				}
			}
			if (freeIdx >= 0)
			{
				NewObjectDefinitionType def;
				memset(&def, 0, sizeof(def));
				def.group = pr->group;
				def.type = pr->type;
				def.coord.x = pr->x;
				def.coord.y = pr->y;
				def.coord.z = pr->z;
				def.flags = pr->statusBits & NET_PROJ_SYNC_STATUS_MASK;
				def.slot = PARTICLE_SLOT;
				def.moveCall = NULL;
				def.scale = pr->scale > 0.0f ? pr->scale : 1.0f;

				ObjNode* vis = NULL;
				if (pr->kind == 1)
				{
					def.type = SKELETON_TYPE_BIRDBOMB;
					def.animNum = 0;
					vis = MakeNewSkeletonObject(&def);
					if (vis && vis->Skeleton)
						vis->Skeleton->AnimSpeed = 2.0f;
				}
				else
				{
					vis = MakeNewDisplayGroupObject(&def);
				}
				if (vis)
				{
					vis->StatusBits = (vis->StatusBits & ~NET_PROJ_SYNC_STATUS_MASK) | (pr->statusBits & NET_PROJ_SYNC_STATUS_MASK);
					vis->Rot.x = pr->rotX;
					vis->Rot.y = pr->rotY;
					vis->Rot.z = pr->rotZ;
					UpdateObjectTransforms(vis);
					sClientWeapons[freeIdx].active = true;
					sClientWeapons[freeIdx].netId = pr->netId;
					sClientWeapons[freeIdx].kind = pr->kind;
					sClientWeapons[freeIdx].obj = vis;
					keepTracker[freeIdx] = true;
				}
			}
		}
		else
		{
			ObjNode* vis = sClientWeapons[foundIdx].obj;
			if (vis)
			{
				vis->StatusBits = (vis->StatusBits & ~NET_PROJ_SYNC_STATUS_MASK) | (pr->statusBits & NET_PROJ_SYNC_STATUS_MASK);
				if (pr->scale > 0.0f)
				{
					vis->Scale.x = pr->scale;
					vis->Scale.y = pr->scale;
					vis->Scale.z = pr->scale;
				}
				vis->Coord.x = pr->x;
				vis->Coord.y = pr->y;
				vis->Coord.z = pr->z;
				vis->Rot.x = pr->rotX;
				vis->Rot.y = pr->rotY;
				vis->Rot.z = pr->rotZ;
				UpdateObjectTransforms(vis);
			}
			keepTracker[foundIdx] = true;
		}
	}

	for (int s = 0; s < NET_MAX_PROJECTILES; s++)
	{
		if (sClientWeapons[s].active && !keepTracker[s])
		{
			if (sClientWeapons[s].obj && !IsObjectBeingDeleted(sClientWeapons[s].obj))
			{
				DeleteObject(sClientWeapons[s].obj);
			}
			memset(&sClientWeapons[s], 0, sizeof(sClientWeapons[s]));
		}
	}

	if (snap->gameOver)
	{
		gGameOver = true;
	}
}

static void HandlePacketOnHost(ENetPeer* peer, const uint8_t* data, size_t len)
{
	if (len < 1)
		return;

	uint8_t msgType = data[0];
	switch (msgType)
	{
		case kNetMsgJoinRequest:
			if (len >= sizeof(NetJoinRequestMsg))
				HandleJoinRequest(peer, (const NetJoinRequestMsg*)data);
			break;

		case kNetMsgVehicleSelect:
			if (len >= sizeof(NetVehicleSelectMsg))
			{
				const NetVehicleSelectMsg* vs = (const NetVehicleSelectMsg*)data;
				int slot = (int)vs->playerNum;
				if (slot >= 1 && slot < MAX_PLAYERS && sClientPeers[slot] == peer)
				{
					sLobbyState.players[slot].vehicleSelected = 1;
					sLobbyState.players[slot].ready = 1;
					sLobbyState.players[slot].sex = vs->sex;
					sLobbyState.players[slot].vehicleType = vs->vehicleType;
					sLobbyState.players[slot].skin = vs->skin;
					sLobbyState.players[slot].team = vs->team;

					gPlayerInfo[slot].sex = vs->sex;
					gPlayerInfo[slot].vehicleType = vs->vehicleType;
					gPlayerInfo[slot].skin = vs->skin;
					gPlayerInfo[slot].team = vs->team;

					BroadcastLobbyState();
				}
			}
			break;

		case kNetMsgLevelPrepared:
		{
			for (int i = 0; i < MAX_PLAYERS; i++)
			{
				if (sClientPeers[i] == peer)
					sClientLevelPreparedSent[i] = true;
			}
			break;
		}

		case kNetMsgClientInput:
			if (len >= sizeof(NetClientInputMsg))
			{
				const NetClientInputMsg* in = (const NetClientInputMsg*)data;
				int slot = (int)in->playerNum;
				if (slot >= 1 && slot < MAX_PLAYERS && sClientPeers[slot] == peer)
				{
					if (!sHostHasInput[slot] || (int32_t)(in->seq - sHostInputSeq[slot]) > 0)
					{
						sHostInputSeq[slot] = in->seq;
						sHostLatestInput[slot] = *in;
						sHostHasInput[slot] = true;
					}
					/* Accumulate edge-triggered bits so quick taps aren't lost between ticks */
					sHostPendingNewBits[slot] |= in->controlBitsNew;
				}
			}
			break;

		case kNetMsgPlayerLeave:
		{
			HandlePeerDisconnectOnHost(peer);
			break;
		}

		default:
			break;
	}
}

static void HandlePacketOnClient(const uint8_t* data, size_t len)
{
	if (len < 1)
		return;

	uint8_t msgType = data[0];
	switch (msgType)
	{
		case kNetMsgJoinAck:
			if (len >= sizeof(NetJoinAckMsg))
			{
				const NetJoinAckMsg* ack = (const NetJoinAckMsg*)data;
				if (ack->accepted)
				{
					sMyMachineId = ack->clientMachineId;
					int count = (int)ack->assignedCount;
					if (count < 1) count = 1;
					if (count > MAX_LOCAL_PLAYERS) count = MAX_LOCAL_PLAYERS;
					gNumLocalPlayers = (short)count;
					for (int k = 0; k < count; k++)
					{
						gLocalPlayerNums[k] = (short)ack->assignedSlots[k];
					}
					gMyNetworkPlayerNum = gLocalPlayerNums[0];
					sConnState = kNetConn_Connected;
					if (count == 1)
					{
						SDL_snprintf(sStatusText, sizeof(sStatusText), "CONNECTED AS PLAYER %d",
						             (int)ack->assignedSlots[0] + 1);
					}
					else
					{
						SDL_snprintf(sStatusText, sizeof(sStatusText), "CONNECTED AS P%d-P%d",
						             (int)ack->assignedSlots[0] + 1,
						             (int)ack->assignedSlots[count - 1] + 1);
					}
				}
				else
				{
					sConnState = kNetConn_Failed;
					if (ack->rejectReason == 1)
						SafeCopyStr(sStatusText, sizeof(sStatusText), "LAN CONNECTIONS ONLY");
					else if (ack->rejectReason == 3)
						SafeCopyStr(sStatusText, sizeof(sStatusText), "GAME IS FULL");
					else
						SafeCopyStr(sStatusText, sizeof(sStatusText), "JOIN REJECTED");
				}
			}
			break;

		case kNetMsgLobbyState:
			if (len >= sizeof(NetLobbyStateMsg))
			{
				memcpy(&sLobbyState, data, sizeof(NetLobbyStateMsg));
				ApplyLobbyStateToGlobals();
			}
			break;

		case kNetMsgStartMatch:
			if (len >= sizeof(NetStartMatchMsg))
			{
				const NetStartMatchMsg* sm = (const NetStartMatchMsg*)data;
				sCurrentMatchId = sm->matchId;
				sPendingSeed = sm->randomSeed;
				sClientMatchStarted = true;
				sClientLevelGo = false;
			}
			break;

		case kNetMsgReturnToLobby:
			if (len >= sizeof(NetReturnToLobbyMsg))
			{
				const NetReturnToLobbyMsg* rtl = (const NetReturnToLobbyMsg*)data;
				if (rtl->endedMatchId >= sCurrentMatchId)
				{
					sClientMatchStarted = false;
					sClientLevelGo = false;
					sLobbyState.matchStarted = 0;
					Net_ResetGameSyncState();
					ApplyLobbyStateToGlobals();
				}
			}
			break;

		case kNetMsgMatchGo:
			if (len >= sizeof(NetMatchGoMsg))
			{
				const NetMatchGoMsg* go = (const NetMatchGoMsg*)data;
				sPendingSeed = go->randomSeed;
				sPendingWhoIsIt = go->whoIsIt;
				gNumTotalPlayers = go->numPlayers;
				sClientLevelGo = true;
			}
			break;

		case kNetMsgHostSnapshot:
			if (sClientMatchStarted && len >= offsetof(NetHostSnapshotMsg, players))
			{
				NetHostSnapshotMsg unpacked;
				memset(&unpacked, 0, sizeof(unpacked));
				memcpy(&unpacked, data, offsetof(NetHostSnapshotMsg, players));

				size_t pCount = (unpacked.numPlayers <= MAX_PLAYERS) ? unpacked.numPlayers : MAX_PLAYERS;
				size_t tCount = (unpacked.numTorches <= NET_MAX_TORCHES) ? unpacked.numTorches : NET_MAX_TORCHES;
				size_t wCount = (unpacked.numProjectiles <= NET_MAX_PROJECTILES) ? unpacked.numProjectiles : NET_MAX_PROJECTILES;
				size_t needLen = offsetof(NetHostSnapshotMsg, players)
					+ (size_t)unpacked.numPlayers * sizeof(NetPlayerSnapshot)
					+ (size_t)unpacked.numTorches * sizeof(NetTorchSnapshot)
					+ (size_t)unpacked.numProjectiles * sizeof(NetProjectileSnapshot);

				if (len >= needLen &&
				    (!sHasSnapshot || (int32_t)(unpacked.seq - sLastRecvSnapSeq) > 0))
				{
					const uint8_t* ptr = data + offsetof(NetHostSnapshotMsg, players);
					if (pCount > 0)
						memcpy(unpacked.players, ptr, pCount * sizeof(NetPlayerSnapshot));
					ptr += (size_t)unpacked.numPlayers * sizeof(NetPlayerSnapshot);
					if (tCount > 0)
						memcpy(unpacked.torches, ptr, tCount * sizeof(NetTorchSnapshot));
					ptr += (size_t)unpacked.numTorches * sizeof(NetTorchSnapshot);
					if (wCount > 0)
						memcpy(unpacked.projectiles, ptr, wCount * sizeof(NetProjectileSnapshot));

					unpacked.numPlayers = (uint8_t)pCount;
					unpacked.numTorches = (uint8_t)tCount;
					unpacked.numProjectiles = (uint8_t)wCount;

					sLastRecvSnapSeq = unpacked.seq;
					sCurrSnap = unpacked;
					sHasSnapshot = true;
					sLastSnapRecvMs = NetNowMs();
					ApplySnapshotToClientWorld(&sCurrSnap);
				}
			}
			break;

		case kNetMsgReliableEvent:
			if (len >= sizeof(NetReliableEventMsg))
			{
				HandleReliableEventOnClient((const NetReliableEventMsg*)data);
			}
			break;

		case kNetMsgPlayerLeave:
			if (len >= sizeof(NetPlayerLeaveMsg))
			{
				const NetPlayerLeaveMsg* pl = (const NetPlayerLeaveMsg*)data;
				if (pl->playerNum == 0)
				{
					sMatchLost = true;
					gGameOver = true;
					sConnState = kNetConn_Disconnected;
					SafeCopyStr(sStatusText, sizeof(sStatusText), "HOST DISCONNECTED");
				}
			}
			break;

		default:
			break;
	}
}

/**********************/
/*   PUBLIC APIs      */
/**********************/

void InitNetworkManager(void)
{
	if (!sENetInitialized)
	{
		if (enet_initialize() != 0)
		{
			SDL_Log("InitNetworkManager: enet_initialize failed");
			return;
		}
		sENetInitialized = true;
	}

	gIsNetworkHost = false;
	gIsNetworkClient = false;
	gNetGameInProgress = false;
	sConnState = kNetConn_Idle;
	sStatusText[0] = '\0';
	sLobbyState.msgType = kNetMsgLobbyState;
	sCurrentMatchId = 0;
	memset(sClientPeers, 0, sizeof(sClientPeers));
	memset(&sLobbyState, 0, sizeof(sLobbyState));
	Net_ResetGameSyncState();
	SyncPlayerNamesFromLobby();
}

void EndNetworkGame(void)
{
	Net_DisconnectGracefully();
	ShutdownNetworkManager();
}

void DeinitNetworkManager(void)
{
	ShutdownNetworkManager();
	if (sENetInitialized)
	{
		enet_deinitialize();
		sENetInitialized = false;
	}
}

void Net_SetPorts(uint16_t gamePort, uint16_t discoveryPort)
{
	if (gamePort != 0)
		sGamePort = gamePort;
	if (discoveryPort != 0)
		sDiscoveryPort = discoveryPort;
}

void Net_GetPorts(uint16_t* outGamePort, uint16_t* outDiscoveryPort)
{
	if (outGamePort)
		*outGamePort = sGamePort;
	if (outDiscoveryPort)
		*outDiscoveryPort = sDiscoveryPort;
}

void Net_ResetGameSyncState(void)
{
	sSendInputSeq = 0;
	sLastSentInputMs = 0;
	memset(sClientOutNewAccum, 0, sizeof(sClientOutNewAccum));

	sSendSnapSeq = 0;
	sLastSentSnapMs = 0;
	sLastRecvSnapSeq = 0;
	sHasSnapshot = false;
	memset(&sCurrSnap, 0, sizeof(sCurrSnap));

	memset(sHostInputSeq, 0, sizeof(sHostInputSeq));
	memset(sHostLatestInput, 0, sizeof(sHostLatestInput));
	memset(sHostPendingNewBits, 0, sizeof(sHostPendingNewBits));
	memset(sHostHasInput, 0, sizeof(sHostHasInput));
	memset(sClientLevelPreparedSent, 0, sizeof(sClientLevelPreparedSent));

	memset(sHostWeapons, 0, sizeof(sHostWeapons));
	sNextWeaponNetId = 1;
	memset(sClientWeapons, 0, sizeof(sClientWeapons));
	sMatchLost = false;
	sLastSnapRecvMs = 0;
	memset(sClientAnnouncedPOW, 0, sizeof(sClientAnnouncedPOW));
	memset(sClientPrevThrowMask, 0, sizeof(sClientPrevThrowMask));
}

void ShutdownNetworkManager(void)
{
	Net_StopClientDiscovery();

	if (sHostDiscoverySock != NET_INVALID_SOCKET)
	{
		NetCloseSocket(sHostDiscoverySock);
		sHostDiscoverySock = NET_INVALID_SOCKET;
	}

	if (sServerPeer)
	{
		enet_peer_disconnect(sServerPeer, 0);
		if (sHost)
			enet_host_flush(sHost);
		sServerPeer = NULL;
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sClientPeers[i])
		{
			ENetPeer* peer = sClientPeers[i];
			enet_peer_disconnect(peer, 0);
			for (int j = i; j < MAX_PLAYERS; j++)
			{
				if (sClientPeers[j] == peer)
					sClientPeers[j] = NULL;
			}
		}
	}

	if (sHost)
	{
		enet_host_flush(sHost);
		enet_host_destroy(sHost);
		sHost = NULL;
	}

	gNetGameInProgress = false;
	gIsNetworkHost = false;
	gIsNetworkClient = false;
	gMyNetworkPlayerNum = 0;
	for (int k = 0; k < MAX_LOCAL_PLAYERS; k++)
		gLocalPlayerNums[k] = (short)k;
	sMyMachineId = 0;
	sNextMachineId = 2;
	sCurrentMatchId = 0;
	sClientMatchStarted = false;
	sClientLevelGo = false;
	sConnState = kNetConn_Idle;
	sStatusText[0] = '\0';
	memset(&sLobbyState, 0, sizeof(sLobbyState));
	Net_ResetGameSyncState();
}

Boolean Net_StartHostLobby(const char* hostName)
{
	short savedLocalPlayers = gNumLocalPlayers;
	if (savedLocalPlayers < 1)
		savedLocalPlayers = 1;
	if (savedLocalPlayers > MAX_LOCAL_PLAYERS)
		savedLocalPlayers = MAX_LOCAL_PLAYERS;

	ShutdownNetworkManager();
	InitNetworkManager();

	gNumLocalPlayers = savedLocalPlayers;

	if (!sENetInitialized)
		return false;

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = sGamePort;

	sHost = enet_host_create(&address, MAX_PLAYERS - 1, NET_NUM_CHANNELS, 0, 0);
	if (!sHost)
	{
		SDL_snprintf(sStatusText, sizeof(sStatusText), "PORT %u IN USE", (unsigned)sGamePort);
		sConnState = kNetConn_Failed;
		return false;
	}

	/* Bind UDP discovery listener socket on sDiscoveryPort */
	sHostDiscoverySock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sHostDiscoverySock != NET_INVALID_SOCKET)
	{
		SetSocketNonBlockingAndBroadcast(sHostDiscoverySock, true);
		struct sockaddr_in discAddr;
		memset(&discAddr, 0, sizeof(discAddr));
		discAddr.sin_family = AF_INET;
		discAddr.sin_port = htons(sDiscoveryPort);
		discAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(sHostDiscoverySock, (struct sockaddr*)&discAddr, sizeof(discAddr)) != 0)
		{
			NetCloseSocket(sHostDiscoverySock);
			sHostDiscoverySock = NET_INVALID_SOCKET;
		}
	}

	gIsNetworkHost = true;
	gIsNetworkClient = false;
	gNetGameInProgress = true;
	gMyNetworkPlayerNum = 0;
	sMyMachineId = 1;
	sNextMachineId = 2;
	sConnState = kNetConn_Hosting;

	memset(&sLobbyState, 0, sizeof(sLobbyState));
	sLobbyState.msgType = kNetMsgLobbyState;
	sLobbyState.gameMode = (uint8_t)gGameMode;
	sLobbyState.age = (uint8_t)gTheAge;
	sLobbyState.trackNum = (uint8_t)gTrackNum;
	sLobbyState.difficulty = (uint8_t)gGamePrefs.difficulty;
	sLobbyState.multiplayerCPUs = gMultiplayerCPUs ? 1 : 0;
	sLobbyState.tagDuration = (uint8_t)gGamePrefs.tagDuration;
	sLobbyState.numRealPlayers = (uint8_t)savedLocalPlayers;

	const char* baseName = (hostName && hostName[0]) ? hostName : "HOST";
	for (int pane = 0; pane < savedLocalPlayers; pane++)
	{
		gLocalPlayerNums[pane] = (short)pane;
		sLobbyState.players[pane].active = 1;
		sLobbyState.players[pane].ready = 1;
		sLobbyState.players[pane].team = (uint8_t)(pane & 1);
		sLobbyState.players[pane].machineId = sMyMachineId;
		if (pane == 0)
		{
			SafeCopyStr(sLobbyState.players[pane].name, sizeof(sLobbyState.players[pane].name), baseName);
		}
		else
		{
			SDL_snprintf(sLobbyState.players[pane].name, sizeof(sLobbyState.players[pane].name),
			             "%.*s P%d", 24, baseName, pane + 1);
		}
	}

	gNumRealPlayers = savedLocalPlayers;
	SyncPlayerNamesFromLobby();

	char localIP[32];
	Net_GetLocalIPString(localIP, sizeof(localIP));
	SDL_snprintf(sStatusText, sizeof(sStatusText), "HOSTING AT %s:%u", localIP, (unsigned)sGamePort);
	return true;
}

uint16_t Net_GetMyMachineId(void)
{
	return sMyMachineId;
}

void Net_SyncHostLobbySettings(void)
{
	if (gIsNetworkHost && sHost)
	{
		BroadcastLobbyState();
	}
}

Boolean Net_HostLaunchMatch(void)
{
	if (!gIsNetworkHost || !sHost)
		return false;

	gGameOver = false;
	gTrackCompleted = false;
	memset(sClientLevelPreparedSent, 0, sizeof(sClientLevelPreparedSent));
	Net_ResetGameSyncState();

	sCurrentMatchId++;
	sLobbyState.matchStarted = 1;
	BroadcastLobbyState();

	NetStartMatchMsg sm;
	sm.msgType = kNetMsgStartMatch;
	sm.matchId = sCurrentMatchId;
	sm.randomSeed = (uint32_t)MyRandomLong();
	if (sm.randomSeed == 0)
		sm.randomSeed = 12345u;
	sPendingSeed = sm.randomSeed;
	SetMyRandomSeed(sm.randomSeed);

	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &sm, sizeof(sm), ENET_PACKET_FLAG_RELIABLE, -1);
	enet_host_flush(sHost);
	return true;
}

Boolean Net_HasRemoteClients(void)
{
	if (gIsNetworkHost)
	{
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (sClientPeers[i] != NULL)
				return true;
		}
		return false;
	}
	if (gIsNetworkClient)
		return sServerPeer != NULL && sConnState == kNetConn_Connected;
	return false;
}

short Net_GetLocalPlayerCount(void)
{
	if (sMyMachineId == 0)
		return gNumLocalPlayers > 0 ? gNumLocalPlayers : 1;

	short count = 0;
	for (int i = 0; i < MAX_PLAYERS && count < MAX_LOCAL_PLAYERS; i++)
	{
		if (sLobbyState.players[i].active && sLobbyState.players[i].machineId == sMyMachineId)
			count++;
	}
	return count > 0 ? count : 1;
}

void Net_HostReturnToLobby(void)
{
	if (!gIsNetworkHost || !sHost)
		return;

	sLobbyState.matchStarted = 0;
	CompactLobbySlots();

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sLobbyState.players[i].active)
		{
			sLobbyState.players[i].vehicleSelected = 0;
			sLobbyState.players[i].ready = (sLobbyState.players[i].machineId == sMyMachineId) ? 1 : 0;
		}
	}

	memset(sClientLevelPreparedSent, 0, sizeof(sClientLevelPreparedSent));
	Net_ResetGameSyncState();
	gGameOver = false;
	gTrackCompleted = false;

	NetReturnToLobbyMsg rtl;
	memset(&rtl, 0, sizeof(rtl));
	rtl.msgType = kNetMsgReturnToLobby;
	rtl.endedMatchId = sCurrentMatchId;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &rtl, sizeof(rtl), ENET_PACKET_FLAG_RELIABLE, -1);
	BroadcastLobbyState();
	enet_host_flush(sHost);
}

void Net_ClientPrepareReturnToLobby(void)
{
	if (!gIsNetworkClient)
		return;

	sClientMatchStarted = false;
	sClientLevelGo = false;
	sLobbyState.matchStarted = 0;
	Net_ResetGameSyncState();
	ApplyLobbyStateToGlobals();
	gGameOver = false;
	gTrackCompleted = false;
}

int Net_GetConnectedPlayerCount(void)
{
	int count = 0;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sLobbyState.players[i].active)
			count++;
	}
	return count;
}

const NetLobbyStateMsg* Net_GetLobbyState(void)
{
	return &sLobbyState;
}

NetLobbyConnectionState Net_GetConnectionState(const char** outStatusText)
{
	if (outStatusText)
		*outStatusText = sStatusText;
	return sConnState;
}

Boolean Net_StartClientDiscovery(void)
{
	Net_StopClientDiscovery();
	sNumDiscoveredHosts = 0;
	memset(sDiscoveredHosts, 0, sizeof(sDiscoveredHosts));

	sClientDiscoverySock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sClientDiscoverySock == NET_INVALID_SOCKET)
		return false;

	SetSocketNonBlockingAndBroadcast(sClientDiscoverySock, true);

	/* Bind ephemeral port (0) so local test client never collides with host on sDiscoveryPort */
	struct sockaddr_in bindAddr;
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = 0;
	bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sClientDiscoverySock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) != 0)
	{
		NetCloseSocket(sClientDiscoverySock);
		sClientDiscoverySock = NET_INVALID_SOCKET;
		return false;
	}

	sLastProbeMs = 0;
	Net_PollDiscovery();
	return true;
}

void Net_StopClientDiscovery(void)
{
	if (sClientDiscoverySock != NET_INVALID_SOCKET)
	{
		NetCloseSocket(sClientDiscoverySock);
		sClientDiscoverySock = NET_INVALID_SOCKET;
	}
}

void Net_PollDiscovery(void)
{
	if (sClientDiscoverySock == NET_INVALID_SOCKET)
		return;

	uint32_t now = NetNowMs();
	if ((now - sLastProbeMs) >= DISCOVERY_INTERVAL_MS)
	{
		sLastProbeMs = now;
		NetDiscoveryProbePacket probe;
		probe.magic = NET_MAGIC;
		probe.version = NET_PROTOCOL_VERSION;
		probe.reserved = 0;

		struct sockaddr_in dest;
		memset(&dest, 0, sizeof(dest));
		dest.sin_family = AF_INET;
		dest.sin_port = htons(sDiscoveryPort);
		dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		sendto(sClientDiscoverySock, (const char*)&probe, sizeof(probe), 0, (struct sockaddr*)&dest, sizeof(dest));

		/* Also probe loopback directly so 2 instances on the same machine find each other */
		dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		sendto(sClientDiscoverySock, (const char*)&probe, sizeof(probe), 0, (struct sockaddr*)&dest, sizeof(dest));
	}

	for (int iter = 0; iter < 32; iter++)
	{
		uint8_t rxBuf[256];
		struct sockaddr_in fromAddr;
		socklen_t fromLen = sizeof(fromAddr);
		int n = (int)recvfrom(sClientDiscoverySock, (char*)rxBuf, sizeof(rxBuf), 0,
		                      (struct sockaddr*)&fromAddr, &fromLen);
		if (n <= 0)
			break;

		if (n == (int)sizeof(NetDiscoveryBeaconPacket))
		{
			NetDiscoveryBeaconPacket beacon;
			memcpy(&beacon, rxBuf, sizeof(beacon));
			uint32_t ipHostOrder = ntohl(fromAddr.sin_addr.s_addr);
			UpsertDiscoveredHost(ipHostOrder, &beacon);
		}
	}

	/* Prune expired entries */
	int writeIdx = 0;
	for (int i = 0; i < sNumDiscoveredHosts; i++)
	{
		if ((now - sDiscoveredHosts[i].lastSeenMs) <= DISCOVERY_PRUNE_MS)
		{
			if (writeIdx != i)
				sDiscoveredHosts[writeIdx] = sDiscoveredHosts[i];
			writeIdx++;
		}
	}
	sNumDiscoveredHosts = writeIdx;
}

int Net_GetDiscoveredHosts(NetDiscoveredHostInfo* outHosts, int maxOut)
{
	int count = (sNumDiscoveredHosts < maxOut) ? sNumDiscoveredHosts : maxOut;
	if (outHosts && count > 0)
		memcpy(outHosts, sDiscoveredHosts, (size_t)count * sizeof(NetDiscoveredHostInfo));
	return count;
}

static char sPendingClientName[NET_MAX_NAME_LEN] = "PLAYER";

Boolean Net_ConnectToHost(const char* ipString, uint16_t port, const char* playerName)
{
	ShutdownNetworkManager();
	InitNetworkManager();

	if (!sENetInitialized)
		return false;

	uint32_t hostOrderIP = 0;
	if (!Net_ParseLANIPString(ipString, &hostOrderIP))
	{
		sConnState = kNetConn_Failed;
		SafeCopyStr(sStatusText, sizeof(sStatusText), "LAN IP ADDRESSES ONLY");
		return false;
	}

	sHost = enet_host_create(NULL, 1, NET_NUM_CHANNELS, 0, 0);
	if (!sHost)
	{
		sConnState = kNetConn_Failed;
		SafeCopyStr(sStatusText, sizeof(sStatusText), "FAILED TO CREATE CLIENT");
		return false;
	}

	ENetAddress address;
	address.host = htonl(hostOrderIP);
	address.port = (port != 0) ? port : sGamePort;

	sServerPeer = enet_host_connect(sHost, &address, NET_NUM_CHANNELS, 0);
	if (!sServerPeer)
	{
		enet_host_destroy(sHost);
		sHost = NULL;
		sConnState = kNetConn_Failed;
		SafeCopyStr(sStatusText, sizeof(sStatusText), "CONNECT FAILED");
		return false;
	}
	enet_peer_timeout(sServerPeer, 32, 15000, 45000);

	SafeCopyStr(sPendingClientName, sizeof(sPendingClientName),
	            (playerName && playerName[0]) ? playerName : "PLAYER");

	gIsNetworkHost = false;
	gIsNetworkClient = true;
	gNetGameInProgress = true;
	sClientMatchStarted = false;
	sClientLevelGo = false;
	sConnState = kNetConn_Connecting;
	SDL_snprintf(sStatusText, sizeof(sStatusText), "CONNECTING TO %s...", ipString);
	return true;
}

void Net_PollLobby(void)
{
	if (gIsNetworkHost)
		PollHostDiscoverySocket();

	if (!sHost)
		return;

	ENetEvent event;
	while (enet_host_service(sHost, &event, 0) > 0)
	{
		switch (event.type)
		{
			case ENET_EVENT_TYPE_CONNECT:
				if (gIsNetworkHost)
				{
					uint32_t ip = ntohl(event.peer->address.host);
					if (!Net_IsLANAddress(ip))
					{
						enet_peer_disconnect_now(event.peer, 0);
					}
					else
					{
						enet_peer_timeout(event.peer, 32, 15000, 45000);
					}
					/* Client will send kNetMsgJoinRequest */
				}
				else if (gIsNetworkClient)
				{
					int localCount = gNumLocalPlayers;
					if (localCount < 1) localCount = 1;
					if (localCount > MAX_LOCAL_PLAYERS) localCount = MAX_LOCAL_PLAYERS;

					NetJoinRequestMsg req;
					memset(&req, 0, sizeof(req));
					req.msgType = kNetMsgJoinRequest;
					req.version = NET_PROTOCOL_VERSION;
					req.numLocalPlayers = (uint8_t)localCount;
					SafeCopyStr(req.playerName, sizeof(req.playerName), sPendingClientName);
					SendENetPacket(sServerPeer, NET_CHANNEL_RELIABLE, &req, sizeof(req), ENET_PACKET_FLAG_RELIABLE);
				}
				break;

			case ENET_EVENT_TYPE_RECEIVE:
				if (gIsNetworkHost)
					HandlePacketOnHost(event.peer, event.packet->data, event.packet->dataLength);
				else if (gIsNetworkClient)
					HandlePacketOnClient(event.packet->data, event.packet->dataLength);
				enet_packet_destroy(event.packet);
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				if (gIsNetworkHost)
				{
					HandlePeerDisconnectOnHost(event.peer);
				}
				else if (gIsNetworkClient && event.peer == sServerPeer)
				{
					sServerPeer = NULL;
					sConnState = kNetConn_Disconnected;
					SafeCopyStr(sStatusText, sizeof(sStatusText), "DISCONNECTED FROM HOST");
					if (sLobbyState.matchStarted)
					{
						sMatchLost = true;
						gGameOver = true;
					}
				}
				break;

			default:
				break;
		}
	}
}

Boolean Net_ClientHasMatchStarted(void)
{
	return sClientMatchStarted;
}

void Net_DisconnectGracefully(void)
{
	if (!gNetGameInProgress || !sHost)
		return;

	NetPlayerLeaveMsg pl;
	pl.msgType = kNetMsgPlayerLeave;
	pl.playerNum = (uint8_t)gMyNetworkPlayerNum;

	if (gIsNetworkClient && sServerPeer)
	{
		SendENetPacket(sServerPeer, NET_CHANNEL_RELIABLE, &pl, sizeof(pl), ENET_PACKET_FLAG_RELIABLE);
		enet_host_flush(sHost);
	}
	else if (gIsNetworkHost)
	{
		BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &pl, sizeof(pl), ENET_PACKET_FLAG_RELIABLE, -1);
		enet_host_flush(sHost);
	}
}

Boolean Net_CheckMatchLost(void)
{
	return sMatchLost;
}

/**********************/
/* VEHICLE SELECTION  */
/**********************/

void PlayerBroadcastVehicleType(void)
{
	if (!gNetGameInProgress || !sHost)
		return;

	int localCount = gNumLocalPlayers;
	if (localCount < 1) localCount = 1;
	if (localCount > MAX_LOCAL_PLAYERS) localCount = MAX_LOCAL_PLAYERS;

	for (int pane = 0; pane < localCount; pane++)
	{
		int slot = gLocalPlayerNums[pane];
		if (slot < 0 || slot >= MAX_PLAYERS)
			continue;

		sLobbyState.players[slot].vehicleSelected = 1;
		sLobbyState.players[slot].ready = 1;
		sLobbyState.players[slot].sex = (int8_t)gPlayerInfo[slot].sex;
		sLobbyState.players[slot].vehicleType = (int8_t)gPlayerInfo[slot].vehicleType;
		sLobbyState.players[slot].skin = (int8_t)gPlayerInfo[slot].skin;
		sLobbyState.players[slot].team = gPlayerInfo[slot].team;

		if (gIsNetworkClient && sServerPeer)
		{
			NetVehicleSelectMsg msg;
			msg.msgType = kNetMsgVehicleSelect;
			msg.playerNum = (uint8_t)slot;
			msg.sex = (int8_t)gPlayerInfo[slot].sex;
			msg.vehicleType = (int8_t)gPlayerInfo[slot].vehicleType;
			msg.skin = (int8_t)gPlayerInfo[slot].skin;
			msg.team = gPlayerInfo[slot].team;
			SendENetPacket(sServerPeer, NET_CHANNEL_RELIABLE, &msg, sizeof(msg), ENET_PACKET_FLAG_RELIABLE);
		}
	}

	if (gIsNetworkHost)
	{
		BroadcastLobbyState();
	}
	else if (gIsNetworkClient && sServerPeer)
	{
		enet_host_flush(sHost);
	}
}

int Net_CheckVehicleSelectionsReady(void)
{
	if (!gNetGameInProgress)
		return 1;

	Net_PollLobby();

	if (gIsNetworkClient && (sConnState == kNetConn_Disconnected || sConnState == kNetConn_Failed || sMatchLost))
		return -1;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sLobbyState.players[i].active && !sLobbyState.players[i].vehicleSelected)
			return 0;
	}

	ApplyLobbyStateToGlobals();
	if (sPendingSeed != 0)
		SetMyRandomSeed(sPendingSeed);
	return 1;
}

Boolean GetVehicleSelectionFromNetPlayers(void)
{
	if (!gNetGameInProgress)
		return false;

	while (true)
	{
		DoSDLMaintenance();

		int ready = Net_CheckVehicleSelectionsReady();
		if (ready > 0)
			return false;
		if (ready < 0)
			return true;

		/* Allow backing out while waiting for slow peers */
		if (GetNewNeedStateAnyP(kNeed_UIBack))
		{
			Net_DisconnectGracefully();
			return true;
		}

		SDL_Delay(10);
	}
}

/**********************/
/* LEVEL LOAD BARRIER */
/**********************/

void HostWaitForPlayersToPrepareLevel(void)
{
	if (!gIsNetworkHost || !sHost)
		return;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sLobbyState.players[i].active && sClientPeers[i] == NULL)
			sClientLevelPreparedSent[i] = true;
	}
	uint32_t startMs = NetNowMs();

	while (true)
	{
		DoSDLMaintenance();
		Net_PollLobby();

		Boolean allPrepared = true;
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (sLobbyState.players[i].active && sClientPeers[i] && !sClientLevelPreparedSent[i])
			{
				allPrepared = false;
				break;
			}
		}

		if (allPrepared)
			break;

		/* Safety timeout (15s) so dropped silent peer doesn't wedge host forever */
		if ((NetNowMs() - startMs) > 15000u)
			break;

		SDL_Delay(5);
	}

	NetMatchGoMsg go;
	go.msgType = kNetMsgMatchGo;
	go.randomSeed = sPendingSeed;
	go.whoIsIt = (int8_t)gWhoIsIt;
	go.numPlayers = (uint8_t)gNumTotalPlayers;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &go, sizeof(go), ENET_PACKET_FLAG_RELIABLE, -1);
	enet_host_flush(sHost);
}

void ClientTellHostLevelIsPrepared(void)
{
	if (!gIsNetworkClient || !sHost || !sServerPeer)
		return;

	NetLevelPreparedMsg msg;
	msg.msgType = kNetMsgLevelPrepared;
	msg.playerNum = (uint8_t)gMyNetworkPlayerNum;
	SendENetPacket(sServerPeer, NET_CHANNEL_RELIABLE, &msg, sizeof(msg), ENET_PACKET_FLAG_RELIABLE);
	enet_host_flush(sHost);

	uint32_t startMs = NetNowMs();
	while (!sClientLevelGo && !sMatchLost &&
	       sConnState != kNetConn_Disconnected &&
	       sConnState != kNetConn_Failed)
	{
		DoSDLMaintenance();
		Net_PollLobby();

		if ((NetNowMs() - startMs) > 15000u)
			break;

		SDL_Delay(5);
	}

	if (gGameMode == GAME_MODE_TAG1 || gGameMode == GAME_MODE_TAG2)
	{
		gWhoIsIt = sPendingWhoIsIt;
	}
}

/**********************/
/* IN-GAME LOOP HOOKS */
/**********************/

void ClientSend_ControlInfoToHost(void)
{
	if (!gIsNetworkClient || !sHost || !sServerPeer)
		return;

	int localCount = gNumLocalPlayers;
	if (localCount < 1) localCount = 1;
	if (localCount > MAX_LOCAL_PLAYERS) localCount = MAX_LOCAL_PLAYERS;

	uint32_t anyNew = 0;
	for (int pane = 0; pane < localCount; pane++)
	{
		int slot = gLocalPlayerNums[pane];
		if (slot >= 0 && slot < MAX_PLAYERS)
		{
			sClientOutNewAccum[pane] |= gPlayerInfo[slot].controlBits_New;
			anyNew |= sClientOutNewAccum[pane];
		}
	}

	uint32_t now = NetNowMs();
	if ((now - sLastSentInputMs) < INPUT_SEND_INTERVAL_MS && anyNew == 0)
		return;
	sLastSentInputMs = now;

	uint32_t seq = ++sSendInputSeq;
	for (int pane = 0; pane < localCount; pane++)
	{
		int slot = gLocalPlayerNums[pane];
		if (slot < 0 || slot >= MAX_PLAYERS)
			continue;

		NetClientInputMsg msg;
		msg.msgType = kNetMsgClientInput;
		msg.playerNum = (uint8_t)slot;
		msg.seq = seq;
		msg.controlBits = gPlayerInfo[slot].controlBits;
		msg.controlBitsNew = sClientOutNewAccum[pane];
		msg.analogX = gPlayerInfo[slot].analogSteering.x;
		msg.analogY = gPlayerInfo[slot].analogSteering.y;

		sClientOutNewAccum[pane] = 0;
		SendENetPacket(sServerPeer, NET_CHANNEL_UNRELIABLE, &msg, sizeof(msg), 0);
	}
	enet_host_flush(sHost);
}

void HostReceive_ControlInfoFromClients(void)
{
	if (!gIsNetworkHost || !sHost)
		return;

	Net_PollLobby();

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (sClientPeers[i] == NULL || gPlayerInfo[i].isComputer)
			continue;

		if (sHostHasInput[i])
		{
			gPlayerInfo[i].controlBits = sHostLatestInput[i].controlBits;
			gPlayerInfo[i].controlBits_New = sHostPendingNewBits[i];
			gPlayerInfo[i].analogSteering.x = sHostLatestInput[i].analogX;
			gPlayerInfo[i].analogSteering.y = sHostLatestInput[i].analogY;
		}
		else
		{
			gPlayerInfo[i].controlBits = 0;
			gPlayerInfo[i].controlBits_New = 0;
			gPlayerInfo[i].analogSteering.x = 0.0f;
			gPlayerInfo[i].analogSteering.y = 0.0f;
		}
		sHostPendingNewBits[i] = 0;
	}
}

void HostSend_ControlInfoToClients(void)
{
	if (!gIsNetworkHost || !sHost)
		return;

	uint32_t now = NetNowMs();
	if ((now - sLastSentSnapMs) < SNAPSHOT_INTERVAL_MS)
		return;
	sLastSentSnapMs = now;

	NetHostSnapshotMsg snap;
	memset(&snap, 0, sizeof(snap));
	snap.msgType = kNetMsgHostSnapshot;
	snap.seq = ++sSendSnapSeq;
	snap.whoIsIt = (int8_t)gWhoIsIt;
	snap.numPlayers = (uint8_t)gNumTotalPlayers;
	snap.flagCount0 = (uint8_t)gCapturedFlagCount[0];
	snap.flagCount1 = (uint8_t)gCapturedFlagCount[1];
	snap.gameOver = gGameOver ? 1 : 0;
	snap.gameWinner = -1;

	int count = (gNumTotalPlayers <= MAX_PLAYERS) ? gNumTotalPlayers : MAX_PLAYERS;
	for (int i = 0; i < count; i++)
	{
		const PlayerInfoType* pi = &gPlayerInfo[i];
		NetPlayerSnapshot* ps = &snap.players[i];
		const ObjNode* car = pi->objNode;

		if (car)
		{
			ps->x = car->Coord.x;
			ps->y = car->Coord.y;
			ps->z = car->Coord.z;
			ps->rotX = car->Rot.x;
			ps->rotY = car->Rot.y;
			ps->rotZ = car->Rot.z;
			ps->vx = car->Delta.x;
			ps->vy = car->Delta.y;
			ps->vz = car->Delta.z;
			ps->speed2D = car->Speed2D;
		}

		ps->steering = pi->steering;
		ps->tagTimer = pi->tagTimer;
		ps->frozenTimer = pi->frozenTimer;
		ps->nitroTimer = pi->nitroTimer;
		ps->stickyTiresTimer = pi->stickyTiresTimer;
		ps->superSuspensionTimer = pi->superSuspensionTimer;
		ps->invisibilityTimer = pi->invisibilityTimer;
		ps->flamingTimer = pi->flamingTimer;
		ps->submarineImmobilized = pi->submarineImmobilized;

		float hp = pi->health * 100.0f;
		if (hp < 0.0f) hp = 0.0f;
		if (hp > 255.0f) hp = 255.0f;
		ps->health = (uint8_t)(hp + 0.5f);

		ps->powType = (int8_t)pi->powType;
		ps->powQuantity = (uint8_t)pi->powQuantity;
		ps->lapNum = (uint8_t)pi->lapNum;
		ps->place = (uint8_t)pi->place;
		ps->cameraMode = pi->cameraMode;
		ps->carriedTorchIdx = -1;

		uint16_t flags = 0;
		if (pi->isIt)         flags |= kPlayerSnapFlag_IsIt;
		if (pi->isEliminated) flags |= kPlayerSnapFlag_Eliminated;
		if (pi->raceComplete) flags |= kPlayerSnapFlag_RaceComplete;
		if (pi->isComputer)   flags |= kPlayerSnapFlag_IsComputer;
		if (pi->onWater)      flags |= kPlayerSnapFlag_OnWater;
		if (pi->isPlaning)    flags |= kPlayerSnapFlag_IsPlaning;
		if (pi->gasPedalDown) flags |= kPlayerSnapFlag_GasDown;
		if (pi->braking)      flags |= kPlayerSnapFlag_Braking;
		if (pi->wrongWay)     flags |= kPlayerSnapFlag_WrongWay;
		if (pi->makingSkid)   flags |= kPlayerSnapFlag_MakingSkid;
		if (car && (car->StatusBits & STATUS_BIT_ONGROUND))
			flags |= kPlayerSnapFlag_OnGround;
		if (pi->headObj && pi->headObj->Skeleton)
		{
			if (pi->headObj->Skeleton->AnimNum == PLAYER_ANIM_THROWFORWARD)
				flags |= kPlayerSnapFlag_ThrowFwd;
			else if (pi->headObj->Skeleton->AnimNum == PLAYER_ANIM_THROWBACKWARD)
				flags |= kPlayerSnapFlag_ThrowBack;
		}
		ps->flags = flags;
	}

	int torchCount = (gNumTorches <= NET_MAX_TORCHES) ? gNumTorches : NET_MAX_TORCHES;
	snap.numTorches = (uint8_t)torchCount;
	for (int t = 0; t < torchCount; t++)
	{
		if (gTorchObjs[t])
		{
			snap.torches[t].mode = (uint8_t)gTorchObjs[t]->Mode;
			snap.torches[t].playerNum = (int8_t)gTorchObjs[t]->PlayerNum;
			snap.torches[t].x = gTorchObjs[t]->Coord.x;
			snap.torches[t].y = gTorchObjs[t]->Coord.y;
			snap.torches[t].z = gTorchObjs[t]->Coord.z;
		}
	}

	int projCount = 0;
	for (int w = 0; w < NET_MAX_PROJECTILES && projCount < NET_MAX_PROJECTILES; w++)
	{
		if (sHostWeapons[w].active && sHostWeapons[w].obj && !IsObjectBeingDeleted(sHostWeapons[w].obj))
		{
			const ObjNode* obj = sHostWeapons[w].obj;
			NetProjectileSnapshot* pr = &snap.projectiles[projCount++];
			pr->netId = sHostWeapons[w].netId;
			pr->statusBits = obj->StatusBits & NET_PROJ_SYNC_STATUS_MASK;
			pr->group = obj->Group;
			pr->type = obj->Type;
			pr->kind = sHostWeapons[w].kind;
			pr->scale = obj->Scale.x;
			pr->x = obj->Coord.x;
			pr->y = obj->Coord.y;
			pr->z = obj->Coord.z;
			pr->rotX = obj->Rot.x;
			pr->rotY = obj->Rot.y;
			pr->rotZ = obj->Rot.z;
		}
	}
	snap.numProjectiles = (uint8_t)projCount;

	uint8_t wireBuf[sizeof(NetHostSnapshotMsg)];
	size_t hdrLen = offsetof(NetHostSnapshotMsg, players);
	size_t pBytes = (size_t)snap.numPlayers * sizeof(NetPlayerSnapshot);
	size_t tBytes = (size_t)snap.numTorches * sizeof(NetTorchSnapshot);
	size_t wBytes = (size_t)snap.numProjectiles * sizeof(NetProjectileSnapshot);
	size_t totalBytes = hdrLen + pBytes + tBytes + wBytes;

	uint8_t* dst = wireBuf;
	memcpy(dst, &snap, hdrLen);
	dst += hdrLen;
	if (pBytes > 0) { memcpy(dst, snap.players, pBytes); dst += pBytes; }
	if (tBytes > 0) { memcpy(dst, snap.torches, tBytes); dst += tBytes; }
	if (wBytes > 0) { memcpy(dst, snap.projectiles, wBytes); }

	BroadcastENetPacketToClients(NET_CHANNEL_UNRELIABLE, wireBuf, totalBytes, ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT, -1);
	enet_host_flush(sHost);
}

void ClientReceive_ControlInfoFromHost(void)
{
	if (!gIsNetworkClient || !sHost)
		return;

	Net_PollLobby();
}

void Net_ClientUpdateVisualFrame(void)
{
	if (!gIsNetworkClient || !sHasSnapshot)
		return;

	float fps = gFramesPerSecondFrac;
	float lerpAlpha = 1.0f - expf(-25.0f * (fps > 0.0f ? fps : 0.016f));
	if (lerpAlpha < 0.0f) lerpAlpha = 0.0f;
	if (lerpAlpha > 1.0f) lerpAlpha = 1.0f;

	float snapAge = (sLastSnapRecvMs != 0) ? ((float)(NetNowMs() - sLastSnapRecvMs) * 0.001f) : 0.0f;
	if (snapAge < 0.0f) snapAge = 0.0f;
	if (snapAge > 0.045f) snapAge = 0.045f;

	int count = (sCurrSnap.numPlayers <= MAX_PLAYERS) ? sCurrSnap.numPlayers : MAX_PLAYERS;
	for (int i = 0; i < count; i++)
	{
		ObjNode* car = gPlayerInfo[i].objNode;
		if (!car)
			continue;

		const NetPlayerSnapshot* ps = &sCurrSnap.players[i];
		float targetX = ps->x + ps->vx * snapAge;
		float targetY = ps->y + ps->vy * snapAge;
		float targetZ = ps->z + ps->vz * snapAge;

		car->OldCoord = car->Coord;
		car->Coord.x += (targetX - car->Coord.x) * lerpAlpha;
		car->Coord.y += (targetY - car->Coord.y) * lerpAlpha;
		car->Coord.z += (targetZ - car->Coord.z) * lerpAlpha;
		gPlayerInfo[i].coord = car->Coord;

		/* Wrap-safe Y rotation blend */
		float dRotY = ps->rotY - car->Rot.y;
		while (dRotY > PI)  dRotY -= PI2;
		while (dRotY < -PI) dRotY += PI2;

		car->Rot.x += (ps->rotX - car->Rot.x) * lerpAlpha;
		car->Rot.y += dRotY * lerpAlpha;
		car->Rot.z += (ps->rotZ - car->Rot.z) * lerpAlpha;

		UpdateCarVisualsFromSnapshot(car);
		if (gTrackNum == TRACK_NUM_ATLANTIS)
			AlignPropellerOnSubmarine(car);
	}

	for (int s = 0; s < NET_MAX_PROJECTILES; s++)
	{
		if (sClientWeapons[s].active && sClientWeapons[s].obj && !IsObjectBeingDeleted(sClientWeapons[s].obj))
		{
			UpdateClientProjectileTrail(sClientWeapons[s].obj);
		}
	}
}

/**********************/
/* HOST EVENT SENDERS */
/**********************/

void Net_HostSendPOWPickup(uint32_t itemIndex, short playerNum, short powType, float respawnTimer)
{
	if (!gIsNetworkHost || !sHost)
		return;

	NetReliableEventMsg ev;
	memset(&ev, 0, sizeof(ev));
	ev.msgType = kNetMsgReliableEvent;
	ev.kind = kNetEvent_POWPickup;
	ev.a = (int8_t)playerNum;
	ev.b = (int8_t)powType;
	ev.paramU32 = itemIndex;
	ev.f0 = respawnTimer;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
}

void Net_HostSendExplosion(float x, float y, float z, short typeHint)
{
	if (!gIsNetworkHost || !sHost)
		return;

	NetReliableEventMsg ev;
	memset(&ev, 0, sizeof(ev));
	ev.msgType = kNetMsgReliableEvent;
	ev.kind = kNetEvent_Explosion;
	ev.c = typeHint;
	ev.f0 = x;
	ev.f1 = y;
	ev.f2 = z;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
}

void Net_HostSendSound3D(short effectId, float x, float y, float z, uint32_t rateMultiplier, float volume)
{
	if (!gIsNetworkHost || !sHost)
		return;

	NetReliableEventMsg ev;
	memset(&ev, 0, sizeof(ev));
	ev.msgType = kNetMsgReliableEvent;
	ev.kind = kNetEvent_Sound3D;
	ev.c = effectId;
	ev.paramU32 = rateMultiplier;
	ev.f0 = x;
	ev.f1 = y;
	ev.f2 = z;
	ev.f3 = volume;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
}

void Net_HostSendAnnouncerSound(short targetPlayer, short effectId, Boolean override, float delay)
{
	if (!gIsNetworkHost || !sHost)
		return;

	NetReliableEventMsg ev;
	memset(&ev, 0, sizeof(ev));
	ev.msgType = kNetMsgReliableEvent;
	ev.kind = kNetEvent_AnnouncerSound;
	ev.a = (int8_t)targetPlayer;
	ev.b = override ? 1 : 0;
	ev.c = effectId;
	ev.f0 = delay;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
}

void Net_HostSendLapOrFinish(short playerNum)
{
	if (!gIsNetworkHost || !sHost)
		return;

	NetReliableEventMsg ev;
	memset(&ev, 0, sizeof(ev));
	ev.msgType = kNetMsgReliableEvent;
	ev.kind = kNetEvent_LapOrFinish;
	ev.a = (int8_t)playerNum;
	ev.b = (playerNum >= 0 && playerNum < MAX_PLAYERS) ? (int8_t)gPlayerInfo[playerNum].place : 0;
	ev.c = gTrackCompleted ? 1 : 0;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
}

void Net_HostSendFlagEvent(short eventKind, short actorPlayer)
{
	if (!gIsNetworkHost || !sHost)
		return;

	NetReliableEventMsg ev;
	memset(&ev, 0, sizeof(ev));
	ev.msgType = kNetMsgReliableEvent;
	ev.kind = kNetEvent_FlagEvent;
	ev.a = (int8_t)gCapturedFlagCount[0];
	ev.b = (int8_t)gCapturedFlagCount[1];
	ev.c = eventKind;
	ev.paramU32 = (uint32_t)actorPlayer;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
}

void Net_HostSendGameOver(short winnerPlayer)
{
	if (!gIsNetworkHost || !sHost)
		return;

	NetReliableEventMsg ev;
	memset(&ev, 0, sizeof(ev));
	ev.msgType = kNetMsgReliableEvent;
	ev.kind = kNetEvent_GameOver;
	ev.a = (int8_t)winnerPlayer;
	if (winnerPlayer >= 0 && winnerPlayer < MAX_PLAYERS)
		ev.b = (int8_t)gPlayerInfo[winnerPlayer].team;
	ev.c = 0;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
}

void Net_HostSendElimination(short eliminatedPlayer)
{
	if (!gIsNetworkHost || !sHost)
		return;

	NetReliableEventMsg ev;
	memset(&ev, 0, sizeof(ev));
	ev.msgType = kNetMsgReliableEvent;
	ev.kind = kNetEvent_GameOver;
	ev.a = (int8_t)eliminatedPlayer;
	ev.c = 1;
	BroadcastENetPacketToClients(NET_CHANNEL_RELIABLE, &ev, sizeof(ev), ENET_PACKET_FLAG_RELIABLE, -1);
}

/**********************/
/* PROJECTILE TRACKING*/
/**********************/

static Boolean Net_IsStaticGroundHazard(const ObjNode* obj)
{
	return obj && obj->Group == MODEL_GROUP_WEAPONS &&
		(obj->Type == WEAPONS_ObjType_OilPatch || obj->Type == WEAPONS_ObjType_LandMine);
}

void Net_RegisterWeaponObj(struct ObjNode* obj, uint8_t kind)
{
	if (!gIsNetworkHost || !obj)
		return;

	int freeIdx = -1;
	int oldestHazardIdx = -1;
	int oldestAnyIdx = -1;

	for (int i = 0; i < NET_MAX_PROJECTILES; i++)
	{
		if (sHostWeapons[i].active)
		{
			if (!sHostWeapons[i].obj || IsObjectBeingDeleted(sHostWeapons[i].obj))
			{
				memset(&sHostWeapons[i], 0, sizeof(sHostWeapons[i]));
				if (freeIdx < 0)
					freeIdx = i;
				continue;
			}
			if (sHostWeapons[i].obj == obj)
				return;

			if (Net_IsStaticGroundHazard(sHostWeapons[i].obj))
			{
				if (oldestHazardIdx < 0 || sHostWeapons[i].netId < sHostWeapons[oldestHazardIdx].netId)
					oldestHazardIdx = i;
			}
			if (oldestAnyIdx < 0 || sHostWeapons[i].netId < sHostWeapons[oldestAnyIdx].netId)
				oldestAnyIdx = i;
		}
		else if (freeIdx < 0)
		{
			freeIdx = i;
		}
	}

	/* Evict oldest static ground hazard (or oldest entry) so newly fired items never drop mid-match */
	int slot = freeIdx;
	if (slot < 0)
		slot = (oldestHazardIdx >= 0) ? oldestHazardIdx : oldestAnyIdx;
	if (slot < 0)
		return;

	sHostWeapons[slot].active = true;
	sHostWeapons[slot].netId = sNextWeaponNetId++;
	sHostWeapons[slot].kind = kind;
	sHostWeapons[slot].obj = obj;
}

void Net_UnregisterWeaponObj(struct ObjNode* obj)
{
	if (!obj)
		return;

	for (int i = 0; i < NET_MAX_PROJECTILES; i++)
	{
		if (sHostWeapons[i].active && sHostWeapons[i].obj == obj)
		{
			memset(&sHostWeapons[i], 0, sizeof(sHostWeapons[i]));
		}
		if (sClientWeapons[i].active && sClientWeapons[i].obj == obj)
		{
			memset(&sClientWeapons[i], 0, sizeof(sClientWeapons[i]));
		}
	}
}
