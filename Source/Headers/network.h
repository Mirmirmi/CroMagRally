//
// network.h — Host-Authoritative LAN Multiplayer & Discovery
//

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_DEFAULT_PORT        29400
#define NET_DISCOVERY_PORT      29401
#define NET_PROTOCOL_VERSION    5
#define NET_MAGIC               0x434D524Eu /* 'CMRN' */
#define NET_MAX_DISCOVERED_HOSTS 16
#define NET_MAX_PROJECTILES     64
#define NET_MAX_TORCHES         12
#define NET_MAX_NAME_LEN        32

typedef enum NetPlayerSnapFlags
{
	kPlayerSnapFlag_IsIt         = 1 << 0,
	kPlayerSnapFlag_Eliminated   = 1 << 1,
	kPlayerSnapFlag_RaceComplete = 1 << 2,
	kPlayerSnapFlag_IsComputer   = 1 << 3,
	kPlayerSnapFlag_OnWater      = 1 << 4,
	kPlayerSnapFlag_IsPlaning    = 1 << 5,
	kPlayerSnapFlag_GasDown      = 1 << 6,
	kPlayerSnapFlag_Braking      = 1 << 7,
	kPlayerSnapFlag_WrongWay     = 1 << 8,
	kPlayerSnapFlag_MakingSkid   = 1 << 9,
	kPlayerSnapFlag_SubMarine    = 1 << 10,
	kPlayerSnapFlag_OnGround     = 1 << 11,
	kPlayerSnapFlag_ThrowFwd     = 1 << 12,
	kPlayerSnapFlag_ThrowBack    = 1 << 13
} NetPlayerSnapFlags;

typedef enum NetChannelID
{
	NET_CHANNEL_UNRELIABLE = 0,
	NET_CHANNEL_RELIABLE   = 1,
	NET_NUM_CHANNELS       = 2
} NetChannelID;

typedef enum NetMsgType
{
	kNetMsgJoinRequest = 1,
	kNetMsgJoinAck,
	kNetMsgLobbyState,
	kNetMsgVehicleSelect,
	kNetMsgStartMatch,
	kNetMsgLevelPrepared,
	kNetMsgMatchGo,
	kNetMsgClientInput,
	kNetMsgHostSnapshot,
	kNetMsgReliableEvent,
	kNetMsgPlayerLeave,
	kNetMsgReturnToLobby
} NetMsgType;

typedef enum NetReliableEventKind
{
	kNetEvent_POWPickup = 1,
	kNetEvent_Explosion,
	kNetEvent_Sound3D,
	kNetEvent_LapOrFinish,
	kNetEvent_FlagEvent,
	kNetEvent_GameOver,
	kNetEvent_PlayerLeft,
	kNetEvent_AnnouncerSound
} NetReliableEventKind;

typedef enum NetExplosionKind
{
	kNetExplosion_Bomb = 0,
	kNetExplosion_Snow,
	kNetExplosion_RomanCandle,
	kNetExplosion_BottleRocket,
	kNetExplosion_Torpedo,
	kNetExplosion_LandMine
} NetExplosionKind;

typedef enum NetLobbyConnectionState
{
	kNetConn_Idle = 0,
	kNetConn_Hosting,
	kNetConn_Connecting,
	kNetConn_Connected,
	kNetConn_Disconnected,
	kNetConn_Failed
} NetLobbyConnectionState;

#pragma pack(push, 1)

typedef struct NetDiscoveryProbePacket
{
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
} NetDiscoveryProbePacket;

typedef struct NetDiscoveryBeaconPacket
{
	uint32_t magic;
	uint16_t version;
	uint16_t gamePort;
	uint8_t  gameMode;
	uint8_t  age;
	uint8_t  trackNum;
	uint8_t  curPlayers;
	uint8_t  maxPlayers;
	uint8_t  inProgress;
	char     hostName[NET_MAX_NAME_LEN];
} NetDiscoveryBeaconPacket;

typedef struct NetJoinRequestMsg
{
	uint8_t  msgType;
	uint16_t version;
	uint8_t  numLocalPlayers;
	char     playerName[NET_MAX_NAME_LEN];
} NetJoinRequestMsg;

typedef struct NetJoinAckMsg
{
	uint8_t  msgType;
	uint8_t  accepted;
	uint8_t  rejectReason;
	uint8_t  assignedCount;
	uint16_t clientMachineId;
	uint8_t  assignedSlots[MAX_LOCAL_PLAYERS];
} NetJoinAckMsg;

typedef struct NetLobbyPlayerSlot
{
	uint8_t  active;
	uint8_t  ready;
	uint8_t  vehicleSelected;
	int8_t   sex;
	int8_t   vehicleType;
	int8_t   skin;
	uint8_t  team;
	uint16_t machineId;
	char     name[NET_MAX_NAME_LEN];
} NetLobbyPlayerSlot;

typedef struct NetLobbyStateMsg
{
	uint8_t            msgType;
	uint8_t            gameMode;
	uint8_t            age;
	uint8_t            trackNum;
	uint8_t            difficulty;
	uint8_t            multiplayerCPUs;
	uint8_t            tagDuration;
	uint8_t            numRealPlayers;
	uint8_t            matchStarted;
	NetLobbyPlayerSlot players[MAX_PLAYERS];
} NetLobbyStateMsg;

typedef struct NetVehicleSelectMsg
{
	uint8_t msgType;
	uint8_t playerNum;
	int8_t  sex;
	int8_t  vehicleType;
	int8_t  skin;
	uint8_t team;
} NetVehicleSelectMsg;

typedef struct NetStartMatchMsg
{
	uint8_t  msgType;
	uint32_t matchId;
	uint32_t randomSeed;
} NetStartMatchMsg;

typedef struct NetReturnToLobbyMsg
{
	uint8_t  msgType;
	uint32_t endedMatchId;
} NetReturnToLobbyMsg;

typedef struct NetLevelPreparedMsg
{
	uint8_t msgType;
	uint8_t playerNum;
} NetLevelPreparedMsg;

typedef struct NetMatchGoMsg
{
	uint8_t  msgType;
	uint32_t randomSeed;
	int8_t   whoIsIt;
	uint8_t  numPlayers;
} NetMatchGoMsg;

typedef struct NetClientInputMsg
{
	uint8_t  msgType;
	uint8_t  playerNum;
	uint32_t seq;
	uint32_t controlBits;
	uint32_t controlBitsNew;
	float    analogX;
	float    analogY;
} NetClientInputMsg;

typedef struct NetPlayerSnapshot
{
	float    x, y, z;
	float    rotX, rotY, rotZ;
	float    vx, vy, vz;
	float    speed2D;
	float    steering;
	float    tagTimer;
	float    frozenTimer;
	float    nitroTimer;
	float    stickyTiresTimer;
	float    superSuspensionTimer;
	float    invisibilityTimer;
	float    flamingTimer;
	float    submarineImmobilized;
	float    finishTime;
	uint16_t flags;         /* isIt, eliminated, finished, isComputer, onWater, isPlaning, gasDown, braking, wrongWay, makingSkid */
	uint8_t  health;
	int8_t   powType;
	uint8_t  powQuantity;
	uint8_t  lapNum;
	uint8_t  place;
	uint8_t  cameraMode;
	int8_t   carriedTorchIdx;
} NetPlayerSnapshot;

typedef struct NetTorchSnapshot
{
	uint8_t mode;
	int8_t  playerNum;
	float   x, y, z;
} NetTorchSnapshot;

typedef struct NetProjectileSnapshot
{
	uint32_t netId;
	uint32_t statusBits;
	uint8_t  group;
	uint8_t  type;
	uint8_t  kind;
	float    scale;
	float    x, y, z;
	float    rotX, rotY, rotZ;
} NetProjectileSnapshot;

typedef struct NetHostSnapshotMsg
{
	uint8_t               msgType;
	uint32_t              seq;
	int8_t                whoIsIt;
	uint8_t               numPlayers;
	uint8_t               flagCount0;
	uint8_t               flagCount1;
	uint8_t               gameOver;
	int8_t                gameWinner;
	uint8_t               numTorches;
	uint8_t               numProjectiles;
	float                 elapsedTime;
	NetPlayerSnapshot     players[MAX_PLAYERS];
	NetTorchSnapshot      torches[NET_MAX_TORCHES];
	NetProjectileSnapshot projectiles[NET_MAX_PROJECTILES];
} NetHostSnapshotMsg;

typedef struct NetReliableEventMsg
{
	uint8_t  msgType;
	uint8_t  kind;
	int8_t   a;
	int8_t   b;
	int16_t  c;
	uint32_t paramU32;
	float    f0;
	float    f1;
	float    f2;
	float    f3;
} NetReliableEventMsg;

typedef struct NetPlayerLeaveMsg
{
	uint8_t msgType;
	uint8_t playerNum;
} NetPlayerLeaveMsg;

#pragma pack(pop)

/* UI / Discovery host entry */
typedef struct NetDiscoveredHostInfo
{
	uint32_t ipHostOrder;
	char     ipString[32];
	uint16_t gamePort;
	uint8_t  gameMode;
	uint8_t  age;
	uint8_t  trackNum;
	uint8_t  curPlayers;
	uint8_t  maxPlayers;
	uint8_t  inProgress;
	char     hostName[NET_MAX_NAME_LEN];
	uint32_t lastSeenMs;
} NetDiscoveredHostInfo;

/* Global player name array shared with Infobar */
extern Str32 gPlayerNameStrings[MAX_PLAYERS];

/* Core lifecycle */
void    InitNetworkManager(void);
void    DeinitNetworkManager(void);
void    ShutdownNetworkManager(void);
void    EndNetworkGame(void);
void    Net_SetPorts(uint16_t gamePort, uint16_t discoveryPort);
void    Net_GetPorts(uint16_t* outGamePort, uint16_t* outDiscoveryPort);

/* LAN utilities */
Boolean Net_IsLANAddress(uint32_t hostOrderIPv4);
Boolean Net_ParseLANIPString(const char* ipStr, uint32_t* outHostOrderIPv4);
void    Net_GetLocalIPString(char* outBuf, size_t bufSize);

/* Host & Client session APIs (non-blocking, UI/test friendly) */
Boolean Net_StartHostLobby(const char* hostName);
void    Net_SyncHostLobbySettings(void);
Boolean Net_HostLaunchMatch(void);
void    Net_HostReturnToLobby(void);
void    Net_ClientPrepareReturnToLobby(void);
Boolean Net_HasRemoteClients(void);
short   Net_GetLocalPlayerCount(void);
Boolean Net_HostKickOrRejectCount(void);
int     Net_GetConnectedPlayerCount(void);
uint16_t Net_GetMyMachineId(void);
const NetLobbyStateMsg* Net_GetLobbyState(void);
NetLobbyConnectionState Net_GetConnectionState(const char** outStatusText);

Boolean Net_StartClientDiscovery(void);
void    Net_StopClientDiscovery(void);
void    Net_PollDiscovery(void);
int     Net_GetDiscoveredHosts(NetDiscoveredHostInfo* outHosts, int maxOut);

Boolean Net_ConnectToHost(const char* ipString, uint16_t port, const char* playerName);
void    Net_PollLobby(void);
Boolean Net_ClientHasMatchStarted(void);
void    Net_DisconnectGracefully(void);
Boolean Net_CheckMatchLost(void);

/* Screen / Menu entry points (Phase 3 + Persistent Lobby) */
Boolean SetupNetworkHosting(void);
Boolean SetupNetworkJoin(void);
Boolean ReenterHostLobby(void);
Boolean ReenterClientLobby(void);

/* Vehicle selection sync */
void    PlayerBroadcastVehicleType(void);
int     Net_CheckVehicleSelectionsReady(void);
Boolean GetVehicleSelectionFromNetPlayers(void);

/* Level load barrier */
void    HostWaitForPlayersToPrepareLevel(void);
void    ClientTellHostLevelIsPrepared(void);

/* In-game Host-Authoritative loop hooks */
void    HostSend_ControlInfoToClients(void);
void    ClientSend_ControlInfoToHost(void);
void    ClientReceive_ControlInfoFromHost(void);
void    HostReceive_ControlInfoFromClients(void);

/* Host-only reliable event emitters */
void    Net_HostSendPOWPickup(uint32_t itemIndex, short playerNum, short powType, float respawnTimer);
void    Net_HostSendExplosion(float x, float y, float z, short typeHint);
void    Net_HostSendSound3D(short effectId, float x, float y, float z, uint32_t rateMultiplier, float volume);
void    Net_HostSendAnnouncerSound(short targetPlayer, short effectId, Boolean override, float delay);
void    Net_HostSendLapOrFinish(short playerNum);
void    Net_HostSendFlagEvent(short eventKind, short actorPlayer);
void    Net_HostSendElimination(short eliminatedPlayer);
void    Net_HostSendGameOver(short winnerPlayer);

/* Projectile tracking on Host & Client */
struct ObjNode;
void    Net_ResetGameSyncState(void);
void    Net_RegisterWeaponObj(struct ObjNode* obj, uint8_t kind);
void    Net_UnregisterWeaponObj(struct ObjNode* obj);

/* Client visual application (interpolates / drives client scene without physics) */
void    Net_ClientUpdateVisualFrame(void);

#ifdef __cplusplus
}
#endif
