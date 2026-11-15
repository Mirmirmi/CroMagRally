/****************************/
/*   	SELECT VEHICLE.C 	*/
/* By Brian Greenstone      */
/* (c)2000 Pangea Software  */
/* (c)2022 Iliyas Jorio     */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "miscscreens.h"
#include "network.h"


/****************************/
/*    PROTOTYPES            */
/****************************/

static void SetupVehicleSelectScreen(short whichPlayer);
static Boolean DoVehicleSelectControls(short whichPlayer, Boolean allowAborting);
static void FreeVehicleSelectArt(void);
static void MoveCarModel(ObjNode *theNode);
static void MakeVehicleName(void);
static void UpdateSelectedVehicle(void);
static const char* GetBoneString(int n);



/****************************/
/*    CONSTANTS             */
/****************************/

#define	LEFT_ARROW_X	-192
#define RIGHT_ARROW_X	192
#define	ARROW_Y			-48
#define	ARROW_SCALE		.5

#define	CAR_Y			70

#define	PARAMETERS_X		-224
#define	PARAMETERS_Y		92
#define	PARAMETERS_SCALE	.5

#define	LINE_SPACING		34

#define	NAME_Y				-170

#define	NUM_PARAM_BONES		8


/*********************/
/*    VARIABLES      */
/*********************/

static int		gSelectedVehicleIndex;
static int		gPawPatrolVehicleSkin = 0;

static const short kPawPatrolVehicles[] =
{
	CAR_TYPE_POLICE,
	CAR_TYPE_FIRETRUCK,
	CAR_TYPE_RESCUE_ATV,
	CAR_TYPE_HELICOPTER,
	CAR_TYPE_SCOOTER,
	CAR_TYPE_BULLDOZER,
	CAR_TYPE_HOVERCRAFT,
	CAR_TYPE_RECYCLING_TRUCK,
};
#define NUM_PAW_PATROL_VEHICLES	((int)(sizeof(kPawPatrolVehicles) / sizeof(kPawPatrolVehicles[0])))
#define NUM_VEHICLE_SLOTS		(CAR_TYPE_POLICE + 1)

static int GetNumVehicleSlots(void)
{
	return IsPawPatrolEnabled() ? NUM_VEHICLE_SLOTS : CAR_TYPE_POLICE;
}

static short GetEffectiveSelectedVehicle(void)
{
	if (gSelectedVehicleIndex == CAR_TYPE_POLICE)
		return kPawPatrolVehicles[gPawPatrolVehicleSkin];
	return gSelectedVehicleIndex;
}
static ObjNode	*gVehicleObj;
static ObjNode	*gVehicleName;
static ObjNode	*gBoneMeters[NUM_VEHICLE_PARAMETERS];
static ObjNode	*gVehicleLeftArrow = nil;
static ObjNode	*gVehicleRightArrow = nil;
static ObjNode	*gVehiclePadlock = nil;

static int		gNumVehiclesToChooseFrom;



/******************* DO MULTIPLAYER VEHICLE SELECTIONS ********************/

static void OnToggleMultiplayerCPUs(const MenuItem* item)
{
	(void)item;
	LayoutCurrentMenuAgain();
}

static Boolean AreMultiplayerCPUsActive(void)
{
	return (IsBattleGameMode(gGameMode) && gNumRealPlayers == 1) || (gMultiplayerCPUs != 0);
}

static int GetMultiplayerRaceOptionFlags(const MenuItem* item)
{
	(void)item;
	return IsBattleGameMode(gGameMode) ? (kMILayoutFlagHidden | kMILayoutFlagDisabled) : 0;
}

static int GetMultiplayerBattleCPUToggleFlags(const MenuItem* item)
{
	(void)item;
	return (IsBattleGameMode(gGameMode) && gNumRealPlayers > 1) ? 0 : (kMILayoutFlagHidden | kMILayoutFlagDisabled);
}

static int GetMultiplayerBattleStartFlags(const MenuItem* item)
{
	(void)item;
	return IsBattleGameMode(gGameMode) ? 0 : (kMILayoutFlagHidden | kMILayoutFlagDisabled);
}

static int GetMultiplayerDifficultyFlags(const MenuItem* item)
{
	(void)item;
	return AreMultiplayerCPUsActive() ? 0 : (kMILayoutFlagHidden | kMILayoutFlagDisabled);
}

static void OnStartMultiplayerRace(const MenuItem* item)
{
	(void)item;
	SavePrefs();
	gNumTotalPlayers = AreMultiplayerCPUsActive() ? MAX_PLAYERS : gNumRealPlayers;
	gGameView->fadePillarbox = true;
	gGameView->fadeSound = true;
	gGameView->fadeOutDuration = .3f;
}

static const MenuItem gMultiplayerRaceOptionsTree[] =
{
	{ .id = 'mprc' },
	{
		kMICycler1, STR_CPU_RACERS, .getLayoutFlags = GetMultiplayerRaceOptionFlags, .callback = OnToggleMultiplayerCPUs, .cycler =
		{
			.valuePtr = &gMultiplayerCPUs,
			.choices =
			{
				{ STR_OFF, 0 },
				{ STR_ON,  1 },
			}
		}
	},
	{
		kMICycler1, STR_CPU_PLAYERS, .getLayoutFlags = GetMultiplayerBattleCPUToggleFlags, .callback = OnToggleMultiplayerCPUs, .cycler =
		{
			.valuePtr = &gMultiplayerCPUs,
			.choices =
			{
				{ STR_OFF, 0 },
				{ STR_ON,  1 },
			}
		}
	},
	{
		kMICycler1, STR_DIFFICULTY, .getLayoutFlags = GetMultiplayerDifficultyFlags, .cycler =
		{
			.valuePtr = &gGamePrefs.difficulty,
			.choices =
			{
				{ STR_DIFFICULTY_1, DIFFICULTY_SIMPLISTIC },
				{ STR_DIFFICULTY_2, DIFFICULTY_EASY },
				{ STR_DIFFICULTY_3, DIFFICULTY_MEDIUM },
				{ STR_DIFFICULTY_4, DIFFICULTY_HARD },
			}
		}
	},
	{ kMISpacer, .customHeight = .5f },
	{ kMIPick, STR_START_RACE, .getLayoutFlags = GetMultiplayerRaceOptionFlags, .callback = OnStartMultiplayerRace, .id = 1, .next = 'EXIT' },
	{ kMIPick, STR_START_MATCH, .getLayoutFlags = GetMultiplayerBattleStartFlags, .callback = OnStartMultiplayerRace, .id = 1, .next = 'EXIT' },
	{ 0 },
};

static Boolean DoMultiplayerRaceOptionsScreen(void)
{
	OGLSetupInputType viewDef;
	OGL_NewViewDef(&viewDef);
	viewDef.view.pillarboxRatio = PILLARBOX_RATIO_4_3;
	viewDef.view.fontName       = "rockfont";
	OGL_SetupGameView(&viewDef);

	MakeScrollingBackgroundPattern();
	MakeFadeEvent(true);

	MenuStyle style = kDefaultMenuStyle;
	style.canBackOutOfRootMenu = true;

	int pick = StartMenu(gMultiplayerRaceOptionsTree, &style, MoveObjects, DrawObjects);

	DeleteAllObjects();
	OGL_DisposeGameView();

	return pick != 1;
}

static Boolean WaitForNetVehicleSelectionsScreen(void)
{
	Net_PollLobby();
	int state = Net_CheckVehicleSelectionsReady();
	if (state != 0)
		return state < 0;

	OGLSetupInputType viewDef;
	OGL_NewViewDef(&viewDef);
	viewDef.camera.from[0].z = 700;
	viewDef.camera.from[0].y = 250;
	viewDef.view.clearColor = (OGLColorRGBA){0.08f, 0.06f, 0.04f, 1.0f};
	viewDef.styles.useFog = false;
	viewDef.view.pillarboxRatio = PILLARBOX_RATIO_4_3;
	viewDef.view.fontName = "rockfont";
	OGL_SetupGameView(&viewDef);

	MakeScrollingBackgroundPattern();

	NewObjectDefinitionType def =
	{
		.coord = {0, -10, 0},
		.scale = .42f,
		.slot  = SPRITE_SLOT,
	};
	ObjNode* waitLabel = TextMesh_New("WAITING ON OTHER PLAYERS...", kTextMeshAlignCenter, &def);
	if (waitLabel)
		waitLabel->ColorFilter = (OGLColorRGBA){1.0f, 0.88f, 0.55f, 1.0f};

	def.coord.y = 220;
	def.scale = .27f;
	ObjNode* escPrompt = TextMesh_New(Localize(STR_PRESS_ESC_TO_GO_BACK), kTextMeshAlignCenter, &def);
	if (escPrompt)
	{
		escPrompt->ColorFilter = (OGLColorRGBA){0.75f, 0.75f, 0.75f, 1.0f};
		MakeTwitch(escPrompt, kTwitchPreset_PressKeyPrompt);
	}

	MakeFadeEvent(true);
	CalcFramesPerSecond();
	ReadKeyboard();

	Boolean aborted = false;
	while (true)
	{
		CalcFramesPerSecond();
		ReadKeyboard();
		Net_PollLobby();

		state = Net_CheckVehicleSelectionsReady();
		if (state != 0)
		{
			aborted = (state < 0);
			break;
		}

		if (GetNewNeedStateAnyP(kNeed_UIBack))
		{
			PlayEffect(EFFECT_GETPOW);
			aborted = true;
			break;
		}

		MoveObjects();
		OGL_DrawScene(DrawObjects);
	}

	if (!aborted)
		OGL_FadeOutScene(DrawObjects, MoveObjects);

	DeleteAllObjects();
	OGL_DisposeGameView();
	return aborted;
}

Boolean DoMultiPlayerVehicleSelections(void)
{
	if (gNetGameInProgress)
	{
		short localCount = gNumLocalPlayers;
		if (localCount < 1) localCount = 1;
		if (localCount > MAX_LOCAL_PLAYERS) localCount = MAX_LOCAL_PLAYERS;

		short currentScreen = 0;
		const short screensPerPlayer = 2;
		short totalLocalScreens = screensPerPlayer * localCount;

		while (currentScreen < totalLocalScreens)
		{
			short pane = currentScreen / screensPerPlayer;
			short slot = gLocalPlayerNums[pane];
			short screenToShow = currentScreen % screensPerPlayer;
			Boolean bailed = false;

			switch (screenToShow)
			{
				case 0:
					bailed = DoCharacterSelectScreen(slot, true);
					break;

				case 1:
					bailed = DoVehicleSelectScreen(slot, true);
					break;
			}

			if (bailed)
			{
				if (currentScreen == 0)
				{
					Net_DisconnectGracefully();
					return true;
				}
				currentScreen--;
			}
			else
			{
				currentScreen++;
			}
		}

		PlayerBroadcastVehicleType();								// tell other net players about all local seats
		if (WaitForNetVehicleSelectionsScreen())					// get types from other net players
		{
			Net_DisconnectGracefully();
			return true;
		}
		return false;
	}
	else
	{
		short currentScreen = 0;
		short screensPerPlayer = 2;
		short totalHumanScreens = screensPerPlayer * gNumLocalPlayers;
		bool bailed;

		while (1)
		{
			while (currentScreen < totalHumanScreens)
			{
				gCurrentPlayerNum = currentScreen / screensPerPlayer;
				int screenToShow = currentScreen % screensPerPlayer;

				switch (screenToShow)
				{
					case 0:
						bailed = DoCharacterSelectScreen(gCurrentPlayerNum, true);
						break;

					case 1:
						bailed = DoVehicleSelectScreen(gCurrentPlayerNum, true);
						break;

					default:
						DoFatalAlert("Unknown MP prep screen %d", screenToShow);
				}

				if (bailed)
				{
					if (currentScreen == 0)
					{
						return true;	// bail
					}
					else
					{
						currentScreen--;
					}
				}
				else
				{
					currentScreen++;
				}
			}

			if ((gGameMode == GAME_MODE_MULTIPLAYERRACE || IsBattleGameMode(gGameMode)) && DoMultiplayerRaceOptionsScreen())
			{
				currentScreen = totalHumanScreens - 1;
				continue;
			}

			return false;
		}
	}
}


/********************** DO VEHICLE SELECT SCREEN **************************/
//
// Return true if user aborts.
//

Boolean DoVehicleSelectScreen(short whichPlayer, Boolean allowAborting)
{
	if (gTrackNum == TRACK_NUM_ATLANTIS)		// dont select this on atlantis
		return(false);

	SetupVehicleSelectScreen(whichPlayer);
	MakeFadeEvent(true);


				/*************/
				/* MAIN LOOP */
				/*************/

	CalcFramesPerSecond();
	ReadKeyboard();

	while(true)
	{
			/* SEE IF MAKE SELECTION */

		if (DoVehicleSelectControls(whichPlayer, allowAborting))
			break;


			/**************/
			/* DRAW STUFF */
			/**************/

		CalcFramesPerSecond();
		ReadKeyboard();
		if (gNetGameInProgress)
			Net_PollLobby();
		MoveObjects();
		OGL_DrawScene(DrawObjects);
	}

			/***********/
			/* CLEANUP */
			/***********/

	bool bailed = gSelectedVehicleIndex == -1;								// see if user bailed

	if (!bailed)
	{
		gGameView->fadePillarbox = true;

		Boolean isLastLocal = gNetGameInProgress
			? (gPlayerInfo[whichPlayer].splitPaneNum == gNumLocalPlayers - 1)
			: (whichPlayer == gNumRealPlayers - 1);
		if (isLastLocal &&
			((gGameMode != GAME_MODE_MULTIPLAYERRACE && !IsBattleGameMode(gGameMode)) || gNetGameInProgress))
		{
			gGameView->fadeSound = true;
			gGameView->fadeOutDuration = .3f;
		}
	}

	OGL_FadeOutScene(DrawObjects, MoveObjects);
	FreeVehicleSelectArt();

	if (bailed)
		return(true);


		/* SET VEHICLE TYPE SELECTED */

	gPlayerInfo[whichPlayer].vehicleType = GetEffectiveSelectedVehicle();

	return(false);
}


/********************* SETUP VEHICLESELECT SCREEN **********************/

static void SetupVehicleSelectScreen(short whichPlayer)
{
FSSpec				spec;
OGLSetupInputType	viewDef;
OGLColorRGBA		ambientColor = { .4, .4, .4, 1 };
OGLColorRGBA		fillColor1 = { 1.0, 1.0, 1.0, 1 };
OGLColorRGBA		fillColor2 = { .5, .5, .5, 1 };
OGLVector3D			fillDirection1 = { .9, -.7, -1 };
OGLVector3D			fillDirection2 = { -1, -.2, -.5 };
int					age;

	gVehicleName = nil;
	gSelectedVehicleIndex = 0;
	gPawPatrolVehicleSkin = 0;

	for (int i = 0; i < NUM_VEHICLE_PARAMETERS; i++)
		gBoneMeters[i] = nil;

	age = GetNumAgesCompleted();
	int maxLandCars = IsPawPatrolEnabled() ? NUM_LAND_CAR_TYPES : CAR_TYPE_POLICE;
	if (age >= 3)											// completing Iron Age unlocks Police Cruiser
		gNumVehiclesToChooseFrom = maxLandCars;
	else
		gNumVehiclesToChooseFrom = 7 + (age * 2);			// set # cars we can pick from

	if ((gNumRealPlayers > 1) || IsBattleGameMode(gGameMode) || gNetGameInProgress)	// let use any car in multiplayer/battle/net modes
		gNumVehiclesToChooseFrom = maxLandCars;

			/**************/
			/* SETUP VIEW */
			/**************/

	OGL_NewViewDef(&viewDef);

	viewDef.camera.fov 				= 1.1;
	viewDef.camera.hither 			= 10;
	viewDef.camera.yon 				= 3000;
	viewDef.camera.from[0].z		= -700;
	viewDef.camera.from[0].y		= 250;

	viewDef.view.clearColor 		= (OGLColorRGBA) {.76f, .61f, .45f, 1.0f};
	viewDef.styles.useFog			= false;
	viewDef.view.pillarboxRatio	= PILLARBOX_RATIO_4_3;

	viewDef.lights.ambientColor 	= ambientColor;
	viewDef.lights.numFillLights 	= 2;
	viewDef.lights.fillDirection[0] = fillDirection1;
	viewDef.lights.fillDirection[1] = fillDirection2;
	viewDef.lights.fillColor[0] 	= fillColor1;
	viewDef.lights.fillColor[1] 	= fillColor2;

	OGL_SetupGameView(&viewDef);



				/************/
				/* LOAD ART */
				/************/


			/* MAKE BACKGROUND PICTURE OBJECT */

	MakeBackgroundPictureObject(":Images:VehicleSelectScreen.jpg");


			/* LOAD SPRITES */

	LoadSpriteGroup(SPRITE_GROUP_MAINMENU, "menus", 0);

			/* LOAD MODELS */

	FSMakeFSSpec(gDataSpec.vRefNum, gDataSpec.parID, ":Models:carselect.bg3d", &spec);
	ImportBG3D(&spec, MODEL_GROUP_CARSELECT);

			/*****************/
			/* BUILD OBJECTS */
			/*****************/

			/* VEHICLE MODEL */

	{
		NewObjectDefinitionType def =
		{
			.group		= MODEL_GROUP_CARSELECT,
			.type		= gSelectedVehicleIndex,
			.coord		= {0, CAR_Y, 0},
			.slot		= 100,
			.moveCall 	= MoveCarModel,
			.scale		= 1,
		};
		gVehicleObj = MakeNewDisplayGroupObject(&def);
	}


			/* VEHICLE NAME */

	MakeVehicleName();


			/* PARAMETER STRINGS */

	{
		NewObjectDefinitionType def =
		{
			.coord		= {PARAMETERS_X, PARAMETERS_Y, 0},
			.scale		= PARAMETERS_SCALE,
			.slot		= SPRITE_SLOT,
		};

		for (int i = 0; i < NUM_VEHICLE_PARAMETERS; i++)
		{
			OGLPoint3D leftCoord = def.coord;

			TextMesh_New(Localize(STR_CAR_STAT_1 + i), kTextMeshAlignLeft, &def);

			def.coord.x += 320;
			gBoneMeters[i] = TextMesh_New(GetBoneString(i), kTextMeshAlignLeft, &def);
	
			def.coord = leftCoord;
			def.coord.y += LINE_SPACING;
		}
	}


			/* ARROWS, PADLOCK */


	{
		NewObjectDefinitionType def =
		{
			.group = SPRITE_GROUP_MAINMENU,
			.type = MENUS_SObjType_LeftArrow,
			.coord = { LEFT_ARROW_X, ARROW_Y, 0 },
			.slot = SPRITE_SLOT,
			.scale = ARROW_SCALE,
		};
		gVehicleLeftArrow = MakeSpriteObject(&def);

		def.type = MENUS_SObjType_RightArrow;
		def.coord.x = RIGHT_ARROW_X;
		gVehicleRightArrow = MakeSpriteObject(&def);
	}

	{
		NewObjectDefinitionType def =
		{
			.group = SPRITE_GROUP_MAINMENU,
			.type = MENUS_SObjType_Padlock,
			.coord = { 0, ARROW_Y, 0 },
			.slot = SPRITE_SLOT,
			.scale = ARROW_SCALE,
		};
		gVehiclePadlock = MakeSpriteObject(&def);
	}



			/* SEE IF DOING 2-PLAYER LOCALLY */

	if (gNumLocalPlayers > 1)
	{
		ObjNode	*newObj;

		NewObjectDefinitionType def =
		{
			.coord		= {0, -208, 0},
			.scale		= .4,
			.slot 		= SPRITE_SLOT,
		};
		
		newObj = TextMesh_New(GetPlayerName(whichPlayer), kTextMeshAlignCenter, &def);

		newObj->ColorFilter.r = .5;
		newObj->ColorFilter.g = .3;
		newObj->ColorFilter.b = .2;
	}



	UpdateSelectedVehicle();
}


/******************** MAKE VEHICLE NAME ************************/

static void MakeVehicleName(void)
{
	if (gVehicleName)
	{
		DeleteObject(gVehicleName);
		gVehicleName = nil;
	}

	NewObjectDefinitionType def =
	{
		.coord		= {0, NAME_Y, 0},
		.scale		= .6,
		.slot 		= 99,	// Behind car
	};

	const char* nameStr = "???";
	short carType = GetEffectiveSelectedVehicle();
	if (gSelectedVehicleIndex < gNumVehiclesToChooseFrom)
	{
		nameStr = Localize(STR_CAR_MODEL_1 + carType);
	}

	gVehicleName = TextMesh_New(nameStr, kTextMeshAlignCenter, &def);

	gVehicleName->ColorFilter = (OGLColorRGBA) {.3, .5, .2, 1};
}



/********************** FREE VEHICLESELECT ART **********************/

static void FreeVehicleSelectArt(void)
{
	DeleteAllObjects();
	DisposeAllBG3DContainers();
	OGL_DisposeGameView();
}


/*************** REFERSH GRAPHICS FOR SELECTED CAR ********************/


static void UpdateSelectedVehicle(void)
{

	gVehicleObj->Type = GetEffectiveSelectedVehicle();
	ResetDisplayGroupObject(gVehicleObj);
	MakeVehicleName();


	for (int i = 0; i < NUM_VEHICLE_PARAMETERS; i++)
	{
		TextMesh_Update(GetBoneString(i), kTextMeshAlignLeft, gBoneMeters[i]);
	}

			/* FOR LOCKED CARS SHOW OUTLINE ONLY */

	if (gSelectedVehicleIndex >= gNumVehiclesToChooseFrom)
	{
		gVehicleObj->ColorFilter = (OGLColorRGBA) {0,0,0,1};
	}
	else
	{
		gVehicleObj->ColorFilter = (OGLColorRGBA) {1,1,1,1};
	}

			/* SHOW/HIDE ARROWS, PADLOCK */

	SetObjectVisible(gVehicleLeftArrow,		gSelectedVehicleIndex > 0);
	SetObjectVisible(gVehicleRightArrow,	gSelectedVehicleIndex < GetNumVehicleSlots() - 1);
	SetObjectVisible(gVehiclePadlock,		gSelectedVehicleIndex >= gNumVehiclesToChooseFrom);
}


/*********** GET "BONE METER" STRING TO DISPLAY VEHICLE STATS **********/

static const char* GetBoneString(int statID)
{
	int n = gUserPhysics.carStats[GetEffectiveSelectedVehicle()].params[statID];

	static char boneString[NUM_PARAM_BONES+1];

	if (n < 1 || n > 7)
	{
		// Show percentage for crazy values (most likely tweaked by user in physics sandbox)
		SDL_snprintf(boneString, sizeof(boneString), "%d%%", (int) roundf(100.0f * n / 7.0f));
		return boneString;
	}

	if (n > NUM_PARAM_BONES-1)
		n = NUM_PARAM_BONES-1;

	n++;		// Always draw an extra bone

	int i = 0;

	for (; i < n; i++)
		boneString[i] = '\\';

	for (; i < NUM_PARAM_BONES; i++)
		boneString[i] = '`';

	boneString[i] = '\0';

	return boneString;
}



/***************** DO VEHICLESELECT CONTROLS *******************/

static Boolean DoVehicleSelectControls(short whichPlayer, Boolean allowAborting)
{
short	p;


	p = gNetGameInProgress ? gPlayerInfo[whichPlayer].splitPaneNum : whichPlayer;
	if (p < 0)
		p = 0;


		/* SEE IF ABORT */

	if (allowAborting && GetNewNeedStateAnyP(kNeed_UIBack))		// anyone can abort
	{
		PlayEffect(EFFECT_GETPOW);
		gSelectedVehicleIndex = -1;
		return(true);
	}

		/* SEE IF SELECT THIS ONE */

	if (GetNewNeedState(kNeed_UIConfirm, p))
	{
		if (gSelectedVehicleIndex < gNumVehiclesToChooseFrom)
		{
			PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 2/3);
			return(true);
		}
		else
		{
			PlayEffect(EFFECT_BADSELECT);
			MakeTwitch(gVehiclePadlock, kTwitchPreset_PadlockWiggle);
		}
	}
	else
	if (IsCheatKeyComboDown())		// useful to test local multiplayer without having all controllers plugged in
	{
		PlayEffect(EFFECT_ROMANCANDLE_LAUNCH);
		return true;
	}


		/* SEE IF CHANGE SELECTION */

	if (GetNewNeedState(kNeed_UILeft, p) && (gSelectedVehicleIndex > 0))
	{
		PlayEffect(EFFECT_SELECTCLICK);
		gSelectedVehicleIndex--;
		UpdateSelectedVehicle();
		MakeTwitch(gVehicleLeftArrow, kTwitchPreset_DisplaceLTR);
		MakeTwitch(gVehicleName, kTwitchPreset_DisplaceRTL);
	}
	else
	if (GetNewNeedState(kNeed_UIRight, p) && (gSelectedVehicleIndex < GetNumVehicleSlots() - 1))
	{
		PlayEffect(EFFECT_SELECTCLICK);
		gSelectedVehicleIndex++;
		UpdateSelectedVehicle();
		MakeTwitch(gVehicleRightArrow, kTwitchPreset_DisplaceRTL);
		MakeTwitch(gVehicleName, kTwitchPreset_DisplaceLTR);
	}
	else
	{
		int verticalDir = 0;
		if (GetNewNeedState(kNeed_UIUp, p))
			verticalDir = 1;
		else if (GetNewNeedState(kNeed_UIDown, p))
			verticalDir = -1;

		if (verticalDir != 0)
		{
			if (gSelectedVehicleIndex == CAR_TYPE_POLICE)
			{
				PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 0.7f);
				gPawPatrolVehicleSkin = (gPawPatrolVehicleSkin + verticalDir + NUM_PAW_PATROL_VEHICLES) % NUM_PAW_PATROL_VEHICLES;
				UpdateSelectedVehicle();
				MakeTwitch(gVehicleName, (verticalDir > 0) ? kTwitchPreset_DisplaceLTR : kTwitchPreset_DisplaceRTL);
			}
			else
			{
				PlayEffect(EFFECT_BADSELECT);
			}
		}
	}

	return(false);
}





/****************** MOVE CAR MODEL *********************/

static void MoveCarModel(ObjNode *theNode)
{
	if (theNode->Type >= CAR_TYPE_POLICE && theNode->Type < NUM_LAND_CAR_TYPES)
		UpdatePoliceSirenLights(MODEL_GROUP_CARSELECT);
	theNode->Rot.y += gFramesPerSecondFrac * 1.5f;
	UpdateObjectTransforms(theNode);
}
