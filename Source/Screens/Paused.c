/****************************/
/*   	PAUSED.C			*/
/* By Brian Greenstone      */
/* (c)2000 Pangea Software  */
/* (c)2022 Iliyas Jorio     */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "menu.h"
#include "network.h"
#include "player.h"
#include "miscscreens.h"

/****************************/
/*    PROTOTYPES            */
/****************************/

static int ShouldDisplaySplitscreenModeCycler(const MenuItem* mi);
static void OnToggleSplitscreenMode(const MenuItem* mi);


/****************************/
/*    CONSTANTS             */
/****************************/

static const MenuItem gPauseMenuTree[] =
{
	{ .id='paus' },

	{kMIPick, STR_RESUME_GAME, .id='resu', .next='EXIT' },

	{kMISpacer, .customHeight=.3f},

	// 2P split-screen mode chooser
	{
		.type = kMICycler1,
		.text = STR_SPLITSCREEN_MODE,
		.id = 2,	// ShouldDisplaySplitscreenModeCycler looks at this ID to know it's meant for 2P
		.getLayoutFlags = ShouldDisplaySplitscreenModeCycler,
		.callback = OnToggleSplitscreenMode,
		.cycler =
		{
			.valuePtr = &gGamePrefs.splitScreenMode2P,
			.choices =
			{
				{ .text = STR_SPLITSCREEN_HORIZ, .value = SPLITSCREEN_MODE_2P_TALL },
				{ .text = STR_SPLITSCREEN_VERT, .value = SPLITSCREEN_MODE_2P_WIDE },
			},
		},
	},

	// 3P split-screen mode chooser
	{
		.type = kMICycler1,
		.text = STR_SPLITSCREEN_MODE,
		.id = 3,	// ShouldDisplaySplitscreenModeCycler looks at this ID to know it's meant for 3P
		.getLayoutFlags = ShouldDisplaySplitscreenModeCycler,
		.callback = OnToggleSplitscreenMode,
		.cycler =
		{
			.valuePtr = &gGamePrefs.splitScreenMode3P,
			.choices =
			{
				{ .text = STR_SPLITSCREEN_HORIZ, .value = SPLITSCREEN_MODE_3P_TALL },
				{ .text = STR_SPLITSCREEN_VERT, .value = SPLITSCREEN_MODE_3P_WIDE },
			},
		},
	},

	// Race timer
	{
		.type = kMICycler1,
		.text = STR_RACE_TIMER,
		.cycler=
		{
			.valuePtr=&gGamePrefs.raceTimer,
			.choices=
			{
					{STR_RACE_TIMER_HIDDEN, 0},
					{STR_RACE_TIMER_SIMPLE, 1},
					{STR_RACE_TIMER_DETAILED, 2},
			},
		},
	},

	{kMIPick, STR_SETTINGS, .callback=RegisterSettingsMenu, .next='sett' },

	{kMISpacer, .customHeight=.3f},

	{kMIPick, STR_RETIRE_GAME, .id='bail', .next='EXIT' },

//	{kMIPick, STR_QUIT_APPLICATION, .id='quit', .next='EXIT' },

	{ 0 },
};


/*********************/
/*    VARIABLES      */
/*********************/

Boolean gGamePaused = false;


/****************** TOGGLE SPLIT-SCREEN MODE ********************/

int ShouldDisplaySplitscreenModeCycler(const MenuItem* mi)
{
	if (gNumSplitScreenPanes == mi->id)
		return 0;
	else
		return kMILayoutFlagHidden | kMILayoutFlagDisabled;
}

void OnToggleSplitscreenMode(const MenuItem* mi)
{
	switch (gNumSplitScreenPanes)
	{
		case 2:
			gActiveSplitScreenMode = gGamePrefs.splitScreenMode2P;
			break;

		case 3:
			gActiveSplitScreenMode = gGamePrefs.splitScreenMode3P;
			break;

		default:
			SDL_Log("%s: what am I supposed to do with %d split-screen panes?", __func__, gNumSplitScreenPanes);
	}

	SetDefaultCameraModeForAllPlayers();
	UpdateCameras(false, true);
}


/********************** DO PAUSED **************************/

static void UpdatePausedMenuCallback(void)
{
	if (gNetGameInProgress)
	{
		/* Keep LAN session ticking live behind pause menu */
		gGamePaused = false;
		if (gIsNetworkHost)
		{
			HostReceive_ControlInfoFromClients();
			for (int pane = 0; pane < gNumLocalPlayers && pane < MAX_LOCAL_PLAYERS; pane++)
			{
				short slot = gLocalPlayerNums[pane];
				if (slot >= 0 && slot < MAX_PLAYERS)
				{
					gPlayerInfo[slot].controlBits = 0;
					gPlayerInfo[slot].controlBits_New = 0;
					gPlayerInfo[slot].analogSteering = (OGLVector2D){0, 0};
				}
			}
			MoveEverything();
			UpdateGameModeSpecifics();
			DoPlayerTerrainUpdate();
			HostSend_ControlInfoToClients();
		}
		else
		{
			ClientReceive_ControlInfoFromHost();
			MoveEverything();
			Net_ClientUpdateVisualFrame();
			if (gGameMode == GAME_MODE_TAG1 || gGameMode == GAME_MODE_TAG2)
				UpdateTagMarker();
			DoPlayerTerrainUpdate();
			for (int pane = 0; pane < gNumLocalPlayers && pane < MAX_LOCAL_PLAYERS; pane++)
			{
				short slot = gLocalPlayerNums[pane];
				if (slot >= 0 && slot < MAX_PLAYERS)
				{
					gPlayerInfo[slot].controlBits = 0;
					gPlayerInfo[slot].controlBits_New = 0;
					gPlayerInfo[slot].analogSteering = (OGLVector2D){0, 0};
				}
			}
			ClientSend_ControlInfoToHost();
		}
		gGamePaused = true;
		return;
	}

	MoveObjects();
	DoPlayerTerrainUpdate();							// need to call this to keep supertiles active
}

void DoPaused(void)
{
	MenuStyle style = kDefaultMenuStyle;
	style.canBackOutOfRootMenu = true;
	style.fadeOutSceneOnExit = false;
	style.darkenPaneOpacity = .6f;
	style.labelColor = (OGLColorRGBA){.7,.7,.7,1};
	style.startButtonExits = true;

	PushKeys();										// save key state so things dont get de-synced during net games

	PauseAllChannels(true);

	gGamePaused = true;
	gHideInfobar = true;

				/*************/
				/* MAIN LOOP */
				/*************/

	CalcFramesPerSecond();
	ReadKeyboard();

	int outcome = StartMenu(gPauseMenuTree, &style, UpdatePausedMenuCallback, DrawTerrain);

	gGamePaused = false;
	PauseAllChannels(false);
	
	PopKeys();										// restore key state

	switch (outcome)
	{
		case	'resu':								// RESUME
		default:
			gHideInfobar = false;
			break;

		case	'bail':								// EXIT
			Net_DisconnectGracefully();
			gGameOver = true;
			break;

		case	'quit':								// QUIT
			Net_DisconnectGracefully();
			ShutdownNetworkManager();
			CleanQuit();
			break;
	}
}
