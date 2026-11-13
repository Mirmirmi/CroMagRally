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
static int		gPawPatrolVariant;		// 0 = Chase (sex 2), 1 = Marshall (sex 3)

static ObjNode	*gSex[NUM_CHARACTER_SLOTS];
static ObjNode	*gPawPatrolModels[2];
static ObjNode	*gCharacterArrow;

static const struct
{
	uint8_t		skelType;
	LocStrID	nameStr;
	float		x3d;
	float		nameX2d;
	float		rotY;
} kCharacterSlots[NUM_CHARACTER_SLOTS] =
{
	{ SKELETON_TYPE_MALESTANDING,   STR_BROG,       -CHAR_3D_SPREAD, -ARROW_2D_SPREAD, PI },
	{ SKELETON_TYPE_FEMALESTANDING, STR_GRAG,        0.0f,            0.0f,            PI },
	{ SKELETON_TYPE_CHASESTANDING,  STR_PAW_PATROL,  CHAR_3D_SPREAD,  ARROW_2D_SPREAD, PI - 0.50f },
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

	gPlayerInfo[whichPlayer].sex = (gSelectedCharacterIndex == 2)
		? (2 + gPawPatrolVariant)
		: gSelectedCharacterIndex;

	return(false);
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
	gPawPatrolVariant = (gPlayerInfo[whichPlayer].sex == 3) ? 1 : 0;

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
	LoadASkeleton(SKELETON_TYPE_MARSHALLSTANDING);



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
			TextMesh_New(Localize(kCharacterSlots[i].nameStr), kTextMeshAlignCenter, &newObjDef_NameString);
		}
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
		NewObjectDefinitionType newObjDef_Marshall =
		{
			.type = SKELETON_TYPE_MARSHALLSTANDING,
			.animNum = (gSelectedCharacterIndex == 2) ? 1 : 0,
			.coord = {kCharacterSlots[2].x3d, 0, 0},
			.slot = 100,
			.rot = kCharacterSlots[2].rotY,
			.scale = .5f
		};
		gPawPatrolModels[0] = gSex[2];
		gPawPatrolModels[1] = MakeNewSkeletonObject(&newObjDef_Marshall);
		SetObjectVisible(gPawPatrolModels[0], gPawPatrolVariant == 0);
		SetObjectVisible(gPawPatrolModels[1], gPawPatrolVariant == 1);
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

		gSex[0]->Skeleton->overrideTexture = gCavemanSkins[0][skinID];
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

	gSex[0]->Skeleton->overrideTexture = gCavemanSkins[0][newSkin];
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

static void TogglePawPatrolVariant(void)
{
	gPawPatrolVariant ^= 1;
	SetObjectVisible(gPawPatrolModels[0], gPawPatrolVariant == 0);
	SetObjectVisible(gPawPatrolModels[1], gPawPatrolVariant == 1);
	gSex[2] = gPawPatrolModels[gPawPatrolVariant];
	MorphToSkeletonAnim(gSex[2]->Skeleton, (gSelectedCharacterIndex == 2) ? 1 : 0, 5.0);
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
				TogglePawPatrolVariant();
			}
			else if (gGameMode != GAME_MODE_CAPTUREFLAG && gSelectedCharacterIndex < 2)
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


