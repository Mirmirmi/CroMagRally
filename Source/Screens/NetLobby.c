/****************************/
/*       NETLOBBY.C         */
/* (c)2026 LAN Multiplayer  */
/****************************/

#include "game.h"
#include "menu.h"
#include "network.h"
#include "miscscreens.h"

/**********************/
/*   STATIC STATE     */
/**********************/

#define MAX_UI_DISCOVERED_ROWS      4
#define DIRECT_IP_FALLBACK_DELAY_MS 30000ULL

static char                   sDirectIP[32] = "127.0.0.1";
static char                   sDynamicTextBuf[8][96];
static NetDiscoveredHostInfo  sUIHosts[NET_MAX_DISCOVERED_HOSTS];
static int                    sUINumHosts = 0;
static int                    sPickedHostIdx = -1;
static Uint64                 sJoinSearchStartMs = 0;
static Boolean                sShowDirectIPFallback = false;

static int                    sLastRenderedPlayers = -1;
static int                    sLastRenderedHosts = -1;
static char                   sLastStatusSnapshot[128] = "";

/**********************/
/*  SHARED HELPERS    */
/**********************/

static const char* GetGameModeLabel(uint8_t mode)
{
	switch (mode)
	{
		case GAME_MODE_MULTIPLAYERRACE: return Localize(STR_RACE);
		case GAME_MODE_TAG1:            return Localize(STR_TAG1);
		case GAME_MODE_TAG2:            return Localize(STR_TAG2);
		case GAME_MODE_SURVIVAL:        return Localize(STR_SURVIVAL);
		case GAME_MODE_CAPTUREFLAG:     return Localize(STR_CAPTUREFLAG);
		default:                        return Localize(STR_NET_GAME);
	}
}

static void SetupNetLobbyView(void)
{
	OGLSetupInputType viewDef;
	OGL_NewViewDef(&viewDef);
	viewDef.view.pillarboxRatio = PILLARBOX_RATIO_4_3;
	viewDef.view.fontName = "rockfont";
	OGL_SetupGameView(&viewDef);

	MakeScrollingBackgroundPattern();
	MakeFadeEvent(true);
}

static void CleanupNetLobbyView(void)
{
	DeleteAllObjects();
	OGL_DisposeGameView();
}

static const char* GetPlayerRowText(const MenuItem* mi)
{
	int slot = mi->id;
	const NetLobbyStateMsg* st = Net_GetLobbyState();
	if (slot < 0 || slot >= MAX_PLAYERS || !st->players[slot].active)
		return "";

	uint16_t myMachine = Net_GetMyMachineId();
	const char* badge = "";
	if (myMachine != 0 && st->players[slot].machineId == myMachine)
		badge = " (YOU)";

	SDL_snprintf(sDynamicTextBuf[slot], sizeof(sDynamicTextBuf[slot]),
	             "%d. %s%s",
	             slot + 1,
	             st->players[slot].name[0] ? st->players[slot].name : "PLAYER",
	             badge);
	return sDynamicTextBuf[slot];
}

static int IsPlayerRowHidden(const MenuItem* mi)
{
	int slot = mi->id;
	const NetLobbyStateMsg* st = Net_GetLobbyState();
	return (slot < 0 || slot >= MAX_PLAYERS || !st->players[slot].active) ? kMILayoutFlagHidden : 0;
}

/**********************/
/*    HOST LOBBY      */
/**********************/

static const char* GetHostStatusText(const MenuItem* mi)
{
	(void)mi;
	const char* status = NULL;
	Net_GetConnectionState(&status);
	SDL_snprintf(sDynamicTextBuf[6], sizeof(sDynamicTextBuf[6]), "%s", status ? status : "");
	return sDynamicTextBuf[6];
}

static const char* GetHostSummaryText(const MenuItem* mi)
{
	(void)mi;
	const NetLobbyStateMsg* st = Net_GetLobbyState();
	SDL_snprintf(sDynamicTextBuf[7], sizeof(sDynamicTextBuf[7]),
	             "%s - %s",
	             GetGameModeLabel(st->gameMode),
	             Localize(STR_LEVEL_1 + st->trackNum));
	return sDynamicTextBuf[7];
}

static void OnHostSettingChanged(const MenuItem* mi)
{
	(void)mi;
	Net_SyncHostLobbySettings();
}

static const MenuItem gHostLobbyMenuTree[] =
{
	{ .id='hlob' },
	{ .type=kMILabel, .text=STR_LAN_LOBBY },
	{kMILabel, STR_NULL, .getDynamicText=GetHostStatusText, .customHeight=0.6f },
	{kMILabel, STR_NULL, .getDynamicText=GetHostSummaryText, .customHeight=0.6f },
	{kMISpacer, .customHeight=0.5f },
	{kMILabel, STR_NULL, .id=0, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=1, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=2, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=3, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=4, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=5, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMISpacer, .customHeight=0.5f },
	{
		kMICycler1, STR_CPU_PLAYERS, .callback=OnHostSettingChanged, .cycler=
		{
			.valuePtr=&gMultiplayerCPUs, .choices=
			{
				{STR_OFF, false},
				{STR_ON,  true},
			}
		}
	},
	{
		kMICycler1, STR_DIFFICULTY, .callback=OnHostSettingChanged, .cycler=
		{
			.valuePtr=&gGamePrefs.difficulty, .choices=
			{
				{STR_DIFFICULTY_1, DIFFICULTY_SIMPLISTIC},
				{STR_DIFFICULTY_2, DIFFICULTY_EASY},
				{STR_DIFFICULTY_3, DIFFICULTY_MEDIUM},
				{STR_DIFFICULTY_4, DIFFICULTY_HARD},
			}
		}
	},
	{kMISpacer, .customHeight=0.45f },
	{kMILabel, STR_WAIT_FOR_PLAYERS_OR_START, .customHeight=0.55f },
	{kMISpacer, .customHeight=0.25f },
	{kMIPick, STR_START_MATCH, .id=1, .next='EXIT' },
	{kMIPick, STR_CANCEL,      .id=0, .next='EXIT' },
	{ .type=kMISENTINEL }
};

static void UpdateHostLobbyMenu(void)
{
	MoveObjects();
	Net_PollLobby();

	int count = Net_GetConnectedPlayerCount();
	if (count != sLastRenderedPlayers)
	{
		sLastRenderedPlayers = count;
		LayoutCurrentMenuAgain();
	}
}

Boolean SetupNetworkHosting(void)
{
	if (!Net_StartHostLobby(gGamePrefs.playerName))
	{
		const char* status = NULL;
		Net_GetConnectionState(&status);
		DoAlert("Could not host LAN game: %s", status ? status : "Error");
		ShutdownNetworkManager();
		return true;
	}

	sLastRenderedPlayers = Net_GetConnectedPlayerCount();

	SetupNetLobbyView();

	MenuStyle style = kDefaultMenuStyle;
	style.canBackOutOfRootMenu = true;
	style.rowHeight = 36;

	int pick = StartMenu(gHostLobbyMenuTree, &style, UpdateHostLobbyMenu, DrawObjects);

	CleanupNetLobbyView();

	if (pick != 1)
	{
		ShutdownNetworkManager();
		return true;
	}

	Net_HostLaunchMatch();
	return false;
}

/**********************/
/*    JOIN BROWSER    */
/**********************/

static const char* GetDiscoveredHostText(const MenuItem* mi)
{
	int idx = mi->id - 100;
	if (idx < 0 || idx >= sUINumHosts)
		return "";

	SDL_snprintf(sDynamicTextBuf[idx], sizeof(sDynamicTextBuf[idx]),
	             "%s (%s) %d/%d",
	             sUIHosts[idx].hostName[0] ? sUIHosts[idx].hostName : "HOST",
	             sUIHosts[idx].ipString,
	             (int)sUIHosts[idx].curPlayers,
	             (int)sUIHosts[idx].maxPlayers);
	return sDynamicTextBuf[idx];
}

static int IsDiscoveredHostHidden(const MenuItem* mi)
{
	int idx = mi->id - 100;
	return (idx >= 0 && idx < sUINumHosts) ? 0 : kMILayoutFlagHidden;
}

static const char* GetBrowserPromptText(const MenuItem* mi)
{
	(void)mi;
	if (sUINumHosts == 0)
		return Localize(STR_SEARCHING_LAN);
	return "";
}

static void OnPickDiscoveredHost(const MenuItem* mi)
{
	sPickedHostIdx = mi->id - 100;
}

static int IsDirectIPFallbackHidden(const MenuItem* mi)
{
	(void)mi;
	return sShowDirectIPFallback ? 0 : (kMILayoutFlagHidden | kMILayoutFlagDisabled);
}

static const MenuItem gJoinBrowserMenuTree[] =
{
	{ .id='join' },
	{ .type=kMILabel, .text=STR_JOIN_LAN_GAME },
	{kMILabel, STR_NULL, .getDynamicText=GetBrowserPromptText, .customHeight=0.6f },
	{kMISpacer, .customHeight=0.4f },
	{kMIPick, STR_NULL, .id=100, .next='EXIT', .callback=OnPickDiscoveredHost, .getDynamicText=GetDiscoveredHostText, .getLayoutFlags=IsDiscoveredHostHidden },
	{kMIPick, STR_NULL, .id=101, .next='EXIT', .callback=OnPickDiscoveredHost, .getDynamicText=GetDiscoveredHostText, .getLayoutFlags=IsDiscoveredHostHidden },
	{kMIPick, STR_NULL, .id=102, .next='EXIT', .callback=OnPickDiscoveredHost, .getDynamicText=GetDiscoveredHostText, .getLayoutFlags=IsDiscoveredHostHidden },
	{kMIPick, STR_NULL, .id=103, .next='EXIT', .callback=OnPickDiscoveredHost, .getDynamicText=GetDiscoveredHostText, .getLayoutFlags=IsDiscoveredHostHidden },
	{kMISpacer, .customHeight=0.5f, .getLayoutFlags=IsDirectIPFallbackHidden },
	{kMILabel, STR_TRY_DIRECT_CONNECTION, .customHeight=0.55f, .getLayoutFlags=IsDirectIPFallbackHidden },
	{kMITextInput, STR_DIRECT_IP, .textInput={ sDirectIP, (int)sizeof(sDirectIP), true }, .customHeight=0.75f, .getLayoutFlags=IsDirectIPFallbackHidden },
	{kMIPick, STR_CONNECT_TO_IP, .id=2, .next='EXIT', .getLayoutFlags=IsDirectIPFallbackHidden },
	{kMIPick, STR_CANCEL,        .id=0, .next='EXIT' },
	{ .type=kMISENTINEL }
};

static void UpdateJoinBrowserMenu(void)
{
	MoveObjects();
	Net_PollDiscovery();

	int newCount = Net_GetDiscoveredHosts(sUIHosts, NET_MAX_DISCOVERED_HOSTS);
	if (newCount > MAX_UI_DISCOVERED_ROWS)
		newCount = MAX_UI_DISCOVERED_ROWS;

	Uint64 nowMs = SDL_GetTicks();
	Boolean needRelayout = false;

	if (newCount > 0)
	{
		sJoinSearchStartMs = nowMs;
	}
	else if (!sShowDirectIPFallback && (nowMs - sJoinSearchStartMs) >= DIRECT_IP_FALLBACK_DELAY_MS)
	{
		sShowDirectIPFallback = true;
		needRelayout = true;
	}

	if (newCount != sLastRenderedHosts)
	{
		sUINumHosts = newCount;
		sLastRenderedHosts = newCount;
		needRelayout = true;
	}
	else
	{
		sUINumHosts = newCount;
	}

	if (needRelayout)
		LayoutCurrentMenuAgain();
}

/**********************/
/*  CLIENT WAIT ROOM  */
/**********************/

static const char* GetClientStatusText(const MenuItem* mi)
{
	(void)mi;
	const char* status = NULL;
	NetLobbyConnectionState cs = Net_GetConnectionState(&status);
	if (cs == kNetConn_Connected)
	{
		const NetLobbyStateMsg* st = Net_GetLobbyState();
		SDL_snprintf(sDynamicTextBuf[6], sizeof(sDynamicTextBuf[6]),
		             "%s - %s",
		             GetGameModeLabel(st->gameMode),
		             Localize(STR_LEVEL_1 + st->trackNum));
	}
	else
	{
		SDL_snprintf(sDynamicTextBuf[6], sizeof(sDynamicTextBuf[6]), "%s", status ? status : "");
	}
	return sDynamicTextBuf[6];
}

static const MenuItem gClientWaitMenuTree[] =
{
	{ .id='wait' },
	{ .type=kMILabel, .text=STR_WAITING_FOR_HOST },
	{kMILabel, STR_NULL, .getDynamicText=GetClientStatusText, .customHeight=0.6f },
	{kMISpacer, .customHeight=0.5f },
	{kMILabel, STR_NULL, .id=0, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=1, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=2, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=3, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=4, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMILabel, STR_NULL, .id=5, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.55f },
	{kMISpacer, .customHeight=0.5f },
	{kMIPick, STR_LEAVE_LOBBY, .id=0, .next='EXIT' },
	{ .type=kMISENTINEL }
};

static void UpdateClientWaitMenu(void)
{
	MoveObjects();
	Net_PollLobby();

	if (Net_ClientHasMatchStarted())
	{
		KillMenu(1);
		return;
	}

	const char* status = NULL;
	NetLobbyConnectionState cs = Net_GetConnectionState(&status);
	if (cs == kNetConn_Failed || cs == kNetConn_Disconnected)
	{
		KillMenu(2);
		return;
	}

	int count = Net_GetConnectedPlayerCount();
	const char* s = status ? status : "";
	if (count != sLastRenderedPlayers || SDL_strcmp(s, sLastStatusSnapshot) != 0)
	{
		sLastRenderedPlayers = count;
		SDL_strlcpy(sLastStatusSnapshot, s, sizeof(sLastStatusSnapshot));
		LayoutCurrentMenuAgain();
	}
}

Boolean SetupNetworkJoin(void)
{
	sJoinSearchStartMs = SDL_GetTicks();
	sShowDirectIPFallback = false;

	while (true)
	{
		sUINumHosts = 0;
		sPickedHostIdx = -1;
		sLastRenderedHosts = 0;
		if (!sShowDirectIPFallback)
			sJoinSearchStartMs = SDL_GetTicks();

		Net_StartClientDiscovery();
		SetupNetLobbyView();

		MenuStyle style = kDefaultMenuStyle;
		style.canBackOutOfRootMenu = true;
		style.rowHeight = 36;

		int pick = StartMenu(gJoinBrowserMenuTree, &style, UpdateJoinBrowserMenu, DrawObjects);

		CleanupNetLobbyView();
		Net_StopClientDiscovery();

		if (pick <= 0)
		{
			ShutdownNetworkManager();
			return true;
		}

		char targetIP[32];
		uint16_t targetPort = NET_DEFAULT_PORT;

		if (pick >= 100 && sPickedHostIdx >= 0 && sPickedHostIdx < sUINumHosts)
		{
			SDL_strlcpy(targetIP, sUIHosts[sPickedHostIdx].ipString, sizeof(targetIP));
			targetPort = sUIHosts[sPickedHostIdx].gamePort;
		}
		else
		{
			SDL_strlcpy(targetIP, sDirectIP, sizeof(targetIP));
		}

		if (!Net_ConnectToHost(targetIP, targetPort, gGamePrefs.playerName))
		{
			const char* status = NULL;
			Net_GetConnectionState(&status);
			DoAlert("Cannot join: %s", status ? status : "Invalid LAN IP");
			ShutdownNetworkManager();
			continue;
		}

		sLastRenderedPlayers = -1;
		sLastStatusSnapshot[0] = '\0';

		SetupNetLobbyView();
		int waitPick = StartMenu(gClientWaitMenuTree, &style, UpdateClientWaitMenu, DrawObjects);
		CleanupNetLobbyView();

		if (waitPick == 1)
		{
			return false; /* Host launched match! */
		}

		if (waitPick == 2)
		{
			const char* status = NULL;
			Net_GetConnectionState(&status);
			DoAlert("Connection ended: %s", status ? status : "Disconnected");
		}

		ShutdownNetworkManager();
		/* Return to LAN browser */
	}
}
