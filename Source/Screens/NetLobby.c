/****************************/
/*       NETLOBBY.C         */
/* (c)2026 LAN Multiplayer  */
/****************************/

#include "game.h"
#include "menu.h"
#include "network.h"
#include "miscscreens.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, Web_ReadInitialRoomCode, (char* outBuf, int maxLen), {
	let code = '';
	if (typeof window !== 'undefined') {
		if (window.__croMagInitialRoomCode) {
			code = String(window.__croMagInitialRoomCode);
			window.__croMagInitialRoomCode = '';
		} else {
			try {
				const raw = new URLSearchParams(window.location.search || '').get('room') || '';
				const norm = raw.trim().toUpperCase().replace(/[^A-Z0-9]/g, '');
				if (norm.length === 4) code = norm;
			} catch (e) {}
		}
	}
	if (code && outBuf && maxLen > 0) {
		stringToUTF8(code, outBuf, maxLen);
	}
});

EM_JS(void, Web_CopyInviteLink, (const char* codePtr), {
	const code = UTF8ToString(codePtr || 0).trim().toUpperCase();
	if (!code || typeof window === 'undefined') return;
	const url = window.location.origin + window.location.pathname + '?room=' + encodeURIComponent(code);
	const notify = (msg) => {
		if (typeof window.__croMagShowAlert === 'function') {
			window.__croMagShowAlert(msg);
		}
	};
	if (typeof navigator !== 'undefined' && navigator.clipboard &&
	    typeof navigator.clipboard.writeText === 'function') {
		navigator.clipboard.writeText(url).then(function () {
			notify('Invite link copied: ' + url);
		}).catch(function () {
			notify('Invite link: ' + url);
		});
	} else {
		notify('Invite link: ' + url);
	}
});
#endif

/**********************/
/*   STATIC STATE     */
/**********************/

#define MAX_UI_DISCOVERED_ROWS      4
#define DIRECT_IP_FALLBACK_DELAY_MS 2500ULL

static char                   sRoomCode[32] = "";
static char                   sDynamicTextBuf[9][96];
static NetDiscoveredHostInfo  sUIHosts[NET_MAX_DISCOVERED_HOSTS];
static int                    sUINumHosts = 0;
static int                    sPickedHostIdx = -1;
static Uint64                 sJoinSearchStartMs = 0;
static Boolean                sShowDirectIPFallback = false;

static int                    sLastRenderedPlayers = -1;
static int                    sLastRenderedHosts = -1;
static uint8_t                sLastRenderedMode = 255;
static uint8_t                sLastRenderedTrack = 255;
static char                   sLastStatusSnapshot[128] = "";
static char                   sLastHostStatusSnapshot[128] = "";
static char                   sLastHostRoomCode[8] = "";
static Byte                   sHostLobbyMode = GAME_MODE_MULTIPLAYERRACE;
static Boolean                sExitWhenNoClients = false;

/**********************/
/*  SHARED HELPERS    */
/**********************/

static const char* GetTrackLabel(uint8_t mode, uint8_t trackNum)
{
	if (IsBattleGameMode(mode))
	{
		int idx = (int)trackNum - NUM_RACE_TRACKS;
		if (idx < 0)
			idx = 0;
		if (idx >= (NUM_TRACKS - NUM_RACE_TRACKS))
			idx = (NUM_TRACKS - NUM_RACE_TRACKS) - 1;
		return Localize(STR_MPLEVEL_1 + idx);
	}
	int idx = (int)trackNum;
	if (idx < 0 || idx >= NUM_RACE_TRACKS)
		idx = 0;
	return Localize(STR_LEVEL_1 + idx);
}

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

static const char* GetHostRoomCodeText(const MenuItem* mi)
{
	(void)mi;
	char code[8] = "";
	if (Net_GetHostRoomCode(code, sizeof(code)) && code[0] != '\0')
	{
		SDL_snprintf(sDynamicTextBuf[8], sizeof(sDynamicTextBuf[8]), "ROOM CODE: %s", code);
	}
	else
	{
		const char* status = NULL;
		Net_GetConnectionState(&status);
		if (status && (SDL_strstr(status, "OFFLINE") != NULL || SDL_strstr(status, "LAN ONLY") != NULL))
			SDL_snprintf(sDynamicTextBuf[8], sizeof(sDynamicTextBuf[8]), "ROOM CODE: OFFLINE");
		else
			SDL_snprintf(sDynamicTextBuf[8], sizeof(sDynamicTextBuf[8]), "ROOM CODE: CONNECTING...");
	}
	return sDynamicTextBuf[8];
}

static int GetHostRoomCodeFlags(const MenuItem* mi)
{
	(void)mi;
	return kMILayoutFlagHighlight;
}

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
	             GetTrackLabel(st->gameMode, st->trackNum));
	return sDynamicTextBuf[7];
}

static void OnHostModeChanged(const MenuItem* mi)
{
	(void)mi;
	int oldMode = gGameMode;
	gGameMode = (int)sHostLobbyMode;
	if (IsBattleGameMode(oldMode) != IsBattleGameMode(gGameMode))
	{
		gTrackNum = IsBattleGameMode(gGameMode) ? TRACK_NUM_STONEHENGE : TRACK_NUM_DESERT;
	}
	Net_SyncHostLobbySettings();
	LayoutCurrentMenuAgain();
}

static void OnHostSettingChanged(const MenuItem* mi)
{
	(void)mi;
	Net_SyncHostLobbySettings();
}

static const char* GetCopyInviteLinkText(const MenuItem* mi)
{
	(void)mi;
	return "COPY INVITE LINK";
}

static int GetCopyInviteLinkFlags(const MenuItem* mi)
{
	(void)mi;
#ifndef __EMSCRIPTEN__
	return kMILayoutFlagHidden | kMILayoutFlagDisabled;
#else
	char code[8] = "";
	if (Net_GetHostRoomCode(code, sizeof(code)) && code[0] != '\0')
		return 0;
	return kMILayoutFlagDisabled;
#endif
}

static void OnPickCopyInviteLink(const MenuItem* mi)
{
	(void)mi;
#ifdef __EMSCRIPTEN__
	char code[8] = "";
	if (Net_GetHostRoomCode(code, sizeof(code)) && code[0] != '\0')
	{
		Web_CopyInviteLink(code);
	}
#endif
}

static const MenuItem gHostLobbyMenuTree[] =
{
	{ .id='hlob' },
#ifdef __EMSCRIPTEN__
	{ .type=kMILabel, .text=STR_HOST_NET_GAME, .customHeight=0.95f },
#else
	{ .type=kMILabel, .text=STR_LAN_LOBBY, .customHeight=0.95f },
#endif
	{kMILabel, STR_NULL, .getDynamicText=GetHostRoomCodeText, .getLayoutFlags=GetHostRoomCodeFlags, .customHeight=0.85f },
	{kMILabel, STR_NULL, .getDynamicText=GetHostStatusText, .customHeight=0.50f },
	{kMILabel, STR_NULL, .getDynamicText=GetHostSummaryText, .customHeight=0.50f },
	{kMISpacer, .customHeight=0.30f },
	{kMILabel, STR_NULL, .id=0, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.50f },
	{kMILabel, STR_NULL, .id=1, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.50f },
	{kMILabel, STR_NULL, .id=2, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.50f },
	{kMILabel, STR_NULL, .id=3, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.50f },
	{kMILabel, STR_NULL, .id=4, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.50f },
	{kMILabel, STR_NULL, .id=5, .getDynamicText=GetPlayerRowText, .getLayoutFlags=IsPlayerRowHidden, .customHeight=0.50f },
	{kMISpacer, .customHeight=0.30f },
	{
		kMICycler1, STR_GAME_MODE, .callback=OnHostModeChanged, .customHeight=0.85f, .cycler=
		{
			.valuePtr=&sHostLobbyMode, .choices=
			{
				{STR_RACE,        GAME_MODE_MULTIPLAYERRACE},
				{STR_TAG1,        GAME_MODE_TAG1},
				{STR_TAG2,        GAME_MODE_TAG2},
				{STR_SURVIVAL,    GAME_MODE_SURVIVAL},
				{STR_CAPTUREFLAG, GAME_MODE_CAPTUREFLAG},
			}
		}
	},
	{
		kMICycler1, STR_CPU_PLAYERS, .callback=OnHostSettingChanged, .customHeight=0.85f, .cycler=
		{
			.valuePtr=&gMultiplayerCPUs, .choices=
			{
				{STR_OFF, false},
				{STR_ON,  true},
			}
		}
	},
	{
		kMICycler1, STR_DIFFICULTY, .callback=OnHostSettingChanged, .customHeight=0.85f, .cycler=
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
	{kMISpacer, .customHeight=0.25f },
	{kMILabel, STR_WAIT_FOR_PLAYERS_OR_START, .customHeight=0.50f },
	{kMISpacer, .customHeight=0.20f },
	{kMIPick, STR_NULL, .id=3, .next=0, .callback=OnPickCopyInviteLink, .getDynamicText=GetCopyInviteLinkText, .getLayoutFlags=GetCopyInviteLinkFlags, .customHeight=0.80f },
	{kMIPick, STR_CHANGE_TRACK, .id=2, .next='EXIT', .customHeight=0.85f },
	{kMIPick, STR_START_MATCH,  .id=1, .next='EXIT', .customHeight=0.85f },
	{kMIPick, STR_CANCEL,       .id=0, .next='EXIT', .customHeight=0.85f },
	{ .type=kMISENTINEL }
};

static void UpdateHostLobbyMenu(void)
{
	MoveObjects();
	Net_PollLobby();

	if (sExitWhenNoClients && !Net_HasRemoteClients())
	{
		KillMenu(0);
		return;
	}

	const char* status = NULL;
	Net_GetConnectionState(&status);
	const char* s = status ? status : "";
	char curRoom[8] = "";
	Net_GetHostRoomCode(curRoom, sizeof(curRoom));
	int count = Net_GetConnectedPlayerCount();
	if (count != sLastRenderedPlayers ||
	    SDL_strcmp(s, sLastHostStatusSnapshot) != 0 ||
	    SDL_strcmp(curRoom, sLastHostRoomCode) != 0)
	{
		sLastRenderedPlayers = count;
		SDL_strlcpy(sLastHostStatusSnapshot, s, sizeof(sLastHostStatusSnapshot));
		SDL_strlcpy(sLastHostRoomCode, curRoom, sizeof(sLastHostRoomCode));
		LayoutCurrentMenuAgain();
	}
}

static Boolean RunHostLobbyMenu(Boolean exitWhenNoClients)
{
	sExitWhenNoClients = exitWhenNoClients;

	while (true)
	{
		if (sExitWhenNoClients && !Net_HasRemoteClients())
		{
			ShutdownNetworkManager();
			return true;
		}

		sHostLobbyMode = (Byte)gGameMode;
		sLastRenderedPlayers = Net_GetConnectedPlayerCount();
		const char* initStatus = NULL;
		Net_GetConnectionState(&initStatus);
		SDL_strlcpy(sLastHostStatusSnapshot, initStatus ? initStatus : "", sizeof(sLastHostStatusSnapshot));
		sLastHostRoomCode[0] = '\0';
		Net_GetHostRoomCode(sLastHostRoomCode, sizeof(sLastHostRoomCode));

		SetupNetLobbyView();

		MenuStyle style = kDefaultMenuStyle;
		style.canBackOutOfRootMenu = true;
		style.rowHeight = 36;
		style.labelColor = (OGLColorRGBA){0.74f, 0.74f, 0.78f, 1.0f};

		int pick = StartMenu(gHostLobbyMenuTree, &style, UpdateHostLobbyMenu, DrawObjects);

		CleanupNetLobbyView();

		if (pick == 2)
		{
			int prevTrack = gTrackNum;
			if (SelectSingleTrack())
				gTrackNum = prevTrack;
			Net_SyncHostLobbySettings();
			Net_PollLobby();
			continue;
		}

		if (pick != 1)
		{
			Net_DisconnectGracefully();
			ShutdownNetworkManager();
			return true;
		}

		Net_HostLaunchMatch();
		return false;
	}
}

Boolean SetupNetworkHosting(void)
{
	if (!Net_StartHostLobby(gGamePrefs.playerName))
	{
		const char* status = NULL;
		Net_GetConnectionState(&status);
		DoAlert("Could not host net game: %s", status ? status : "Error");
		ShutdownNetworkManager();
		return true;
	}

	return RunHostLobbyMenu(false);
}

Boolean ReenterHostLobby(void)
{
	if (!Net_HasRemoteClients())
	{
		ShutdownNetworkManager();
		return true;
	}

	Net_HostReturnToLobby();
	return RunHostLobbyMenu(true);
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
#ifdef __EMSCRIPTEN__
	(void)mi;
	return kMILayoutFlagHidden | kMILayoutFlagDisabled;
#else
	int idx = mi->id - 100;
	return (idx >= 0 && idx < sUINumHosts) ? 0 : kMILayoutFlagHidden;
#endif
}

static int IsSearchingPromptHidden(const MenuItem* mi)
{
	(void)mi;
#ifdef __EMSCRIPTEN__
	return kMILayoutFlagHidden;
#else
	return (sUINumHosts > 0) ? kMILayoutFlagHidden : 0;
#endif
}

static int IsManualRoomCodeTriggerHidden(const MenuItem* mi)
{
	(void)mi;
#ifdef __EMSCRIPTEN__
	return kMILayoutFlagHidden | kMILayoutFlagDisabled;
#else
	return sShowDirectIPFallback ? (kMILayoutFlagHidden | kMILayoutFlagDisabled) : 0;
#endif
}

static void OnPickShowRoomCode(const MenuItem* mi)
{
	(void)mi;
	sShowDirectIPFallback = true;
	LayoutCurrentMenuAgain();
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

static int IsEnterRoomLabelHidden(const MenuItem* mi)
{
#ifdef __EMSCRIPTEN__
	(void)mi;
	return kMILayoutFlagHidden | kMILayoutFlagDisabled;
#else
	return IsDirectIPFallbackHidden(mi);
#endif
}

static int IsJoinTopSpacerHidden(const MenuItem* mi)
{
	(void)mi;
#ifdef __EMSCRIPTEN__
	return kMILayoutFlagHidden;
#else
	return 0;
#endif
}

static const MenuItem gJoinBrowserMenuTree[] =
{
	{ .id='join' },
#ifdef __EMSCRIPTEN__
	{ .type=kMILabel, .text=STR_JOIN_NET_GAME },
#else
	{ .type=kMILabel, .text=STR_JOIN_LAN_GAME },
#endif
	{kMILabel, STR_NULL, .getDynamicText=GetBrowserPromptText, .customHeight=0.6f, .getLayoutFlags=IsSearchingPromptHidden },
	{kMISpacer, .customHeight=0.4f, .getLayoutFlags=IsJoinTopSpacerHidden },
	{kMIPick, STR_NULL, .id=100, .next='EXIT', .callback=OnPickDiscoveredHost, .getDynamicText=GetDiscoveredHostText, .getLayoutFlags=IsDiscoveredHostHidden },
	{kMIPick, STR_NULL, .id=101, .next='EXIT', .callback=OnPickDiscoveredHost, .getDynamicText=GetDiscoveredHostText, .getLayoutFlags=IsDiscoveredHostHidden },
	{kMIPick, STR_NULL, .id=102, .next='EXIT', .callback=OnPickDiscoveredHost, .getDynamicText=GetDiscoveredHostText, .getLayoutFlags=IsDiscoveredHostHidden },
	{kMIPick, STR_NULL, .id=103, .next='EXIT', .callback=OnPickDiscoveredHost, .getDynamicText=GetDiscoveredHostText, .getLayoutFlags=IsDiscoveredHostHidden },
	{kMIPick, STR_ENTER_ROOM_CODE, .id=3, .next=0, .callback=OnPickShowRoomCode, .customHeight=0.7f, .getLayoutFlags=IsManualRoomCodeTriggerHidden },
	{kMISpacer, .customHeight=0.5f, .getLayoutFlags=IsDirectIPFallbackHidden },
	{kMILabel, STR_ENTER_ROOM_CODE, .customHeight=0.55f, .getLayoutFlags=IsEnterRoomLabelHidden },
	{kMITextInput, STR_ROOM_CODE, .textInput={ sRoomCode, (int)sizeof(sRoomCode), false }, .customHeight=0.85f, .getLayoutFlags=IsDirectIPFallbackHidden },
	{kMISpacer, .customHeight=0.45f, .getLayoutFlags=IsDirectIPFallbackHidden },
	{kMIPick, STR_JOIN_ROOM, .id=2, .next='EXIT', .getLayoutFlags=IsDirectIPFallbackHidden },
	{kMIPick, STR_CANCEL,    .id=0, .next='EXIT' },
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

	if (needRelayout && !IsMenuEditingText())
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
		             GetTrackLabel(st->gameMode, st->trackNum));
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

	const NetLobbyStateMsg* st = Net_GetLobbyState();
	int count = Net_GetConnectedPlayerCount();
	const char* s = status ? status : "";
	if (count != sLastRenderedPlayers ||
	    st->gameMode != sLastRenderedMode ||
	    st->trackNum != sLastRenderedTrack ||
	    SDL_strcmp(s, sLastStatusSnapshot) != 0)
	{
		sLastRenderedPlayers = count;
		sLastRenderedMode = st->gameMode;
		sLastRenderedTrack = st->trackNum;
		SDL_strlcpy(sLastStatusSnapshot, s, sizeof(sLastStatusSnapshot));
		LayoutCurrentMenuAgain();
	}
}

static int RunClientWaitRoom(void)
{
	MenuStyle style = kDefaultMenuStyle;
	style.canBackOutOfRootMenu = true;
	style.rowHeight = 40;
	style.labelColor = (OGLColorRGBA){0.74f, 0.74f, 0.78f, 1.0f};

	sLastRenderedPlayers = -1;
	sLastRenderedMode = 255;
	sLastRenderedTrack = 255;
	sLastStatusSnapshot[0] = '\0';

	SetupNetLobbyView();
	int waitPick = StartMenu(gClientWaitMenuTree, &style, UpdateClientWaitMenu, DrawObjects);
	if (waitPick == 2)
	{
		/* Render the lobby backdrop before showing the OS alert so the window is not blank */
		OGL_DrawScene(DrawObjects);
		const char* status = NULL;
		Net_GetConnectionState(&status);
		DoAlert("Connection ended: %s", status ? status : "Disconnected");
	}
	CleanupNetLobbyView();
	return waitPick;
}

Boolean SetupNetworkJoin(void)
{
	sJoinSearchStartMs = SDL_GetTicks();
#ifdef __EMSCRIPTEN__
	sShowDirectIPFallback = true;
	{
		char initialRoom[32] = "";
		Web_ReadInitialRoomCode(initialRoom, sizeof(initialRoom));
		if (initialRoom[0] != '\0')
		{
			SDL_strlcpy(sRoomCode, initialRoom, sizeof(sRoomCode));
		}
	}
#else
	sShowDirectIPFallback = false;
#endif

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
		style.rowHeight = 44;
		style.labelColor = (OGLColorRGBA){0.74f, 0.74f, 0.78f, 1.0f};

		int pick = StartMenu(gJoinBrowserMenuTree, &style, UpdateJoinBrowserMenu, DrawObjects);

		CleanupNetLobbyView();
		Net_StopClientDiscovery();

		if (pick <= 0)
		{
			ShutdownNetworkManager();
			return true;
		}

		Boolean connected = false;
		if (pick >= 100 && sPickedHostIdx >= 0 && sPickedHostIdx < sUINumHosts)
		{
			char targetIP[32];
			uint16_t targetPort = NET_DEFAULT_PORT;
			SDL_strlcpy(targetIP, sUIHosts[sPickedHostIdx].ipString, sizeof(targetIP));
			targetPort = sUIHosts[sPickedHostIdx].gamePort;
			connected = Net_ConnectToHost(targetIP, targetPort, gGamePrefs.playerName);
		}
		else if (SDL_strchr(sRoomCode, '.') != NULL)
		{
			connected = Net_ConnectToHost(sRoomCode, NET_DEFAULT_PORT, gGamePrefs.playerName);
		}
		else
		{
			connected = Net_ConnectToRoom(sRoomCode, gGamePrefs.playerName);
		}

		if (!connected)
		{
			const char* status = NULL;
			Net_GetConnectionState(&status);
			DoAlert("Cannot join: %s", status ? status : "Connection failed");
			ShutdownNetworkManager();
			continue;
		}

		int waitPick = RunClientWaitRoom();

		if (waitPick == 1)
		{
			return false; /* Host launched match! */
		}

		if (waitPick != 2)
		{
			Net_DisconnectGracefully();
		}

		ShutdownNetworkManager();
		/* Return to LAN browser */
	}
}

Boolean ReenterClientLobby(void)
{
	Net_ClientPrepareReturnToLobby();
	if (!Net_HasRemoteClients())
	{
		ShutdownNetworkManager();
		return true;
	}

	int waitPick = RunClientWaitRoom();
	if (waitPick == 1)
	{
		return false; /* Host launched next match! */
	}

	if (waitPick != 2)
	{
		Net_DisconnectGracefully();
	}

	ShutdownNetworkManager();
	return true;
}
