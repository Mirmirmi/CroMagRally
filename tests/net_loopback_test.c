#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "game.h"
#include "network.h"

/* Globals declared in game.h and referenced by network.c */
Boolean                gGameOver = false;
Boolean                gIsInGame = false;
Boolean                gTrackCompleted = false;
Byte                   gMultiplayerCPUs = 0;
float                  gFramesPerSecondFrac = 1.0f / 60.0f;
float                  gTrackCompletedCoolDownTimer = 0.0f;
int                    gGameMode = GAME_MODE_MULTIPLAYERRACE;
int                    gTheAge = 0;
int                    gTrackNum = TRACK_NUM_DESERT;
ObjNode               *gTorchObjs[NET_MAX_TORCHES];
PlayerInfoType         gPlayerInfo[MAX_PLAYERS];
PrefsType              gGamePrefs;
short                  gCapturedFlagCount[2] = {0, 0};
short                  gLocalPlayerNums[MAX_LOCAL_PLAYERS] = {0, 1, 2, 3};
short                  gMyNetworkPlayerNum = 0;
short                  gNumLapsThisRace = LAPS_PER_RACE;
short                  gNumLocalPlayers = 1;
short                  gNumRealPlayers = 0;
short                  gNumTorches = 0;
short                  gNumTotalPlayers = 0;
short                  gWhoIsIt = 0;
TerrainItemEntryType **gMasterItemList = NULL;

/* Stub observation counters for event verification */
static int                  sPowHideCalls = 0;
static const TerrainItemEntryType *sLastPowTarget = NULL;
static float                sLastPowTimer = 0.0f;
static int                  sShowWinLoseCalls = 0;
static short                sLastWinner = -1;
static int                  sShowFinalPlaceCalls = 0;
static int                  sSound3DCalls = 0;
static short                sLastSound3DEffect = -1;
static uint32_t             sLastSound3DRate = 0;
static float                sLastSound3DVolume = 0.0f;
static int                  sAnnouncerCalls = 0;
static short                sLastAnnouncerEffect = -1;
static Boolean              sLastAnnouncerOverride = false;
static float                sLastAnnouncerDelay = 0.0f;
static int                  sSnowExplosionCalls = 0;
static int                  sWeaponExplosionCalls = 0;
static short                sLastWeaponExplosionKind = -1;
static int                  sTrailCalls = 0;
static int                  sShowLapNumCalls = 0;
static int                  sMorphAnimCalls = 0;
static long                 sLastMorphAnim = -1;
static ObjNode              sTestObjPool[128];
static int                  sTestObjPoolNext = 0;
static unsigned long        sSeed = 0x12345678UL;

void DeleteObject(ObjNode *theNode)
{
	(void)theNode;
}

void DoSDLMaintenance(void)
{
}

Boolean GetNewNeedStateAnyP(int needID)
{
	(void)needID;
	return false;
}

Boolean IsObjectBeingDeleted(ObjNode *theNode)
{
	(void)theNode;
	return false;
}

void MakeBombExplosion(ObjNode *theBomb, float x, float z, const OGLVector3D *delta)
{
	(void)theBomb;
	(void)x;
	(void)z;
	(void)delta;
}

void MakeSnowExplosion(const OGLPoint3D *where)
{
	(void)where;
	sSnowExplosionCalls++;
}

void PlayWeaponExplosionFX(short explosionKind, const OGLPoint3D *where)
{
	(void)where;
	sWeaponExplosionCalls++;
	sLastWeaponExplosionKind = explosionKind;
}

void UpdateClientProjectileTrail(ObjNode *theNode)
{
	(void)theNode;
	sTrailCalls++;
}

ObjNode *MakeNewDisplayGroupObject(NewObjectDefinitionType *newObjDef)
{
	if (!newObjDef)
		return NULL;
	ObjNode *obj = &sTestObjPool[sTestObjPoolNext++ % 128];
	memset(obj, 0, sizeof(*obj));
	obj->Group = newObjDef->group;
	obj->Type = newObjDef->type;
	obj->Coord = newObjDef->coord;
	obj->StatusBits = newObjDef->flags;
	obj->Scale.x = obj->Scale.y = obj->Scale.z = (newObjDef->scale > 0.0f) ? newObjDef->scale : 1.0f;
	return obj;
}

ObjNode *MakeNewSkeletonObject(NewObjectDefinitionType *newObjDef)
{
	return MakeNewDisplayGroupObject(newObjDef);
}

unsigned long MyRandomLong(void)
{
	sSeed = sSeed * 1664525UL + 1013904223UL;
	return sSeed;
}

void SetMyRandomSeed(unsigned long seed)
{
	sSeed = seed;
}

void Net_ClientHidePOWTrigger(const TerrainItemEntryType *targetPtr, float timer)
{
	sPowHideCalls++;
	sLastPowTarget = targetPtr;
	sLastPowTimer = timer;
}

short PlayEffect(short effectNum)
{
	(void)effectNum;
	return 0;
}

short PlayEffect_Parms(short effectNum, uint32_t leftVolume, uint32_t rightVolume, unsigned long rateMultiplier)
{
	(void)effectNum;
	(void)leftVolume;
	(void)rightVolume;
	(void)rateMultiplier;
	return 0;
}

short PlayEffect_Parms3D(short effectNum, const OGLPoint3D *where, uint32_t rateMultiplier, float volumeAdjust)
{
	(void)where;
	sSound3DCalls++;
	sLastSound3DEffect = effectNum;
	sLastSound3DRate = rateMultiplier;
	sLastSound3DVolume = volumeAdjust;
	return 0;
}

void PlayAnnouncerSound(short effectNum, Boolean override, float delay)
{
	sAnnouncerCalls++;
	sLastAnnouncerEffect = effectNum;
	sLastAnnouncerOverride = override;
	sLastAnnouncerDelay = delay;
}

void ShowFinalPlace(short playerNum, int rankInScoreboard)
{
	(void)playerNum;
	(void)rankInScoreboard;
	sShowFinalPlaceCalls++;
}

void ShowLapNum(short playerNum)
{
	(void)playerNum;
	sShowLapNumCalls++;
}

void MorphToSkeletonAnim(SkeletonObjDataType *skeleton, long animNum, float speed)
{
	(void)speed;
	sMorphAnimCalls++;
	sLastMorphAnim = animNum;
	if (skeleton)
		skeleton->AnimNum = (int16_t)animNum;
}

void ShowWinLose(short playerNum, Byte mode, short winner)
{
	(void)playerNum;
	(void)mode;
	sShowWinLoseCalls++;
	sLastWinner = winner;
}

void UpdateCarVisualsFromSnapshot(ObjNode *theNode)
{
	(void)theNode;
}

void AlignPropellerOnSubmarine(ObjNode *theCar)
{
	(void)theCar;
}

void UpdateObjectTransforms(ObjNode *theNode)
{
	(void)theNode;
}

#define TEST_ASSERT(cond, msg)                                                           \
	do                                                                               \
	{                                                                                \
		if (!(cond))                                                             \
		{                                                                        \
			fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, (msg)); \
			return 1;                                                        \
		}                                                                        \
	} while (0)

static int TestLANValidation(void)
{
	/* Valid RFC1918, loopback, and link-local IPv4 host-order addresses */
	TEST_ASSERT(Net_IsLANAddress(0x7F000001u), "127.0.0.1 should be LAN");
	TEST_ASSERT(Net_IsLANAddress(0x0A000001u), "10.0.0.1 should be LAN");
	TEST_ASSERT(Net_IsLANAddress(0x0AFFFFFEu), "10.255.255.254 should be LAN");
	TEST_ASSERT(Net_IsLANAddress(0xAC100001u), "172.16.0.1 should be LAN");
	TEST_ASSERT(Net_IsLANAddress(0xAC1FFFFEu), "172.31.255.254 should be LAN");
	TEST_ASSERT(Net_IsLANAddress(0xC0A80001u), "192.168.0.1 should be LAN");
	TEST_ASSERT(Net_IsLANAddress(0xC0A8FFFEu), "192.168.255.254 should be LAN");
	TEST_ASSERT(Net_IsLANAddress(0xA9FE0101u), "169.254.1.1 should be LAN");

	/* Rejected public / unspecified / out-of-range addresses */
	TEST_ASSERT(!Net_IsLANAddress(0x00000000u), "0.0.0.0 must be rejected");
	TEST_ASSERT(!Net_IsLANAddress(0x08080808u), "8.8.8.8 must be rejected");
	TEST_ASSERT(!Net_IsLANAddress(0x01010101u), "1.1.1.1 must be rejected");
	TEST_ASSERT(!Net_IsLANAddress(0xAC0FFFFFu), "172.15.255.255 must be rejected");
	TEST_ASSERT(!Net_IsLANAddress(0xAC200001u), "172.32.0.1 must be rejected");
	TEST_ASSERT(!Net_IsLANAddress(0xC0A90101u), "192.169.1.1 must be rejected");
	TEST_ASSERT(!Net_IsLANAddress(0xE0000001u), "224.0.0.1 must be rejected");
	TEST_ASSERT(!Net_IsLANAddress(0xFFFFFFFFu), "255.255.255.255 must be rejected");

	uint32_t parsed = 0;
	TEST_ASSERT(Net_ParseLANIPString("127.0.0.1", &parsed) && parsed == 0x7F000001u,
	            "Parse 127.0.0.1");
	TEST_ASSERT(Net_ParseLANIPString("192.168.1.50", &parsed) && parsed == 0xC0A80132u,
	            "Parse 192.168.1.50");
	TEST_ASSERT(Net_ParseLANIPString("10.42.0.9", &parsed) && parsed == 0x0A2A0009u,
	            "Parse 10.42.0.9");
	TEST_ASSERT(Net_ParseLANIPString("172.20.1.2", &parsed) && parsed == 0xAC140102u,
	            "Parse 172.20.1.2");
	TEST_ASSERT(Net_ParseLANIPString("169.254.10.20", &parsed) && parsed == 0xA9FE0A14u,
	            "Parse 169.254.10.20");

	TEST_ASSERT(!Net_ParseLANIPString("8.8.8.8", &parsed), "Reject public 8.8.8.8");
	TEST_ASSERT(!Net_ParseLANIPString("172.32.0.1", &parsed), "Reject 172.32.0.1");
	TEST_ASSERT(!Net_ParseLANIPString("192.169.0.1", &parsed), "Reject 192.169.0.1");
	TEST_ASSERT(!Net_ParseLANIPString("not-an-ip", &parsed), "Reject malformed string");
	TEST_ASSERT(!Net_ParseLANIPString("", &parsed), "Reject empty string");
	TEST_ASSERT(!Net_ParseLANIPString(NULL, &parsed), "Reject NULL string");

	char localIP[32];
	Net_GetLocalIPString(localIP, sizeof(localIP));
	TEST_ASSERT(localIP[0] != '\0', "Net_GetLocalIPString returns non-empty string");
	return 0;
}

static int RunHostChild(uint16_t gamePort, uint16_t discPort)
{
	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	memset(&gGamePrefs, 0, sizeof(gGamePrefs));
	gGameMode = GAME_MODE_MULTIPLAYERRACE;
	gTheAge = 1;
	gTrackNum = TRACK_NUM_CHINA;
	gGamePrefs.difficulty = 1;
	gGamePrefs.tagDuration = 3;
	gMultiplayerCPUs = 1;
	gGameOver = false;
	gTrackCompleted = false;
	gNumLocalPlayers = 1;
	gLocalPlayerNums[0] = 0;
	gLocalPlayerNums[1] = 1;
	gLocalPlayerNums[2] = 2;
	gLocalPlayerNums[3] = 3;

	Net_SetPorts(gamePort, discPort);
	InitNetworkManager();

	TEST_ASSERT(Net_StartHostLobby("HostCaveman"), "Host failed to bind lobby");
	TEST_ASSERT(gIsNetworkHost == true, "gIsNetworkHost should be true");
	TEST_ASSERT(gMyNetworkPlayerNum == 0, "Host player num should be 0");
	TEST_ASSERT(Net_GetConnectionState(NULL) == kNetConn_Hosting, "Conn state should be Hosting");

	/* Wait for Client discovery + join */
	uint32_t start = SDL_GetTicks();
	while (Net_GetConnectedPlayerCount() < 2 && (SDL_GetTicks() - start) < 6000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_GetConnectedPlayerCount() == 2, "Host should observe 2 connected players");

	const NetLobbyStateMsg *lobby = Net_GetLobbyState();
	TEST_ASSERT(strcmp(lobby->players[0].name, "HostCaveman") == 0, "Host name mismatch");
	TEST_ASSERT(strcmp(lobby->players[1].name, "ClientBrog") == 0, "Client name mismatch");

	/* Launch match and perform vehicle selection + level barrier */
	TEST_ASSERT(Net_HostLaunchMatch(), "Net_HostLaunchMatch failed");

	gPlayerInfo[0].sex = 1;
	gPlayerInfo[0].vehicleType = 2;
	gPlayerInfo[0].skin = 1;
	gPlayerInfo[0].team = 0;
	PlayerBroadcastVehicleType();

	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "Host vehicle selection aborted");
	TEST_ASSERT(gNumRealPlayers == 2, "gNumRealPlayers should be 2");
	TEST_ASSERT(gPlayerInfo[1].sex == 0, "Client sex mismatch on Host");
	TEST_ASSERT(gPlayerInfo[1].vehicleType == 3, "Client vehicleType mismatch on Host");
	TEST_ASSERT(gPlayerInfo[1].skin == 2, "Client skin mismatch on Host");
	TEST_ASSERT(gPlayerInfo[1].team == 1, "Client team mismatch on Host");
	TEST_ASSERT(gPlayerInfo[1].isComputer == false, "Client should be human before disconnect");

	gNumTotalPlayers = 2;
	HostWaitForPlayersToPrepareLevel();

	/* Simulate gameplay ticks: receive client input -> send authoritative snapshots & reliable events */
	ObjNode hostCar0;
	ObjNode hostCar1;
	memset(&hostCar0, 0, sizeof(hostCar0));
	memset(&hostCar1, 0, sizeof(hostCar1));
	hostCar0.Coord.x = 100.0f;
	hostCar0.Coord.y = 20.0f;
	hostCar0.Coord.z = -300.0f;
	hostCar1.Coord.x = 555.0f;
	hostCar1.Coord.y = 12.0f;
	hostCar1.Coord.z = 444.0f;

	gPlayerInfo[0].objNode = &hostCar0;
	gPlayerInfo[1].objNode = &hostCar1;
	gPlayerInfo[1].health = 0.85f;

	Boolean gotInput = false;
	uint32_t expectedHold = (1u << kControlBit_Forward);
	uint32_t expectedTap  = (1u << kControlBit_ThrowForward);
	start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < 5000)
	{
		HostReceive_ControlInfoFromClients();
		if ((gPlayerInfo[1].controlBits & expectedHold) &&
		    (gPlayerInfo[1].controlBits_New & expectedTap) &&
		    fabsf(gPlayerInfo[1].analogSteering.x - 0.75f) < 0.01f)
		{
			gotInput = true;
			break;
		}
		HostSend_ControlInfoToClients();
		SDL_Delay(10);
	}
	TEST_ASSERT(gotInput, "Host did not receive expected Client input bits/analog steering");

	/* Force an immediate snapshot + reliable POW and GameOver events */
	SDL_Delay(50);
	HostSend_ControlInfoToClients();
	Net_HostSendPOWPickup(7u, 1, POW_TYPE_NITRO, 5.0f);
	Net_HostSendGameOver(0);

	/* Keep pumping until Client disconnects cleanly -> verify AI takeover */
	start = SDL_GetTicks();
	while (!gPlayerInfo[1].isComputer && (SDL_GetTicks() - start) < 5000)
	{
		HostReceive_ControlInfoFromClients();
		HostSend_ControlInfoToClients();
		SDL_Delay(10);
	}
	TEST_ASSERT(gPlayerInfo[1].isComputer == true, "Host did not promote disconnected client to AI");

	ShutdownNetworkManager();
	return 0;
}

static int TestHostClientLoopback(void)
{
	uint16_t basePort = (uint16_t)(39000 + (getpid() % 500) * 2);
	uint16_t gamePort = basePort;
	uint16_t discPort = (uint16_t)(basePort + 1);

	pid_t pid = fork();
	TEST_ASSERT(pid >= 0, "fork() failed");
	if (pid == 0)
	{
		int rc = RunHostChild(gamePort, discPort);
		_exit(rc);
	}

	/* Parent acts as Client */
	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	memset(&gGamePrefs, 0, sizeof(gGamePrefs));
	gGameMode = GAME_MODE_PRACTICE;
	gTheAge = 0;
	gTrackNum = TRACK_NUM_DESERT;
	gMultiplayerCPUs = 0;
	gGameOver = false;
	gTrackCompleted = false;
	gNumLocalPlayers = 1;
	gLocalPlayerNums[0] = 0;
	gLocalPlayerNums[1] = 1;
	gLocalPlayerNums[2] = 2;
	gLocalPlayerNums[3] = 3;

	TerrainItemEntryType dummyItems[16];
	memset(dummyItems, 0, sizeof(dummyItems));
	TerrainItemEntryType *itemListPtr = dummyItems;
	gMasterItemList = &itemListPtr;

	Net_SetPorts(gamePort, discPort);
	InitNetworkManager();

	/* 1. LAN UDP discovery */
	TEST_ASSERT(Net_StartClientDiscovery(), "Net_StartClientDiscovery failed");
	NetDiscoveredHostInfo foundHost;
	memset(&foundHost, 0, sizeof(foundHost));
	int numFound = 0;
	uint32_t start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < 5000)
	{
		Net_PollDiscovery();
		numFound = Net_GetDiscoveredHosts(&foundHost, 1);
		if (numFound >= 1)
			break;
		SDL_Delay(20);
	}
	Net_StopClientDiscovery();
	TEST_ASSERT(numFound >= 1, "Client failed to discover Host over LAN/loopback UDP");
	TEST_ASSERT(foundHost.gamePort == gamePort, "Discovered gamePort mismatch");
	TEST_ASSERT(strcmp(foundHost.hostName, "HostCaveman") == 0, "Discovered hostName mismatch");
	TEST_ASSERT(foundHost.gameMode == GAME_MODE_MULTIPLAYERRACE, "Discovered gameMode mismatch");
	TEST_ASSERT(foundHost.trackNum == TRACK_NUM_CHINA, "Discovered trackNum mismatch");

	/* 2. Reject WAN direct connect before connecting to discovered LAN host */
	TEST_ASSERT(!Net_ConnectToHost("8.8.8.8", gamePort, "ClientBrog"),
	            "Direct connect to public IP must be blocked");

	/* 3. ENet handshake & Lobby state sync */
	TEST_ASSERT(Net_ConnectToHost(foundHost.ipString, foundHost.gamePort, "ClientBrog"),
	            "Net_ConnectToHost to discovered LAN IP failed");

	start = SDL_GetTicks();
	while (Net_GetConnectionState(NULL) == kNetConn_Connecting && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_GetConnectionState(NULL) == kNetConn_Connected, "Client failed to connect");
	TEST_ASSERT(gMyNetworkPlayerNum == 1, "Client assigned player num should be 1");

	/* 4. Wait for Host to launch match & verify lobby settings replicated */
	start = SDL_GetTicks();
	while (!Net_ClientHasMatchStarted() && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_ClientHasMatchStarted(), "Client did not receive StartMatch");
	TEST_ASSERT(gGameMode == GAME_MODE_MULTIPLAYERRACE, "Client gGameMode mismatch");
	TEST_ASSERT(gTrackNum == TRACK_NUM_CHINA, "Client gTrackNum mismatch");
	TEST_ASSERT(gGamePrefs.difficulty == 1, "Client difficulty mismatch");
	TEST_ASSERT(gMultiplayerCPUs == 1, "Client multiplayerCPUs mismatch");

	/* 5. Vehicle selection sync */
	gPlayerInfo[1].sex = 0;
	gPlayerInfo[1].vehicleType = 3;
	gPlayerInfo[1].skin = 2;
	gPlayerInfo[1].team = 1;
	PlayerBroadcastVehicleType();

	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "Client vehicle selection aborted");
	TEST_ASSERT(gPlayerInfo[0].sex == 1, "Host sex mismatch on Client");
	TEST_ASSERT(gPlayerInfo[0].vehicleType == 2, "Host vehicleType mismatch on Client");
	TEST_ASSERT(gPlayerInfo[0].skin == 1, "Host skin mismatch on Client");
	TEST_ASSERT(gPlayerInfo[0].team == 0, "Host team mismatch on Client");

	/* 6. Level load barrier */
	ClientTellHostLevelIsPrepared();

	/* 7. Gameplay input send + snapshot receive + visual interpolation + reliable events */
	ObjNode clientCar0;
	ObjNode clientCar1;
	memset(&clientCar0, 0, sizeof(clientCar0));
	memset(&clientCar1, 0, sizeof(clientCar1));
	gPlayerInfo[0].objNode = &clientCar0;
	gPlayerInfo[1].objNode = &clientCar1;

	gPlayerInfo[1].controlBits = (1u << kControlBit_Forward);
	gPlayerInfo[1].controlBits_New = (1u << kControlBit_ThrowForward);
	gPlayerInfo[1].analogSteering.x = 0.75f;
	ClientSend_ControlInfoToHost();

	start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < 5000)
	{
		ClientSend_ControlInfoToHost();
		ClientReceive_ControlInfoFromHost();
		Net_ClientUpdateVisualFrame();
		if (fabsf(gPlayerInfo[1].health - 0.85f) < 0.02f &&
		    sPowHideCalls >= 1 &&
		    sShowWinLoseCalls >= 1 &&
		    gTrackCompleted &&
		    clientCar1.Coord.x > 10.0f)
		{
			break;
		}
		SDL_Delay(15);
	}

	TEST_ASSERT(fabsf(gPlayerInfo[1].health - 0.85f) < 0.02f, "Client did not receive authoritative health snapshot");
	TEST_ASSERT(clientCar1.Coord.x > 10.0f, "Client visual interpolation did not move car1 toward Host coord");
	TEST_ASSERT(sPowHideCalls >= 1 && sLastPowTarget == &dummyItems[7] && fabsf(sLastPowTimer - 5.0f) < 0.01f,
	            "Client did not receive POW pickup reliable event");
	TEST_ASSERT(sShowWinLoseCalls >= 1 && sLastWinner == 0 && gTrackCompleted == true,
	            "Client did not receive GameOver reliable event");

	/* 8. Graceful disconnect so Host triggers AI takeover */
	Net_DisconnectGracefully();
	SDL_Delay(50);
	ShutdownNetworkManager();

	int status = 0;
	pid_t waited = waitpid(pid, &status, 0);
	TEST_ASSERT(waited == pid, "waitpid failed");
	TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Host child process failed assertions");
	return 0;
}

static int RunSplitHostChild(uint16_t gamePort, uint16_t discPort)
{
	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	memset(&gGamePrefs, 0, sizeof(gGamePrefs));
	gGameMode = GAME_MODE_MULTIPLAYERRACE;
	gTheAge = 2;
	gTrackNum = TRACK_NUM_EUROPE;
	gGamePrefs.difficulty = 2;
	gGamePrefs.tagDuration = 2;
	gMultiplayerCPUs = 0;
	gGameOver = false;
	gTrackCompleted = false;
	gNumLocalPlayers = 2;
	gLocalPlayerNums[0] = 0;
	gLocalPlayerNums[1] = 1;
	gLocalPlayerNums[2] = 2;
	gLocalPlayerNums[3] = 3;

	Net_SetPorts(gamePort, discPort);
	InitNetworkManager();

	TEST_ASSERT(Net_StartHostLobby("HostSquad"), "Split host failed to bind lobby");
	TEST_ASSERT(Net_GetConnectedPlayerCount() == 2, "Split host should start with 2 local seats");

	uint32_t start = SDL_GetTicks();
	while (Net_GetConnectedPlayerCount() < 4 && (SDL_GetTicks() - start) < 6000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_GetConnectedPlayerCount() == 4, "Split host should observe 4 total seats");

	const NetLobbyStateMsg *lobby = Net_GetLobbyState();
	TEST_ASSERT(strcmp(lobby->players[0].name, "HostSquad") == 0, "Split host P1 name mismatch");
	TEST_ASSERT(strcmp(lobby->players[1].name, "HostSquad P2") == 0, "Split host P2 name mismatch");
	TEST_ASSERT(strcmp(lobby->players[2].name, "ClientDuo") == 0, "Split client P1 name mismatch");
	TEST_ASSERT(strcmp(lobby->players[3].name, "ClientDuo P2") == 0, "Split client P2 name mismatch");
	TEST_ASSERT(lobby->players[0].machineId == lobby->players[1].machineId, "Host local seats must share machineId");
	TEST_ASSERT(lobby->players[2].machineId == lobby->players[3].machineId, "Client local seats must share machineId");
	TEST_ASSERT(lobby->players[0].machineId != lobby->players[2].machineId, "Host and Client machineIds must differ");

	TEST_ASSERT(Net_HostLaunchMatch(), "Split host Net_HostLaunchMatch failed");

	/* Host local seats 0 and 1 */
	gPlayerInfo[0].sex = 0;
	gPlayerInfo[0].vehicleType = 1;
	gPlayerInfo[0].skin = 0;
	gPlayerInfo[0].team = 0;
	gPlayerInfo[1].sex = 1;
	gPlayerInfo[1].vehicleType = 4;
	gPlayerInfo[1].skin = 2;
	gPlayerInfo[1].team = 0;
	PlayerBroadcastVehicleType();

	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "Split host vehicle selection aborted");
	TEST_ASSERT(gNumRealPlayers == 4, "Split host gNumRealPlayers should be 4");
	TEST_ASSERT(gPlayerInfo[2].vehicleType == 2 && gPlayerInfo[2].skin == 1 && gPlayerInfo[2].team == 1,
	            "Client seat 2 vehicle mismatch on Host");
	TEST_ASSERT(gPlayerInfo[3].vehicleType == 5 && gPlayerInfo[3].skin == 3 && gPlayerInfo[3].team == 1,
	            "Client seat 3 vehicle mismatch on Host");
	TEST_ASSERT(!gPlayerInfo[2].isComputer && !gPlayerInfo[3].isComputer, "Client seats should be human");

	gNumTotalPlayers = 4;
	HostWaitForPlayersToPrepareLevel();

	ObjNode cars[4];
	memset(cars, 0, sizeof(cars));
	for (int i = 0; i < 4; i++)
	{
		gPlayerInfo[i].objNode = &cars[i];
		cars[i].Coord.x = 100.0f * (float)(i + 1);
	}
	gPlayerInfo[2].health = 0.65f;
	gPlayerInfo[3].health = 0.40f;

	Boolean gotBothInputs = false;
	uint32_t hold2 = (1u << kControlBit_Forward);
	uint32_t hold3 = (1u << kControlBit_Brakes);
	start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < 5000)
	{
		HostReceive_ControlInfoFromClients();
		if ((gPlayerInfo[2].controlBits & hold2) &&
		    fabsf(gPlayerInfo[2].analogSteering.x - (-0.5f)) < 0.01f &&
		    (gPlayerInfo[3].controlBits & hold3) &&
		    fabsf(gPlayerInfo[3].analogSteering.x - 0.8f) < 0.01f)
		{
			gotBothInputs = true;
			break;
		}
		HostSend_ControlInfoToClients();
		SDL_Delay(10);
	}
	TEST_ASSERT(gotBothInputs, "Host did not receive independent inputs for both split client seats");
	TEST_ASSERT(gPlayerInfo[0].controlBits == 0 && gPlayerInfo[1].controlBits == 0,
	            "Client inputs must not clobber Host local seats 0/1");

	SDL_Delay(50);
	HostSend_ControlInfoToClients();
	Net_HostSendGameOver(0);

	start = SDL_GetTicks();
	while ((!gPlayerInfo[2].isComputer || !gPlayerInfo[3].isComputer) && (SDL_GetTicks() - start) < 5000)
	{
		HostReceive_ControlInfoFromClients();
		HostSend_ControlInfoToClients();
		SDL_Delay(10);
	}
	TEST_ASSERT(gPlayerInfo[2].isComputer && gPlayerInfo[3].isComputer,
	            "Host did not promote both departed split client seats to AI");
	TEST_ASSERT(!gPlayerInfo[0].isComputer && !gPlayerInfo[1].isComputer,
	            "Host local seats must remain human after client disconnect");

	ShutdownNetworkManager();
	return 0;
}

static int TestSplitScreenLANLoopback(void)
{
	uint16_t basePort = (uint16_t)(39002 + (getpid() % 500) * 4);
	uint16_t gamePort = basePort;
	uint16_t discPort = (uint16_t)(basePort + 1);

	pid_t pid = fork();
	TEST_ASSERT(pid >= 0, "fork() failed for split-screen test");
	if (pid == 0)
	{
		int rc = RunSplitHostChild(gamePort, discPort);
		_exit(rc);
	}

	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	memset(&gGamePrefs, 0, sizeof(gGamePrefs));
	gGameMode = GAME_MODE_PRACTICE;
	gTheAge = 0;
	gTrackNum = TRACK_NUM_DESERT;
	gMultiplayerCPUs = 0;
	gGameOver = false;
	gTrackCompleted = false;
	gNumLocalPlayers = 2;
	gLocalPlayerNums[0] = 0;
	gLocalPlayerNums[1] = 1;
	gLocalPlayerNums[2] = 2;
	gLocalPlayerNums[3] = 3;
	sPowHideCalls = 0;
	sShowWinLoseCalls = 0;
	sShowFinalPlaceCalls = 0;
	sLastWinner = -1;

	Net_SetPorts(gamePort, discPort);
	InitNetworkManager();

	TEST_ASSERT(Net_StartClientDiscovery(), "Split client Net_StartClientDiscovery failed");
	NetDiscoveredHostInfo foundHost;
	memset(&foundHost, 0, sizeof(foundHost));
	int numFound = 0;
	uint32_t start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < 5000)
	{
		Net_PollDiscovery();
		numFound = Net_GetDiscoveredHosts(&foundHost, 1);
		if (numFound >= 1)
			break;
		SDL_Delay(20);
	}
	Net_StopClientDiscovery();
	TEST_ASSERT(numFound >= 1, "Split client failed to discover split host");
	TEST_ASSERT(foundHost.curPlayers == 2 && foundHost.maxPlayers == MAX_PLAYERS,
	            "Split host discovery curPlayers/maxPlayers mismatch");

	TEST_ASSERT(Net_ConnectToHost(foundHost.ipString, foundHost.gamePort, "ClientDuo"),
	            "Split client Net_ConnectToHost failed");

	start = SDL_GetTicks();
	while (Net_GetConnectionState(NULL) == kNetConn_Connecting && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_GetConnectionState(NULL) == kNetConn_Connected, "Split client failed to connect");
	TEST_ASSERT(gNumLocalPlayers == 2, "Split client gNumLocalPlayers should stay 2");
	TEST_ASSERT(gLocalPlayerNums[0] == 2 && gLocalPlayerNums[1] == 3,
	            "Split client should be assigned contiguous slots [2, 3]");
	TEST_ASSERT(gMyNetworkPlayerNum == 2, "Split client primary gMyNetworkPlayerNum should be 2");

	/* Match InitPlayerInfo_Game mapping on split client */
	for (short pane = 0; pane < gNumLocalPlayers; pane++)
	{
		short slot = gLocalPlayerNums[pane];
		gPlayerInfo[slot].onThisMachine = true;
		gPlayerInfo[slot].splitPaneNum = pane;
	}

	start = SDL_GetTicks();
	while (!Net_ClientHasMatchStarted() && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_ClientHasMatchStarted(), "Split client did not receive StartMatch");

	gPlayerInfo[2].sex = 0;
	gPlayerInfo[2].vehicleType = 2;
	gPlayerInfo[2].skin = 1;
	gPlayerInfo[2].team = 1;
	gPlayerInfo[3].sex = 1;
	gPlayerInfo[3].vehicleType = 5;
	gPlayerInfo[3].skin = 3;
	gPlayerInfo[3].team = 1;
	PlayerBroadcastVehicleType();

	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "Split client vehicle selection aborted");
	TEST_ASSERT(gPlayerInfo[0].vehicleType == 1 && gPlayerInfo[1].vehicleType == 4,
	            "Split client did not receive both Host vehicles");

	ClientTellHostLevelIsPrepared();

	ObjNode cars[4];
	memset(cars, 0, sizeof(cars));
	for (int i = 0; i < 4; i++)
		gPlayerInfo[i].objNode = &cars[i];

	gPlayerInfo[2].controlBits = (1u << kControlBit_Forward);
	gPlayerInfo[2].analogSteering.x = -0.5f;
	gPlayerInfo[3].controlBits = (1u << kControlBit_Brakes);
	gPlayerInfo[3].analogSteering.x = 0.8f;

	start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < 5000)
	{
		ClientSend_ControlInfoToHost();
		ClientReceive_ControlInfoFromHost();
		Net_ClientUpdateVisualFrame();
		if (fabsf(gPlayerInfo[2].health - 0.65f) < 0.02f &&
		    fabsf(gPlayerInfo[3].health - 0.40f) < 0.02f &&
		    sShowWinLoseCalls >= 2 &&
		    gTrackCompleted &&
		    cars[2].Coord.x > 10.0f &&
		    cars[3].Coord.x > 10.0f)
		{
			break;
		}
		SDL_Delay(15);
	}

	TEST_ASSERT(fabsf(gPlayerInfo[2].health - 0.65f) < 0.02f &&
	            fabsf(gPlayerInfo[3].health - 0.40f) < 0.02f,
	            "Split client did not receive snapshots for both local seats");
	TEST_ASSERT(cars[2].Coord.x > 10.0f && cars[3].Coord.x > 10.0f,
	            "Split client visual interpolation did not update both local cars");
	TEST_ASSERT(sShowWinLoseCalls >= 2 && sLastWinner == 0 && gTrackCompleted,
	            "Split client did not trigger ShowWinLose for each local pane");

	Net_DisconnectGracefully();
	SDL_Delay(50);
	ShutdownNetworkManager();

	int status = 0;
	pid_t waited = waitpid(pid, &status, 0);
	TEST_ASSERT(waited == pid, "waitpid failed for split-screen test");
	TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Split host child failed assertions");
	return 0;
}

static int TestHostDiscoveryNoSelfLoop(void)
{
	/* Verify periodic host discovery beacons on loopback/broadcast do not echo back into PollHostDiscoverySocket */
	uint16_t gamePort = 29620 + (uint16_t)(getpid() % 100);
	uint16_t discPort = gamePort + 1;
	Net_SetPorts(gamePort, discPort);
	InitNetworkManager();

	TEST_ASSERT(Net_StartHostLobby("BeaconHost"), "Host failed to bind for self-loop test");

	uint32_t maxCallMs = 0;
	for (int i = 0; i < 40; i++)
	{
		uint32_t t0 = (uint32_t)SDL_GetTicks();
		Net_PollLobby();
		uint32_t dt = (uint32_t)SDL_GetTicks() - t0;
		if (dt > maxCallMs)
			maxCallMs = dt;
		SDL_Delay(20);
	}
	TEST_ASSERT(maxCallMs < 100, "Net_PollLobby blocked or spun during discovery beacon interval");

	ShutdownNetworkManager();
	return 0;
}

static int RunPersistHostChild(uint16_t gamePort, uint16_t discPort)
{
	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	memset(&gGamePrefs, 0, sizeof(gGamePrefs));
	gGameMode = GAME_MODE_MULTIPLAYERRACE;
	gTheAge = 2;
	gTrackNum = TRACK_NUM_EUROPE;
	gGamePrefs.difficulty = 2;
	gGamePrefs.tagDuration = 2;
	gMultiplayerCPUs = 0;
	gGameOver = false;
	gTrackCompleted = false;
	gNumLocalPlayers = 2;
	gLocalPlayerNums[0] = 0;
	gLocalPlayerNums[1] = 1;
	gLocalPlayerNums[2] = 2;
	gLocalPlayerNums[3] = 3;

	Net_SetPorts(gamePort, discPort);
	InitNetworkManager();

	TEST_ASSERT(Net_StartHostLobby("PersistHost"), "Persist host failed to bind lobby");
	TEST_ASSERT(!Net_HasRemoteClients(), "Persist host should start with no remote clients");

	uint32_t start = SDL_GetTicks();
	while (Net_GetConnectedPlayerCount() < 4 && (SDL_GetTicks() - start) < 6000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_GetConnectedPlayerCount() == 4, "Persist host expected 4 players (2 local + 2 remote)");
	TEST_ASSERT(Net_HasRemoteClients(), "Persist host must report remote clients before match 1");

	/* Launch Match 1: Multiplayer Race */
	TEST_ASSERT(Net_HostLaunchMatch(), "Persist host failed to launch match 1");

	gPlayerInfo[0].sex = 0;
	gPlayerInfo[0].vehicleType = 1;
	gPlayerInfo[0].skin = 0;
	gPlayerInfo[0].team = 0;
	gPlayerInfo[1].sex = 1;
	gPlayerInfo[1].vehicleType = 2;
	gPlayerInfo[1].skin = 1;
	gPlayerInfo[1].team = 0;
	PlayerBroadcastVehicleType();
	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "Persist host match 1 vehicle selection aborted");
	gNumTotalPlayers = gNumRealPlayers;
	HostWaitForPlayersToPrepareLevel();

	ObjNode cars[4];
	memset(cars, 0, sizeof(cars));
	for (int i = 0; i < 4; i++)
	{
		gPlayerInfo[i].objNode = &cars[i];
		gPlayerInfo[i].isComputer = false;
		gPlayerInfo[i].raceComplete = false;
	}

	/* ClientA (slot 2) finishes early while Host & ClientB have not finished yet */
	gPlayerInfo[2].raceComplete = true;
	gPlayerInfo[2].place = 1;
	gTrackCompleted = false;
	Net_HostSendLapOrFinish(2);

	/* Keep pumping until ClientB (slot 3) disconnects mid-race */
	start = SDL_GetTicks();
	while (!gPlayerInfo[3].isComputer && (SDL_GetTicks() - start) < 6000)
	{
		HostReceive_ControlInfoFromClients();
		HostSend_ControlInfoToClients();
		SDL_Delay(10);
	}
	TEST_ASSERT(gPlayerInfo[3].isComputer, "Departed ClientB (slot 3) was not promoted to AI");
	TEST_ASSERT(!gPlayerInfo[2].isComputer, "Remaining ClientA (slot 2) must stay human after ClientB leaves");
	TEST_ASSERT(Net_HasRemoteClients(), "Host must still have remote ClientA after ClientB leaves");

	/* Remaining humans (Host P1 & P2) finish, completing Match 1 */
	gPlayerInfo[0].raceComplete = true;
	gPlayerInfo[1].raceComplete = true;
	gTrackCompleted = true;
	Net_HostSendLapOrFinish(-1);

	start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < 200)
	{
		HostReceive_ControlInfoFromClients();
		HostSend_ControlInfoToClients();
		SDL_Delay(10);
	}

	/* Return all still-connected players to persistent lobby */
	gTrackCompleted = false;
	TEST_ASSERT(Net_HasRemoteClients(), "Host must still have ClientA when returning to lobby");
	Net_HostReturnToLobby();
	TEST_ASSERT(Net_GetConnectedPlayerCount() == 3, "Lobby must compact to 3 players (Host 2P + ClientA)");
	TEST_ASSERT(Net_HasRemoteClients(), "Host must still report ClientA in lobby");

	/* Give ClientA a moment to return to lobby, then change mode & launch Match 2 */
	start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < 250)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}

	gGameMode = GAME_MODE_SURVIVAL;
	gTheAge = 0;
	gTrackNum = TRACK_NUM_EUROPE;
	gTrackCompleted = false;
	TEST_ASSERT(Net_HostLaunchMatch(), "Persist host failed to launch match 2");

	gPlayerInfo[0].vehicleType = 1;
	gPlayerInfo[1].vehicleType = 2;
	PlayerBroadcastVehicleType();
	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "Persist host match 2 vehicle selection aborted");
	gNumTotalPlayers = gNumRealPlayers;
	HostWaitForPlayersToPrepareLevel();

	/* Verify Survival elimination broadcast reaches ClientA */
	Net_HostSendElimination(2);

	/* Pump until final client disconnects cleanly on its own */
	start = SDL_GetTicks();
	while (Net_HasRemoteClients() && (SDL_GetTicks() - start) < 6000)
	{
		HostReceive_ControlInfoFromClients();
		HostSend_ControlInfoToClients();
		SDL_Delay(10);
	}
	TEST_ASSERT(!Net_HasRemoteClients(), "Host must see Net_HasRemoteClients() == false once all clients leave");

	ShutdownNetworkManager();
	return 0;
}

static int RunPersistClientBChild(uint16_t gamePort, uint16_t discPort, int readyFd)
{
	char go = 0;
	if (read(readyFd, &go, 1) != 1)
	{
		close(readyFd);
		return 1;
	}
	close(readyFd);

	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	memset(&gGamePrefs, 0, sizeof(gGamePrefs));
	gGameMode = GAME_MODE_PRACTICE;
	gTheAge = 0;
	gTrackNum = TRACK_NUM_DESERT;
	gMultiplayerCPUs = 0;
	gGameOver = false;
	gTrackCompleted = false;
	gNumLocalPlayers = 1;
	gLocalPlayerNums[0] = 0;
	sShowWinLoseCalls = 0;
	sShowFinalPlaceCalls = 0;

	Net_SetPorts(gamePort, discPort);
	InitNetworkManager();

	TEST_ASSERT(Net_ConnectToHost("127.0.0.1", gamePort, "ClientB"), "ClientB Net_ConnectToHost failed");
	uint32_t start = SDL_GetTicks();
	while (Net_GetConnectionState(NULL) == kNetConn_Connecting && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_GetConnectionState(NULL) == kNetConn_Connected, "ClientB failed to connect");
	TEST_ASSERT(gMyNetworkPlayerNum == 3, "ClientB expected slot 3");

	gPlayerInfo[3].onThisMachine = true;
	gPlayerInfo[3].splitPaneNum = 0;

	start = SDL_GetTicks();
	while (!Net_ClientHasMatchStarted() && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_ClientHasMatchStarted(), "ClientB missed Match 1 start");

	gPlayerInfo[3].vehicleType = 4;
	PlayerBroadcastVehicleType();
	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "ClientB vehicle selection failed");
	ClientTellHostLevelIsPrepared();

	ObjNode cars[4];
	memset(cars, 0, sizeof(cars));
	for (int i = 0; i < 4; i++)
		gPlayerInfo[i].objNode = &cars[i];

	/* Wait until ClientA finishes early (and verify race did NOT prematurely end) */
	start = SDL_GetTicks();
	while (!gPlayerInfo[2].raceComplete && (SDL_GetTicks() - start) < 5000)
	{
		ClientReceive_ControlInfoFromHost();
		ClientSend_ControlInfoToHost();
		SDL_Delay(10);
	}
	TEST_ASSERT(gPlayerInfo[2].raceComplete, "ClientB did not observe ClientA partial finish");
	TEST_ASSERT(!gTrackCompleted, "Race must not end on ClientB when only ClientA finished");

	/* Disconnect mid-race */
	Net_DisconnectGracefully();
	SDL_Delay(50);
	ShutdownNetworkManager();
	return 0;
}

static int TestPersistentLobbyAndMultiFinishLoopback(void)
{
	uint16_t basePort = (uint16_t)(41000 + (getpid() % 500) * 4);
	uint16_t gamePort = basePort;
	uint16_t discPort = (uint16_t)(basePort + 1);

	/* Fork all child processes BEFORE InitNetworkManager() so ENet sockets are isolated */
	int syncPipe[2];
	TEST_ASSERT(pipe(syncPipe) == 0, "pipe() failed");

	pid_t hostPid = fork();
	TEST_ASSERT(hostPid >= 0, "fork() failed for persistent lobby host");
	if (hostPid == 0)
	{
		close(syncPipe[0]);
		close(syncPipe[1]);
		_exit(RunPersistHostChild(gamePort, discPort));
	}

	pid_t clientBPid = fork();
	TEST_ASSERT(clientBPid >= 0, "fork() failed for persistent lobby ClientB");
	if (clientBPid == 0)
	{
		close(syncPipe[1]);
		_exit(RunPersistClientBChild(gamePort, discPort, syncPipe[0]));
	}
	close(syncPipe[0]);

	SDL_Delay(40);

	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	memset(&gGamePrefs, 0, sizeof(gGamePrefs));
	gGameMode = GAME_MODE_PRACTICE;
	gTheAge = 0;
	gTrackNum = TRACK_NUM_DESERT;
	gMultiplayerCPUs = 0;
	gGameOver = false;
	gTrackCompleted = false;
	gNumLocalPlayers = 1;
	gLocalPlayerNums[0] = 0;
	sShowWinLoseCalls = 0;
	sShowFinalPlaceCalls = 0;
	sLastWinner = -1;

	Net_SetPorts(gamePort, discPort);
	InitNetworkManager();

	/* Connect ClientA first so slot 2 is deterministic, then unblock ClientB for slot 3 */
	TEST_ASSERT(Net_ConnectToHost("127.0.0.1", gamePort, "ClientA"), "ClientA Net_ConnectToHost failed");
	uint32_t start = SDL_GetTicks();
	while (Net_GetConnectionState(NULL) == kNetConn_Connecting && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_GetConnectionState(NULL) == kNetConn_Connected, "ClientA failed to connect");
	TEST_ASSERT(gMyNetworkPlayerNum == 2, "ClientA expected slot 2");

	char go = 1;
	TEST_ASSERT(write(syncPipe[1], &go, 1) == 1, "write to syncPipe failed");
	close(syncPipe[1]);

	gPlayerInfo[2].onThisMachine = true;
	gPlayerInfo[2].splitPaneNum = 0;

	start = SDL_GetTicks();
	while (!Net_ClientHasMatchStarted() && (SDL_GetTicks() - start) < 6000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_ClientHasMatchStarted() && gGameMode == GAME_MODE_MULTIPLAYERRACE,
	            "ClientA did not receive Match 1 StartMatch");

	gPlayerInfo[2].vehicleType = 3;
	PlayerBroadcastVehicleType();
	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "ClientA match 1 vehicle selection failed");
	ClientTellHostLevelIsPrepared();

	ObjNode cars[4];
	memset(cars, 0, sizeof(cars));
	for (int i = 0; i < 4; i++)
		gPlayerInfo[i].objNode = &cars[i];

	/* 1. ClientA finishes early: raceComplete becomes true, ShowFinalPlace called, gTrackCompleted STILL false */
	start = SDL_GetTicks();
	while (!gPlayerInfo[2].raceComplete && (SDL_GetTicks() - start) < 5000)
	{
		ClientReceive_ControlInfoFromHost();
		ClientSend_ControlInfoToHost();
		SDL_Delay(10);
	}
	TEST_ASSERT(gPlayerInfo[2].raceComplete, "ClientA did not observe own early finish");
	TEST_ASSERT(sShowFinalPlaceCalls >= 1, "ClientA did not trigger ShowFinalPlace on early finish");
	TEST_ASSERT(!gTrackCompleted, "Match 1 must NOT end when only ClientA finished");

	/* 2. ClientB disconnects mid-race, Host & P2 finish -> gTrackCompleted becomes true, ClientA stays connected */
	start = SDL_GetTicks();
	while (!gTrackCompleted && (SDL_GetTicks() - start) < 6000)
	{
		ClientReceive_ControlInfoFromHost();
		ClientSend_ControlInfoToHost();
		SDL_Delay(10);
	}
	TEST_ASSERT(gTrackCompleted, "ClientA did not receive final Match 1 completion");
	TEST_ASSERT(gPlayerInfo[3].isComputer, "ClientA did not observe departed ClientB promoted to AI");
	TEST_ASSERT(Net_GetConnectionState(NULL) == kNetConn_Connected,
	            "ClientA must stay connected after ClientB disconnects");

	/* 3. Persistent return to lobby across matches */
	gTrackCompleted = false;
	Net_ClientPrepareReturnToLobby();

	start = SDL_GetTicks();
	while ((Net_ClientHasMatchStarted() || Net_GetConnectedPlayerCount() != 3) && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(!Net_ClientHasMatchStarted(), "ClientA did not return to lobby state");
	TEST_ASSERT(Net_GetConnectedPlayerCount() == 3, "ClientA lobby player count should compact to 3");
	TEST_ASSERT(gMyNetworkPlayerNum == 2 && gLocalPlayerNums[0] == 2,
	            "ClientA slot must stay compact slot 2 after ClientB (slot 3) left");

	/* 4. Wait for Match 2 (Survival) on the same persistent ENet connection */
	start = SDL_GetTicks();
	while ((!Net_ClientHasMatchStarted() || gGameMode != GAME_MODE_SURVIVAL) && (SDL_GetTicks() - start) < 6000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_ClientHasMatchStarted() && gGameMode == GAME_MODE_SURVIVAL,
	            "ClientA did not receive Match 2 StartMatch with Survival mode");

	sShowWinLoseCalls = 0;
	gPlayerInfo[2].vehicleType = 5;
	PlayerBroadcastVehicleType();
	TEST_ASSERT(!GetVehicleSelectionFromNetPlayers(), "ClientA match 2 vehicle selection failed");
	ClientTellHostLevelIsPrepared();

	for (int i = 0; i < 4; i++)
		gPlayerInfo[i].objNode = &cars[i];

	start = SDL_GetTicks();
	while (sShowWinLoseCalls == 0 && (SDL_GetTicks() - start) < 5000)
	{
		ClientReceive_ControlInfoFromHost();
		ClientSend_ControlInfoToHost();
		SDL_Delay(10);
	}
	TEST_ASSERT(sShowWinLoseCalls >= 1, "ClientA did not receive Survival elimination broadcast");

	/* 5. ClientA leaves on its own accord */
	Net_DisconnectGracefully();
	SDL_Delay(50);
	ShutdownNetworkManager();

	int statusB = 0;
	TEST_ASSERT(waitpid(clientBPid, &statusB, 0) == clientBPid, "waitpid failed for ClientB");
	TEST_ASSERT(WIFEXITED(statusB) && WEXITSTATUS(statusB) == 0, "ClientB child failed assertions");

	int statusH = 0;
	TEST_ASSERT(waitpid(hostPid, &statusH, 0) == hostPid, "waitpid failed for Persist Host");
	TEST_ASSERT(WIFEXITED(statusH) && WEXITSTATUS(statusH) == 0, "Persist Host child failed assertions");
	return 0;
}

static int TestRefinementsLoopback(void)
{
	uint16_t basePort = (uint16_t)(42500 + (getpid() % 400) * 4);
	const uint16_t testPort = basePort;
	const uint16_t discPort = (uint16_t)(basePort + 1);

	pid_t hostPid = fork();
	TEST_ASSERT(hostPid >= 0, "fork failed for refinement host");
	if (hostPid == 0)
	{
		memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
		memset(&gGamePrefs, 0, sizeof(gGamePrefs));
		gGameOver = false;
		gTrackCompleted = false;
		gGameMode = GAME_MODE_MULTIPLAYERRACE;
		gTheAge = 0;
		gTrackNum = TRACK_NUM_DESERT;
		gMultiplayerCPUs = 0;
		gNumLocalPlayers = 1;
		gLocalPlayerNums[0] = 0;
		gPlayerInfo[0].onThisMachine = true;

		Net_SetPorts(testPort, discPort);
		InitNetworkManager();
		if (!Net_StartHostLobby("RefHost"))
			_exit(3);

		uint32_t start = SDL_GetTicks();
		while (Net_GetConnectedPlayerCount() < 2 && (SDL_GetTicks() - start) < 5000)
		{
			Net_PollLobby();
			SDL_Delay(10);
		}
		if (Net_GetConnectedPlayerCount() < 2)
			_exit(4);

		if (!Net_HostLaunchMatch())
			_exit(5);

		/* Client picks vehicle first, host delays slightly to test wait status on client */
		start = SDL_GetTicks();
		while ((SDL_GetTicks() - start) < 300)
		{
			Net_PollLobby();
			SDL_Delay(10);
		}
		gPlayerInfo[0].vehicleType = 0;
		PlayerBroadcastVehicleType();
		if (GetVehicleSelectionFromNetPlayers())
			_exit(6);

		gNumTotalPlayers = 2;
		HostWaitForPlayersToPrepareLevel();

		ObjNode cars[2];
		memset(cars, 0, sizeof(cars));
		ObjNode head0;
		SkeletonObjDataType headSkel0;
		memset(&head0, 0, sizeof(head0));
		memset(&headSkel0, 0, sizeof(headSkel0));
		headSkel0.AnimNum = PLAYER_ANIM_THROWFORWARD;
		head0.Skeleton = &headSkel0;

		gNumTotalPlayers = 2;
		gPlayerInfo[0].objNode = &cars[0];
		gPlayerInfo[0].headObj = &head0;
		gPlayerInfo[1].objNode = &cars[1];
		gPlayerInfo[1].isComputer = false;
		gPlayerInfo[1].lapNum = 1;

		/* Fill every projectile slot with oil patches to verify static hazard eviction lets new weapons replicate */
		ObjNode oilPool[NET_MAX_PROJECTILES];
		memset(oilPool, 0, sizeof(oilPool));
		for (int o = 0; o < NET_MAX_PROJECTILES; o++)
		{
			oilPool[o].Group = MODEL_GROUP_WEAPONS;
			oilPool[o].Type = WEAPONS_ObjType_OilPatch;
			Net_RegisterWeaponObj(&oilPool[o], 0);
		}

		ObjNode proj;
		memset(&proj, 0, sizeof(proj));
		proj.Group = MODEL_GROUP_WEAPONS;
		proj.Type = WEAPONS_ObjType_RomanCandleBullet;
		proj.Scale.x = proj.Scale.y = proj.Scale.z = 2.0f;
		proj.StatusBits = STATUS_BIT_AIMATCAMERA | STATUS_BIT_KEEPBACKFACES | STATUS_BIT_GLOW |
		                  STATUS_BIT_NOLIGHTING | STATUS_BIT_NOTEXTUREWRAP;
		proj.Coord.x = 123.0f;
		proj.Coord.y = 45.0f;
		proj.Coord.z = 678.0f;
		Net_RegisterWeaponObj(&proj, 0);

		HostSend_ControlInfoToClients();

		/* Sound3D with effectId >= 128 + custom rate, AnnouncerSound, Snow & RomanCandle explosions */
		Net_HostSendSound3D(140, 10.0f, 20.0f, 30.0f, NORMAL_CHANNEL_RATE + 0x2000u, 3.5f);
		Net_HostSendAnnouncerSound(1, 77, true, 0.25f);
		Net_HostSendAnnouncerSound(0, 88, true, 0.25f); /* Host only: client must ignore */
		Net_HostSendExplosion(10.0f, 20.0f, 30.0f, kNetExplosion_Snow);
		Net_HostSendExplosion(11.0f, 22.0f, 33.0f, kNetExplosion_RomanCandle);

		/* Pump until client closes or 3s elapse */
		start = SDL_GetTicks();
		while (Net_HasRemoteClients() && (SDL_GetTicks() - start) < 3000)
		{
			HostReceive_ControlInfoFromClients();
			HostSend_ControlInfoToClients();
			SDL_Delay(10);
		}

		ShutdownNetworkManager();
		_exit(0);
	}

	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	memset(&gGamePrefs, 0, sizeof(gGamePrefs));
	Net_ResetGameSyncState();
	gGameOver = false;
	gTrackCompleted = false;
	gGameMode = GAME_MODE_PRACTICE;
	gNumLocalPlayers = 1;
	gLocalPlayerNums[0] = 0;

	Net_SetPorts(testPort, discPort);
	InitNetworkManager();
	TEST_ASSERT(Net_ConnectToHost("127.0.0.1", testPort, "RefClient"), "Client connect failed");

	uint32_t start = SDL_GetTicks();
	while (Net_GetConnectionState(NULL) == kNetConn_Connecting && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_GetConnectionState(NULL) == kNetConn_Connected, "Client failed to connect");

	start = SDL_GetTicks();
	while (!Net_ClientHasMatchStarted() && (SDL_GetTicks() - start) < 5000)
	{
		Net_PollLobby();
		SDL_Delay(10);
	}
	TEST_ASSERT(Net_ClientHasMatchStarted(), "Client never saw MatchStarted");

	gPlayerInfo[1].onThisMachine = true;
	gPlayerInfo[1].isComputer = false;
	gPlayerInfo[1].vehicleType = 1;
	PlayerBroadcastVehicleType();
	/* Should initially wait on host's selection, then become ready */
	int firstCheck = Net_CheckVehicleSelectionsReady();
	TEST_ASSERT(firstCheck == 0, "Expected client to wait on host vehicle selection");
	start = SDL_GetTicks();
	while (Net_CheckVehicleSelectionsReady() == 0 && (SDL_GetTicks() - start) < 5000)
		SDL_Delay(10);
	TEST_ASSERT(Net_CheckVehicleSelectionsReady() == 1, "Vehicle selections did not finish");

	sSound3DCalls = 0;
	sLastSound3DEffect = -1;
	sLastSound3DRate = 0;
	sLastSound3DVolume = 0.0f;
	sAnnouncerCalls = 0;
	sLastAnnouncerEffect = -1;
	sSnowExplosionCalls = 0;
	sWeaponExplosionCalls = 0;
	sLastWeaponExplosionKind = -1;
	sTrailCalls = 0;
	sShowLapNumCalls = 0;
	sMorphAnimCalls = 0;
	sLastMorphAnim = -1;
	sTestObjPoolNext = 0;

	ClientTellHostLevelIsPrepared();

	gIsInGame = true;
	ObjNode cars[2];
	memset(cars, 0, sizeof(cars));
	ObjNode clientHead0;
	SkeletonObjDataType clientHeadSkel0;
	memset(&clientHead0, 0, sizeof(clientHead0));
	memset(&clientHeadSkel0, 0, sizeof(clientHeadSkel0));
	clientHeadSkel0.AnimNum = PLAYER_ANIM_SIT;
	clientHead0.Skeleton = &clientHeadSkel0;

	gPlayerInfo[0].objNode = &cars[0];
	gPlayerInfo[0].headObj = &clientHead0;
	gPlayerInfo[1].objNode = &cars[1];
	gPlayerInfo[1].onThisMachine = true;
	gPlayerInfo[1].isComputer = false;
	gPlayerInfo[1].lapNum = 0;

	start = SDL_GetTicks();
	while ((sWeaponExplosionCalls == 0 || sAnnouncerCalls == 0 || sSound3DCalls == 0 ||
	        sTestObjPoolNext == 0 || sTrailCalls == 0 || sShowLapNumCalls == 0 || sMorphAnimCalls == 0) &&
	       (SDL_GetTicks() - start) < 4000)
	{
		ClientReceive_ControlInfoFromHost();
		Net_ClientUpdateVisualFrame();
		SDL_Delay(10);
	}

	gIsInGame = false;
	TEST_ASSERT(NET_PROTOCOL_VERSION == 5 && NET_MAX_PROJECTILES == 64,
	            "Expected updated protocol v5 and projectile cap 64");
	TEST_ASSERT(sSound3DCalls >= 1 && sLastSound3DEffect == 140 &&
	            sLastSound3DRate == (NORMAL_CHANNEL_RATE + 0x2000u) &&
	            fabsf(sLastSound3DVolume - 3.5f) < 0.01f,
	            "Sound3D high effectId / rateMultiplier / volume mismatch on client");
	TEST_ASSERT(sAnnouncerCalls == 1 && sLastAnnouncerEffect == 77 && sLastAnnouncerOverride == true &&
	            fabsf(sLastAnnouncerDelay - 0.25f) < 0.01f,
	            "Targeted AnnouncerSound mismatch or leaked wrong-player call on client");
	TEST_ASSERT(sSnowExplosionCalls >= 1, "Client did not trigger MakeSnowExplosion");
	TEST_ASSERT(sWeaponExplosionCalls >= 1 && sLastWeaponExplosionKind == kNetExplosion_RomanCandle,
	            "Client did not trigger PlayWeaponExplosionFX(RomanCandle)");

	Boolean foundRomanCandle = false;
	for (int k = 0; k < sTestObjPoolNext && k < 128; k++)
	{
		if (sTestObjPool[k].Type == WEAPONS_ObjType_RomanCandleBullet &&
		    (sTestObjPool[k].StatusBits & (STATUS_BIT_AIMATCAMERA | STATUS_BIT_GLOW)) ==
		    (STATUS_BIT_AIMATCAMERA | STATUS_BIT_GLOW))
		{
			foundRomanCandle = true;
			break;
		}
	}
	TEST_ASSERT(foundRomanCandle,
	            "Client did not receive RomanCandleBullet after oil-patch eviction with preserved statusBits");
	TEST_ASSERT(sTrailCalls >= 1, "Net_ClientUpdateVisualFrame did not update projectile trails");
	TEST_ASSERT(sShowLapNumCalls == 1, "Client did not announce lap progression on ShowLapNum");
	TEST_ASSERT(sMorphAnimCalls >= 1 && sLastMorphAnim == PLAYER_ANIM_THROWFORWARD,
	            "Client did not replicate throw animation from host snapshot flags");

	Net_DisconnectGracefully();
	SDL_Delay(50);
	ShutdownNetworkManager();
	int statusH = 0;
	TEST_ASSERT(waitpid(hostPid, &statusH, 0) == hostPid, "waitpid failed for Refinement Host");
	TEST_ASSERT(WIFEXITED(statusH) && WEXITSTATUS(statusH) == 0, "Refinement Host exited non-zero");
	return 0;
}

int main(void)
{
	if (TestLANValidation() != 0)
		return 1;
	printf("PASS: TestLANValidation\n");

	if (TestHostDiscoveryNoSelfLoop() != 0)
		return 1;
	printf("PASS: TestHostDiscoveryNoSelfLoop\n");

	if (TestHostClientLoopback() != 0)
		return 1;
	printf("PASS: TestHostClientLoopback\n");

	if (TestSplitScreenLANLoopback() != 0)
		return 1;
	printf("PASS: TestSplitScreenLANLoopback\n");

	if (TestPersistentLobbyAndMultiFinishLoopback() != 0)
		return 1;
	printf("PASS: TestPersistentLobbyAndMultiFinishLoopback\n");

	if (TestRefinementsLoopback() != 0)
		return 1;
	printf("PASS: TestRefinementsLoopback\n");
	return 0;
}
