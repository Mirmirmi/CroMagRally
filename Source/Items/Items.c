/****************************/
/*   		ITEMS.C		    */
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"

/****************************/
/*    PROTOTYPES            */
/****************************/

static void MoveTorchPot(ObjNode *theNode);
static void MoveAtlantisStartline(ObjNode *theNode);
static void MoveVolcano(ObjNode *theNode);
static void MovePolarBear(ObjNode *theNode);
static void MoveViking(ObjNode *theNode);


/****************************/
/*    CONSTANTS             */
/****************************/

#define	BEAR_SCALE			3.0f
#define	VIKING_SCALE		1.6f

/*********************/
/*    VARIABLES      */
/*********************/

ObjNode	*gCycloramaObj = nil;




/********************* INIT ITEMS MANAGER *************************/

void InitItemsManager(void)
{
int	i;

	gCycloramaObj = CreateCyclorama();

	for (i = 0; i < MAX_POW_TYPES; i++)
		gAnnouncedPOW[i] = false;

	gNumTorches = 0;
}


/************************* CREATE CYCLORAMA *********************************/
//
// This one cyc is used for all players on this machine.  Coords are reset during player render loop for each player.
//

ObjNode* CreateCyclorama(void)
{
	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_LEVELSPECIFIC,
		.type		= 0,						// cyc is always 1st model in level bg3d file!
		.coord		= {0, 0, 0},
		.flags		= STATUS_BIT_DONTCULL|STATUS_BIT_NOLIGHTING|STATUS_BIT_NOFOG,
		.slot		= CYCLORAMA_SLOT,
		.moveCall	= nil,
		.rot		= 0,
		.scale		= gGameView->yon * .99f,
	};

	return MakeNewDisplayGroupObject(&def);
}



#pragma mark -






/************************* ADD FINISH LINE *********************************/

Boolean AddFinishLine(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
CollisionBoxType *boxPtr;
float			y,d;
OGLMatrix3x3	m;
OGLPoint2D		p,p1,p2;

static const float xOff[] =
{
	2490,							// desert
	0,								// jungle
	1825,							// ice

	0,								// crete
	1806,							// china
	1751,							// egypt

	1498,							// europe
	1920,							// viking
	0,								// atlantis

};

static const float diameter[] =
{
	422,							// desert
	0,								// jungle
	255,							// ice

	0,								// crete
	354,							// china
	220,							// egypt

	423,							// europe
	280,							// viking
	0,								// atlantis
};


	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= DESERT_ObjType_StartingLine,
		.coord		= {x,0,z},	// y filled in below
		.flags 		= STATUS_BIT_NOLIGHTING|gAutoFadeStatusBits,
		.slot 		= 100,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f)),
		.scale 	    = 1,
	};
	def.coord.y 	= y = GetMinTerrainY(x,z, def.group, def.type, 1.0),
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


	switch(gTrackNum)
	{
				/*********/
				/* CRETE */
				/*********/

		case	TRACK_NUM_CRETE:

				newObj->CType 			= CTYPE_MISC;
				newObj->CBits			= CBITS_ALLSOLID;

						/* BUILD 4 COLLISION BOXES */

				AllocateCollisionBoxMemory(newObj, 4);								// alloc 4 collision boxes
				boxPtr = newObj->CollisionBoxes;										// get ptr to boxes
				boxPtr[0].left 		= x - 4030.0f;
				boxPtr[0].right 	= x - 3517.0f;
				boxPtr[0].top 		= y + 2000.0f;
				boxPtr[0].bottom 	= y - 10.0f;
				boxPtr[0].back 		= z - 1000.0f;
				boxPtr[0].front 	= z + 1000.0f;

				boxPtr[1].left 		= x - 1674.0f;
				boxPtr[1].right 	= x - 1233.0f;
				boxPtr[1].top 		= y + 2000.0f;
				boxPtr[1].bottom 	= y - 10.0f;
				boxPtr[1].back 		= z - 1000.0f;
				boxPtr[1].front 	= z + 1000.0f;

				boxPtr[2].left 		= x + 1233.0f;
				boxPtr[2].right 	= x + 1674.0f;
				boxPtr[2].top 		= y + 2000.0f;
				boxPtr[2].bottom 	= y - 10.0f;
				boxPtr[2].back 		= z - 1000.0f;
				boxPtr[2].front 	= z + 1000.0f;

				boxPtr[3].left 		= x + 3517.0f;
				boxPtr[3].right 	= x + 4030.0f;
				boxPtr[3].top 		= y + 2000.0f;
				boxPtr[3].bottom 	= y - 10.0f;
				boxPtr[3].back 		= z - 1000.0f;
				boxPtr[3].front 	= z + 1000.0f;

				KeepOldCollisionBoxes(newObj);


				break;

				/************/
				/* ATLANTIS */
				/************/

		case	TRACK_NUM_ATLANTIS:
				newObj->MoveCall = MoveAtlantisStartline;
				break;


				/************/
				/* JUNGLE   */
				/************/

		case	TRACK_NUM_JUNGLE:
				newObj->CType 			= CTYPE_MISC | CTYPE_AVOID;
				newObj->CBits			= CBITS_ALLSOLID;
				CreateCollisionBoxFromBoundingBox(newObj, 1.0, 1.0);
				break;



				/********************/
				/* BRIDGE COLLISION */
				/********************/

		default:

						/* SET COLLISION STUFF */

				newObj->CType 			= CTYPE_MISC;
				newObj->CBits			= CBITS_ALLSOLID;


					/* CALC COORDS OF EACH SUPPORT BEAM */

				OGLMatrix3x3_SetRotate(&m, -newObj->Rot.y);
				p.x = -xOff[gTrackNum];
				p.y = 0;
				OGLPoint2D_Transform(&p, &m, &p1);				// calc left
				p.x = -p.x;
				OGLPoint2D_Transform(&p, &m, &p2);				// calc right

						/* BUILD 2 COLLISION BOXES */

				d = diameter[gTrackNum];

				AllocateCollisionBoxMemory(newObj, 2);					// alloc 2 collision boxes
				boxPtr = newObj->CollisionBoxes;						// get ptr to boxes
				boxPtr[0].left 		= newObj->Coord.x + p1.x - d;
				boxPtr[0].right 	= newObj->Coord.x + p1.x + d;
				boxPtr[0].top 		= newObj->Coord.y + 1000.0;
				boxPtr[0].bottom 	= newObj->Coord.y - 10.0f;
				boxPtr[0].back 		= newObj->Coord.z + p1.y - d;
				boxPtr[0].front 	= newObj->Coord.z + p1.y + d;
				boxPtr[1].left 		= newObj->Coord.x + p2.x - d;
				boxPtr[1].right 	= newObj->Coord.x + p2.x + d;
				boxPtr[1].top 		= newObj->Coord.y + 1000.0;
				boxPtr[1].bottom 	= newObj->Coord.y - 10.0f;
				boxPtr[1].back 		= newObj->Coord.z + p2.y - d;
				boxPtr[1].front 	= newObj->Coord.z + p2.y + d;
				KeepOldCollisionBoxes(newObj);

	}







	return(true);													// item was added
}

/********** MOVE ATLANTIS START LINE ********************/

static void MoveAtlantisStartline(ObjNode *theNode)
{
	if (TrackTerrainItem(theNode))							// just check to see if it's gone
	{
		DeleteObject(theNode);
		return;
	}

	theNode->SpecialF[0] += gFramesPerSecondFrac * 3.0f;

	GetObjectInfo(theNode);

	gCoord.y = theNode->InitCoord.y + 1000.0f + sin(theNode->SpecialF[0]) * 300.0f;

	UpdateObject(theNode);

}


/************************* ADD TREE *********************************/

Boolean AddTree(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
static const short types[NUM_TRACKS][4] =
{
	[TRACK_NUM_DESERT]      = {0,0,0,0},
	[TRACK_NUM_JUNGLE]      = {JUNGLE_ObjType_Tree1,JUNGLE_ObjType_Tree2,JUNGLE_ObjType_Tree3,JUNGLE_ObjType_Tree3},
	[TRACK_NUM_ICE]         = {ICE_ObjType_Tree,0,0,0},
	[TRACK_NUM_CRETE]       = {CRETE_ObjType_TallTree,CRETE_ObjType_WideTree,0,0},
	[TRACK_NUM_CHINA]       = {0,0,0,0},
	[TRACK_NUM_EGYPT]       = {0,0,0,0},
	[TRACK_NUM_EUROPE]      = {EUROPE_ObjType_TallPine,EUROPE_ObjType_WidePine,0,0},
	[TRACK_NUM_SCANDINAVIA] = {SCANDINAVIA_ObjType_TallPine,SCANDINAVIA_ObjType_WidePine,0,0},
	[TRACK_NUM_ATLANTIS]    = {0,0,0,0},
	[TRACK_NUM_STONEHENGE]  = {0,0,0,0},
	[TRACK_NUM_AZTEC]       = {AZTEC_ObjType_Tree,0,0,0},
	[TRACK_NUM_COLISEUM]    = {0,0,0,0},
};

static const short aimAtPlayer[NUM_TRACKS][4] =
{
	[TRACK_NUM_DESERT]      = {0,0,0,0},
	[TRACK_NUM_JUNGLE]      = {false,false,false,true},
	[TRACK_NUM_ICE]         = {false,0,0,0},
	[TRACK_NUM_CRETE]       = {false,false,0,0},
	[TRACK_NUM_CHINA]       = {0,0,0,0},
	[TRACK_NUM_EGYPT]       = {0,0,0,0},
	[TRACK_NUM_EUROPE]      = {0,0,0,0},
	[TRACK_NUM_SCANDINAVIA] = {0,0,0,0},
	[TRACK_NUM_ATLANTIS]    = {0,0,0,0},
	[TRACK_NUM_STONEHENGE]  = {0,0,0,0},
	[TRACK_NUM_AZTEC]       = {false,0,0,0},
	[TRACK_NUM_COLISEUM]    = {0,0,0,0},
};

Boolean	isSolid = itemPtr->parm[3] & 1;

	if (itemPtr->parm[0] >= 4)
	{
#if _DEBUG
		SDL_Log("Illegal tree type #%d %ld %ld", itemPtr->parm[0], x, z);
#endif
		return false;
	}

	short type = SDL_clamp(itemPtr->parm[0], 0, 1);
	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= types[gTrackNum][type],
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits|STATUS_BIT_KEEPBACKFACES|STATUS_BIT_NOLIGHTING|STATUS_BIT_NOTEXTUREWRAP|STATUS_BIT_CLIPALPHA,
		.slot		= isSolid ? 642 : SLOT_OF_DUMB,
		.moveCall 	= MoveStaticObject,
		.rot 		= 0,
		.scale 		= 1.0 + RandomFloat() * .3f,
	};

	if (itemPtr->parm[3] & (1<<1))											// see if bump up
		def.coord.y += 500.0f;

	if (aimAtPlayer[gTrackNum][itemPtr->parm[0]])
		def.flags |= STATUS_BIT_AIMATCAMERA;

	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	if (isSolid)
	{
				/* SET COLLISION STUFF */

		newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
		newObj->CBits			= CBITS_ALLSOLID;

		if (gTrackNum == TRACK_NUM_AZTEC)
		{
			/* Aztec tree trunks are wider/offset in aztec.bg3d and often sit on sloped
			   pyramid hills where a fixed bottom=-10 lets downhill cars pass underneath. */
			const short left   = (short)lroundf(-95.0f * newObj->Scale.x);
			const short right  = (short)lroundf( 75.0f * newObj->Scale.x);
			const short front  = (short)lroundf(110.0f * newObj->Scale.z);
			const short back   = (short)lroundf(-60.0f * newObj->Scale.z);
			const float pad    = 220.0f;
			float minY = def.coord.y;
			float maxY = def.coord.y;

			for (int dx = -1; dx <= 1; dx++)
			{
				for (int dz = -1; dz <= 1; dz++)
				{
					const float sampleY = GetTerrainY((float)x + (float)dx * pad, (float)z + (float)dz * pad);
					if (sampleY < minY) minY = sampleY;
					if (sampleY > maxY) maxY = sampleY;
				}
			}

			const float rawBottom = fminf(-100.0f, minY - def.coord.y - 100.0f);
			const float rawTop    = fmaxf(1000.0f, maxY - def.coord.y + 300.0f);
			const short bottom    = (short)SDL_clamp((long)floorf(rawBottom), -3000L, -10L);
			const short top       = (short)SDL_clamp((long)ceilf(rawTop), 1000L, 30000L);
			SetObjectCollisionBounds(newObj, top, bottom, left, right, front, back);
		}
		else
		{
			SetObjectCollisionBounds(newObj, 1000, -10, -50, 50, 50, -50);
		}
	}


	return(true);													// item was added
}


/************************* ADD VINE *********************************/

Boolean AddVine(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= JUNGLE_ObjType_Vine,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits|STATUS_BIT_NOLIGHTING|STATUS_BIT_CLIPALPHA,
		.slot 		= SLOT_OF_DUMB+1,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f)),
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	return(true);													// item was added
}



/************************* ADD EASTER ISLAND HEAD *********************************/

Boolean AddEasterHead(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= JUNGLE_ObjType_EasterHead,
		.coord		= {x,0,z},	// y filled in below
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 10,
		.moveCall 	= MoveStaticObject,
		.rot 		= (float)itemPtr->parm[0] / 8.0f * PI2,
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;

	static const LocalCollisionSubBox headBoxes[] =
	{
		{ -230.0f,  230.0f,   1.3f,  220.0f, -449.3f, -180.0f },
		{ -314.0f,  314.0f,   1.3f,  220.0f, -180.0f,  180.0f },
		{ -210.0f,  210.0f,   1.3f,  220.0f,  180.0f,  364.9f },
		{ -260.0f,  260.0f, 220.0f, 1364.5f, -424.0f, -180.0f },
		{ -373.9f,  373.9f, 220.0f, 1364.5f, -180.0f,  180.0f },
		{ -260.0f,  260.0f, 220.0f, 1364.5f,  180.0f,  449.3f },
	};
	CreateRotatedSubCollisionBoxes(newObj, headBoxes, (short)(sizeof(headBoxes) / sizeof(headBoxes[0])));

	return(true);													// item was added
}

#pragma mark -


/************************* ADD PILLAR *********************************/

typedef struct
{
	short 	type[4];
	float	tweakXZ[4],tweakY[4];
}ColumnInfo;

Boolean AddPillar(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
static const ColumnInfo info[NUM_TRACKS] =
{
	[TRACK_NUM_DESERT] =
	{
		{DESERT_ObjType_RockColumn1,DESERT_ObjType_RockColumn2,DESERT_ObjType_RockColumn3,0},
		{1,1,.9,1},
		{1,1,1,1},
	},

	[TRACK_NUM_JUNGLE] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_ICE] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_CRETE] =
	{
		{CRETE_ObjType_Column1,CRETE_ObjType_Column2,CRETE_ObjType_Column3,CRETE_ObjType_Column1},
		{1,1,.8,1},
		{1,1,1,1},
	},

	[TRACK_NUM_CHINA] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_EGYPT] =
	{
		{EGYPT_ObjType_Pillar,EGYPT_ObjType_Obelisk,0,0},
		{.9,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_EUROPE] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_SCANDINAVIA] =
	{
		{SCANDINAVIA_ObjType_LookoutTower,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_ATLANTIS] =
	{
		{ATLANTIS_ObjType_Tower,ATLANTIS_ObjType_Column1,ATLANTIS_ObjType_Column2,0},
		{1,.6,.6,1},
		{.5,1,1,1},
	},

	[TRACK_NUM_STONEHENGE] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_AZTEC] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_COLISEUM] =
	{
		{COLISEUM_ObjType_Column,0,0,0},
		{.9,1,1,1},
		{1,1,1,1},
	},
};

Boolean	notSolid = itemPtr->parm[3] & 1;						// see if solid or not
short	type = SDL_clamp(itemPtr->parm[0], 0, 3);

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= info[gTrackNum].type[type],
		.coord		= {x,0,z},	// y filled in below
		.flags 		= gAutoFadeStatusBits|STATUS_BIT_CLIPALPHA,
		.moveCall 	= MoveStaticObject,
		.slot		= notSolid? (SLOT_OF_DUMB+2): 90,
		.rot 		= 0,
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),

	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	if (!notSolid)
	{
		newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
		newObj->CBits			= CBITS_ALLSOLID;

		CreateCollisionBoxFromBoundingBox(newObj,info[gTrackNum].tweakXZ[type],info[gTrackNum].tweakY[type]);

	}

	return(true);													// item was added
}

/************************* ADD PYLON *********************************/

Boolean AddPylon(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= EGYPT_ObjType_Pylon,
		.coord	 	= {x,0,z},	// y filled in later
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 40,
		.moveCall 	= MoveStaticObject,
		.rot 		= 0,
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Rotated(newObj,1,1);

	return(true);													// item was added
}


/***************** SET ROTATED SUB-COLLISION BOX ******************/
/* Builds one world-space AABB in boxes from local bounds rotated by theNode->Rot.y. */

/*************** CREATE VIKING SHIP COLLISION BOXES *****************/
/* Builds multi-box geometry for SCANDINAVIA_ObjType_VikingShip:
   8 slanted support legs (3 tiers each), 7-slice hull + raised ends, and mast,
   avoiding a single oversized diagonal AABB while keeping ground clearance under the hull. */

static void CreateVikingShipCollisionBoxes(ObjNode *theNode)
{
	enum
	{
		NUM_SUPPORT_STATIONS = 4,
		NUM_SUPPORT_TIERS    = 3,
		NUM_HULL_SLICES      = 7,
		NUM_END_SEGMENTS     = 2,
		TOTAL_BOXES          = (NUM_SUPPORT_STATIONS * 2 * NUM_SUPPORT_TIERS)
		                       + (NUM_HULL_SLICES * 3)
		                       + (2 * NUM_END_SEGMENTS)
		                       + 1,
	};

	static const float supportZ[NUM_SUPPORT_STATIONS] = {-1799.6f, -749.6f, 749.6f, 1799.6f};
	static const float supportHalfZ = 64.0f;

	typedef struct
	{
		float innerX;
		float outerX;
		float minY;
		float maxY;
	} SupportTierDef;

	/* Tier 2 extends minY to -30 so CollisionDetect's minimum-penetration fallback
	   cannot resolve low side hits downward onto Tier 1's floor side. */
	static const SupportTierDef supportTiers[NUM_SUPPORT_TIERS] =
	{
		{507.0f, 724.0f, -30.0f, 160.0f},
		{400.0f, 639.0f, -30.0f, 340.0f},
		{248.0f, 532.0f, 340.0f, 615.0f},
	};

	typedef struct
	{
		float innerZ;
		float outerZ;
		float halfX;
		float minY;
		float maxY;
	} BowSternDef;

	static const BowSternDef bowSternDefs[NUM_END_SEGMENTS] =
	{
		{2100.0f, 2550.0f, 360.0f, 440.0f, 1050.0f},
		{2550.0f, 3050.0f, 180.0f, 765.0f, 1800.0f},
	};

	OGLMatrix3x3 rotMat;
	OGLMatrix3x3_SetRotate(&rotMat, -theNode->Rot.y);

	AllocateCollisionBoxMemory(theNode, TOTAL_BOXES);
	CollisionBoxType *boxes = theNode->CollisionBoxes;
	int boxIndex = 0;

	/* 8 slanted wooden support cradles (24 boxes) */
	for (int station = 0; station < NUM_SUPPORT_STATIONS; station++)
	{
		const float minZ = supportZ[station] - supportHalfZ;
		const float maxZ = supportZ[station] + supportHalfZ;

		for (int side = -1; side <= 1; side += 2)
		{
			for (int tier = 0; tier < NUM_SUPPORT_TIERS; tier++)
			{
				const SupportTierDef *t = &supportTiers[tier];
				const float minX = (side < 0) ? -t->outerX : t->innerX;
				const float maxX = (side < 0) ? -t->innerX : t->outerX;
				SetRotatedSubCollisionBox(theNode, &rotMat, &boxes[boxIndex++],
										  minX, maxX, t->minY, t->maxY, minZ, maxZ);
			}
		}
	}

	/* Hull midbody sliced along Z to stay tight at 45-degree rotations (21 boxes) */
	for (int slice = 0; slice < NUM_HULL_SLICES; slice++)
	{
		const float z0 = -2100.0f + slice * 600.0f;
		const float z1 = z0 + 600.0f;

		SetRotatedSubCollisionBox(theNode, &rotMat, &boxes[boxIndex++],
								  -260.0f, 260.0f, 350.0f, 520.0f, z0, z1);
		SetRotatedSubCollisionBox(theNode, &rotMat, &boxes[boxIndex++],
								  -580.0f, 0.0f, 520.0f, 990.0f, z0, z1);
		SetRotatedSubCollisionBox(theNode, &rotMat, &boxes[boxIndex++],
								  0.0f, 580.0f, 520.0f, 990.0f, z0, z1);
	}

	/* Raised bow and stern flares (4 boxes) */
	for (int endSign = -1; endSign <= 1; endSign += 2)
	{
		for (int seg = 0; seg < NUM_END_SEGMENTS; seg++)
		{
			const BowSternDef *d = &bowSternDefs[seg];
			const float minZ = (endSign < 0) ? -d->outerZ : d->innerZ;
			const float maxZ = (endSign < 0) ? -d->innerZ : d->outerZ;
			SetRotatedSubCollisionBox(theNode, &rotMat, &boxes[boxIndex++],
									  -d->halfX, d->halfX, d->minY, d->maxY, minZ, maxZ);
		}
	}

	/* Mast (1 box) */
	SetRotatedSubCollisionBox(theNode, &rotMat, &boxes[boxIndex++],
							  -70.0f, 70.0f, 900.0f, 3600.0f, -70.0f, 70.0f);

	GAME_ASSERT(boxIndex == TOTAL_BOXES);
	KeepOldCollisionBoxes(theNode);
}


/************************* ADD BOAT *********************************/

Boolean AddBoat(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.coord		= {x, GetTerrainY(x,z), z},
		.flags 		= gAutoFadeStatusBits,
		.moveCall 	= MoveStaticObject,
		.slot 		= 40,
		.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/8.0f)),
		.scale 		= 1.0,
	};

	switch(gTrackNum)
	{
		case	TRACK_NUM_EGYPT:
				def.type = EGYPT_ObjType_Boat;
				def.coord.y = gWaterHeights[gTrackNum][itemPtr->parm[0]];	// put on top of water
				break;

		case	TRACK_NUM_CRETE:
				def.type = CRETE_ObjType_Boat;
				def.coord.y = gWaterHeights[gTrackNum][itemPtr->parm[0]];	// put on top of water
				break;

		case	TRACK_NUM_SCANDINAVIA:
				def.type = SCANDINAVIA_ObjType_VikingShip;
				break;

		case	TRACK_NUM_ATLANTIS:
				def.type = ATLANTIS_ObjType_Shipwreck;
				break;

		default:
				DoFatalAlert("Can't AddBoat in track %d!", gTrackNum);
	}

	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC;
	newObj->CBits			= CBITS_ALLSOLID;
	if (gTrackNum == TRACK_NUM_SCANDINAVIA)
		CreateVikingShipCollisionBoxes(newObj);
	else if (gTrackNum == TRACK_NUM_ATLANTIS)
	{
		OGLMatrix3x3 rotMat;
		AllocateCollisionBoxMemory(newObj, 15);
		OGLMatrix3x3_SetRotate(&rotMat, -newObj->Rot.y);
		SetRotatedSubCollisionBox(newObj, &rotMat, &newObj->CollisionBoxes[0],
								   192.0f,  884.0f,  420.0f, 1100.0f, -1857.0f, -1300.0f);
		SetRotatedSubCollisionBox(newObj, &rotMat, &newObj->CollisionBoxes[1],
								  -194.0f,  341.0f, -105.0f, 1336.0f, -1300.0f,  -850.0f);
		FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[2],
								-1191.0f,  614.0f, -145.0f, 1196.0f,  -850.0f,  -350.0f, 3, 1);
		FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[5],
								 -569.0f,  855.0f, -166.0f, 1252.0f,  -350.0f,   150.0f, 3, 1);
		FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[8],
								-1769.0f,  581.0f, -125.0f, 1030.0f,   150.0f,   650.0f, 3, 1);
		FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[11],
								-1676.0f,  626.0f, -111.0f,  878.0f,   650.0f,  1150.0f, 3, 1);
		SetRotatedSubCollisionBox(newObj, &rotMat, &newObj->CollisionBoxes[14],
								  -139.0f,  576.0f,  100.0f,  800.0f,  1150.0f,  1842.0f);
		KeepOldCollisionBoxes(newObj);
	}
	else if (gTrackNum == TRACK_NUM_CRETE)
	{
		static const LocalCollisionSubBox creteBoatBoxes[] =
		{
			{  -211.0f,  173.0f,  241.0f,  932.0f, -3990.0f, -3000.0f },
			{  -736.0f,  699.0f, -114.0f,  425.0f, -3000.0f, -2000.0f },
			{  -480.0f,  443.0f, -114.0f,  245.0f, -2000.0f,  2000.0f },
			{ -1198.0f, 1198.0f,  245.0f, 2709.0f, -2000.0f,  2000.0f },
			{  -742.0f,  699.0f, -114.0f, 1422.0f,  2000.0f,  3000.0f },
			{  -141.0f,  103.0f, -114.0f,  993.0f,  3000.0f,  3990.0f },
		};
		CreateRotatedSubCollisionBoxes(newObj, creteBoatBoxes, (short)(sizeof(creteBoatBoxes) / sizeof(creteBoatBoxes[0])));
	}
	else if (gTrackNum == TRACK_NUM_EGYPT)
	{
		static const LocalCollisionSubBox egyptBoatBoxes[] =
		{
			{ -182.0f, 182.0f, 253.0f, 1866.0f, -3500.0f, -2800.0f },
			{  -62.0f,  62.0f, -41.0f,  200.0f, -2800.0f,  2800.0f },
			{ -311.0f, 311.0f, 200.0f, 1866.0f, -2800.0f,  2800.0f },
			{ -182.0f, 182.0f, 253.0f, 1866.0f,  2800.0f,  3500.0f },
		};
		CreateRotatedSubCollisionBoxes(newObj, egyptBoatBoxes, (short)(sizeof(egyptBoatBoxes) / sizeof(egyptBoatBoxes[0])));
	}
	else
		CreateCollisionBoxFromBoundingBox_Rotated(newObj,1,1);


	if (gTrackNum == TRACK_NUM_ATLANTIS)			// blue tint for atlantis
	{
		newObj->ColorFilter.r = .7;
		newObj->ColorFilter.g = .8;
		newObj->ColorFilter.b = 1.0;

	}

	return(true);													// item was added
}



/************************* ADD STATUE *********************************/
//
// p0 = rot 0..7 ccw
//

Boolean AddStatue(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
static const short types[NUM_TRACKS][2] =
{
	[TRACK_NUM_DESERT]      = {0,0},
	[TRACK_NUM_JUNGLE]      = {0,0},
	[TRACK_NUM_ICE]         = {0,0},
	[TRACK_NUM_CRETE]       = {CRETE_ObjType_BullStatue,0},
	[TRACK_NUM_CHINA]       = {0,0},
	[TRACK_NUM_EGYPT]       = {EGYPT_ObjType_Statue, EGYPT_ObjType_CatStatue},
	[TRACK_NUM_EUROPE]      = {0,0},
	[TRACK_NUM_SCANDINAVIA] = {0,0},
	[TRACK_NUM_ATLANTIS]    = {0,0},
};


	short type = SDL_clamp(itemPtr->parm[0], 0, 1);
	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= types[gTrackNum][type],
		.coord.x 	= x,
		.coord.z 	= z,
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 42,
		.moveCall 	= MoveStaticObject,
		.rot 		= itemPtr->parm[1] * (PI/4),
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0);
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;

	if (gTrackNum == TRACK_NUM_EGYPT && newObj->Type == EGYPT_ObjType_CatStatue)
	{
		static const LocalCollisionSubBox catStatueBoxes[] =
		{
			{ -324.5f, 328.0f,   0.4f,  244.0f, -339.1f, 335.9f },
			{ -263.5f, 262.0f, 244.0f, 1343.9f, -452.2f, 573.0f },
		};
		CreateRotatedSubCollisionBoxes(newObj, catStatueBoxes, (short)(sizeof(catStatueBoxes) / sizeof(catStatueBoxes[0])));
	}
	else if (gTrackNum == TRACK_NUM_EGYPT && newObj->Type == EGYPT_ObjType_Statue)
	{
		OGLMatrix3x3 rotMat;
		AllocateCollisionBoxMemory(newObj, 4);
		OGLMatrix3x3_SetRotate(&rotMat, -newObj->Rot.y);
		FillRotatedGridSubBoxes(newObj, &rotMat, newObj->CollisionBoxes,
								-353.2f, 353.2f, 0.9f, 2195.5f, -359.2f, 359.2f, 2, 2);
		KeepOldCollisionBoxes(newObj);
	}
	else
		CreateCollisionBoxFromBoundingBox_Rotated(newObj, 1, 1);

	return(true);													// item was added
}


/************************* ADD SPHINX *********************************/

Boolean AddSphinx(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= EGYPT_ObjType_Sphinx,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z) - 100.0f,
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 50,
		.moveCall 	= MoveStaticObject,
		.rot 		= itemPtr->parm[0] * (PI/2),
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC;
	newObj->CBits			= CBITS_ALLSOLID;
	static const LocalCollisionSubBox sphinxBoxes[] =
	{
		{ -1033.5f, 1033.5f,   1.5f,  260.0f, -1208.8f, -352.5f },
		{  -820.0f,  820.0f,   1.5f, 1546.0f,  -352.5f,  150.0f },
		{  -600.0f,  600.0f,   1.5f,  800.0f,   150.0f,  650.0f },
		{  -390.0f,  390.0f,   1.5f,  490.0f,   650.0f, 1150.0f },
		{  -220.0f,  220.0f,   1.5f,  260.0f,  1150.0f, 1596.4f },
		{ -1319.1f, 1319.1f, 260.0f, 2023.2f, -1664.6f, -352.5f },
	};
	CreateRotatedSubCollisionBoxes(newObj, sphinxBoxes, (short)(sizeof(sphinxBoxes) / sizeof(sphinxBoxes[0])));

	return(true);													// item was added
}



#pragma mark -


/************************* ADD SIGN *********************************/

Boolean AddSign(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_GLOBAL,
		.type 		= GLOBAL_ObjType_Sign_Fire + itemPtr->parm[0],
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits|STATUS_BIT_KEEPBACKFACES|STATUS_BIT_NOTEXTUREWRAP|STATUS_BIT_CLIPALPHA,
		.slot 		= 10,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/8.0f)),
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	SetObjectCollisionBounds(newObj, 400, -10, -50, 50, 50, -50);

	return(true);													// item was added
}





#pragma mark -


/************************* ADD STUMP *********************************/

Boolean AddStump(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= SCANDINAVIA_ObjType_Stump1 + (MyRandomLong() & 0x3),
		.coord		= {x,0,z},
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 400,
		.moveCall 	= MoveStaticObject,
		.rot 		= RandomFloat() * PI2,
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	return(true);													// item was added
}

/************************* ADD VIKING FLAG *********************************/

Boolean AddVikingFlag(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= SCANDINAVIA_ObjType_VikingFlag,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 659,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f)),
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	SetObjectCollisionBounds(newObj, 3000, -10, -100, 100, 100, -100);

	return(true);													// item was added
}


/************************* ADD WEAPONS RACK *********************************/

Boolean AddWeaponsRack(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= SCANDINAVIA_ObjType_WeaponsRack,
		.coord		= {x,0,z},	// y filled in below
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 400,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/4.0f)),
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Rotated(newObj,1,1);

	return(true);													// item was added
}


/************************* ADD BARACADE *********************************/

Boolean AddBaracade(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= SCANDINAVIA_ObjType_Baracade1 + itemPtr->parm[0],
		.coord		= {x,0,z},	// y filled in below
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 300,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/4.0f)),
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Rotated(newObj,1,1);

	return(true);													// item was added
}



/************************* ADD ROCK *********************************/

Boolean AddRock(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_GLOBAL,
		.type 		= GLOBAL_ObjType_GreyRock + itemPtr->parm[0],
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z) + 30.0f,
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 10,
		.moveCall 	= MoveStaticObject,
		.rot 		= RandomFloat() * PI2,
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,.8,1);

	return(true);													// item was added
}



/************************* ADD BRONTO NECK *********************************/

Boolean AddBrontoNeck(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.type 		= SKELETON_TYPE_BRONTONECK,
		.animNum	= 0,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z) + 500.0f,
		.flags 		= gAutoFadeStatusBits,
		.slot 		= SLOT_OF_DUMB+2,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f)),
		.scale 		= 50.0,
	};
	newObj = MakeNewSkeletonObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* CREATE ROCK AT BASE OF NECK */
			// So it doesn't look like the neck is hovering in the air.

	def.group	= MODEL_GROUP_GLOBAL;
	def.type	= GLOBAL_ObjType_GreyRock;
	def.coord.y	= GetTerrainY(x,z) + 50.0f;
	def.scale	= 1.5f;
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);
	newObj->Scale.x = 3;
	newObj->Scale.z = 3;
	UpdateObjectTransforms(newObj);


	return(true);													// item was added
}


/************************* ADD ROCK OVERHANG *********************************/

Boolean AddRockOverhang(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits,
		.slot 		= SLOT_OF_DUMB+3,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f)),
		.scale 		= 1.0,
	};

	switch(gTrackNum)
	{
		case	TRACK_NUM_DESERT:
				def.type 		= DESERT_ObjType_RockOverhang  + itemPtr->parm[1];
				break;

		case	TRACK_NUM_ICE:
				def.type 		= ICE_ObjType_IceBridge;
				break;

		default:
				return(true);
	}

	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


	return(true);													// item was added
}



/************************* ADD RICKSHAW *********************************/

Boolean AddRickshaw(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= CHINA_ObjType_Rickshaw,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 108,
		.moveCall 	= MoveStaticObject,
		.rot 		= 0,
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Rotated(newObj,.9,1);

	return(true);													// item was added
}


/********************** ADD AZTEC HEAD **************************/

Boolean AddAztecHead(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= AZTEC_ObjType_StoneHead,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 105,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f)),
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Maximized(newObj);


	return(true);													// item was added
}


/************************* ADD CASTLE TOWER *********************************/

Boolean AddCastleTower(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
Boolean	isSolid = itemPtr->parm[3] & 1;						// see if solid or not

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= EUROPE_ObjType_CastleTower + itemPtr->parm[0],
		.coord		= {x,0,z},	// y filled in below
		.flags 		= gAutoFadeStatusBits|STATUS_BIT_NOTEXTUREWRAP|STATUS_BIT_CLIPALPHA,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f)),
		.scale 		= 1.0,
	};

	if (isSolid)
	{
		def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0);
		def.slot 		= 40;
	}
	else
	{
		def.coord.y 	= GetTerrainY(x,z);
		def.slot 		= SLOT_OF_DUMB+4;
	}

	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	if (isSolid)
	{
		newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
		newObj->CBits			= CBITS_ALLSOLID;
		if (newObj->Type == EUROPE_ObjType_SiegeTower)
		{
			OGLMatrix3x3 rotMat;
			AllocateCollisionBoxMemory(newObj, 16);
			OGLMatrix3x3_SetRotate(&rotMat, -newObj->Rot.y);
			FillRotatedGridSubBoxes(newObj, &rotMat, newObj->CollisionBoxes,
									-424.7f, 424.7f, -1.3f, 1384.7f, -514.1f, 514.1f, 4, 4);
			KeepOldCollisionBoxes(newObj);
		}
		else if (newObj->Type == EUROPE_ObjType_Cannon)
		{
			OGLMatrix3x3 rotMat;
			AllocateCollisionBoxMemory(newObj, 12);
			OGLMatrix3x3_SetRotate(&rotMat, -newObj->Rot.y);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[0],
									-300.2f, 297.0f, 0.0f, 260.0f, -504.0f, 417.6f, 2, 3);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[6],
									-377.2f, 384.1f, 260.0f, 656.1f, -643.4f, 599.0f, 2, 3);
			KeepOldCollisionBoxes(newObj);
		}
		else
		{
			CreateCollisionBoxFromBoundingBox_Rotated(newObj, 1, 1);
		}
	}

	return(true);													// item was added
}


/************************** ADD HOUSE *******************************/

typedef struct
{
	short 	type[4];
	float	tweakXZ[4],tweakY[4];
}HouseInfo;

Boolean AddHouse(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
static const HouseInfo info[NUM_TRACKS] =
{
	[TRACK_NUM_DESERT] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_JUNGLE] =
	{
		{JUNGLE_ObjType_Hut1,JUNGLE_ObjType_Hut2,0,0},
		{.9,.9,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_ICE] =
	{
		{ICE_ObjType_Igloo,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_CRETE] =
	{
		{CRETE_ObjType_House1,CRETE_ObjType_House2,CRETE_ObjType_Palace,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_CHINA] =
	{
		{CHINA_ObjType_House,0,0,0},
		{.9,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_EGYPT] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_EUROPE] =
	{
		{EUROPE_ObjType_Cottage,EUROPE_ObjType_Lodge,EUROPE_ObjType_TownHouse,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_SCANDINAVIA] =
	{
		{SCANDINAVIA_ObjType_Cabin1,SCANDINAVIA_ObjType_Cabin2,SCANDINAVIA_ObjType_Cabin3,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_ATLANTIS] =
	{
		{ATLANTIS_ObjType_BugDome,ATLANTIS_ObjType_SaucerDome,ATLANTIS_ObjType_TwinkieDome,0},
		{1,1,1,1},
		{.55,.5,.55,1},
	},
};

Boolean	notSolid = itemPtr->parm[3] & 1;						// see if solid or not
short	type = SDL_clamp(itemPtr->parm[0], 0, 3);

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= info[gTrackNum].type[type],
		.coord		= {x,0,z},	// y filled in below
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/8.0f)),
		.scale 		= 1.0,
		.slot		= notSolid ? (SLOT_OF_DUMB+2) : 77,
		.flags 		= gAutoFadeStatusBits | STATUS_BIT_CLIPALPHA,
	};

	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0);
	if ((gTrackNum == TRACK_NUM_JUNGLE) && (def.type == JUNGLE_ObjType_Hut1))
		def.flags 		|= STATUS_BIT_KEEPBACKFACES;

	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	if (!notSolid)
	{
		newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
		newObj->CBits			= CBITS_ALLSOLID;
		if (gTrackNum == TRACK_NUM_EUROPE && newObj->Type == EUROPE_ObjType_TownHouse)
		{
			OGLMatrix3x3 rotMat;
			AllocateCollisionBoxMemory(newObj, 32);
			OGLMatrix3x3_SetRotate(&rotMat, -newObj->Rot.y);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[0],
									-1212.8f, 1212.8f, -29.4f, 600.0f, -1247.4f, 831.6f, 4, 4);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[16],
									-1316.7f, 1316.7f, 600.0f, 3712.8f, -1351.3f, 1351.3f, 4, 4);
			KeepOldCollisionBoxes(newObj);
		}
		else if (gTrackNum == TRACK_NUM_SCANDINAVIA && newObj->Type == SCANDINAVIA_ObjType_Cabin1)
		{
			OGLMatrix3x3 rotMat;
			AllocateCollisionBoxMemory(newObj, 8);
			OGLMatrix3x3_SetRotate(&rotMat, -newObj->Rot.y);
			SetRotatedSubCollisionBox(newObj, &rotMat, &newObj->CollisionBoxes[0],
									  -551.1f, 551.1f, 0.3f, 1043.0f, -1404.4f, -1000.0f);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[1],
									-1330.4f, 1330.4f, 0.3f, 1393.2f, -1000.0f, 1000.0f, 3, 2);
			SetRotatedSubCollisionBox(newObj, &rotMat, &newObj->CollisionBoxes[7],
									  -551.1f, 551.1f, 0.3f, 1043.0f, 1000.0f, 1404.4f);
			KeepOldCollisionBoxes(newObj);
		}
		else if (gTrackNum == TRACK_NUM_SCANDINAVIA && newObj->Type == SCANDINAVIA_ObjType_Cabin2)
		{
			OGLMatrix3x3 rotMat;
			AllocateCollisionBoxMemory(newObj, 16);
			OGLMatrix3x3_SetRotate(&rotMat, -newObj->Rot.y);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[0],
									-720.0f, 720.0f, -2.6f, 240.0f, -1170.0f, 1170.0f, 2, 4);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[8],
									-835.2f, 835.2f, 240.0f, 1428.4f, -1240.2f, 1240.2f, 2, 4);
			KeepOldCollisionBoxes(newObj);
		}
		else if (gTrackNum == TRACK_NUM_SCANDINAVIA && newObj->Type == SCANDINAVIA_ObjType_Cabin3)
		{
			OGLMatrix3x3 rotMat;
			AllocateCollisionBoxMemory(newObj, 16);
			OGLMatrix3x3_SetRotate(&rotMat, -newObj->Rot.y);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[0],
									-720.0f, 720.0f, -2.4f, 240.0f, -1260.0f, 1260.0f, 2, 4);
			FillRotatedGridSubBoxes(newObj, &rotMat, &newObj->CollisionBoxes[8],
									-760.0f, 760.0f, 240.0f, 1655.1f, -1330.2f, 1330.2f, 2, 4);
			KeepOldCollisionBoxes(newObj);
		}
		else if (gTrackNum == TRACK_NUM_ATLANTIS && newObj->Type == ATLANTIS_ObjType_SaucerDome)
		{
			static const LocalCollisionSubBox saucerDomeBoxes[] =
			{
				{ -1170.0f, 1170.0f,    2.3f,  720.0f,  -450.0f,  450.0f },
				{  -450.0f,  450.0f,    2.3f,  720.0f, -1170.0f, 1170.0f },
				{  -830.0f,  830.0f,    2.3f,  720.0f,  -830.0f,  830.0f },
				{ -2916.0f, 2916.0f,  720.0f, 1550.0f, -1116.0f, 1116.0f },
				{ -1116.0f, 1116.0f,  720.0f, 1550.0f, -2916.0f, 2916.0f },
				{ -2062.0f, 2062.0f,  720.0f, 1550.0f, -2062.0f, 2062.0f },
				{ -1712.0f, 1712.0f, 1550.0f, 2400.0f, -1712.0f, 1712.0f },
			};
			CreateRotatedSubCollisionBoxes(newObj, saucerDomeBoxes, (short)(sizeof(saucerDomeBoxes) / sizeof(saucerDomeBoxes[0])));
		}
		else
		{
			CreateCollisionBoxFromBoundingBox_Rotated(newObj,info[gTrackNum].tweakXZ[type],info[gTrackNum].tweakY[type]);
		}
	}

	return(true);													// item was added
}


/********************** ADD WELL **************************/

Boolean AddWell(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= EUROPE_ObjType_Well,
		.coord.x 	= x,
		.coord.z 	= z,
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 222,
		.moveCall 	= MoveStaticObject,
		.rot 		= 0,
		.scale 		= 1.0,
		.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);


	return(true);													// item was added
}


/********************** ADD CLOCK **************************/

Boolean AddClock(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= CRETE_ObjType_Clock,
		.coord		= {x,0,z},
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 420,
		.moveCall 	= MoveStaticObject,
		.rot 		= 0,
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);


	return(true);													// item was added
}


/********************** ADD CLAM **************************/

Boolean AddClam(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= ATLANTIS_ObjType_Clam,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= 0,
		.slot 		= 200,
		.moveCall 	= MoveStaticObject,
		.rot 		= RandomFloat() * PI2,
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);


	return(true);													// item was added
}



/********************** ADD FLAG POLE **************************/

Boolean AddFlagPole(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.type 		= SKELETON_TYPE_FLAG,
		.animNum	= 0,
		.coord		= {x,0,z},
		.flags 		= gAutoFadeStatusBits|STATUS_BIT_CLIPALPHA,
		.slot 		= 285,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f)),
		.scale 		= 10.0,
	};
	def.coord.y 	= GetTerrainY(x,z) - gObjectGroupBBoxList[MODEL_GROUP_SKELETONBASE+def.type][0].min.y;
	newObj = MakeNewSkeletonObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	newObj->Skeleton->AnimSpeed = 1.3f + RandomFloat();

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	SetObjectCollisionBounds(newObj, 2000, -10, -50, 50, 50, -50);


	return(true);													// item was added
}




#pragma mark -

typedef struct
{
	float x;
	float z;
} StoneQuadVertex;

typedef struct
{
	StoneQuadVertex verts[4];	/* base footprint quad in local XZ */
	float			minY;
	float			maxY;
	int				nz;
	int				nx;
} StonePylonSpec;

static void GetQuadXRangeForSlab(const StoneQuadVertex verts[4], float z0, float z1,
								 float *outMinX, float *outMaxX)
{
	float minX = 0.0f;
	float maxX = 0.0f;
	Boolean initialized = false;

	for (int i = 0; i < 4; i++)
	{
		const StoneQuadVertex *a = &verts[i];
		const StoneQuadVertex *b = &verts[(i + 1) & 3];

		if ((a->z >= z0) && (a->z <= z1))
		{
			if (!initialized)
			{
				minX = maxX = a->x;
				initialized = true;
			}
			else
			{
				if (a->x < minX) minX = a->x;
				if (a->x > maxX) maxX = a->x;
			}
		}

		const float dz = b->z - a->z;
		if (fabsf(dz) > 1e-4f)
		{
			const float cuts[2] = {z0, z1};
			for (int c = 0; c < 2; c++)
			{
				const float zc = cuts[c];
				if (((a->z < zc) && (b->z > zc)) || ((b->z < zc) && (a->z > zc)))
				{
					const float x = a->x + ((zc - a->z) / dz) * (b->x - a->x);
					if (!initialized)
					{
						minX = maxX = x;
						initialized = true;
					}
					else
					{
						if (x < minX) minX = x;
						if (x > maxX) maxX = x;
					}
				}
			}
		}
	}

	*outMinX = minX;
	*outMaxX = maxX;
}

static int AppendStonePylonBoxes(const ObjNode *theNode, const OGLMatrix3x3 *rotMat,
								 const StonePylonSpec *spec, CollisionBoxType *outBoxes, int maxBoxes)
{
	float zMin = spec->verts[0].z;
	float zMax = spec->verts[0].z;
	for (int i = 1; i < 4; i++)
	{
		if (spec->verts[i].z < zMin) zMin = spec->verts[i].z;
		if (spec->verts[i].z > zMax) zMax = spec->verts[i].z;
	}

	int count = 0;
	const float dz = (zMax - zMin) / (float)spec->nz;
	for (int iz = 0; iz < spec->nz; iz++)
	{
		const float za = zMin + (float)iz * dz;
		const float zb = (iz == spec->nz - 1) ? zMax : (za + dz);
		float xa, xb;
		GetQuadXRangeForSlab(spec->verts, za, zb, &xa, &xb);

		if (xb <= xa)
			continue;

		const float dx = (xb - xa) / (float)spec->nx;
		for (int ix = 0; ix < spec->nx; ix++)
		{
			const float x0 = xa + (float)ix * dx;
			const float x1 = (ix == spec->nx - 1) ? xb : (x0 + dx);
			GAME_ASSERT(count < maxBoxes);
			SetRotatedSubCollisionBox(theNode, rotMat, &outBoxes[count++],
									  x0, x1, spec->minY, spec->maxY, za, zb);
		}
	}

	return count;
}

static void CreateStoneHengeCollisionBoxes(ObjNode *theNode, short type)
{
	/* Base footprints extracted from Data/Models/stonehenge.bg3d so all stone
	   volumes remain solid at every rotation without oversized axis-aligned bounds. */
	static const StonePylonSpec postSpec =
	{
		.verts = {{-568.9f, -617.2f}, {556.1f, -542.2f}, {568.9f, 617.2f}, {-506.4f, 616.2f}},
		.minY  = -30.0f,
		.maxY  = 3160.0f,
		.nz    = 3,
		.nx    = 3,
	};

	/* Split each InnerHenge pillar into an outer body (nz=24, nx=2) and a fine inner-wall
	   strip (nz=64, nx=1) inset slightly from the base vertices so the car's axis-aligned
	   240x240 box at 45/135 degrees clears the archway cleanly with 0 interior gaps. */
	#define HENGE_INNER_INSET 70.0f
	#define HENGE_INNER_STRIP 180.0f
	#define HENGE_INNER_BACK  (HENGE_INNER_INSET + HENGE_INNER_STRIP)
	#define HENGE_OUTER_EDGE  (HENGE_INNER_BACK - 15.0f)
	static const StonePylonSpec innerSpecs[4] =
	{
		{
			.verts = {{-2026.3f, -659.5f}, {-251.8f - HENGE_OUTER_EDGE, -681.6f}, {-351.0f - HENGE_OUTER_EDGE, 538.3f}, {-2356.9f, 802.7f}},
			.minY  = -30.0f,
			.maxY  = 7430.0f,
			.nz    = 24,
			.nx    = 2,
		},
		{
			.verts = {{-251.8f - HENGE_INNER_BACK, -681.6f}, {-251.8f - HENGE_INNER_INSET, -681.6f}, {-351.0f - HENGE_INNER_INSET, 538.3f}, {-351.0f - HENGE_INNER_BACK, 538.3f}},
			.minY  = -30.0f,
			.maxY  = 7430.0f,
			.nz    = 64,
			.nx    = 1,
		},
		{
			.verts = {{271.7f + HENGE_INNER_INSET, -477.7f}, {271.7f + HENGE_INNER_BACK, -477.7f}, {172.5f + HENGE_INNER_BACK, 742.2f}, {172.5f + HENGE_INNER_INSET, 742.2f}},
			.minY  = -30.0f,
			.maxY  = 7430.0f,
			.nz    = 64,
			.nx    = 1,
		},
		{
			.verts = {{271.7f + HENGE_OUTER_EDGE, -477.7f}, {2277.6f, -742.2f}, {1947.0f, 720.1f}, {172.5f + HENGE_OUTER_EDGE, 742.2f}},
			.minY  = -30.0f,
			.maxY  = 7430.0f,
			.nz    = 24,
			.nx    = 2,
		},
	};
	#undef HENGE_OUTER_EDGE
	#undef HENGE_INNER_BACK
	#undef HENGE_INNER_STRIP
	#undef HENGE_INNER_INSET

	static const StonePylonSpec outerSpec =
	{
		.verts = {{-1239.0f, -882.4f}, {1135.8f, -913.4f}, {1239.0f, 748.2f}, {-1104.8f, 913.4f}},
		.minY  = -30.0f,
		.maxY  = 6495.0f,
		.nz    = 5,
		.nx    = 6,
	};

	enum { MAX_HENGE_BOXES = 255 };	/* ObjNode.NumCollisionBoxes is Byte (uint8_t) */
	CollisionBoxType tempBoxes[MAX_HENGE_BOXES];
	int count = 0;

	OGLMatrix3x3 rotMat;
	OGLMatrix3x3_SetRotate(&rotMat, -theNode->Rot.y);

	switch (type)
	{
		case 0: /* post */
			count = AppendStonePylonBoxes(theNode, &rotMat, &postSpec, tempBoxes, MAX_HENGE_BOXES);
			break;

		case 1: /* inner */
			for (int i = 0; i < (int)(sizeof(innerSpecs) / sizeof(innerSpecs[0])); i++)
				count += AppendStonePylonBoxes(theNode, &rotMat, &innerSpecs[i], tempBoxes + count, MAX_HENGE_BOXES - count);
			break;

		case 2: /* outer */
			count = AppendStonePylonBoxes(theNode, &rotMat, &outerSpec, tempBoxes, MAX_HENGE_BOXES);
			break;

		default:
			CreateCollisionBoxFromBoundingBox(theNode, 1, 1);
			return;
	}

	AllocateCollisionBoxMemory(theNode, (short)count);
	SDL_memcpy(theNode->CollisionBoxes, tempBoxes, (size_t)count * sizeof(CollisionBoxType));
	KeepOldCollisionBoxes(theNode);
}

/********************** ADD STONEHENGE **************************/

Boolean AddStoneHenge(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
short	type = itemPtr->parm[0];

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= STONEHENGE_ObjType_Post + type,
		.coord		= {x,0,z},	// y filled in below
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 100,
		.moveCall 	= MoveStaticObject,
		.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/64.0f)),
		.scale 		= 1.0,
	};
	def.coord.y 	= GetMinTerrainY(x,z, def.group, def.type, 1.0),
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;

	CreateStoneHengeCollisionBoxes(newObj, type);


	return(true);													// item was added
}






/********************** ADD COLISEUM **************************/

Boolean AddColiseum(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= COLISEUM_ObjType_Wall,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= STATUS_BIT_NOLIGHTING,
		.slot 		= 10,
		.moveCall 	= MoveStaticObject,
		.rot 		= 0,
		.scale 		= 1.06,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


	return(true);													// item was added
}






#pragma mark -

/************************* ADD VOLCANO *********************************/

Boolean AddVolcano(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= JUNGLE_ObjType_Volcano,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits,
		.slot 		= SLOT_OF_DUMB+4,
		.moveCall 	= MoveVolcano,
		.rot 		= 0,
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	newObj->SmokeTimer= 0;

	return(true);													// item was added
}


/********************* MOVE VOLCANO *******************************/

static void MoveVolcano(ObjNode *theNode)
{
float				top,x,z;
float				fps = gFramesPerSecondFrac;
int					particleGroup,magicNum,i;
NewParticleGroupDefType	groupDef;
NewParticleDefType	newParticleDef;
OGLVector3D			d;
OGLPoint3D			p;

	if (TrackTerrainItem(theNode))							// just check to see if it's gone
	{
		DeleteObject(theNode);
		return;
	}

	top = theNode->Coord.y + gObjectGroupBBoxList[theNode->Group][theNode->Type].max.y;
	x = theNode->Coord.x;
	z = theNode->Coord.z;

		/**************/
		/* MAKE SMOKE */
		/**************/

	if (gFramesPerSecond > 15.0f)										// only do smoke if running at good frame rate
	{
		theNode->SmokeTimer -= fps;													// see if add smoke
		if (theNode->SmokeTimer <= 0.0f)
		{
			theNode->SmokeTimer += 0.06;										// reset timer

			particleGroup 	= theNode->SmokeParticleGroup;
			magicNum 		= theNode->SmokeParticleMagic;

			if ((particleGroup == -1) || (!VerifyParticleGroupMagicNum(particleGroup, magicNum)))
			{

				theNode->SmokeParticleMagic = magicNum = MyRandomLong();			// generate a random magic num

				groupDef.magicNum				= magicNum;
				groupDef.type					= PARTICLE_TYPE_FALLINGSPARKS;
				groupDef.flags					= PARTICLE_FLAGS_DONTCHECKGROUND;
				groupDef.gravity				= 0;
				groupDef.magnetism				= 0;
				groupDef.baseScale				= 100;
				groupDef.decayRate				=  -.2f;
				groupDef.fadeRate				= .1;
				groupDef.particleTextureNum		= PARTICLE_SObjType_BlackSmoke;
				groupDef.srcBlend				= GL_SRC_ALPHA;
				groupDef.dstBlend				= GL_ONE_MINUS_SRC_ALPHA;
				theNode->SmokeParticleGroup = particleGroup = NewParticleGroup(&groupDef);
			}

			if (particleGroup != -1)
			{
				for (i = 0; i < 5; i++)
				{
					p.x = x + RandomFloat2() * 400.0f;
					p.y = top + RandomFloat() * 150.0f;
					p.z = z + RandomFloat2() * 400.0f;

					d.x = RandomFloat2() * 200.0f;
					d.y = 200.0f + RandomFloat() * 500.0f;
					d.z = RandomFloat2() * 200.0f;

					newParticleDef.groupNum		= particleGroup;
					newParticleDef.where		= &p;
					newParticleDef.delta		= &d;
					newParticleDef.scale		= RandomFloat() + 1.0f;
					newParticleDef.rotZ			= RandomFloat() * PI2;
					newParticleDef.rotDZ		= RandomFloat2();
					newParticleDef.alpha		= .8;
					if (AddParticleToGroup(&newParticleDef))
					{
						theNode->SmokeParticleGroup = -1;
						break;
					}
				}
			}
		}
	}


}

#pragma mark -

/************************* ADD TORCHPOT *********************************/

Boolean AddTorchPot(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	NewObjectDefinitionType def =
	{
		.group 		= MODEL_GROUP_LEVELSPECIFIC,
		.type 		= SCANDINAVIA_ObjType_TorchPot,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits,
		.slot 		= 400,
		.moveCall 	= MoveTorchPot,
		.rot 		= 0,
		.scale 		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	return(true);													// item was added
}


/********************* MOVE TORCHPOT **********************/

static void MoveTorchPot(ObjNode *theNode)
{

	if (TrackTerrainItem(theNode))							// just check to see if it's gone
	{
		DeleteObject(theNode);
		return;
	}

	BurnFire(theNode, theNode->Coord.x, theNode->Coord.y + 1000.0f, theNode->Coord.z, true, PARTICLE_SObjType_Fire, 2.0);

}

#pragma mark -

/************************ PRIME POLARBEAR *************************/

Boolean PrimePolarBear(long splineNum, SplineItemType *itemPtr)
{
ObjNode			*newObj;
float			x,z,placement;

			/* GET SPLINE INFO */

	placement = itemPtr->placement;
	GetCoordOnSpline(&(*gSplineList)[splineNum], placement, &x, &z);



		/* MAKE OBJECT */

	NewObjectDefinitionType def =
	{
		.type 		= SKELETON_TYPE_POLARBEAR,
		.animNum	= 0,
		.coord.x 	= x,
		.coord.y 	= GetTerrainY(x,z),
		.coord.z 	= z,
		.flags 		= STATUS_BIT_ONSPLINE|gAutoFadeStatusBits,
		.slot 		= 168,
		.rot 		= 0,
		.scale 		= BEAR_SCALE,
	};

	newObj = MakeNewSkeletonObject(&def);
	if (newObj == nil)
		return(false);

	DetachObject(newObj);									// detach this object from the linked list


	newObj->Skeleton->AnimSpeed = 1.5;


				/* SET MORE INFO */

	newObj->SplineItemPtr 	= itemPtr;
	newObj->SplineNum 		= splineNum;
	newObj->SplinePlacement = placement;
	newObj->SplineMoveCall 	= MovePolarBear;						// set move call
	newObj->CType			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;

	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	AttachShadowToObject(newObj, SHADOW_TYPE_CIRCULAR, 20, 30, false);


			/* ADD SPLINE OBJECT TO SPLINE OBJECT LIST */

	AddToSplineObjectList(newObj);
	return(true);
}


/******************** MOVE POLARBEAR ON SPLINE ***************************/

static void MovePolarBear(ObjNode *theNode)
{
Boolean isVisible;

	isVisible = IsSplineItemVisible(theNode);					// update its visibility


		/* MOVE ALONG THE SPLINE */

	IncreaseSplineIndex(theNode, 55);

	GetObjectCoordOnSpline(theNode, &theNode->Coord.x, &theNode->Coord.z);


			/***************************/
			/* UPDATE STUFF IF VISIBLE */
			/***************************/

	if (isVisible)
	{
		theNode->Rot.y = CalcYAngleFromPointToPoint(theNode->Rot.y, theNode->OldCoord.x, theNode->OldCoord.z,	// calc y rot aim
												theNode->Coord.x, theNode->Coord.z);

		theNode->Coord.y = GetTerrainY(theNode->Coord.x, theNode->Coord.z) - theNode->BottomOff;	// get ground Y
		UpdateObjectTransforms(theNode);												// update transforms
		CalcObjectBoxFromNode(theNode);													// update collision box
		theNode->Delta.x = (theNode->Coord.x - theNode->OldCoord.x) * gFramesPerSecond;	// calc deltas
		theNode->Delta.y = (theNode->Coord.y - theNode->OldCoord.y) * gFramesPerSecond;
		theNode->Delta.z = (theNode->Coord.z - theNode->OldCoord.z) * gFramesPerSecond;
		UpdateShadow(theNode);
	}
}


#pragma mark -

/********************** ADD FLOWER **************************/

Boolean AddFlower(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.type 		= SKELETON_TYPE_FLOWER,
		.animNum	= 0,
		.coord.x 	= x,
		.coord.z 	= z,
		.coord.y 	= GetTerrainY(x,z),
		.flags 		= gAutoFadeStatusBits|STATUS_BIT_CLIPALPHA,
		.slot 		= 70,
		.moveCall 	= MoveStaticObject,
		.rot 		= RandomFloat() * PI2,
		.scale 		= 20.0,
	};
	newObj = MakeNewSkeletonObject(&def);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	newObj->Skeleton->AnimSpeed = 1.0f + RandomFloat() * .5f;

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	SetObjectCollisionBounds(newObj, 1000, -10, -80, 80, 80, -80);


	return(true);													// item was added
}

#pragma mark -

/************************ PRIME VIKING *************************/

Boolean PrimeViking(long splineNum, SplineItemType *itemPtr)
{
ObjNode			*newObj;
float			x,z,placement;

			/* GET SPLINE INFO */

	placement = itemPtr->placement;
	GetCoordOnSpline(&(*gSplineList)[splineNum], placement, &x, &z);



		/* MAKE OBJECT */

	NewObjectDefinitionType def =
	{
		.type 		= SKELETON_TYPE_VIKING,
		.animNum	= 0,
		.coord.x 	= x,
		.coord.y 	= GetTerrainY(x,z),
		.coord.z 	= z,
		.flags 		= STATUS_BIT_ONSPLINE|gAutoFadeStatusBits,
		.slot 		= 168,
		.rot 		= 0,
		.scale 		= VIKING_SCALE,
	};

	newObj = MakeNewSkeletonObject(&def);
	if (newObj == nil)
		return(false);

	DetachObject(newObj);									// detach this object from the linked list


	newObj->Skeleton->AnimSpeed = 1.5;


				/* SET MORE INFO */

	newObj->SplineItemPtr 	= itemPtr;
	newObj->SplineNum 		= splineNum;
	newObj->SplinePlacement = placement;
	newObj->SplineMoveCall 	= MoveViking;						// set move call
	newObj->CType			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;

	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	AttachShadowToObject(newObj, SHADOW_TYPE_CIRCULAR, 7, 7, false);


			/* ADD SPLINE OBJECT TO SPLINE OBJECT LIST */

	AddToSplineObjectList(newObj);
	return(true);
}


/******************** MOVE VIKING ON SPLINE ***************************/

static void MoveViking(ObjNode *theNode)
{
Boolean isVisible;

	isVisible = IsSplineItemVisible(theNode);					// update its visibility


		/* MOVE ALONG THE SPLINE */

	IncreaseSplineIndex(theNode, 50);

	GetObjectCoordOnSpline(theNode, &theNode->Coord.x, &theNode->Coord.z);


			/***************************/
			/* UPDATE STUFF IF VISIBLE */
			/***************************/

	if (isVisible)
	{
		theNode->Rot.y = CalcYAngleFromPointToPoint(theNode->Rot.y, theNode->OldCoord.x, theNode->OldCoord.z,	// calc y rot aim
												theNode->Coord.x, theNode->Coord.z);

		theNode->Coord.y = GetTerrainY(theNode->Coord.x, theNode->Coord.z) - theNode->BottomOff;	// get ground Y
		UpdateObjectTransforms(theNode);												// update transforms
		CalcObjectBoxFromNode(theNode);													// update collision box
		theNode->Delta.x = (theNode->Coord.x - theNode->OldCoord.x) * gFramesPerSecond;	// calc deltas
		theNode->Delta.y = (theNode->Coord.y - theNode->OldCoord.y) * gFramesPerSecond;
		theNode->Delta.z = (theNode->Coord.z - theNode->OldCoord.z) * gFramesPerSecond;
		UpdateShadow(theNode);
	}
}

