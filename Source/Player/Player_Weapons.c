/****************************/
/*   	PLAYER_WEAPONS.C    */
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "network.h"

/****************************/
/*    PROTOTYPES            */
/****************************/

static void VehicleActivatePOW(ObjNode *theVehicle, Boolean forwardThrow);
static void MoveOilBullet(ObjNode *theNode);
static void MakeOilSpill(float x, float z);
static void MoveOilSpill(ObjNode *theNode);
static void PlayerThrowsWeapon(short playerNum, Boolean forwardThrow);
static void MoveBoneWeapon(ObjNode *theNode);
static void SetThrowDeltas(ObjNode *theProjectile, ObjNode *theCar, float force, Boolean throwForward);
static void SetThrowDeltas2(ObjNode *theProjectile, ObjNode *theCar, float force, Boolean throwForward);
static void MoveBirdBomb(ObjNode *theNode);
static void PlayerLaunchRomanCandle(short playerNum);
static void MoveRomanCandle(ObjNode *theNode);
static void PlayerLaunchBottleRocket(short playerNum, Boolean forwardThrow);
static void MoveBottleRocket(ObjNode *theNode);
static void ExplodeRomanCandle(ObjNode *theBullet, const OGLPoint3D *where);
static void ExplodeBottleRocket(ObjNode *theBullet, const OGLPoint3D *where);
static void PlayerLaunchTorpedo(short playerNum);
static void MoveTorpedo(ObjNode *theNode);
static void ExplodeTorpedo(ObjNode *theBullet, const OGLPoint3D *where);
static void MoveFreezeWeapon(ObjNode *theNode);
static void MoveLandMine(ObjNode *theNode);
static void DropLandMine(short playerNum);
static void SpawnWeaponTrailSparks(ObjNode *theNode, const OGLPoint3D *pos, float fps,
	float gravity, float decayRate, float fadeRate, Byte textureNum, int dstBlend,
	int count, float posSpreadXZ, float posSpreadY, Boolean symmetricY,
	float velSpreadXZ, float velSpreadY, float rotZRange);
static void SpawnExplosionSparkBurst(const OGLPoint3D *where, uint32_t flags, float gravity,
	float baseScale, float decayRate, float fadeRate, Byte textureNum,
	int count, float posSpreadXZ, float posOffsetY, float posSpreadY,
	float velSpreadXZ, float velSpreadY, float velBaseY, float rotDZ);


/****************************/
/*    CONSTANTS             */
/****************************/

#define	NITRO_DURATION		5.0f

#define	BIRD_BOMB_MAX_ATTACK_DIST	15000.0f
#define	BIRD_FLY_SPEED		6000.0f

#define	BOTTLE_ROCKET_SPEED			5500.0f
#define	BOTTLEROCKET_BLAST_RADIUS	600.0f

#define TORPEDO_SPEED			(MAX_SUBMARINE_SPEED + 1900.0f)
#define	TORPEDO_BLAST_RADIUS	600.0f



/*********************/
/*    VARIABLES      */
/*********************/

#define	TargetPlayer	Special[1]

#define	ParticleTimer	SpecialF[0]
#define	RomanCandleTimer	SpecialF[1]

#define	BottleRocketTimer	SpecialF[1]

#define	TorpedoTimer		SpecialF[1]

#define	FreezeBombTimer		SpecialF[1]

static const OGLPoint3D	gGunNozzelOff = {0,0,0};

#define	MineArmingTimer		SpecialF[0]


/******************* CHECK POWERUP CONTROLS ************************/
//
//

void CheckPOWControls(ObjNode *theNode)
{
short	playerNum = theNode->PlayerNum;

	if (gPlayerMultiPassCount > 0)							// if doing multipass and on secondary passes, then dont check "New" keys.
		return;

				/***************/
				/* SEE IF FIRE */
				/***************/

	if (GetControlStateNew(playerNum, kControlBit_ThrowForward))
		VehicleActivatePOW(theNode, true);
	else
	if (GetControlStateNew(playerNum, kControlBit_ThrowBackward))
		VehicleActivatePOW(theNode, false);

}


/*************************** VEHICLE ACTIVATE POWERUP **********************************/
//
// Called whenever a player presses the fire button.
//

static void VehicleActivatePOW(ObjNode *theVehicle, Boolean forwardThrow)
{
short		playerNum = theVehicle->PlayerNum;
short		powType;

			/* FIRST CHECK IF JUST DROP A FLAG */

	if (gGameMode == GAME_MODE_CAPTUREFLAG)
	{
		ObjNode	*theTorch = (ObjNode *)theVehicle->CapturedFlag;				// get torch object that player is carrying

		if (theTorch != nil)										// if have one, then drop it
		{
			PlayerDropFlag(theVehicle);
			return;
		}
	}


			/* PROCESS POWERUP TYPE */

	powType = gPlayerInfo[playerNum].powType;			// get current weapon type

	switch(powType)
	{
		case	POW_TYPE_NONE:
				break;

		case	POW_TYPE_BONE:
		case	POW_TYPE_OIL:
		case	POW_TYPE_BIRDBOMB:
		case	POW_TYPE_FREEZE:
				PlayerThrowsWeapon(playerNum, forwardThrow);
				break;

		case	POW_TYPE_ROMANCANDLE:
				PlayerLaunchRomanCandle(playerNum);
				break;

		case	POW_TYPE_BOTTLEROCKET:
				PlayerLaunchBottleRocket(playerNum, forwardThrow);
				break;

		case	POW_TYPE_TORPEDO:
				PlayerLaunchTorpedo(playerNum);
				break;

		case	POW_TYPE_NITRO:
				ActivateNitroPOW(playerNum);
				break;

		case	POW_TYPE_MINE:
				DropLandMine(playerNum);
				break;

		default:
				DoFatalAlert("VehicleActivatePOW: unknown powType");
	}

	if (gPlayerInfo[playerNum].sex == SKELETON_TYPE_PLAYER_HUMDINGER &&
		(powType == POW_TYPE_ROMANCANDLE || powType == POW_TYPE_BOTTLEROCKET ||
		 powType == POW_TYPE_TORPEDO || powType == POW_TYPE_MINE))
	{
		PlayEffect_Parms3DSynced(EFFECT_THROW_HUMDINGER, &gPlayerInfo[playerNum].coord, NORMAL_CHANNEL_RATE, 3.5f);
	}
}


/******************** ACTIVATE NITRO POW *******************/

void ActivateNitroPOW(short playerNum)
{
	DecCurrentPOWQuantity(playerNum);
	gPlayerInfo[playerNum].nitroTimer += NITRO_DURATION;		// give player more nitro

	PlayEffect_Parms3DSynced(EFFECT_NITRO, &gPlayerInfo[playerNum].coord, NORMAL_CHANNEL_RATE, 1.5);

}


/*********************** PLAYER THROWS WEAPON **************************/
//
// Called to start the throwing animation.
// Bullet doesn't actually get created until anim raises HEAD_THROW_READY_FLAG!
//

static void PlayerThrowsWeapon(short playerNum, Boolean forwardThrow)
{
	PlayerInfoType* pinfo = &gPlayerInfo[playerNum];
	ObjNode* head = pinfo->headObj;

	head->HEAD_THROW_READY_FLAG = false;

	if ((head->Skeleton->AnimNum != PLAYER_ANIM_THROWFORWARD) && (head->Skeleton->AnimNum != PLAYER_ANIM_THROWBACKWARD))
	{
#if _DEBUG
		GAME_ASSERT(pinfo->powTypeBeingThrown == POW_TYPE_NONE);
#endif

		if (forwardThrow)
			MorphToSkeletonAnim(head->Skeleton, PLAYER_ANIM_THROWFORWARD, 8.0);
		else
			MorphToSkeletonAnim(head->Skeleton, PLAYER_ANIM_THROWBACKWARD, 8.0);

		if (pinfo->sex == SKELETON_TYPE_PLAYER_HUMDINGER)
			PlayEffect_Parms3DSynced(EFFECT_THROW_HUMDINGER, &pinfo->coord, NORMAL_CHANNEL_RATE, 3.5f);
		else
		if (pinfo->sex >= SKELETON_TYPE_PLAYER_CHASE && pinfo->sex <= SKELETON_TYPE_PLAYER_ROCKY)
			PlayEffect_Parms3DSynced(EFFECT_THROW_CHASE, &pinfo->coord, NORMAL_CHANNEL_RATE, 1.5);
		else
		if (pinfo->sex == SKELETON_TYPE_PLAYER_FEMALE)
			PlayEffect_Parms3DSynced(EFFECT_THROW1 + RandomRange(0,2), &pinfo->coord, NORMAL_CHANNEL_RATE + 0x5000, 1.5);
		else
			PlayEffect_Parms3DSynced(EFFECT_THROW1 + RandomRange(0,2), &pinfo->coord, NORMAL_CHANNEL_RATE, 1.5);


			/* REMEMBER POW TYPE UNTIL ANIM IS READY TO CREATE BULLET */

		pinfo->powTypeBeingThrown = pinfo->powType;


			/* DEC THE INVENTORY */

		DecCurrentPOWQuantity(playerNum);
	}
}


/*********************** SET THROW DELTAS ************************/

static void SetThrowDeltas(ObjNode *theProjectile, ObjNode *theCar, float force, Boolean throwForward)
{
static const OGLVector3D	throwVector = {0,.4,-1};
static const OGLVector3D	throwVectorBack = {0,.3,1};

	if (throwForward)																	// rotate vector based on car orientation
	{
		OGLVector3D_Transform(&throwVector,	&theCar->BaseTransformMatrix, &theProjectile->Delta);
		theProjectile->Delta.x *= 3300.0f * force;															// give it speed
		theProjectile->Delta.y *= 1400.0f * force;
		theProjectile->Delta.z *= 3300.0f * force;
	}
	else
	{
		OGLVector3D_Transform(&throwVectorBack,	&theCar->BaseTransformMatrix, &theProjectile->Delta);
		theProjectile->Delta.x *= 1800.0f * force;															// give it speed
		theProjectile->Delta.y *= 1000.0f * force;
		theProjectile->Delta.z *= 1800.0f * force;
	}

	theProjectile->Delta.x += theCar->Delta.x;
	theProjectile->Delta.y += theCar->Delta.y;
	theProjectile->Delta.z += theCar->Delta.z;

}


/*********************** SET THROW DELTAS 2 ************************/

static void SetThrowDeltas2(ObjNode *theProjectile, ObjNode *theCar, float force, Boolean throwForward)
{
static const OGLVector3D	throwVector = {0,.6,-1};
static const OGLVector3D	throwVectorBack = {0,.6,1};

	if (throwForward)																	// rotate vector based on car orientation
	{
		OGLVector3D_Transform(&throwVector,	&theCar->BaseTransformMatrix, &theProjectile->Delta);
		theProjectile->Delta.x *= 3300.0f * force;															// give it speed
		theProjectile->Delta.y *= 3300.0f * force;
		theProjectile->Delta.z *= 3300.0f * force;
	}
	else
	{
		OGLVector3D_Transform(&throwVectorBack,	&theCar->BaseTransformMatrix, &theProjectile->Delta);
		theProjectile->Delta.x *= 1800.0f * force;															// give it speed
		theProjectile->Delta.y *= 1000.0f * force;
		theProjectile->Delta.z *= 1800.0f * force;
	}

	theProjectile->Delta.x += theCar->Delta.x;
	theProjectile->Delta.y += theCar->Delta.y;
	theProjectile->Delta.z += theCar->Delta.z;

}


#pragma mark -

/************************* THROW BONE *********************************/

void ThrowBone(short playerNum, Boolean throwForward)
{
ObjNode						*newObj,*head,*car;

	car = gPlayerInfo[playerNum].objNode;
	head = gPlayerInfo[playerNum].headObj;


		/********************/
		/* MAKE BONE OBJECT */
		/********************/

	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_WEAPONS,
		.type		= WEAPONS_ObjType_BoneBullet,
		.flags		= STATUS_BIT_AIMATCAMERA|STATUS_BIT_KEEPBACKFACES|STATUS_BIT_NOLIGHTING|STATUS_BIT_ROTXZY|STATUS_BIT_NOTEXTUREWRAP,
		.slot		= SLOT_OF_DUMB+2,
		.moveCall	= MoveBoneWeapon,
		.scale		= 1,
	};
	FindCoordOnJointAtFlagEvent(head, 5, &gGunNozzelOff, &def.coord);
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return;


			/* MAKE SHADOW */

	AttachShadowToObject(newObj, SHADOW_TYPE_CIRCULAR, 3.0, 3.0, true);


		/*****************/
		/* SET IN MOTION */
		/*****************/

	SetThrowDeltas(newObj, car, 1.0, throwForward);

	newObj->WhoThrew = (Ptr)car;														// remember who threw it
	Net_RegisterWeaponObj(newObj, 0);
}


/********************** MOVE BONE WEAPON **************************/

static void MoveBoneWeapon(ObjNode *theNode)
{
float	fps = gFramesPerSecondFrac;

	GetObjectInfo(theNode);

	ApplyFrictionToDeltas(fps * 500.0f, &gDelta);			// air friction

	gDelta.y -= 800.0f * fps;								// gravity

	gCoord.x += gDelta.x * fps;								// move it
	gCoord.y += gDelta.y * fps;
	gCoord.z += gDelta.z * fps;

	theNode->Rot.z += fps * 10.0f;


		/***********************/
		/* SEE IF HIT ANYTHING */
		/***********************/

	if ((gCoord.y <= GetTerrainY(gCoord.x, gCoord.z)) ||
		DoSimplePointCollision(&gCoord, CTYPE_MISC|CTYPE_PLAYER, (ObjNode *)theNode->WhoThrew))
	{
		MakeBombExplosion(theNode, gCoord.x, gCoord.z, &gDelta);
		DeleteObject(theNode);
		return;
	}

	UpdateObject(theNode);
}


#pragma mark -

/************************* THROW FREEZE *********************************/

void ThrowFreeze(short playerNum, Boolean throwForward)
{
ObjNode						*newObj,*head,*car;

	car = gPlayerInfo[playerNum].objNode;
	head = gPlayerInfo[playerNum].headObj;


		/********************/
		/* MAKE BONE OBJECT */
		/********************/

	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_WEAPONS,
		.type		= WEAPONS_ObjType_FreezeBullet,
		.flags		= STATUS_BIT_AIMATCAMERA|STATUS_BIT_KEEPBACKFACES|STATUS_BIT_GLOW|STATUS_BIT_NOLIGHTING|STATUS_BIT_NOTEXTUREWRAP,
		.slot		= PARTICLE_SLOT,
		.moveCall	= MoveFreezeWeapon,
		.scale		= 1
	};
	FindCoordOnJointAtFlagEvent(head, 5, &gGunNozzelOff, &def.coord);
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return;

	newObj->WhoThrew = (Ptr)car;														// remember who threw it


			/* MAKE SHADOW */

	AttachShadowToObject(newObj, SHADOW_TYPE_CIRCULAR, 4.0, 4.0, true);


		/*****************/
		/* SET IN MOTION */
		/*****************/

	SetThrowDeltas2(newObj, car, 1.0, throwForward);

	newObj->FreezeBombTimer = 1.7;


	PlayEffect_Parms3DSynced(EFFECT_SNOWBALL, &car->Coord, NORMAL_CHANNEL_RATE-0x4000, 4.0);
	Net_RegisterWeaponObj(newObj, 0);

}


/********************** SPAWN WEAPON TRAIL SPARKS **************************/

static void SpawnWeaponTrailSparks(ObjNode *theNode, const OGLPoint3D *pos, float fps,
	float gravity, float decayRate, float fadeRate, Byte textureNum, int dstBlend,
	int count, float posSpreadXZ, float posSpreadY, Boolean symmetricY,
	float velSpreadXZ, float velSpreadY, float rotZRange)
{
	int						particleGroup, magicNum;
	NewParticleGroupDefType	groupDef;
	NewParticleDefType		newParticleDef;

	theNode->ParticleTimer -= fps;
	if (theNode->ParticleTimer >= 0.0f)
		return;

	theNode->ParticleTimer += .03f;

	particleGroup 	= theNode->ParticleGroup;
	magicNum 		= theNode->ParticleMagicNum;

	if ((particleGroup == -1) || (!VerifyParticleGroupMagicNum(particleGroup, magicNum)))
	{
		theNode->ParticleMagicNum = magicNum = MyRandomLong();			// generate a random magic num

		groupDef.magicNum			= magicNum;
		groupDef.type				= PARTICLE_TYPE_FALLINGSPARKS;
		groupDef.flags				= PARTICLE_FLAGS_BOUNCE;
		groupDef.gravity			= gravity;
		groupDef.magnetism			= 0;
		groupDef.baseScale			= 20.0f;
		groupDef.decayRate			= decayRate;
		groupDef.fadeRate			= fadeRate;
		groupDef.particleTextureNum	= textureNum;
		groupDef.srcBlend			= GL_SRC_ALPHA;
		groupDef.dstBlend			= dstBlend;
		theNode->ParticleGroup = particleGroup = NewParticleGroup(&groupDef);
	}

	if (particleGroup != -1)
	{
		for (int i = 0; i < count; i++)
		{
			OGLPoint3D	p;
			OGLVector3D	d;

			p.x = pos->x + RandomFloat2() * posSpreadXZ;
			p.y = pos->y + (symmetricY ? RandomFloat2() : RandomFloat()) * posSpreadY;
			p.z = pos->z + RandomFloat2() * posSpreadXZ;

			d.x = RandomFloat2() * velSpreadXZ;
			d.y = RandomFloat2() * velSpreadY;
			d.z = RandomFloat2() * velSpreadXZ;

			newParticleDef.groupNum	= particleGroup;
			newParticleDef.where	= &p;
			newParticleDef.delta	= &d;
			newParticleDef.scale	= RandomFloat() + 1.0f;
			newParticleDef.rotZ		= RandomFloat() * rotZRange;
			newParticleDef.rotDZ	= RandomFloat2();
			newParticleDef.alpha	= 1.0f;
			if (AddParticleToGroup(&newParticleDef))
			{
				theNode->ParticleGroup = -1;
				break;
			}
		}
	}
}


/********************** MOVE FREEZE WEAPON **************************/

static void MoveFreezeWeapon(ObjNode *theNode)
{
float	fps = gFramesPerSecondFrac;
float	terrainY;


	GetObjectInfo(theNode);

	ApplyFrictionToDeltas(fps * 500.0f, &gDelta);			// air friction

	gDelta.y -= 2000.0f * fps;								// gravity

	gCoord.x += gDelta.x * fps;								// move it
	gCoord.y += gDelta.y * fps;
	gCoord.z += gDelta.z * fps;


		/* SEE IF TIME TO BOOM */

	theNode->FreezeBombTimer -= fps;
	if (theNode->FreezeBombTimer <= 0.0f)
	{
		goto boom;
	}

		/***********************/
		/* SEE IF HIT ANYTHING */
		/***********************/

	terrainY = GetTerrainY(gCoord.x, gCoord.z);

	if ((gCoord.y <= terrainY) || DoSimplePointCollision(&gCoord, CTYPE_MISC|CTYPE_PLAYER, (ObjNode *)theNode->WhoThrew))	// see if hit something
	{
boom:
		MakeSnowExplosion(&gCoord);
		DeleteObject(theNode);
		return;
	}

	UpdateObject(theNode);


			/***************/
			/* MAKE SPARKS */
			/***************/

	SpawnWeaponTrailSparks(theNode, &gCoord, fps,
		200.0f, 1.0f, 1.0f, PARTICLE_SObjType_SnowFlakes, GL_ONE,
		8, 20.0f, 60.0f, false, 20.0f, 10.0f, 10.0f);
}




#pragma mark -

/************************* THROW OIL *********************************/

void ThrowOil(short playerNum, Boolean throwForward)
{
ObjNode						*newObj,*head,*car;

	car = gPlayerInfo[playerNum].objNode;
	head = gPlayerInfo[playerNum].headObj;


		/***********************/
		/* MAKE OIL PROJECTILE */
		/***********************/

	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_WEAPONS,
		.type		= WEAPONS_ObjType_OilBullet,
		.flags		= 0,
		.slot		= 200,
		.moveCall 	= MoveOilBullet,
		.scale		= 1
	};
	FindCoordOnJointAtFlagEvent(head, 5, &gGunNozzelOff, &def.coord);

	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return;


			/* MAKE SHADOW */

	AttachShadowToObject(newObj, SHADOW_TYPE_CIRCULAR, 2.0, 2.0, true);


		/* SET IN MOTION */

	SetThrowDeltas(newObj, car, .6, throwForward);
	newObj->WhoThrew = (Ptr)car;														// remember who threw it
	Net_RegisterWeaponObj(newObj, 0);

}


/********************** MOVE OIL BULLET **************************/

static void MoveOilBullet(ObjNode *theNode)
{
float	fps = gFramesPerSecondFrac;

	GetObjectInfo(theNode);

	ApplyFrictionToDeltas(fps * 500.0f, &gDelta);			// air friction

	gDelta.y -= 1000.0f * fps;								// gravity

	gCoord.x += gDelta.x * fps;								// move it
	gCoord.y += gDelta.y * fps;
	gCoord.z += gDelta.z * fps;

	theNode->Rot.z += fps * 10.0f;


		/***********************/
		/* SEE IF HIT ANYTHING */
		/***********************/

	if ((gCoord.y <= GetTerrainY(gCoord.x, gCoord.z)) ||
		DoSimplePointCollision(&gCoord, CTYPE_MISC|CTYPE_PLAYER, (ObjNode *)theNode->WhoThrew))
	{
		MakeOilSpill(gCoord.x, gCoord.z);
		DeleteObject(theNode);
		return;
	}

	UpdateObject(theNode);
}



/************************ MAKE OIL SPILL **********************/

static void MakeOilSpill(float x, float z)
{
ObjNode	*newObj;

	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_WEAPONS,
		.type		= WEAPONS_ObjType_OilPatch,
		.coord.x	= x,
		.coord.y	= GetTerrainY(x, z),
		.coord.z	= z,
		.flags		= STATUS_BIT_NOZWRITES|STATUS_BIT_NOLIGHTING|STATUS_BIT_NOTEXTUREWRAP,
		.slot		= SLOT_OF_DUMB+9,
		.moveCall	= MoveOilSpill,
		.rot		= RandomFloat() * PI2,
		.scale		= 3.0f + RandomFloat() * 2.0f
	};
	newObj = MakeNewDisplayGroupObject(&def);

	newObj->CType = CTYPE_AVOID;

	RotateOnTerrain(newObj, 5.0, nil);							// set transform matrix
	SetObjectTransformMatrix(newObj);
	Net_RegisterWeaponObj(newObj, 0);
}


/********************* MOVE OIL SPILL ************************/

static void MoveOilSpill(ObjNode *theNode)
{
float	fps = gFramesPerSecondFrac;
int		i;
float	s,left,right,front,back;


			/* SCALE OUT */

	if (theNode->Scale.x < 10.0f)
		theNode->Scale.x = theNode->Scale.z += fps * 1.5f;
	s = theNode->Scale.x;
	RotateOnTerrain(theNode, 5.0, nil);							// set transform matrix
	SetObjectTransformMatrix(theNode);


		/**************************/
		/* SEE IF PLAYER IS ON IT */
		/**************************/

	left = theNode->Coord.x - (30.0f * s);
	right = theNode->Coord.x + (30.0f * s);
	front = theNode->Coord.z + (30.0f * s);
	back = theNode->Coord.z - (30.0f * s);

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		ObjNode				*player;
		CollisionBoxType	*collisionBox;

		if (gPlayerInfo[i].distToFloor > 5.0f)						// must be on ground
			continue;

		player = gPlayerInfo[i].objNode;
		if (!player)
			continue;

		if (player->Speed2D < 1600.0f)								// if going too slow, then dont bother
			continue;

		collisionBox = player->CollisionBoxes;						// point to player's collision box

		if (!collisionBox)
			continue;

		if (collisionBox[0].right < left)							// see if rectangles overlap
			continue;
		if (collisionBox[0].left > right)
			continue;
		if (collisionBox[0].front < back)
			continue;
		if (collisionBox[0].back > front)
			continue;

			/* PLAYER HIT THE SPILL */

		gPlayerInfo[i].greasedTiresTimer = 1.3;
		gPlayerInfo[i].isPlaning = true;
	}

}




#pragma mark -

/************************* THROW BIRD BOMB *********************************/

void ThrowBirdBomb(short playerNum, Boolean throwForward)
{
ObjNode		*newObj,*head, *car;
OGLVector2D	aimVec,targetVec;
float		bestAngle,r;
short		p,bestP;

	car = gPlayerInfo[playerNum].objNode;
	head = gPlayerInfo[playerNum].headObj;


		/********************/
		/* MAKE OBJECT */
		/********************/

	NewObjectDefinitionType def =
	{
		.moveCall 	= MoveBirdBomb,
		.type 		= SKELETON_TYPE_BIRDBOMB,
		.animNum 	= 0,
		.flags 		= STATUS_BIT_NOLIGHTING,
		.slot 		= SLOT_OF_DUMB+3,
		.rot		= car->Rot.y + (throwForward? 0: PI),
		.scale		= 1,
	};
	FindCoordOnJointAtFlagEvent(head, 5, &gGunNozzelOff, &def.coord);

	newObj = MakeNewSkeletonObject(&def);
	if (newObj == nil)
		DoFatalAlert("ThrowBirdBomb: MakeNewSkeletonObject failed!");

	newObj->Skeleton->AnimSpeed = 2.0f;
	newObj->Delta.y = car->Delta.y + 600.0f;

			/* MAKE SHADOW */

	AttachShadowToObject(newObj, SHADOW_TYPE_CIRCULAR, 6.0, 6.0, true);


	newObj->WhoThrew = (Ptr)car;														// remember who threw it
	Net_RegisterWeaponObj(newObj, 1);

				/**************************/
				/* FIND BEST TARGET AHEAD */
				/**************************/

	r = newObj->Rot.y;														// calc aim vector
	aimVec.x = -sin(r);
	aimVec.y = -cos(r);


	bestAngle = -1.0;
	bestP = -1;

	for (p = 0; p < gNumTotalPlayers; p++)									// check against all players
	{
		float	dot;

		if (p == playerNum)													// dont attack the thrower!
			continue;

		if (gPlayerInfo[p].isEliminated)
			continue;

		if ((gGameMode == GAME_MODE_CAPTUREFLAG) && (gPlayerInfo[p].team == gPlayerInfo[playerNum].team))
			continue;


			/* SEE IF AHEAD */

		targetVec.x = gPlayerInfo[p].coord.x - gPlayerInfo[playerNum].coord.x;		// calc vec from car to target
		targetVec.y = gPlayerInfo[p].coord.z - gPlayerInfo[playerNum].coord.z;
		FastNormalizeVector2D(targetVec.x, targetVec.y, &targetVec, false);

		dot = OGLVector2D_Dot(&aimVec, &targetVec);							// calc angle between
		if (dot < 0.0f)														// see if not aiming at it enough
			continue;
		if (dot < bestAngle)												// see if best aim so far
			continue;

			/* THIS IS THE BEST PICK SO FAR */

		bestAngle = dot;
		bestP = p;
	}

	newObj->TargetPlayer = bestP;

}


/********************** MOVE BIRD BOMB **************************/

static void MoveBirdBomb(ObjNode *theNode)
{
float		fps = gFramesPerSecondFrac;
float		r,dx,dz;
short		p = theNode->TargetPlayer;
float		y;
ObjNode		*who = (ObjNode *)theNode->WhoThrew;

	GetObjectInfo(theNode);




			/*************************/
			/* MOVE AFTER THE TARGET */
			/*************************/

				/* AIM AT TARGET */

	if (p != -1)
		TurnObjectTowardTarget(theNode, &gCoord, gPlayerInfo[p].coord.x, gPlayerInfo[p].coord.z, PI2, false);

	r = theNode->Rot.y;														// calc new aim vector
	dx = -sin(r);
	dz = -cos(r);

	gDelta.x = dx * BIRD_FLY_SPEED;
	gDelta.z = dz * BIRD_FLY_SPEED;

	y = GetTerrainY(gCoord.x, gCoord.z) + 250.0f;						// get target y

	gDelta.y += (y - gCoord.y) * 2.0f * fps;

	gCoord.x += gDelta.x * fps;
	gCoord.y += gDelta.y * fps;
	gCoord.z += gDelta.z * fps;


			/* SEE IF HIT GROUND */

	if (gCoord.y <= GetTerrainY(gCoord.x, gCoord.z))
	{
		MakeBombExplosion((ObjNode *)theNode->WhoThrew, gCoord.x, gCoord.z, &gDelta);
		DeleteObject(theNode);
		return;
	}


			/* IF CLOSE TO ANY PLAYER, THEN BOOM */

	for (p = 0; p < gNumTotalPlayers; p++)									// check against all players
	{
		if (p == who->PlayerNum)											// dont attack the thrower!
			continue;

		if (CalcDistance3D(gCoord.x, gCoord.y, gCoord.z, gPlayerInfo[p].coord.x,  gPlayerInfo[p].coord.y, gPlayerInfo[p].coord.z) < 220.0f)
		{
			MakeBombExplosion((ObjNode *)theNode->WhoThrew, gCoord.x, gCoord.z, &gDelta);
			DeleteObject(theNode);
			return;
		}
	}

	UpdateObject(theNode);


			/************************/
			/* UPDATE SOUND EFFECT */
			/************************/

	if (theNode->EffectChannel != -1)
		Update3DSoundChannel(EFFECT_BIRDCAW, &theNode->EffectChannel, &gCoord);
	else
		theNode->EffectChannel = PlayEffect_Parms3D(EFFECT_BIRDCAW, &gCoord, NORMAL_CHANNEL_RATE, 1.0);

}


#pragma mark -

/**************** PLAYER LAUNCH ROMAN CANDLE ************************/

static void PlayerLaunchRomanCandle(short playerNum)
{
ObjNode		*newObj, *car;

	DecCurrentPOWQuantity(playerNum);

	car = gPlayerInfo[playerNum].objNode;


		/********************/
		/* MAKE OBJECT 		*/
		/********************/

	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_WEAPONS,
		.type		= WEAPONS_ObjType_RomanCandleBullet,
		.coord.x	= car->Coord.x,
		.coord.y	= car->Coord.y + 100.0f,
		.coord.z	= car->Coord.z,
		.flags		= STATUS_BIT_AIMATCAMERA|STATUS_BIT_KEEPBACKFACES|STATUS_BIT_GLOW|STATUS_BIT_NOLIGHTING|STATUS_BIT_NOTEXTUREWRAP,
		.slot		= PARTICLE_SLOT,
		.moveCall	= MoveRomanCandle,
		.rot		= 0,
		.scale		= 1,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return;

	newObj->WhoThrew = (Ptr)car;														// remember who threw it

	newObj->Delta.y = 3000.0f;

	newObj->Mode = 0;																	// shoot up

	PlayEffect_Parms3DSynced(EFFECT_ROMANCANDLE_LAUNCH, &def.coord, NORMAL_CHANNEL_RATE, 1.0f);
	Net_RegisterWeaponObj(newObj, 0);
}



/******************* MOVE ROMAN CANDLE ****************************/

static void MoveRomanCandle(ObjNode *theNode)
{
float fps = gFramesPerSecondFrac;
ObjNode		*car;
Boolean		makeSparks = false;
short		targetP;
float		dist;

	car = (ObjNode *)theNode->WhoThrew;			// point to car who shot it


	switch(theNode->Mode)
	{
				/***************/
				/* SHOOTING UP */
				/***************/

		case	0:
				GetObjectInfo(theNode);
				gCoord.y += gDelta.y * fps;

				if (gCoord.y > (car->Coord.y + 5000.0f))	// see if nice and high now
				{
					theNode->Mode = 1;						// go into coast mode
					theNode->RomanCandleTimer = 1.0;
					theNode->StatusBits |= STATUS_BIT_HIDDEN;	// hide while coasting
				}
				else
					makeSparks = true;
				UpdateObject(theNode);
				break;

					/*********/
					/* COAST */
					/*********/

		case	1:
				theNode->RomanCandleTimer -= fps;
				if (theNode->RomanCandleTimer <= 0.0f)
				{
					theNode->Mode = 2;							// fall
					theNode->StatusBits &= ~STATUS_BIT_HIDDEN;	// hide while coasting

					targetP = FindClosestPlayer(car, car->Coord.x, car->Coord.z, ROMAN_CANDLE_RANGE, true, &dist);	// see who's closest
					if (targetP == -1)							// if nobody else to hit, then hit ourselves
						targetP = car->PlayerNum;

					theNode->Coord.x = gPlayerInfo[targetP].objNode->Coord.x;
					theNode->Coord.y = gPlayerInfo[targetP].objNode->Coord.y + 4000.0f;
					theNode->Coord.z = gPlayerInfo[targetP].objNode->Coord.z;

					theNode->Delta.x = gPlayerInfo[targetP].objNode->Delta.x;	// match target's dx,dz
					theNode->Delta.z = gPlayerInfo[targetP].objNode->Delta.z;
					theNode->Delta.y = -4000;

					theNode->EffectChannel = PlayEffect_Parms3DSynced(EFFECT_ROMANCANDLE_FALL, &theNode->Coord, NORMAL_CHANNEL_RATE, 1.0f);  // XXX: this was gNewObjectDefinition.coord! mistake?

				}
				break;


				/****************/
				/* FALLING DOWN */
				/****************/

		case	2:
				makeSparks = true;
				GetObjectInfo(theNode);
				gCoord.x += gDelta.x * fps;
				gCoord.y += gDelta.y * fps;
				gCoord.z += gDelta.z * fps;

				if ((gCoord.y <= GetTerrainY(gCoord.x, gCoord.z)) || DoSimplePointCollision(&gCoord, CTYPE_MISC|CTYPE_PLAYER, car))
				{
					ExplodeRomanCandle(theNode, &gCoord);
					DeleteObject(theNode);
					return;
				}

				UpdateObject(theNode);

				if (theNode->EffectChannel != -1)
					Update3DSoundChannel(EFFECT_ROMANCANDLE_FALL, &theNode->EffectChannel, &theNode->Coord);

				break;


	}


			/***************/
			/* MAKE SPARKS */
			/***************/

	if (makeSparks)
	{
		SpawnWeaponTrailSparks(theNode, &gCoord, fps,
			200.0f, 1.0f, 1.0f, PARTICLE_SObjType_Fire, GL_ONE,
			8, 30.0f, 80.0f, true, 30.0f, 20.0f, 10.0f);
	}

}


/******************** SPAWN EXPLOSION SPARK BURST ****************************/

static void SpawnExplosionSparkBurst(const OGLPoint3D *where, uint32_t flags, float gravity,
	float baseScale, float decayRate, float fadeRate, Byte textureNum,
	int count, float posSpreadXZ, float posOffsetY, float posSpreadY,
	float velSpreadXZ, float velSpreadY, float velBaseY, float rotDZ)
{
	long				pg;
	NewParticleDefType	newParticleDef;

	gNewParticleGroupDef.magicNum			= 0;
	gNewParticleGroupDef.type				= PARTICLE_TYPE_FALLINGSPARKS;
	gNewParticleGroupDef.flags				= flags;
	gNewParticleGroupDef.gravity			= gravity;
	gNewParticleGroupDef.magnetism			= 0;
	gNewParticleGroupDef.baseScale			= baseScale;
	gNewParticleGroupDef.decayRate			= decayRate;
	gNewParticleGroupDef.fadeRate			= fadeRate;
	gNewParticleGroupDef.particleTextureNum	= textureNum;
	gNewParticleGroupDef.srcBlend			= GL_SRC_ALPHA;
	gNewParticleGroupDef.dstBlend			= GL_ONE;
	pg = NewParticleGroup(&gNewParticleGroupDef);
	if (pg == -1)
		return;

	for (int i = 0; i < count; i++)
	{
		OGLPoint3D	pt;
		OGLVector3D	d;

		pt.x = where->x + RandomFloat2() * posSpreadXZ;
		pt.y = where->y + posOffsetY + RandomFloat2() * posSpreadY;
		pt.z = where->z + RandomFloat2() * posSpreadXZ;

		d.x = velSpreadXZ != 0.0f ? RandomFloat2() * velSpreadXZ : 0.0f;
		d.y = velBaseY + (velSpreadY != 0.0f ? RandomFloat2() * velSpreadY : 0.0f);
		d.z = velSpreadXZ != 0.0f ? RandomFloat2() * velSpreadXZ : 0.0f;

		newParticleDef.groupNum	= pg;
		newParticleDef.where	= &pt;
		newParticleDef.delta	= &d;
		newParticleDef.scale	= RandomFloat() + 1.5f;
		newParticleDef.rotZ		= 0;
		newParticleDef.rotDZ	= rotDZ;
		newParticleDef.alpha	= FULL_ALPHA + (RandomFloat() * .3f);
		AddParticleToGroup(&newParticleDef);
	}
}


/******************** PLAY WEAPON EXPLOSION FX ****************************/

void PlayWeaponExplosionFX(short explosionKind, const OGLPoint3D *where)
{
	if (!where)
		return;

	if (gIsNetworkHost)
		Net_HostSendExplosion(where->x, where->y, where->z, explosionKind);

	switch (explosionKind)
	{
		case kNetExplosion_RomanCandle:
			SpawnExplosionSparkBurst(where, PARTICLE_FLAGS_BOUNCE, 1000.0f, 20.0f, 1.0f, 0.0f,
				PARTICLE_SObjType_WhiteSpark, 200, 60.0f, 60.0f, 60.0f, 800.0f, 800.0f, 0.0f, 0.0f);
			SpawnExplosionSparkBurst(where, PARTICLE_FLAGS_BOUNCE, 1000.0f, 20.0f, 1.0f, 0.0f,
				PARTICLE_SObjType_RedSpark, 200, 60.0f, 60.0f, 60.0f, 1000.0f, 1000.0f, 0.0f, 30.0f);
			PlayEffect_Parms3D(EFFECT_BOOM, where, NORMAL_CHANNEL_RATE*3/2, 4);
			break;

		case kNetExplosion_BottleRocket:
			SpawnExplosionSparkBurst(where, PARTICLE_FLAGS_BOUNCE, 1000.0f, 20.0f, 1.0f, 0.0f,
				PARTICLE_SObjType_RedSpark, 230, 60.0f, 60.0f, 60.0f, 800.0f, 800.0f, 0.0f, 0.0f);
			SpawnExplosionSparkBurst(where, PARTICLE_FLAGS_BOUNCE, 1000.0f, 20.0f, 1.0f, 0.0f,
				PARTICLE_SObjType_BlueSpark, 230, 60.0f, 60.0f, 60.0f, 1000.0f, 1000.0f, 0.0f, 30.0f);
			PlayEffect_Parms3D(EFFECT_BOOM, where, NORMAL_CHANNEL_RATE*3/2, 4);
			break;

		case kNetExplosion_Torpedo:
			SpawnExplosionSparkBurst(where, PARTICLE_FLAGS_DONTCHECKGROUND, -40.0f, 5.0f, 0.0f, 0.2f,
				PARTICLE_SObjType_Bubbles, 100, 160.0f, 0.0f, 160.0f, 0.0f, 0.0f, 800.0f, 0.0f);
			SpawnExplosionSparkBurst(where, PARTICLE_FLAGS_BOUNCE, 0.0f, 20.0f, 0.0f, 2.0f,
				PARTICLE_SObjType_WhiteSpark, 200, 60.0f, 0.0f, 60.0f, 500.0f, 500.0f, 0.0f, 0.0f);
			PlayEffect_Parms3D(EFFECT_BOOM, where, NORMAL_CHANNEL_RATE/2, 4);
			break;

		case kNetExplosion_LandMine:
			MakeSparkExplosion(where->x, where->y + 50.0f, where->z, 60.0f, PARTICLE_SObjType_Fire);
			MakeConeBlast(where->x, where->y, where->z);
			PlayEffect_Parms3D(EFFECT_BOOM, where, NORMAL_CHANNEL_RATE + 0x1000, 4);
			break;
	}
}


/******************** UPDATE CLIENT PROJECTILE TRAIL ****************************/

void UpdateClientProjectileTrail(ObjNode *theNode)
{
	float		fps = gFramesPerSecondFrac;
	OGLPoint3D	bubblePt;

	if (!theNode)
		return;

	if (theNode->StatusBits & STATUS_BIT_HIDDEN)
	{
		if (theNode->EffectChannel != -1)
			StopAChannel(&theNode->EffectChannel);
		return;
	}

	if (theNode->Genre == SKELETON_GENRE && theNode->Type == SKELETON_TYPE_BIRDBOMB)
	{
		if (theNode->EffectChannel != -1)
			Update3DSoundChannel(EFFECT_BIRDCAW, &theNode->EffectChannel, &theNode->Coord);
		else
			theNode->EffectChannel = PlayEffect_Parms3D(EFFECT_BIRDCAW, &theNode->Coord, NORMAL_CHANNEL_RATE, 1.0f);
		return;
	}

	switch (theNode->Type)
	{
		case WEAPONS_ObjType_FreezeBullet:
			SpawnWeaponTrailSparks(theNode, &theNode->Coord, fps,
				200.0f, 1.0f, 1.0f, PARTICLE_SObjType_SnowFlakes, GL_ONE,
				8, 20.0f, 60.0f, false, 20.0f, 10.0f, 10.0f);
			break;

		case WEAPONS_ObjType_RomanCandleBullet:
			SpawnWeaponTrailSparks(theNode, &theNode->Coord, fps,
				200.0f, 1.0f, 1.0f, PARTICLE_SObjType_Fire, GL_ONE,
				8, 30.0f, 80.0f, true, 30.0f, 20.0f, 10.0f);
			break;

		case WEAPONS_ObjType_BottleRocketBullet:
			SpawnWeaponTrailSparks(theNode, &theNode->Coord, fps,
				0.0f, -0.4f, 1.5f, PARTICLE_SObjType_GreySmoke, GL_ONE_MINUS_SRC_ALPHA,
				4, 20.0f, 20.0f, true, 20.0f, 20.0f, 4.0f);
			break;

		case WEAPONS_ObjType_Torpedo:
			bubblePt = theNode->Coord;
			theNode->ParticleTimer -= fps;
			if (theNode->ParticleTimer < 0.0f)
			{
				theNode->ParticleTimer += .03f;
				MakeBubbles(theNode, &bubblePt, .15, 2.5);
			}
			if (theNode->EffectChannel != -1)
				Update3DSoundChannel(EFFECT_HUM, &theNode->EffectChannel, &theNode->Coord);
			else
				theNode->EffectChannel = PlayEffect_Parms3D(EFFECT_HUM, &theNode->Coord, NORMAL_CHANNEL_RATE + 0x2000, 1.0f);
			break;
	}
}


/******************** EXPLODE ROMAN CANDLE ****************************/

static void ExplodeRomanCandle(ObjNode *theBullet, const OGLPoint3D *where)
{
ObjNode					*whoThrew;

	PlayWeaponExplosionFX(kNetExplosion_RomanCandle, where);

		/* SEE IF IT HIT ANY CARS */

	whoThrew = (ObjNode *)theBullet->WhoThrew;
	BlastCars(whoThrew->PlayerNum, where->x, where->y, where->z, BOTTLEROCKET_BLAST_RADIUS);

}



#pragma mark -

/**************** PLAYER LAUNCH BOTTLE ROCKET ************************/

static void PlayerLaunchBottleRocket(short playerNum, Boolean forwardThrow)
{
ObjNode		*newObj, *car;
float		r,dist;
short		targetP;

	DecCurrentPOWQuantity(playerNum);

	car = gPlayerInfo[playerNum].objNode;


		/********************/
		/* MAKE OBJECT 		*/
		/********************/

	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_WEAPONS,
		.type		= WEAPONS_ObjType_BottleRocketBullet,
		.coord.x	= car->Coord.x,
		.coord.y	= car->Coord.y + 120.0f,
		.coord.z	= car->Coord.z,
		.flags		= 0,
		.slot		= SLOT_OF_DUMB,
		.moveCall	= MoveBottleRocket,
		.scale		= 1,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return;

	newObj->WhoThrew = (Ptr)car;														// remember who threw it
	newObj->BottleRocketTimer = 4.0;
	Net_RegisterWeaponObj(newObj, 0);

	newObj->Rot.x = -PI/2.7f;
	newObj->DeltaRot.x = 1.0f;

		/* FIND TARGET TO SHOOT AT */

	if (forwardThrow)
		targetP = FindClosestPlayerInFront(car, BOTTLE_ROCKET_RANGE, true, &dist, 0);	// see who's closest
	else
		targetP = FindClosestPlayerInBack(car, BOTTLE_ROCKET_RANGE, true, &dist, 0);	// see who's closest

	newObj->TargetPlayer = targetP;
	if (targetP == -1)										// if nobody else to hit then just shoot straight
	{
		r = newObj->Rot.y = car->Rot.y;
		if (!forwardThrow)
			r += PI;

		newObj->Delta.x = gDelta.x - sin(r) * BOTTLE_ROCKET_SPEED;
		newObj->Delta.z = gDelta.z - cos(r) * BOTTLE_ROCKET_SPEED;
		newObj->Rot.x = -PI/3;
	}
	else
	{
		r = CalcYAngleFromPointToPoint(car->Rot.y, car->Coord.x, car->Coord.z, gPlayerInfo[targetP].coord.x, gPlayerInfo[targetP].coord.z);
		newObj->Rot.y = r;

		newObj->Delta.x = gDelta.x - sin(r) * BOTTLE_ROCKET_SPEED;
		newObj->Delta.z = gDelta.z - cos(r) * BOTTLE_ROCKET_SPEED;
	}

	newObj->Delta.y = gDelta.y + 2000.0f;


	PlayEffect_Parms3DSynced(EFFECT_ROMANCANDLE_LAUNCH, &def.coord, NORMAL_CHANNEL_RATE, 1.0f);
}


/******************* MOVE BOTTLE ROCKET ****************************/

static void MoveBottleRocket(ObjNode *theNode)
{
float 			fps = gFramesPerSecondFrac;
ObjNode			*car;
static const OGLVector3D up = {0,1,0};
OGLMatrix4x4	m;
short			targetP;

	GetObjectInfo(theNode);
	car = (ObjNode *)theNode->WhoThrew;			// point to car who shot it

	theNode->BottleRocketTimer -= fps;						// see if time to explode
	if (theNode->BottleRocketTimer <= 0.0f)
		goto boom;


		/* TURN TOWARD TARGET */

	targetP = theNode->TargetPlayer;
	if (targetP != -1)
		TurnObjectTowardTarget(theNode, &gCoord, gPlayerInfo[targetP].coord.x, gPlayerInfo[targetP].coord.z, PI, false);


		/* TILT DOWN */

	theNode->DeltaRot.x -= fps * 1.0f;
	if (theNode->DeltaRot.x < 0.0f)
		theNode->DeltaRot.x = 0;

	theNode->Rot.x -= theNode->DeltaRot.x * fps;
	if (theNode->Rot.x < -PI)
		theNode->Rot.x = -PI;



		/* DO PHYSICS */

	OGLMatrix4x4_SetRotate_XYZ(&m, theNode->Rot.x, theNode->Rot.y, theNode->Rot.z);
	OGLVector3D_Transform(&up, &m, &gDelta);

	gDelta.x *= BOTTLE_ROCKET_SPEED;
	gDelta.y *= BOTTLE_ROCKET_SPEED;
	gDelta.z *= BOTTLE_ROCKET_SPEED;

	gCoord.x += gDelta.x * fps;
	gCoord.y += gDelta.y * fps;
	gCoord.z += gDelta.z * fps;

		/* SEE IF HIT */

	if ((gCoord.y <= GetTerrainY(gCoord.x, gCoord.z)) || DoSimplePointCollision(&gCoord, CTYPE_MISC|CTYPE_PLAYER, car))
	{
boom:
		ExplodeBottleRocket(theNode, &gCoord);
		DeleteObject(theNode);
		return;
	}

	UpdateObject(theNode);



			/**************/
			/* MAKE SMOKE */
			/**************/

	SpawnWeaponTrailSparks(theNode, &gCoord, fps,
		0.0f, -0.4f, 1.5f, PARTICLE_SObjType_GreySmoke, GL_ONE_MINUS_SRC_ALPHA,
		4, 20.0f, 20.0f, true, 20.0f, 20.0f, 4.0f);

}


/******************** EXPLODE BOTTLE ROCKET ****************************/

static void ExplodeBottleRocket(ObjNode *theBullet, const OGLPoint3D *where)
{
ObjNode					*whoThrew;

	PlayWeaponExplosionFX(kNetExplosion_BottleRocket, where);


		/* SEE IF IT HIT ANY CARS */

	whoThrew = (ObjNode *)theBullet->WhoThrew;
	BlastCars(whoThrew->PlayerNum, where->x, where->y, where->z, BOTTLEROCKET_BLAST_RADIUS);

}


#pragma mark -

/**************** PLAYER LAUNCH TORPEDO ************************/

static void PlayerLaunchTorpedo(short playerNum)
{
ObjNode		*newObj, *car;
static const OGLPoint3D	nose = {0,0,-100};

	DecCurrentPOWQuantity(playerNum);

	car = gPlayerInfo[playerNum].objNode;


		/********************/
		/* MAKE OBJECT 		*/
		/********************/


	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_WEAPONS,
		.type		= WEAPONS_ObjType_Torpedo,
		.slot		= SLOT_OF_DUMB,
		.moveCall	= MoveTorpedo,
		.rot		= car->Rot.y,
		.scale		= 1,
	};
	OGLPoint3D_Transform(&nose, &car->BaseTransformMatrix, &def.coord);
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return;

	newObj->WhoThrew = (Ptr)car;														// remember who threw it
	newObj->TorpedoTimer = 10.0;

	newObj->Rot = car->Rot;																// match rotation



	PlayEffect_Parms3DSynced(EFFECT_TORPEDOFIRE, &def.coord, NORMAL_CHANNEL_RATE, 3);
	Net_RegisterWeaponObj(newObj, 0);

}


/******************* MOVE TORPEDO ****************************/

static void MoveTorpedo(ObjNode *theNode)
{
float 			fps = gFramesPerSecondFrac;
ObjNode			*car;
static const OGLVector3D go = {0,0,-1};
OGLMatrix4x4	m;
short			targetP;
float			ydiff,targetRotX,dist;
OGLVector3D		aimVec;
OGLPoint3D		bubblePt;


	GetObjectInfo(theNode);
	car = (ObjNode *)theNode->WhoThrew;						// point to car who shot it



	theNode->TorpedoTimer -= fps;						// see if time to explode
	if (theNode->TorpedoTimer <= 0.0f)
		goto boom;


		/* FIND CLOSES TARGET TO AIM AT */

	targetP = theNode->TargetPlayer = FindClosestPlayerInFront(theNode, TORPEDO_RANGE, true, &dist, .1);					// see who's closest


		/* TURN TOWARD TARGET */

	if (targetP != -1)
	{
		TurnObjectTowardTarget(theNode, &gCoord, gPlayerInfo[targetP].coord.x, gPlayerInfo[targetP].coord.z, PI, false);

		ydiff = gPlayerInfo[targetP].coord.y - gCoord.y;				// see if @ same y
		if (fabs(ydiff) < 50.0f)
		{
			targetRotX = 0;
		}
		else
		if (ydiff > 0.0f)
		{
			targetRotX = .5;
		}
		else
			targetRotX = -.5;

		if (theNode->Rot.x < targetRotX)
		{
			theNode->Rot.x += fps * 1.0f;
			if (theNode->Rot.x > targetRotX)
				theNode->Rot.x = targetRotX;
		}
		else
		if (theNode->Rot.x > targetRotX)
		{
			theNode->Rot.x -= fps * 1.0f;
			if (theNode->Rot.x < targetRotX)
				theNode->Rot.x = targetRotX;
		}

	}


		/* DO MOTION */

	OGLMatrix4x4_SetRotate_XYZ(&m, theNode->Rot.x, theNode->Rot.y, theNode->Rot.z);
	OGLVector3D_Transform(&go, &m, &aimVec);

	gDelta.x = aimVec.x * TORPEDO_SPEED;
	gDelta.y = aimVec.y * TORPEDO_SPEED;
	gDelta.z = aimVec.z * TORPEDO_SPEED;

	gCoord.x += gDelta.x * fps;
	gCoord.y += gDelta.y * fps;
	gCoord.z += gDelta.z * fps;


		/* SEE IF HIT */

	if ((gCoord.y <= GetTerrainY(gCoord.x, gCoord.z)) || DoSimplePointCollision(&gCoord, CTYPE_MISC|CTYPE_PLAYER, car))
	{
boom:
		ExplodeTorpedo(theNode, &gCoord);
		DeleteObject(theNode);
		return;
	}

	UpdateObject(theNode);



			/***************/
			/* MAKE BUBBLES */
			/***************/

	bubblePt.x = gCoord.x - aimVec.x * 200.0f;
	bubblePt.y = gCoord.y - aimVec.y * 200.0f;
	bubblePt.z = gCoord.z - aimVec.z * 200.0f;

	theNode->ParticleTimer -= fps;
	if (theNode->ParticleTimer < 0.0f)
	{
		theNode->ParticleTimer += .03f;

		MakeBubbles(theNode, &bubblePt, .15, 2.5);
	}

			/************************/
			/* UPDATE TORPEDO EFFECT */
			/************************/

	if (theNode->EffectChannel != -1)
		Update3DSoundChannel(EFFECT_HUM, &theNode->EffectChannel, &gCoord);
	else
		theNode->EffectChannel = PlayEffect_Parms3D(EFFECT_HUM, &gCoord, NORMAL_CHANNEL_RATE + 0x2000, 1.0);

}


/******************** EXPLODE TORPEDO ****************************/

static void ExplodeTorpedo(ObjNode *theBullet, const OGLPoint3D *where)
{
ObjNode					*whoThrew;

	PlayWeaponExplosionFX(kNetExplosion_Torpedo, where);


		/* SEE IF IT HIT ANY CARS */

	whoThrew = (ObjNode *)theBullet->WhoThrew;
	BlastCars(whoThrew->PlayerNum, where->x, where->y, where->z, TORPEDO_BLAST_RADIUS);

}


#pragma mark -


/************************* DROP LAND MINE *********************************/

static void DropLandMine(short playerNum)
{
ObjNode						*newObj;

	DecCurrentPOWQuantity(playerNum);

	NewObjectDefinitionType def =
	{
		.group		= MODEL_GROUP_WEAPONS,
		.type		= WEAPONS_ObjType_LandMine,
		.coord.x	= gCoord.x,
		.coord.y	= GetTerrainY(gCoord.x, gCoord.z),
		.coord.z	= gCoord.z,
		.slot		= TRIGGER_SLOT,
		.moveCall	= MoveLandMine,
		.scale		= 1.0,
	};
	newObj = MakeNewDisplayGroupObject(&def);
	if (newObj == nil)
		return;

	newObj->CType 			= CTYPE_TRIGGER|CTYPE_AVOID;
	newObj->Kind		 	= TRIGTYPE_LANDMINE;
	newObj->CBits			= CBITS_ALLSOLID;
	newObj->TriggerSides 	= ALL_SOLID_SIDES;				// side(s) to activate it
	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	RotateOnTerrain(newObj, 0, nil);							// set transform matrix
	SetObjectTransformMatrix(newObj);

	newObj->MineArmingTimer = 1.0;							// arming timer

	PlayEffect_Parms3DSynced(EFFECT_MINE, &newObj->Coord, NORMAL_CHANNEL_RATE, 2);
	Net_RegisterWeaponObj(newObj, 0);

}


/********************* MOVE LAND MINE ************************/

static void MoveLandMine(ObjNode *theNode)
{


			/***************/
			/* SEE IF GONE */
			/***************/

	if (TrackTerrainItem(theNode) && (theNode->MineArmingTimer < -30.0f))		// see if out of range and timed out
	{
		DeleteObject(theNode);
		return;
	}

	theNode->MineArmingTimer -= gFramesPerSecondFrac;		// dec arming timer

}


/************** DO TRIGGER - LAND MINE ********************/
//
// OUTPUT: True = want to handle trigger as a solid object
//

Boolean DoTrig_LandMine(ObjNode *theNode, ObjNode *whoNode, Byte sideBits)
{

	(void) sideBits;

	if (theNode->MineArmingTimer > 0.0f)					// do nothing if not armed yet
		return(false);

		/* MAKE CAR SPIN WILDLY */

	whoNode->DeltaRot.y = RandomFloat2() * 20.0f;
	whoNode->DeltaRot.z = RandomFloat2() * 10.0f;


		/* MAKE EXPLOSION */

	PlayWeaponExplosionFX(kNetExplosion_LandMine, &theNode->Coord);


			/* DELETE THE MINE */

	DeleteObject(theNode);

	gDelta.y += 1400.0f;				// pop up the guy who hit the it
	gDelta.x *= .3f;					// slow
	gDelta.z *= .3f;

	return(false);
}










