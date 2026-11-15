/****************************/
/*   	SELECT CHARACTER.C 	*/
/* By Brian Greenstone      */
/* (c)2000 Pangea Software  */
/* (c)2022 Iliyas Jorio     */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"

/****************************/
/*    PROTOTYPES            */
/****************************/

static void SetupCharacterSelectScreen(short whichPlayer);
static Boolean DoCharacterSelectControls(short whichPlayer, Boolean allowAborting);
static void FreeCharacterSelectArt(void);



/****************************/
/*    CONSTANTS             */
/****************************/

#define	ARROW_SCALE			.5
#define CHAR_3D_SPREAD		84.0f
#define ARROW_2D_SPREAD		193.0f
#define ARROW_Y				204.0f
#define NUM_CHARACTER_SLOTS	3

#define GetCharacterArrowHomeX() (ARROW_2D_SPREAD * (gSelectedCharacterIndex - 1))


/*********************/
/*    VARIABLES      */
/*********************/

static int		gSelectedCharacterIndex;
static int		gBrogVariant;			// 0..NUM_CAVEMAN_SKINS-1 = Brog skins, +0 = Ryder, +1 = Humdinger
static int		gPawPatrolVariant;		// 0..NUM_PAW_PATROL_VARIANTS-1

#define NUM_BROG_MODELS			3
#define NUM_BROG_VARIANTS		(NUM_CAVEMAN_SKINS + 2)
#define NUM_PAW_PATROL_VARIANTS	7

static ObjNode	*gSex[NUM_CHARACTER_SLOTS];
static ObjNode	*gBrogModels[NUM_BROG_MODELS];
static ObjNode	*gPawPatrolModels[NUM_PAW_PATROL_VARIANTS];
static ObjNode	*gNameText[NUM_CHARACTER_SLOTS];
static ObjNode	*gCharacterArrow;

static const uint8_t kBrogStandingSkels[NUM_BROG_MODELS] =
{
	SKELETON_TYPE_MALESTANDING,
	SKELETON_TYPE_RYDERSTANDING,
	SKELETON_TYPE_HUMDINGERSTANDING,
};

static const uint8_t kPawPatrolStandingSkels[NUM_PAW_PATROL_VARIANTS] =
{
	SKELETON_TYPE_CHASESTANDING,
	SKELETON_TYPE_MARSHALLSTANDING,
	SKELETON_TYPE_SKYESTANDING,
	SKELETON_TYPE_LIBERTYSTANDING,
	SKELETON_TYPE_RUBBLESTANDING,
	SKELETON_TYPE_ZUMASTANDING,
	SKELETON_TYPE_ROCKYSTANDING,
};

static const float kPawPatrolNameX2d[NUM_PAW_PATROL_VARIANTS] =
{
	177.0f, 173.0f, 184.0f, 175.0f, 173.0f, 170.0f, 181.0f
};

static const float kPawPatrolNameY2d[NUM_PAW_PATROL_VARIANTS] =
{
	-194.0f, -194.0f, -194.0f, -194.0f, -194.0f, -194.0f, -194.0f
};

static const struct
{
	uint8_t		skelType;
	LocStrID	nameStr;
	float		x3d;
	float		nameX2d;
	float		nameY2d;
	float		rotY;
} kCharacterSlots[NUM_CHARACTER_SLOTS] =
{
	{ SKELETON_TYPE_MALESTANDING,   STR_BROG,       -CHAR_3D_SPREAD, -188.0f, -194.0f, PI },
	{ SKELETON_TYPE_FEMALESTANDING, STR_GRAG,        0.0f,              0.0f, -194.0f, PI },
	{ SKELETON_TYPE_CHASESTANDING,  STR_PAW_PATROL,  CHAR_3D_SPREAD,  177.0f, -194.0f, PI - 0.50f },
};


/********************** DO CHARACTER SELECT SCREEN **************************/
//
// Return true if user aborts.
//

Boolean DoCharacterSelectScreen(short whichPlayer, Boolean allowAborting)
{
	SetupCharacterSelectScreen(whichPlayer);
	MakeFadeEvent(true);


				/*************/
				/* MAIN LOOP */
				/*************/

	CalcFramesPerSecond();
	ReadKeyboard();

	while(true)
	{
			/* SEE IF MAKE SELECTION */

		if (DoCharacterSelectControls(whichPlayer, allowAborting))
			break;


			/**************/
			/* DRAW STUFF */
			/**************/

		CalcFramesPerSecond();
		ReadKeyboard();
		MoveObjects();
		OGL_DrawScene(DrawObjects);
	}

			/***********/
			/* CLEANUP */
			/***********/

	OGL_FadeOutScene(DrawObjects, MoveObjects);
	FreeCharacterSelectArt();
	OGL_DisposeGameView();

	if (gSelectedCharacterIndex == -1)										// see if user bailed
		return(true);


		/* SET CHARACTER TYPE SELECTED */

	if (gSelectedCharacterIndex == 2)
	{
		gPlayerInfo[whichPlayer].sex = SKELETON_TYPE_PLAYER_CHASE + gPawPatrolVariant;
	}
	else if (gSelectedCharacterIndex == 0)
	{
		if (gBrogVariant == NUM_CAVEMAN_SKINS)
		{
			gPlayerInfo[whichPlayer].sex = SKELETON_TYPE_PLAYER_RYDER;
			gPlayerInfo[whichPlayer].skin = 0;
		}
		else if (gBrogVariant == NUM_CAVEMAN_SKINS + 1)
		{
			gPlayerInfo[whichPlayer].sex = SKELETON_TYPE_PLAYER_HUMDINGER;
			gPlayerInfo[whichPlayer].skin = 0;
		}
		else
		{
			gPlayerInfo[whichPlayer].sex = SKELETON_TYPE_PLAYER_MALE;
		}
	}
	else
	{
		gPlayerInfo[whichPlayer].sex = SKELETON_TYPE_PLAYER_FEMALE;
	}

	return(false);
}


static void UpdateCharacterHeaderPositions(void)
{
	if (gNameText[0])
	{
		if (gBrogVariant == NUM_CAVEMAN_SKINS)
		{
			gNameText[0]->Coord.x = -192.0f;
			gNameText[0]->Coord.y = kCharacterSlots[0].nameY2d;
		}
		else if (gBrogVariant == NUM_CAVEMAN_SKINS + 1)
		{
			gNameText[0]->Coord.x = -193.0f;
			gNameText[0]->Coord.y = kCharacterSlots[0].nameY2d;
		}
		else
		{
			gNameText[0]->Coord.x = kCharacterSlots[0].nameX2d;
			gNameText[0]->Coord.y = kCharacterSlots[0].nameY2d;
		}
	}

	if (gNameText[2])
	{
		gNameText[2]->Coord.x = kPawPatrolNameX2d[gPawPatrolVariant];
		gNameText[2]->Coord.y = kPawPatrolNameY2d[gPawPatrolVariant];
	}
}


/********************* SETUP CHARACTERSELECT SCREEN **********************/

static void SetupCharacterSelectScreen(short whichPlayer)
{
OGLSetupInputType	viewDef;
OGLColorRGBA		ambientColor = { .5, .5, .5, 1 };
OGLColorRGBA		fillColor1 = { 1.0, 1.0, 1.0, 1 };
OGLVector3D			fillDirection1 = { .9, -.3, -1 };
ObjNode	*multiplayerText = NULL;

	gSelectedCharacterIndex = 0;
	if (gPlayerInfo[whichPlayer].sex >= SKELETON_TYPE_PLAYER_CHASE &&
		gPlayerInfo[whichPlayer].sex <= SKELETON_TYPE_PLAYER_ROCKY)
		gPawPatrolVariant = gPlayerInfo[whichPlayer].sex - SKELETON_TYPE_PLAYER_CHASE;
	else
		gPawPatrolVariant = 0;

	if (gPlayerInfo[whichPlayer].sex == SKELETON_TYPE_PLAYER_RYDER)
		gBrogVariant = NUM_CAVEMAN_SKINS;
	else if (gPlayerInfo[whichPlayer].sex == SKELETON_TYPE_PLAYER_HUMDINGER)
		gBrogVariant = NUM_CAVEMAN_SKINS + 1;
	else
		gBrogVariant = gPlayerInfo[whichPlayer].skin;

	for (int i = 0; i < NUM_CHARACTER_SLOTS; i++)
		gNameText[i] = NULL;

			/**************/
			/* SETUP VIEW */
			/**************/

	OGL_NewViewDef(&viewDef);

	viewDef.camera.fov 				= .3;
	viewDef.camera.hither 			= 10;
	viewDef.camera.yon 				= 3000;
	viewDef.camera.from[0].z		= 700;

	viewDef.view.clearColor 		= (OGLColorRGBA) { .51f, .39f, .27f, 1 };
	viewDef.styles.useFog			= false;
	viewDef.view.pillarboxRatio	= PILLARBOX_RATIO_4_3;

	viewDef.lights.ambientColor 	= ambientColor;
	viewDef.lights.numFillLights 	= 1;
	viewDef.lights.fillDirection[0] = fillDirection1;
	viewDef.lights.fillColor[0] 	= fillColor1;

	OGL_SetupGameView(&viewDef);



				/************/
				/* LOAD ART */
				/************/


			/* MAKE BACKGROUND PICTURE OBJECT */

	MakeBackgroundPictureObject(":Images:CharSelectScreen.jpg");


			/* LOAD SPRITES */

	LoadSpriteGroup(SPRITE_GROUP_MAINMENU, "menus", 0);


			/* LOAD SKELETONS */

	for (int i = 0; i < NUM_CHARACTER_SLOTS; i++)
		LoadASkeleton(kCharacterSlots[i].skelType);
	LoadASkeleton(SKELETON_TYPE_RYDERSTANDING);
	LoadASkeleton(SKELETON_TYPE_HUMDINGERSTANDING);
	for (int i = 1; i < NUM_PAW_PATROL_VARIANTS; i++)
		LoadASkeleton(kPawPatrolStandingSkels[i]);



			/*****************/
			/* BUILD OBJECTS */
			/*****************/


			/* SEE IF DOING 2-PLAYER LOCALLY */

	if (gNumLocalPlayers > 1 || gGameMode == GAME_MODE_CAPTUREFLAG)
	{
		NewObjectDefinitionType newObjDef =
		{
			.coord = {0, -192, 0},
			.scale = .55,
			.slot = 99
		};
		multiplayerText = TextMesh_New(GetPlayerNameWithInputDeviceHint(whichPlayer), kTextMeshAlignCenter, &newObjDef);

		multiplayerText->ColorFilter = (OGLColorRGBA) {.2, .7, .2, 1};
	}
	else
	{
			/* CREATE NAME STRINGS */

		NewObjectDefinitionType newObjDef_NameString =
		{
			.coord = {0, -192, 0},
			.scale = .6f,
			.slot = 99
		};
		for (int i = 0; i < NUM_CHARACTER_SLOTS; i++)
		{
			newObjDef_NameString.coord.x = kCharacterSlots[i].nameX2d;
			newObjDef_NameString.coord.y = kCharacterSlots[i].nameY2d;
			gNameText[i] = TextMesh_New(Localize(kCharacterSlots[i].nameStr), kTextMeshAlignCenter, &newObjDef_NameString);
		}
		UpdateCharacterHeaderPositions();
	}


			/* CREATE SELECTABLE CHARACTERS */

	for (int i = 0; i < NUM_CHARACTER_SLOTS; i++)
	{
		NewObjectDefinitionType newObjDef_Character =
		{
			.type = kCharacterSlots[i].skelType,
			.animNum = (i == gSelectedCharacterIndex) ? 1 : 0,
			.coord = {kCharacterSlots[i].x3d, 0, 0},
			.slot = 100,
			.rot = kCharacterSlots[i].rotY,
			.scale = .5f
		};
		gSex[i] = MakeNewSkeletonObject(&newObjDef_Character);
	}

	{
		int brogModelIdx = (gBrogVariant == NUM_CAVEMAN_SKINS) ? 1 :
						   (gBrogVariant == NUM_CAVEMAN_SKINS + 1) ? 2 : 0;
		gBrogModels[0] = gSex[0];
		for (int i = 1; i < NUM_BROG_MODELS; i++)
		{
			NewObjectDefinitionType newObjDef_BrogVar =
			{
				.type = kBrogStandingSkels[i],
				.animNum = (gSelectedCharacterIndex == 0) ? 1 : 0,
				.coord = {kCharacterSlots[0].x3d, 0, 0},
				.slot = 100,
				.rot = kCharacterSlots[0].rotY,
				.scale = .5f
			};
			gBrogModels[i] = MakeNewSkeletonObject(&newObjDef_BrogVar);
		}
		for (int i = 0; i < NUM_BROG_MODELS; i++)
			SetObjectVisible(gBrogModels[i], brogModelIdx == i);
		gSex[0] = gBrogModels[brogModelIdx];
	}

	{
		gPawPatrolModels[0] = gSex[2];
		for (int i = 1; i < NUM_PAW_PATROL_VARIANTS; i++)
		{
			NewObjectDefinitionType newObjDef_Pup =
			{
				.type = kPawPatrolStandingSkels[i],
				.animNum = (gSelectedCharacterIndex == 2) ? 1 : 0,
				.coord = {kCharacterSlots[2].x3d, 0, 0},
				.slot = 100,
				.rot = kCharacterSlots[2].rotY,
				.scale = .5f
			};
			gPawPatrolModels[i] = MakeNewSkeletonObject(&newObjDef_Pup);
		}
		for (int i = 0; i < NUM_PAW_PATROL_VARIANTS; i++)
			SetObjectVisible(gPawPatrolModels[i], gPawPatrolVariant == i);
		gSex[2] = gPawPatrolModels[gPawPatrolVariant];
	}

			/* CREATE ARROW */

	{
		NewObjectDefinitionType def =
		{
			.group = SPRITE_GROUP_MAINMENU,
			.type = MENUS_SObjType_UpArrow,
			.coord = { GetCharacterArrowHomeX(), ARROW_Y, 0},
			.slot = SPRITE_SLOT,
			.scale = ARROW_SCALE,
		};
		gCharacterArrow = MakeSpriteObject(&def);
	}


				/* SET UP CHARACTER SKINS */

	LoadCavemanSkins();

	int skinID = gPlayerInfo[whichPlayer].skin;

	if ((gGameMode == GAME_MODE_PRACTICE || gGameMode == GAME_MODE_TOURNAMENT)
		&& skinID == 0)
	{
		// Single-player mode: if the player didn't change their skin,
		// keep stock model skins so we get a chance to show off Grag's
		// trademark purple outfit (even though it's actually unused in-game).
	}
	else
	{
				/* OVERRIDE CHARACTER SKINS */

		gBrogModels[0]->Skeleton->overrideTexture = gCavemanSkins[0][skinID];
		gSex[1]->Skeleton->overrideTexture = gCavemanSkins[1][skinID];

		if (gGameMode == GAME_MODE_CAPTUREFLAG && multiplayerText != NULL)
		{
			int team = gPlayerInfo[whichPlayer].team;
			if (team == RED_TEAM)
			{
				multiplayerText->ColorFilter = (OGLColorRGBA) {.8, 0, 0, 1};
			}
			else
			{
				multiplayerText->ColorFilter = (OGLColorRGBA) {0, .8, 0, 1};
			}
		}
	}
}



/********************** FREE CHARACTERSELECT ART **********************/

static void FreeCharacterSelectArt(void)
{
	DeleteAllObjects();
	FreeAllSkeletonFiles(-1);
	DisposeAllBG3DContainers();
}


/***************** CYCLE CHARACTER SKINS *******************/

static void CycleSkin(short whichPlayer, int delta)
{
	GAME_ASSERT(whichPlayer < MAX_PLAYERS);

			/* FIND OUT WHICH SKINS ARE ALREADY TAKEN */

	uint32_t skinsTaken = 0;
	for (int prevPlayer = 0; prevPlayer < whichPlayer; prevPlayer++)
	{
		skinsTaken |= (1 << gPlayerInfo[prevPlayer].skin);
	}

			/* CYCLE TO NEXT AVAILABLE SKIN */

	short oldSkin = gPlayerInfo[whichPlayer].skin;
	short newSkin = oldSkin;

	do
	{
		newSkin += delta;
		newSkin = PositiveModulo(newSkin, NUM_CAVEMAN_SKINS);
	} while (skinsTaken & (1 << newSkin));

	gPlayerInfo[whichPlayer].skin = newSkin;

			/* SET TEXTURES */

	gBrogModels[0]->Skeleton->overrideTexture = gCavemanSkins[0][newSkin];
	gSex[1]->Skeleton->overrideTexture = gCavemanSkins[1][newSkin];

			/* SWAP MY OLD SKIN W/ PLAYER THAT USES THE ONE I WANT */

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (i != whichPlayer && gPlayerInfo[i].skin == newSkin)
		{
			gPlayerInfo[i].skin = oldSkin;
			break;
		}
	}
}


static void UpdateSelectedCharacterAnim(void)
{
	for (int i = 0; i < NUM_CHARACTER_SLOTS; i++)
		MorphToSkeletonAnim(gSex[i]->Skeleton, (i == gSelectedCharacterIndex) ? 1 : 0, 5.0);
}

static void CycleBrogVariant(short whichPlayer, int delta)
{
	GAME_ASSERT(whichPlayer < MAX_PLAYERS);

	uint32_t skinsTaken = 0;
	for (int prevPlayer = 0; prevPlayer < whichPlayer; prevPlayer++)
	{
		skinsTaken |= (1 << gPlayerInfo[prevPlayer].skin);
	}

	int newVar = gBrogVariant;
	do
	{
		newVar = PositiveModulo(newVar + delta, NUM_BROG_VARIANTS);
	} while (newVar < NUM_CAVEMAN_SKINS && (skinsTaken & (1 << newVar)));

	gBrogVariant = newVar;

	int brogModelIdx = 0;
	if (newVar == NUM_CAVEMAN_SKINS)
	{
		brogModelIdx = 1;
	}
	else if (newVar == NUM_CAVEMAN_SKINS + 1)
	{
		brogModelIdx = 2;
	}
	else
	{
		short oldSkin = gPlayerInfo[whichPlayer].skin;
		short newSkin = (short)newVar;
		gPlayerInfo[whichPlayer].skin = newSkin;
		gBrogModels[0]->Skeleton->overrideTexture = gCavemanSkins[0][newSkin];
		gSex[1]->Skeleton->overrideTexture = gCavemanSkins[1][newSkin];
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (i != whichPlayer && gPlayerInfo[i].skin == newSkin)
			{
				gPlayerInfo[i].skin = oldSkin;
				break;
			}
		}
	}

	for (int i = 0; i < NUM_BROG_MODELS; i++)
		SetObjectVisible(gBrogModels[i], brogModelIdx == i);
	gSex[0] = gBrogModels[brogModelIdx];
	MorphToSkeletonAnim(gSex[0]->Skeleton, (gSelectedCharacterIndex == 0) ? 1 : 0, 5.0);
	UpdateCharacterHeaderPositions();
}

static void CyclePawPatrolVariant(int delta)
{
	gPawPatrolVariant = PositiveModulo(gPawPatrolVariant + delta, NUM_PAW_PATROL_VARIANTS);
	for (int i = 0; i < NUM_PAW_PATROL_VARIANTS; i++)
		SetObjectVisible(gPawPatrolModels[i], gPawPatrolVariant == i);
	gSex[2] = gPawPatrolModels[gPawPatrolVariant];
	MorphToSkeletonAnim(gSex[2]->Skeleton, (gSelectedCharacterIndex == 2) ? 1 : 0, 5.0);
	UpdateCharacterHeaderPositions();
}


/***************** DO CHARACTERSELECT CONTROLS *******************/

static Boolean DoCharacterSelectControls(short whichPlayer, Boolean allowAborting)
{
short	p;


	if (gNetGameInProgress)										// if net game, then use P0 controls
		p = 0;
	else
		p = whichPlayer;										// otherwise, use P0 or P1 controls depending.


		/* SEE IF ABORT */

	if (allowAborting && GetNewNeedStateAnyP(kNeed_UIBack))		// anyone can abort
	{
		PlayEffect(EFFECT_GETPOW);
		gSelectedCharacterIndex = -1;
		return(true);
	}

		/* SEE IF SELECT THIS ONE */

	if (GetNewNeedState(kNeed_UIConfirm, p))
	{
		PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 2/3);
		return(true);
	}
	else
	if (IsCheatKeyComboDown())		// useful to test local multiplayer without having all controllers plugged in
	{
		PlayEffect(EFFECT_ROMANCANDLE_LAUNCH);
		return true;
	}


		/* SEE IF CHANGE SELECTION */

	if (GetNewNeedState(kNeed_UILeft, p) && (gSelectedCharacterIndex > 0))
	{
		PlayEffect(EFFECT_SELECTCLICK);
		gSelectedCharacterIndex--;
		UpdateSelectedCharacterAnim();
		gCharacterArrow->Coord.x = GetCharacterArrowHomeX();
		MakeTwitch(gCharacterArrow, kTwitchPreset_DisplaceRTL);
	}
	else
	if (GetNewNeedState(kNeed_UIRight, p) && (gSelectedCharacterIndex < NUM_CHARACTER_SLOTS - 1))
	{
		PlayEffect(EFFECT_SELECTCLICK);
		gSelectedCharacterIndex++;
		UpdateSelectedCharacterAnim();
		gCharacterArrow->Coord.x = GetCharacterArrowHomeX();
		MakeTwitch(gCharacterArrow, kTwitchPreset_DisplaceLTR);
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
			if (gSelectedCharacterIndex == 2)
			{
				PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 0.7f);
				CyclePawPatrolVariant(verticalDir);
			}
			else if (gGameMode != GAME_MODE_CAPTUREFLAG && gSelectedCharacterIndex == 0)
			{
				PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 0.7f);
				CycleBrogVariant(whichPlayer, verticalDir);
			}
			else if (gGameMode != GAME_MODE_CAPTUREFLAG && gSelectedCharacterIndex == 1)
			{
				PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 0.7f);
				CycleSkin(whichPlayer, verticalDir);
			}
			else
			{
				PlayEffect(EFFECT_BADSELECT);
			}
		}
	}

	return(false);
}


