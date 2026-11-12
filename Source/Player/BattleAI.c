//
// BattleAI.c
// Pre-computed arena graph decision making and local whisker steering for
// Tag1, Tag2, Survival, and Capture the Flag (Quest for Fire) CPU players.
//

#include "game.h"
#include "battleai.h"
#include "battlenav.h"
#include "terrain.h"
#include "triggers.h"

#include <math.h>


typedef struct BattleAIDiffParams
{
	float	replanInterval;
	float	fleeAwarenessDist;
	float	pickupMaxDist;
	float	leadTime;
	float	aimWobbleAmp;
} BattleAIDiffParams;

static const BattleAIDiffParams kDiffParams[4] =
{
	/* DIFFICULTY_SIMPLISTIC */	{ 0.55f,  3600.0f, 3200.0f, 0.00f, 0.11f },
	/* DIFFICULTY_EASY */		{ 0.35f,  6000.0f, 4800.0f, 0.12f, 0.06f },
	/* DIFFICULTY_MEDIUM */		{ 0.20f,  9500.0f, 6800.0f, 0.24f, 0.02f },
	/* DIFFICULTY_HARD */		{ 0.12f, 14000.0f, 8200.0f, 0.36f, 0.00f }
};

static const BattleAIDiffParams *GetDiffParams(void)
{
	short d = gGamePrefs.difficulty;
	if (d < DIFFICULTY_SIMPLISTIC)
		d = DIFFICULTY_SIMPLISTIC;
	else if (d > DIFFICULTY_HARD)
		d = DIFFICULTY_HARD;
	return(&kDiffParams[d]);
}

static float NavDistPoints(float x1, float z1, float x2, float z2)
{
	float euclid = CalcQuickDistance(x1, z1, x2, z2);
	int16_t n1 = BattleNav_PointToNode(x1, z1);
	int16_t n2 = BattleNav_PointToNode(x2, z2);

	if ((n1 >= 0) && (n2 >= 0))
	{
		if (n1 == n2)
			return(euclid);

		{
			float dNav = BattleNav_GetPathDist(n1, n2);
			if (dNav < 1e8f)
				return(dNav);
		}
	}

	return(euclid);
}

static Boolean IsValidEnemyTarget(short selfPlayer, short candPlayer,
									Boolean ignoreInvisible, float proximityOverrideDist)
{
	PlayerInfoType *cand;

	if ((candPlayer < 0) || (candPlayer >= gNumTotalPlayers) || (candPlayer == selfPlayer))
		return(false);

	cand = &gPlayerInfo[candPlayer];
	if ((cand->objNode == nil) || cand->isEliminated)
		return(false);

	if ((gGameMode == GAME_MODE_CAPTUREFLAG) && (cand->team == gPlayerInfo[selfPlayer].team))
		return(false);

	if (ignoreInvisible && (cand->invisibilityTimer > 0.0f))
	{
		ObjNode *selfCar = gPlayerInfo[selfPlayer].objNode;
		if ((selfCar == nil) ||
			(CalcQuickDistance(selfCar->Coord.x, selfCar->Coord.z,
							   cand->objNode->Coord.x, cand->objNode->Coord.z) > proximityOverrideDist))
			return(false);
	}

	return(true);
}

static short FindNearestThreatOrTarget(short selfPlayer, float curX, float curZ,
									   short skipPlayer, Boolean ignoreInvisible,
									   float *outDist)
{
	short bestIdx = -1;
	float bestDist = 1e9f;
	short i;

	for (i = 0; i < gNumTotalPlayers; i++)
	{
		float d;
		if (i == skipPlayer)
			continue;
		if (!IsValidEnemyTarget(selfPlayer, i, ignoreInvisible, 1300.0f))
			continue;

		d = NavDistPoints(curX, curZ,
						  gPlayerInfo[i].objNode->Coord.x,
						  gPlayerInfo[i].objNode->Coord.z);
		if (d < bestDist)
		{
			bestDist = d;
			bestIdx = i;
		}
	}

	if (outDist)
		*outDist = bestDist;
	return(bestIdx);
}

static Boolean HasHiddenTriggerNear(float x, float z, float radiusSq)
{
	ObjNode *node = gFirstNodePtr;

	while (node != nil)
	{
		if ((node->Slot == TRIGGER_SLOT) &&
			((node->Kind == TRIGTYPE_POW) ||
			 (node->Kind == TRIGTYPE_INVISIBILITY) ||
			 (node->Kind == TRIGTYPE_TRACTION) ||
			 (node->Kind == TRIGTYPE_SUSPENSION) ||
			 (node->Kind == TRIGTYPE_DRUID)))
		{
			float dx = node->Coord.x - x;
			float dz = node->Coord.z - z;
			if ((dx * dx + dz * dz) <= radiusSq)
				return((Boolean)(node->Flag[0] != 0));
		}
		node = node->NextNode;
	}

	return(false);
}

static Boolean HasUsableWeapon(const PlayerInfoType *pinfo)
{
	return((pinfo->powQuantity > 0) && (pinfo->powType != POW_TYPE_NONE));
}

static Boolean IsGoalIgnored(const PlayerInfoType *pinfo, float x, float z)
{
	if (pinfo->aiIgnoreTimer <= 0.0f)
		return(false);
	return(CalcQuickDistance(pinfo->aiIgnoreX, pinfo->aiIgnoreZ, x, z) <= 600.0f);
}

static Boolean FindBestPickup(short selfPlayer, float curX, float curZ,
							  float maxDist, float *outX, float *outZ, float *outDist)
{
	const PlayerInfoType *pinfo = &gPlayerInfo[selfPlayer];
	float bestScore = 1e9f;
	float bestRawDist = 1e9f;
	Boolean found = false;
	ObjNode *node = gFirstNodePtr;
	uint16_t pIdx;

	while (node != nil)
	{
		if ((node->Slot == TRIGGER_SLOT) && (node->Flag[0] == 0) &&
			((node->Kind == TRIGTYPE_POW) ||
			 (node->Kind == TRIGTYPE_INVISIBILITY) ||
			 (node->Kind == TRIGTYPE_TRACTION) ||
			 (node->Kind == TRIGTYPE_SUSPENSION) ||
			 (node->Kind == TRIGTYPE_DRUID)))
		{
			float groundY = GetTerrainY(node->Coord.x, node->Coord.z);
			float d;

			/* Skip floating mid-air POWs (e.g. Battle_Ramps high flyers) & already-active Druid or orbited pickups */
			if ((node->Coord.y - groundY) > 450.0f)
			{
				node = node->NextNode;
				continue;
			}
			if ((node->Kind == TRIGTYPE_DRUID) && (pinfo->stickyTiresTimer > 10.0f))
			{
				node = node->NextNode;
				continue;
			}
			if (IsGoalIgnored(pinfo, node->Coord.x, node->Coord.z))
			{
				node = node->NextNode;
				continue;
			}

			d = NavDistPoints(curX, curZ, node->Coord.x, node->Coord.z);
			if (d <= maxDist)
			{
				float score = d;
				if (!HasUsableWeapon(pinfo) && (node->Kind == TRIGTYPE_POW))
					score -= 700.0f;

				if (score < bestScore)
				{
					bestScore = score;
					bestRawDist = d;
					*outX = node->Coord.x;
					*outZ = node->Coord.z;
					found = true;
				}
			}
		}
		node = node->NextNode;
	}

	if (gBattleNav.loaded && gBattleNav.pickups)
	{
		for (pIdx = 0; pIdx < gBattleNav.numPickups; pIdx++)
		{
			const BattleNavPickup *pk = &gBattleNav.pickups[pIdx];
			float d;

			if (HasHiddenTriggerNear(pk->x, pk->z, 500.0f * 500.0f))
				continue;
			if (IsGoalIgnored(pinfo, pk->x, pk->z))
				continue;

			d = NavDistPoints(curX, curZ, pk->x, pk->z);
			if (d <= maxDist)
			{
				float score = d + 200.0f;	/* slight tie-break in favor of verified live triggers */
				if (!HasUsableWeapon(pinfo) && (pk->itemType == 5))
					score -= 700.0f;

				if (score < bestScore)
				{
					bestScore = score;
					bestRawDist = d;
					*outX = pk->x;
					*outZ = pk->z;
					found = true;
				}
			}
		}
	}

	if (outDist)
		*outDist = bestRawDist;
	return(found);
}

static void PickPatrolHub(short playerNum, float curX, float curZ,
						  float avoidX, float avoidZ, Boolean hasAvoid,
						  float *outX, float *outZ)
{
	uint16_t i;
	int16_t bestNode = -1;
	float bestScore = -1e18f;

	if (!gBattleNav.loaded || (gBattleNav.numNodes == 0))
	{
		*outX = curX;
		*outZ = curZ;
		return;
	}

	for (i = 0; i < gBattleNav.numNodes; i++)
	{
		const BattleNavNode *node = &gBattleNav.nodes[i];
		float dSelf = CalcQuickDistance(curX, curZ, node->x, node->z);
		float score = 0.0f;

		if (dSelf < 1800.0f)
			continue;

		if (node->flags & BN_NODE_HUB)
			score += 1500.0f;
		if (node->flags & BN_NODE_DEADEND)
			score -= 3000.0f;

		score += node->clearance;
		score -= 0.15f * dSelf;

		if (hasAvoid)
			score += 0.6f * CalcQuickDistance(avoidX, avoidZ, node->x, node->z);

		/* Deterministic player-index salt keeps bots spread out across hubs */
		score += (float)(((i * 37 + playerNum * 53) & 15) * 60);

		if (score > bestScore)
		{
			bestScore = score;
			bestNode = (int16_t)i;
		}
	}

	if (bestNode >= 0)
	{
		*outX = gBattleNav.nodes[bestNode].x;
		*outZ = gBattleNav.nodes[bestNode].z;
	}
	else
	{
		*outX = gBattleNav.nodes[0].x;
		*outZ = gBattleNav.nodes[0].z;
	}
}

static void ComputeInterceptGoal(short targetPlayer, float curX, float curZ,
								 float leadTime, float *outX, float *outZ)
{
	ObjNode *targetCar = gPlayerInfo[targetPlayer].objNode;
	float tx, tz, dist, t, lx, lz;

	if (targetCar == nil)
	{
		*outX = curX;
		*outZ = curZ;
		return;
	}

	tx = targetCar->Coord.x;
	tz = targetCar->Coord.z;

	if (leadTime > 0.0f)
	{
		dist = CalcQuickDistance(curX, curZ, tx, tz);
		if (dist < 3200.0f)
		{
			t = dist / 4200.0f;
			if (t > leadTime)
				t = leadTime;
			lx = tx + targetCar->Delta.x * t;
			lz = tz + targetCar->Delta.z * t;
			if (BattleNav_IsWalkablePoint(lx, lz))
			{
				*outX = lx;
				*outZ = lz;
				return;
			}
		}
	}

	*outX = tx;
	*outZ = tz;
}

static void UpdateTag1Decision(ObjNode *theCar, short playerNum,
							   const BattleAIDiffParams *params)
{
	PlayerInfoType *pinfo = &gPlayerInfo[playerNum];
	float curX = theCar->Coord.x;
	float curZ = theCar->Coord.z;

	if (pinfo->isIt)
	{
		short skip = (gReTagTimer > 0.0f) ? gWhoWasIt : -1;
		float targetDist = 1e9f;
		short target = FindNearestThreatOrTarget(playerNum, curX, curZ, skip, true, &targetDist);

		if (target < 0)
			target = FindNearestThreatOrTarget(playerNum, curX, curZ, -1, false, &targetDist);

		if (target >= 0)
		{
			pinfo->aiMode = AI_MODE_CHASE_TARGET;
			pinfo->aiTargetPlayer = target;
			ComputeInterceptGoal(target, curX, curZ, params->leadTime,
								 &pinfo->aiGoalX, &pinfo->aiGoalZ);
			return;
		}
	}
	else if ((gWhoIsIt >= 0) && (gWhoIsIt < gNumTotalPlayers) &&
			 (gPlayerInfo[gWhoIsIt].objNode != nil) && !gPlayerInfo[gWhoIsIt].isEliminated)
	{
		ObjNode *itCar = gPlayerInfo[gWhoIsIt].objNode;
		float itDist = NavDistPoints(curX, curZ, itCar->Coord.x, itCar->Coord.z);
		Boolean itVisible = (gPlayerInfo[gWhoIsIt].invisibilityTimer <= 0.0f) ||
							(CalcQuickDistance(curX, curZ, itCar->Coord.x, itCar->Coord.z) <= 1500.0f);

		if (itVisible && (itDist < params->fleeAwarenessDist))
		{
			pinfo->aiMode = AI_MODE_FLEE_THREAT;
			pinfo->aiTargetPlayer = gWhoIsIt;
			BattleNav_FindEscapeWaypoint(curX, curZ, itCar->Coord.x, itCar->Coord.z,
										 pinfo->aiEscapeX, pinfo->aiEscapeZ,
										 &pinfo->aiGoalX, &pinfo->aiGoalZ);
			pinfo->aiEscapeX = pinfo->aiGoalX;
			pinfo->aiEscapeZ = pinfo->aiGoalZ;
			return;
		}

		if (!HasUsableWeapon(pinfo))
		{
			float pkX, pkZ, pkDist;
			if (FindBestPickup(playerNum, curX, curZ, params->pickupMaxDist, &pkX, &pkZ, &pkDist))
			{
				pinfo->aiMode = AI_MODE_SEEK_PICKUP;
				pinfo->aiGoalX = pkX;
				pinfo->aiGoalZ = pkZ;
				return;
			}
		}

		pinfo->aiMode = AI_MODE_ROAM_PATROL;
		PickPatrolHub(playerNum, curX, curZ, itCar->Coord.x, itCar->Coord.z, itVisible,
					  &pinfo->aiGoalX, &pinfo->aiGoalZ);
		return;
	}

	pinfo->aiMode = AI_MODE_ROAM_PATROL;
	PickPatrolHub(playerNum, curX, curZ, 0.0f, 0.0f, false, &pinfo->aiGoalX, &pinfo->aiGoalZ);
}

static void UpdateTag2Decision(ObjNode *theCar, short playerNum,
							   const BattleAIDiffParams *params)
{
	PlayerInfoType *pinfo = &gPlayerInfo[playerNum];
	float curX = theCar->Coord.x;
	float curZ = theCar->Coord.z;

	if (pinfo->isIt)
	{
		float threatDist = 1e9f;
		short threat = FindNearestThreatOrTarget(playerNum, curX, curZ, -1, false, &threatDist);

		if (threat >= 0)
		{
			ObjNode *threatCar = gPlayerInfo[threat].objNode;

			if ((threatDist > 5200.0f) && !HasUsableWeapon(pinfo))
			{
				float pkX, pkZ, pkDist;
				if (FindBestPickup(playerNum, curX, curZ, 3000.0f, &pkX, &pkZ, &pkDist) &&
					(NavDistPoints(pkX, pkZ, threatCar->Coord.x, threatCar->Coord.z) > 2500.0f))
				{
					pinfo->aiMode = AI_MODE_SEEK_PICKUP;
					pinfo->aiTargetPlayer = threat;
					pinfo->aiGoalX = pkX;
					pinfo->aiGoalZ = pkZ;
					return;
				}
			}

			pinfo->aiMode = AI_MODE_FLEE_THREAT;
			pinfo->aiTargetPlayer = threat;
			BattleNav_FindEscapeWaypoint(curX, curZ, threatCar->Coord.x, threatCar->Coord.z,
										 pinfo->aiEscapeX, pinfo->aiEscapeZ,
										 &pinfo->aiGoalX, &pinfo->aiGoalZ);
			pinfo->aiEscapeX = pinfo->aiGoalX;
			pinfo->aiEscapeZ = pinfo->aiGoalZ;
			return;
		}
	}
	else if ((gWhoIsIt >= 0) && (gWhoIsIt < gNumTotalPlayers) &&
			 (gPlayerInfo[gWhoIsIt].objNode != nil) && !gPlayerInfo[gWhoIsIt].isEliminated)
	{
		ObjNode *itCar = gPlayerInfo[gWhoIsIt].objNode;
		float itDist = NavDistPoints(curX, curZ, itCar->Coord.x, itCar->Coord.z);
		Boolean itVisible = (gPlayerInfo[gWhoIsIt].invisibilityTimer <= 0.0f) ||
							(CalcQuickDistance(curX, curZ, itCar->Coord.x, itCar->Coord.z) <= 1800.0f);

		if (itVisible)
		{
			if (!HasUsableWeapon(pinfo) && (itDist > 5000.0f))
			{
				float pkX, pkZ, pkDist;
				if (FindBestPickup(playerNum, curX, curZ, 2400.0f, &pkX, &pkZ, &pkDist))
				{
					pinfo->aiMode = AI_MODE_SEEK_PICKUP;
					pinfo->aiGoalX = pkX;
					pinfo->aiGoalZ = pkZ;
					return;
				}
			}

			pinfo->aiMode = AI_MODE_CHASE_TARGET;
			pinfo->aiTargetPlayer = gWhoIsIt;
			ComputeInterceptGoal(gWhoIsIt, curX, curZ, params->leadTime,
								 &pinfo->aiGoalX, &pinfo->aiGoalZ);
			return;
		}
	}

	pinfo->aiMode = AI_MODE_ROAM_PATROL;
	PickPatrolHub(playerNum, curX, curZ, 0.0f, 0.0f, false, &pinfo->aiGoalX, &pinfo->aiGoalZ);
}

static void UpdateSurvivalDecision(ObjNode *theCar, short playerNum,
								   const BattleAIDiffParams *params)
{
	PlayerInfoType *pinfo = &gPlayerInfo[playerNum];
	float curX = theCar->Coord.x;
	float curZ = theCar->Coord.z;
	float threatDist = 1e9f;
	short closestThreat = FindNearestThreatOrTarget(playerNum, curX, curZ, -1, true, &threatDist);
	short bestTarget = -1;
	float bestTargetScore = 1e9f;
	short i;

	if ((pinfo->health < 0.30f) && (closestThreat >= 0) && (threatDist < params->fleeAwarenessDist))
	{
		ObjNode *threatCar = gPlayerInfo[closestThreat].objNode;
		if (!HasUsableWeapon(pinfo) || (gPlayerInfo[closestThreat].health > pinfo->health))
		{
			pinfo->aiMode = AI_MODE_FLEE_THREAT;
			pinfo->aiTargetPlayer = closestThreat;
			BattleNav_FindEscapeWaypoint(curX, curZ, threatCar->Coord.x, threatCar->Coord.z,
										 pinfo->aiEscapeX, pinfo->aiEscapeZ,
										 &pinfo->aiGoalX, &pinfo->aiGoalZ);
			pinfo->aiEscapeX = pinfo->aiGoalX;
			pinfo->aiEscapeZ = pinfo->aiGoalZ;
			return;
		}
	}

	if (!HasUsableWeapon(pinfo))
	{
		float pkX, pkZ, pkDist;
		if (FindBestPickup(playerNum, curX, curZ, params->pickupMaxDist, &pkX, &pkZ, &pkDist))
		{
			Boolean closeWeakEnemy = (closestThreat >= 0) &&
									 (threatDist < 2200.0f) &&
									 (gPlayerInfo[closestThreat].health < 0.45f) &&
									 (pkDist > threatDist);
			if (!closeWeakEnemy)
			{
				pinfo->aiMode = AI_MODE_SEEK_PICKUP;
				pinfo->aiGoalX = pkX;
				pinfo->aiGoalZ = pkZ;
				return;
			}
		}
	}

	for (i = 0; i < gNumTotalPlayers; i++)
	{
		float d, score;
		if (!IsValidEnemyTarget(playerNum, i, true, 1200.0f))
			continue;

		d = NavDistPoints(curX, curZ,
						  gPlayerInfo[i].objNode->Coord.x,
						  gPlayerInfo[i].objNode->Coord.z);
		score = d + gPlayerInfo[i].health * 3200.0f;
		if (score < bestTargetScore)
		{
			bestTargetScore = score;
			bestTarget = i;
		}
	}

	if (bestTarget >= 0)
	{
		pinfo->aiMode = AI_MODE_CHASE_TARGET;
		pinfo->aiTargetPlayer = bestTarget;
		ComputeInterceptGoal(bestTarget, curX, curZ, params->leadTime,
							 &pinfo->aiGoalX, &pinfo->aiGoalZ);
		return;
	}

	pinfo->aiMode = AI_MODE_ROAM_PATROL;
	PickPatrolHub(playerNum, curX, curZ, 0.0f, 0.0f, false, &pinfo->aiGoalX, &pinfo->aiGoalZ);
}

static void UpdateCaptureFlagDecision(ObjNode *theCar, short playerNum,
									  const BattleAIDiffParams *params)
{
	PlayerInfoType *pinfo = &gPlayerInfo[playerNum];
	float curX = theCar->Coord.x;
	float curZ = theCar->Coord.z;
	Byte myTeam = pinfo->team & 1;
	Byte enemyTeam = myTeam ^ 1;
	ObjNode *heldTorch = (ObjNode *)theCar->CapturedFlag;
	float bestTorchDist = 1e9f;
	ObjNode *bestTorch = nil;
	short bestIntercept = -1;
	float bestInterceptScore = 1e9f;
	short i;

	if (heldTorch != nil)
	{
		if (heldTorch->TorchTeam == myTeam)
		{
			pinfo->aiMode = AI_MODE_RETURN_FLAG;
			pinfo->aiGoalX = gBattleNav.baseX[myTeam];
			pinfo->aiGoalZ = gBattleNav.baseZ[myTeam];
			return;
		}
		/* Carrying enemy team's torch: DoCPUPowerupLogic will throw-drop it; avoid enemy base */
	}

	/* Find closest loose own-team torch (Mode == 0), skipping temporarily ignored orbited torch */
	for (i = 0; i < gNumTorches; i++)
	{
		ObjNode *torch = gTorchObjs[i];
		float d;

		if ((torch == nil) || (torch->Mode != 0) || (torch->TorchTeam != myTeam))
			continue;
		if (IsGoalIgnored(pinfo, torch->Coord.x, torch->Coord.z))
			continue;

		d = NavDistPoints(curX, curZ, torch->Coord.x, torch->Coord.z);
		if (d < bestTorchDist)
		{
			bestTorchDist = d;
			bestTorch = torch;
		}
	}

	/* Check enemies currently carrying any torch */
	for (i = 0; i < gNumTotalPlayers; i++)
	{
		ObjNode *enemyCar, *enemyTorch;
		float dSelf, dEnemyBase, score;

		if (!IsValidEnemyTarget(playerNum, i, true, 1600.0f))
			continue;

		enemyCar = gPlayerInfo[i].objNode;
		enemyTorch = (ObjNode *)enemyCar->CapturedFlag;
		if (enemyTorch == nil)
			continue;

		dSelf = NavDistPoints(curX, curZ, enemyCar->Coord.x, enemyCar->Coord.z);
		dEnemyBase = NavDistPoints(enemyCar->Coord.x, enemyCar->Coord.z,
								   gBattleNav.baseX[enemyTeam], gBattleNav.baseZ[enemyTeam]);
		score = dSelf;

		if (enemyTorch->TorchTeam == myTeam)
			score -= 2200.0f;	/* Stealing/holding our torch -> high priority intercept */
		if ((enemyTorch->TorchTeam == enemyTeam) && (dEnemyBase < 5500.0f))
			score -= 1800.0f;	/* About to score for enemy team */

		if (score < bestInterceptScore)
		{
			bestInterceptScore = score;
			bestIntercept = i;
		}
	}

	/* Decide between intercepting an enemy carrier and fetching our own torch */
	if (bestIntercept >= 0)
	{
		ObjNode *carrierCar = gPlayerInfo[bestIntercept].objNode;
		float dCarrier = NavDistPoints(curX, curZ, carrierCar->Coord.x, carrierCar->Coord.z);
		Boolean noLooseTorch = (bestTorch == nil);
		Boolean urgentIntercept = (dCarrier < 4200.0f) &&
								  ((playerNum & 1) || noLooseTorch || (dCarrier < bestTorchDist * 0.85f));

		if (urgentIntercept || noLooseTorch)
		{
			pinfo->aiMode = AI_MODE_INTERCEPT_CARRIER;
			pinfo->aiTargetPlayer = bestIntercept;
			ComputeInterceptGoal(bestIntercept, curX, curZ, params->leadTime,
								 &pinfo->aiGoalX, &pinfo->aiGoalZ);
			return;
		}
	}

	if (bestTorch != nil)
	{
		if (!HasUsableWeapon(pinfo) && (gGamePrefs.difficulty >= DIFFICULTY_MEDIUM))
		{
			float pkX, pkZ, pkDist;
			if (FindBestPickup(playerNum, curX, curZ, 2200.0f, &pkX, &pkZ, &pkDist) &&
				(pkDist < bestTorchDist * 0.7f))
			{
				pinfo->aiMode = AI_MODE_SEEK_PICKUP;
				pinfo->aiGoalX = pkX;
				pinfo->aiGoalZ = pkZ;
				return;
			}
		}

		pinfo->aiMode = AI_MODE_FETCH_FLAG;
		pinfo->aiGoalX = bestTorch->Coord.x;
		pinfo->aiGoalZ = bestTorch->Coord.z;
		return;
	}

	/* All remaining own-team torches are carried by teammates: screen nearest enemy or patrol */
	{
		float enemyDist = 1e9f;
		short enemy = FindNearestThreatOrTarget(playerNum, curX, curZ, -1, true, &enemyDist);
		if (enemy >= 0)
		{
			pinfo->aiMode = AI_MODE_INTERCEPT_CARRIER;
			pinfo->aiTargetPlayer = enemy;
			ComputeInterceptGoal(enemy, curX, curZ, params->leadTime,
								 &pinfo->aiGoalX, &pinfo->aiGoalZ);
			return;
		}
	}

	pinfo->aiMode = AI_MODE_ROAM_PATROL;
	PickPatrolHub(playerNum, curX, curZ, gBattleNav.baseX[enemyTeam], gBattleNav.baseZ[enemyTeam],
				  true, &pinfo->aiGoalX, &pinfo->aiGoalZ);
}

void BattleAI_UpdateDecision(ObjNode *theCar, short playerNum)
{
	const BattleAIDiffParams *params = GetDiffParams();
	PlayerInfoType *pinfo = &gPlayerInfo[playerNum];

	pinfo->aiReplanTimer = params->replanInterval;
	pinfo->aiTargetPlayer = -1;

	switch (gGameMode)
	{
		case GAME_MODE_TAG1:
			UpdateTag1Decision(theCar, playerNum, params);
			break;

		case GAME_MODE_TAG2:
			UpdateTag2Decision(theCar, playerNum, params);
			break;

		case GAME_MODE_SURVIVAL:
			UpdateSurvivalDecision(theCar, playerNum, params);
			break;

		case GAME_MODE_CAPTUREFLAG:
			UpdateCaptureFlagDecision(theCar, playerNum, params);
			break;

		default:
			pinfo->aiMode = AI_MODE_ROAM_PATROL;
			pinfo->aiGoalX = theCar->Coord.x;
			pinfo->aiGoalZ = theCar->Coord.z;
			break;
	}
}

static Boolean IsWhiskerBlocked(float curX, float curZ, float rotY, float dist, Boolean avoidTar)
{
	float wx = curX - sinf(rotY) * dist;
	float wz = curZ - cosf(rotY) * dist;
	uint8_t flags = BattleNav_GetCellFlags(wx, wz);

	if (!(flags & BN_CELL_WALKABLE))
		return(true);

	if (avoidTar && (flags & BN_CELL_HAZARD_TAR))
		return(true);

	if (!BattleNav_IsSegmentClear(curX, curZ, wx, wz, avoidTar))
		return(true);

	return(false);
}

signed char BattleAI_PickUnstuckDir(ObjNode *theCar, short playerNum)
{
	PlayerInfoType *pinfo = &gPlayerInfo[playerNum];
	float curX = theCar->Coord.x;
	float curZ = theCar->Coord.z;
	float r = theCar->Rot.y;
	Boolean avoidTar = ((BattleNav_GetCellFlags(curX, curZ) & BN_CELL_HAZARD_TAR) == 0);
	Boolean backLeftClear = !IsWhiskerBlocked(curX, curZ, r + 3.14159265f - 0.38f, 520.0f, avoidTar);
	Boolean backRightClear = !IsWhiskerBlocked(curX, curZ, r + 3.14159265f + 0.38f, 520.0f, avoidTar);
	Boolean frontLeftClear = !IsWhiskerBlocked(curX, curZ, r + 0.60f, 550.0f, avoidTar);
	Boolean frontRightClear = !IsWhiskerBlocked(curX, curZ, r - 0.60f, 550.0f, avoidTar);

	/* After repeated stuck cycles, abandon any corner-trapped pickup/fetch goal & pick an open hub */
	if (pinfo->aiStuckCount >= 2)
	{
		if ((pinfo->aiMode == AI_MODE_SEEK_PICKUP) || (pinfo->aiMode == AI_MODE_FETCH_FLAG))
		{
			pinfo->aiIgnoreX = pinfo->aiGoalX;
			pinfo->aiIgnoreZ = pinfo->aiGoalZ;
			pinfo->aiIgnoreTimer = 4.5f;
		}
		pinfo->aiGoalTimer = 0.0f;
		pinfo->aiEscapeX = 0.0f;
		pinfo->aiEscapeZ = 0.0f;
		pinfo->aiMode = AI_MODE_ROAM_PATROL;
		PickPatrolHub(playerNum, curX, curZ, pinfo->aiGoalX, pinfo->aiGoalZ, true,
					  &pinfo->aiGoalX, &pinfo->aiGoalZ);
	}

	/* When reversing (movingBackwards), analogSteering +1 moves tail back-right & aims nose left */
	if (backLeftClear && !backRightClear)
		return(-1);
	if (backRightClear && !backLeftClear)
		return(1);

	/* Both rear quarters similarly clear or tight: turn nose toward the open front corridor or goal */
	if (frontRightClear && !frontLeftClear)
		return(-1);
	if (frontLeftClear && !frontRightClear)
		return(1);

	{
		float gdx = pinfo->aiGoalX - curX;
		float gdz = pinfo->aiGoalZ - curZ;
		float gLen = sqrtf(gdx * gdx + gdz * gdz);
		if (gLen > 1.0f)
		{
			OGLVector2D goalVec = { gdx / gLen, gdz / gLen };
			OGLVector2D aimVec = { -sinf(r), -cosf(r) };
			float cross = OGLVector2D_Cross(&goalVec, &aimVec);
			if (fabsf(cross) > 0.15f)
				return((cross > 0.0f) ? 1 : -1);
		}
	}

	return((pinfo->aiStuckDir != 0) ? -pinfo->aiStuckDir : 1);
}

Boolean BattleAI_CalcSteering(ObjNode *theCar, short playerNum,
								OGLVector2D *outPathVec, float *outSteerCmd,
								float *outPathAngle, float *outTurnSharpness,
								Boolean *outWantBrake, Boolean *outWantGas)
{
	PlayerInfoType *pinfo = &gPlayerInfo[playerNum];
	const BattleAIDiffParams *params = GetDiffParams();
	float curX = theCar->Coord.x;
	float curZ = theCar->Coord.z;
	float wpX = pinfo->aiGoalX;
	float wpZ = pinfo->aiGoalZ;
	int16_t wpNode = -1;
	float dx, dz, dist, r, dot, cross, angle, steer;
	OGLVector2D pathVec, aimVec;
	float whiskerDist;
	Boolean isStaticPointGoal;
	float goalDist;

	*outTurnSharpness = 0.0f;
	*outWantBrake = false;
	*outWantGas = true;

	if (!gBattleNav.loaded)
	{
		outPathVec->x = 1.0f;
		outPathVec->y = 0.0f;
		*outSteerCmd = 0.0f;
		*outPathAngle = 0.0f;
		return(false);
	}

	if (pinfo->aiIgnoreTimer > 0.0f)
	{
		pinfo->aiIgnoreTimer -= gFramesPerSecondFrac;
		if (pinfo->aiIgnoreTimer < 0.0f)
			pinfo->aiIgnoreTimer = 0.0f;
	}

	pinfo->aiReplanTimer -= gFramesPerSecondFrac;

	/* Trigger immediate replan if dynamic target eliminated/invalidated or reached transient goal */
	if (((pinfo->aiMode == AI_MODE_CHASE_TARGET) || (pinfo->aiMode == AI_MODE_INTERCEPT_CARRIER)) &&
		!IsValidEnemyTarget(playerNum, pinfo->aiTargetPlayer, false, 1e9f))
	{
		pinfo->aiReplanTimer = 0.0f;
	}
	else if ((pinfo->aiMode == AI_MODE_ROAM_PATROL) || (pinfo->aiMode == AI_MODE_FLEE_THREAT))
	{
		float gdx = pinfo->aiGoalX - curX;
		float gdz = pinfo->aiGoalZ - curZ;
		if ((gdx * gdx + gdz * gdz) < (450.0f * 450.0f))
			pinfo->aiReplanTimer = 0.0f;
	}

	/* Detect prolonged orbiting around a static pickup/torch and temporarily blacklist it */
	isStaticPointGoal = (pinfo->aiMode == AI_MODE_SEEK_PICKUP) ||
						(pinfo->aiMode == AI_MODE_FETCH_FLAG) ||
						(pinfo->aiMode == AI_MODE_RETURN_FLAG);
	goalDist = CalcQuickDistance(curX, curZ, pinfo->aiGoalX, pinfo->aiGoalZ);
	if (isStaticPointGoal && (goalDist < 1500.0f) &&
		(CalcQuickDistance(pinfo->aiLastGoalX, pinfo->aiLastGoalZ, pinfo->aiGoalX, pinfo->aiGoalZ) <= 300.0f))
	{
		pinfo->aiGoalTimer += gFramesPerSecondFrac;
	}
	else
	{
		pinfo->aiGoalTimer = 0.0f;
	}
	pinfo->aiLastGoalX = pinfo->aiGoalX;
	pinfo->aiLastGoalZ = pinfo->aiGoalZ;

	if ((pinfo->aiGoalTimer > 2.8f) &&
		((pinfo->aiMode == AI_MODE_SEEK_PICKUP) || (pinfo->aiMode == AI_MODE_FETCH_FLAG)))
	{
		pinfo->aiIgnoreX = pinfo->aiGoalX;
		pinfo->aiIgnoreZ = pinfo->aiGoalZ;
		pinfo->aiIgnoreTimer = (pinfo->aiMode == AI_MODE_FETCH_FLAG) ? 1.8f : 5.0f;
		pinfo->aiGoalTimer = 0.0f;
		pinfo->aiReplanTimer = 0.0f;
	}

	if (pinfo->aiReplanTimer <= 0.0f)
	{
		BattleAI_UpdateDecision(theCar, playerNum);
		isStaticPointGoal = (pinfo->aiMode == AI_MODE_SEEK_PICKUP) ||
							(pinfo->aiMode == AI_MODE_FETCH_FLAG) ||
							(pinfo->aiMode == AI_MODE_RETURN_FLAG);
		goalDist = CalcQuickDistance(curX, curZ, pinfo->aiGoalX, pinfo->aiGoalZ);
	}

	/* Keep tracking moving targets smoothly between decision intervals */
	if (((pinfo->aiMode == AI_MODE_CHASE_TARGET) || (pinfo->aiMode == AI_MODE_INTERCEPT_CARRIER)) &&
		(pinfo->aiTargetPlayer >= 0) &&
		(gPlayerInfo[pinfo->aiTargetPlayer].objNode != nil) &&
		!gPlayerInfo[pinfo->aiTargetPlayer].isEliminated)
	{
		ComputeInterceptGoal(pinfo->aiTargetPlayer, curX, curZ, params->leadTime,
							 &pinfo->aiGoalX, &pinfo->aiGoalZ);
	}

	if (pinfo->aiMode == AI_MODE_FLEE_THREAT)
	{
		wpX = pinfo->aiGoalX;
		wpZ = pinfo->aiGoalZ;
		wpNode = BattleNav_PointToNode(wpX, wpZ);
	}
	else
	{
		wpNode = BattleNav_GetNextWaypoint(curX, curZ, pinfo->aiGoalX, pinfo->aiGoalZ, &wpX, &wpZ);
	}

	dx = wpX - curX;
	dz = wpZ - curZ;
	dist = sqrtf(dx * dx + dz * dz);
	if (dist < 1.0f)
	{
		pathVec.x = -sinf(theCar->Rot.y);
		pathVec.y = -cosf(theCar->Rot.y);
	}
	else
	{
		pathVec.x = dx / dist;
		pathVec.y = dz / dist;
	}

	if ((params->aimWobbleAmp > 0.0f) && !(isStaticPointGoal && (goalDist < 900.0f)))
	{
		float wobble = sinf((float)playerNum * 1.9f + pinfo->aiReplanTimer * 4.5f) * params->aimWobbleAmp;
		float cw = cosf(wobble);
		float sw = sinf(wobble);
		float rx = pathVec.x * cw - pathVec.y * sw;
		float rz = pathVec.x * sw + pathVec.y * cw;
		pathVec.x = rx;
		pathVec.y = rz;
	}

	r = theCar->Rot.y;
	aimVec.x = -sinf(r);
	aimVec.y = -cosf(r);

	cross = OGLVector2D_Cross(&pathVec, &aimVec);
	dot = OGLVector2D_Dot(&pathVec, &aimVec);
	if (dot > 1.0f)
		dot = 1.0f;
	else if (dot < -1.0f)
		dot = -1.0f;
	angle = acosf(dot);

	if (dot < 0.0f)
		steer = (cross > 0.0f) ? -1.0f : 1.0f;
	else
	{
		steer = -cross * 2.2f;
		if (steer > 1.0f)
			steer = 1.0f;
		else if (steer < -1.0f)
			steer = -1.0f;
	}

	/* Precision speed modulation near static pickups/torches/bases (prevents endless high-speed orbiting) */
	if (isStaticPointGoal && (goalDist < 1500.0f))
	{
		if ((pinfo->aiMode == AI_MODE_RETURN_FLAG) && (pinfo->aiGoalTimer > 3.5f) && (theCar->Speed2D > 600.0f))
		{
			*outWantBrake = true;
			*outWantGas = false;
		}
		else if ((angle > 0.90f) && (theCar->Speed2D > 750.0f))
		{
			*outWantBrake = true;
			*outWantGas = false;
		}
		else if ((angle > 0.42f) && (theCar->Speed2D > 1100.0f))
		{
			if (theCar->Speed2D > 1550.0f)
				*outWantBrake = true;
			*outWantGas = false;
		}
		else if ((goalDist < 850.0f) && (angle > 0.22f) && (theCar->Speed2D > 1350.0f))
		{
			*outWantGas = false;
		}
	}

	/* Forward left/right/center nav-cell and fence feelers prevent corner scraping, center posts & V-pocket traps */
	whiskerDist = (theCar->Speed2D > 1600.0f) ? 850.0f : 550.0f;
	{
		Boolean avoidTar = ((BattleNav_GetCellFlags(curX, curZ) & BN_CELL_HAZARD_TAR) == 0);
		Boolean leftBlocked = IsWhiskerBlocked(curX, curZ, r + 0.42f, whiskerDist, avoidTar);
		Boolean rightBlocked = IsWhiskerBlocked(curX, curZ, r - 0.42f, whiskerDist, avoidTar);
		Boolean centerBlocked = IsWhiskerBlocked(curX, curZ, r, whiskerDist * 0.85f, avoidTar);

		if (leftBlocked && !rightBlocked)
		{
			if (steer < 0.65f)
				steer = 0.65f;
		}
		else if (rightBlocked && !leftBlocked)
		{
			if (steer > -0.65f)
				steer = -0.65f;
		}
		else if ((leftBlocked && rightBlocked) || centerBlocked)
		{
			Boolean wideLeftClear = !IsWhiskerBlocked(curX, curZ, r + 0.85f, whiskerDist * 0.65f, avoidTar);
			Boolean wideRightClear = !IsWhiskerBlocked(curX, curZ, r - 0.85f, whiskerDist * 0.65f, avoidTar);
			if (!wideLeftClear && !wideRightClear)
			{
				wideLeftClear = !IsWhiskerBlocked(curX, curZ, r + 1.30f, whiskerDist * 0.55f, avoidTar);
				wideRightClear = !IsWhiskerBlocked(curX, curZ, r - 1.30f, whiskerDist * 0.55f, avoidTar);
			}
			if (wideLeftClear && !wideRightClear)
				steer = -1.0f;
			else if (wideRightClear && !wideLeftClear)
				steer = 1.0f;
			else if (centerBlocked || (!wideLeftClear && !wideRightClear))
				steer = (cross > 0.0f) ? -1.0f : 1.0f;

			if (centerBlocked && (theCar->Speed2D > 850.0f))
			{
				*outWantBrake = true;
				*outWantGas = false;
			}
		}
	}

	/* Predict upcoming corner sharpness across the next graph hop */
	if (wpNode >= 0)
	{
		int16_t dstNode = BattleNav_PointToNode(pinfo->aiGoalX, pinfo->aiGoalZ);
		if ((dstNode >= 0) && (wpNode != dstNode) && (dist < 2200.0f))
		{
			int16_t hop2 = gBattleNav.nextHop[(long)wpNode * gBattleNav.numNodes + dstNode];
			if ((hop2 >= 0) && (hop2 != wpNode))
			{
				float ndx = gBattleNav.nodes[hop2].x - wpX;
				float ndz = gBattleNav.nodes[hop2].z - wpZ;
				float ndLen = sqrtf(ndx * ndx + ndz * ndz);
				if (ndLen > 1.0f)
				{
					float nextDot = pathVec.x * (ndx / ndLen) + pathVec.y * (ndz / ndLen);
					*outTurnSharpness = 1.0f - nextDot;
					if ((nextDot < -0.15f) && (theCar->Speed2D > 2500.0f) &&
						(gGamePrefs.difficulty > DIFFICULTY_EASY))
					{
						*outWantBrake = true;
					}
				}
			}
		}
	}

	*outPathVec = pathVec;
	*outSteerCmd = steer;
	*outPathAngle = angle;
	return(true);
}
