/****************************/
/*   	BATTLENAV.C         */
/****************************/

#include "game.h"
#include <math.h>
#include <string.h>

BattleNavData gBattleNav;

static const char *kBattleNavFiles[NUM_TRACKS - TRACK_NUM_STONEHENGE] =
{
	":Terrain:Battle_StoneHenge.nav",
	":Terrain:Battle_Aztec.nav",
	":Terrain:Battle_Coliseum.nav",
	":Terrain:Battle_Maze.nav",
	":Terrain:Battle_Celtic.nav",
	":Terrain:Battle_TarPits.nav",
	":Terrain:Battle_Spiral.nav",
	":Terrain:Battle_Ramps.nav",
};

typedef struct NavReader
{
	const uint8_t	*data;
	long			len;
	long			pos;
} NavReader;

static Boolean NavReadBytes(NavReader *r, void *outBuf, long count)
{
	if ((r->pos + count) > r->len)
		return(false);
	SDL_memcpy(outBuf, r->data + r->pos, (size_t)count);
	r->pos += count;
	return(true);
}

static Boolean NavReadU8(NavReader *r, uint8_t *outVal)
{
	if ((r->pos + 1) > r->len)
		return(false);
	*outVal = r->data[r->pos++];
	return(true);
}

static Boolean NavReadU16(NavReader *r, uint16_t *outVal)
{
	uint8_t b[2];
	if (!NavReadBytes(r, b, 2))
		return(false);
	*outVal = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
	return(true);
}

static Boolean NavReadI16(NavReader *r, int16_t *outVal)
{
	uint16_t u = 0;
	if (!NavReadU16(r, &u))
		return(false);
	*outVal = (int16_t)u;
	return(true);
}

static Boolean NavReadF32(NavReader *r, float *outVal)
{
	uint8_t b[4];
	uint32_t u;
	if (!NavReadBytes(r, b, 4))
		return(false);
	u = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	SDL_memcpy(outVal, &u, sizeof(float));
	return(true);
}

static void ClampWorldToCell(float x, float z, int *outCol, int *outRow)
{
	int col = (int)(x / gBattleNav.cellSize);
	int row = (int)(z / gBattleNav.cellSize);

	if (col < 0)
		col = 0;
	else if (col >= (int)gBattleNav.gridW)
		col = (int)gBattleNav.gridW - 1;

	if (row < 0)
		row = 0;
	else if (row >= (int)gBattleNav.gridH)
		row = (int)gBattleNav.gridH - 1;

	*outCol = col;
	*outRow = row;
}

static Boolean SegmentCrossesAnyFence2D(float x1, float z1, float x2, float z2)
{
	long f, i;
	float minX = (x1 < x2) ? x1 : x2;
	float maxX = (x1 > x2) ? x1 : x2;
	float minZ = (z1 < z2) ? z1 : z2;
	float maxZ = (z1 > z2) ? z1 : z2;

	for (f = 0; f < gNumFences; f++)
	{
		FenceDefType *fence = &gFenceList[f];
		OGLPoint3D *nubs = fence->nubList;
		long numSegs = fence->numNubs - 1;

		if ((nubs == nil) || (numSegs <= 0))
			continue;

		if ((maxX < fence->bBox.min.x) || (minX > fence->bBox.max.x) ||
			(maxZ < fence->bBox.min.z) || (minZ > fence->bBox.max.z))
			continue;

		for (i = 0; i < numSegs; i++)
		{
			float fx1 = nubs[i].x;
			float fz1 = nubs[i].z;
			float fx2 = nubs[i + 1].x;
			float fz2 = nubs[i + 1].z;
			float ix, iz;

			if (((fx1 < minX) && (fx2 < minX)) || ((fx1 > maxX) && (fx2 > maxX)) ||
				((fz1 < minZ) && (fz2 < minZ)) || ((fz1 > maxZ) && (fz2 > maxZ)))
				continue;

			if (IntersectLineSegments(x1, z1, x2, z2, fx1, fz1, fx2, fz2, &ix, &iz))
				return(true);
		}
	}

	return(false);
}

static Boolean HasDirectDrivablePath(float x1, float z1, float x2, float z2, Boolean avoidTar)
{
	float dx = x2 - x1;
	float dz = z2 - z1;
	float dist = sqrtf(dx * dx + dz * dz);
	float ux, uz, rx, rz;
	int steps, s;
	float prevY;
	float stepLen;

	if (!gBattleNav.loaded)
		return(true);

	if (dist > 4500.0f)
		return(false);

	if (dist < 100.0f)
		return(!SegmentCrossesAnyFence2D(x1, z1, x2, z2));

	if (SegmentCrossesAnyFence2D(x1, z1, x2, z2))
		return(false);

	ux = dx / dist;
	uz = dz / dist;
	rx = -uz * 160.0f;
	rz = ux * 160.0f;

	steps = (int)ceilf(dist / 200.0f);
	if (steps < 2)
		steps = 2;
	stepLen = dist / (float)steps;
	prevY = GetTerrainY(x1, z1);

	for (s = 1; s <= steps; s++)
	{
		float t = (float)s / (float)steps;
		float px = x1 + dx * t;
		float pz = z1 + dz * t;
		uint8_t flags = BattleNav_GetCellFlags(px, pz);
		float curY;

		if (!(flags & BN_CELL_WALKABLE))
			return(false);

		if (avoidTar && (s < steps) && (flags & BN_CELL_HAZARD_TAR))
			return(false);

		if ((dist > 450.0f) && (t >= 0.15f) && (t <= 0.85f))
		{
			uint8_t lFlags = BattleNav_GetCellFlags(px + rx, pz + rz);
			uint8_t rFlags = BattleNav_GetCellFlags(px - rx, pz - rz);
			if (!(lFlags & BN_CELL_WALKABLE) || !(rFlags & BN_CELL_WALKABLE))
				return(false);
		}

		curY = GetTerrainY(px, pz);
		if ((fabsf(curY - prevY) / stepLen) > 0.90f)
			return(false);
		prevY = curY;
	}

	return(true);
}


/****************** BATTLENAV_DISPOSE **********************/

void BattleNav_Dispose(void)
{
	if (gBattleNav.cellFlags)
		SafeDisposePtr((Ptr)gBattleNav.cellFlags);
	if (gBattleNav.cellNode)
		SafeDisposePtr((Ptr)gBattleNav.cellNode);
	if (gBattleNav.nodes)
		SafeDisposePtr((Ptr)gBattleNav.nodes);
	if (gBattleNav.pickups)
		SafeDisposePtr((Ptr)gBattleNav.pickups);
	if (gBattleNav.nextHop)
		SafeDisposePtr((Ptr)gBattleNav.nextHop);
	if (gBattleNav.distEighth)
		SafeDisposePtr((Ptr)gBattleNav.distEighth);

	SDL_memset(&gBattleNav, 0, sizeof(gBattleNav));
	gBattleNav.baseNode[0] = -1;
	gBattleNav.baseNode[1] = -1;
}


/****************** BATTLENAV_LOAD FOR TRACK **********************/

Boolean BattleNav_LoadForTrack(short trackNum)
{
	long fileLen = 0;
	Ptr rawData = nil;
	NavReader reader;
	char magic[4];
	uint16_t version = 0;
	uint16_t headerFlags = 0;
	long numCells, numMatrix, i;

	BattleNav_Dispose();

	if ((trackNum < TRACK_NUM_STONEHENGE) || (trackNum >= NUM_TRACKS))
		return(false);

	rawData = LoadDataFile(kBattleNavFiles[trackNum - TRACK_NUM_STONEHENGE], &fileLen);
	if ((rawData == nil) || (fileLen < 56))
	{
		SDL_Log("BattleNav: Failed to load nav file for track %d\n", trackNum);
		if (rawData)
			SafeDisposePtr(rawData);
		return(false);
	}

	reader.data = (const uint8_t *)rawData;
	reader.len = fileLen;
	reader.pos = 0;

	if (!NavReadBytes(&reader, magic, 4) ||
		(SDL_memcmp(magic, BATTLENAV_MAGIC, 4) != 0) ||
		!NavReadU16(&reader, &version) ||
		(version != BATTLENAV_VERSION) ||
		!NavReadU16(&reader, &headerFlags) ||
		!NavReadU16(&reader, &gBattleNav.gridW) ||
		!NavReadU16(&reader, &gBattleNav.gridH) ||
		!NavReadF32(&reader, &gBattleNav.cellSize) ||
		!NavReadF32(&reader, &gBattleNav.worldMinX) ||
		!NavReadF32(&reader, &gBattleNav.worldMinZ) ||
		!NavReadF32(&reader, &gBattleNav.worldMaxX) ||
		!NavReadF32(&reader, &gBattleNav.worldMaxZ) ||
		!NavReadU16(&reader, &gBattleNav.numNodes) ||
		!NavReadU16(&reader, &gBattleNav.numPickups) ||
		!NavReadI16(&reader, &gBattleNav.baseNode[0]) ||
		!NavReadI16(&reader, &gBattleNav.baseNode[1]) ||
		!NavReadF32(&reader, &gBattleNav.baseX[0]) ||
		!NavReadF32(&reader, &gBattleNav.baseZ[0]) ||
		!NavReadF32(&reader, &gBattleNav.baseX[1]) ||
		!NavReadF32(&reader, &gBattleNav.baseZ[1]))
	{
		SDL_Log("BattleNav: Corrupt header in nav file for track %d\n", trackNum);
		SafeDisposePtr(rawData);
		BattleNav_Dispose();
		return(false);
	}

	(void)headerFlags;

	if ((gBattleNav.gridW == 0) || (gBattleNav.gridH == 0) ||
		(gBattleNav.numNodes == 0) || (gBattleNav.cellSize <= 0.0f))
	{
		SafeDisposePtr(rawData);
		BattleNav_Dispose();
		return(false);
	}

	numCells = (long)gBattleNav.gridW * (long)gBattleNav.gridH;
	numMatrix = (long)gBattleNav.numNodes * (long)gBattleNav.numNodes;

	gBattleNav.cellFlags = (uint8_t *)AllocPtr((Size)numCells);
	gBattleNav.cellNode = (int16_t *)AllocPtr((Size)(numCells * (long)sizeof(int16_t)));
	gBattleNav.nodes = (BattleNavNode *)AllocPtr((Size)((long)gBattleNav.numNodes * (long)sizeof(BattleNavNode)));
	gBattleNav.pickups = gBattleNav.numPickups
		? (BattleNavPickup *)AllocPtr((Size)((long)gBattleNav.numPickups * (long)sizeof(BattleNavPickup)))
		: nil;
	gBattleNav.nextHop = (int16_t *)AllocPtr((Size)(numMatrix * (long)sizeof(int16_t)));
	gBattleNav.distEighth = (uint16_t *)AllocPtr((Size)(numMatrix * (long)sizeof(uint16_t)));

	if (!gBattleNav.cellFlags || !gBattleNav.cellNode || !gBattleNav.nodes ||
		(gBattleNav.numPickups && !gBattleNav.pickups) ||
		!gBattleNav.nextHop || !gBattleNav.distEighth)
	{
		SafeDisposePtr(rawData);
		BattleNav_Dispose();
		return(false);
	}

	for (i = 0; i < numCells; i++)
	{
		if (!NavReadU8(&reader, &gBattleNav.cellFlags[i]))
			goto fail;
	}

	for (i = 0; i < numCells; i++)
	{
		if (!NavReadI16(&reader, &gBattleNav.cellNode[i]))
			goto fail;
	}

	for (i = 0; i < (long)gBattleNav.numNodes; i++)
	{
		BattleNavNode *node = &gBattleNav.nodes[i];
		if (!NavReadF32(&reader, &node->x) ||
			!NavReadF32(&reader, &node->y) ||
			!NavReadF32(&reader, &node->z) ||
			!NavReadF32(&reader, &node->clearance) ||
			!NavReadU16(&reader, &node->flags) ||
			!NavReadU16(&reader, &node->degree))
			goto fail;
	}

	for (i = 0; i < (long)gBattleNav.numPickups; i++)
	{
		BattleNavPickup *pk = &gBattleNav.pickups[i];
		if (!NavReadF32(&reader, &pk->x) ||
			!NavReadF32(&reader, &pk->z) ||
			!NavReadU16(&reader, &pk->itemType) ||
			!NavReadI16(&reader, &pk->nodeIdx) ||
			!NavReadU8(&reader, &pk->powSubtype) ||
			!NavReadBytes(&reader, pk->reserved, 3))
			goto fail;
	}

	for (i = 0; i < numMatrix; i++)
	{
		if (!NavReadI16(&reader, &gBattleNav.nextHop[i]))
			goto fail;
	}

	for (i = 0; i < numMatrix; i++)
	{
		if (!NavReadU16(&reader, &gBattleNav.distEighth[i]))
			goto fail;
	}

	SafeDisposePtr(rawData);
	gBattleNav.loaded = true;
	return(true);

fail:
	SDL_Log("BattleNav: Truncated nav file for track %d\n", trackNum);
	SafeDisposePtr(rawData);
	BattleNav_Dispose();
	return(false);
}


/****************** BATTLENAV_GET CELL FLAGS **********************/

uint8_t BattleNav_GetCellFlags(float x, float z)
{
	int col, row;

	if (!gBattleNav.loaded || !gBattleNav.cellFlags)
		return(0);

	ClampWorldToCell(x, z, &col, &row);
	return(gBattleNav.cellFlags[row * gBattleNav.gridW + col]);
}


/****************** BATTLENAV_IS WALKABLE POINT **********************/

Boolean BattleNav_IsWalkablePoint(float x, float z)
{
	if (!gBattleNav.loaded)
		return(true);

	return((BattleNav_GetCellFlags(x, z) & BN_CELL_WALKABLE) != 0);
}


/****************** BATTLENAV_POINT TO NODE **********************/

int16_t BattleNav_PointToNode(float x, float z)
{
	int col, row;

	if (!gBattleNav.loaded || !gBattleNav.cellNode)
		return(-1);

	ClampWorldToCell(x, z, &col, &row);
	return(gBattleNav.cellNode[row * gBattleNav.gridW + col]);
}


/****************** BATTLENAV_GET PATH DIST **********************/

float BattleNav_GetPathDist(int16_t fromNode, int16_t toNode)
{
	uint16_t q8;

	if (!gBattleNav.loaded || (fromNode < 0) || (toNode < 0) ||
		(fromNode >= (int16_t)gBattleNav.numNodes) || (toNode >= (int16_t)gBattleNav.numNodes))
		return(1e9f);

	q8 = gBattleNav.distEighth[(long)fromNode * gBattleNav.numNodes + toNode];
	if (q8 == 0xFFFF)
		return(1e9f);

	return((float)q8 * 8.0f);
}


/****************** BATTLENAV_GET NEXT WAYPOINT **********************/

int16_t BattleNav_GetNextWaypoint(float curX, float curZ, float goalX, float goalZ,
								  float *outX, float *outZ)
{
	int16_t srcNode, dstNode, hop, lookahead;
	int step;
	float dx = goalX - curX;
	float dz = goalZ - curZ;

	if (!gBattleNav.loaded)
	{
		*outX = goalX;
		*outZ = goalZ;
		return(-1);
	}

	{
		Boolean curClear = ((BattleNav_GetCellFlags(curX, curZ) & BN_CELL_HAZARD_TAR) == 0);
		Boolean avoidTar = curClear && ((BattleNav_GetCellFlags(goalX, goalZ) & BN_CELL_HAZARD_TAR) == 0);

		/* Direct string-pull if goal is close and cleanly drivable */
		if (((dx * dx + dz * dz) <= (4500.0f * 4500.0f)) &&
			HasDirectDrivablePath(curX, curZ, goalX, goalZ, avoidTar))
		{
			*outX = goalX;
			*outZ = goalZ;
			return(BattleNav_PointToNode(goalX, goalZ));
		}

		srcNode = BattleNav_PointToNode(curX, curZ);
		dstNode = BattleNav_PointToNode(goalX, goalZ);
		if ((srcNode < 0) || (dstNode < 0) || (srcNode == dstNode))
		{
			*outX = goalX;
			*outZ = goalZ;
			return(dstNode);
		}

		hop = gBattleNav.nextHop[(long)srcNode * gBattleNav.numNodes + dstNode];
		if (hop < 0)
		{
			*outX = goalX;
			*outZ = goalZ;
			return(-1);
		}

		/* Look ahead up to 2 hops along shortest path to smooth corners & avoid backtracking to srcNode */
		lookahead = hop;
		for (step = 0; step < 2; step++)
		{
			int16_t nextN = gBattleNav.nextHop[(long)lookahead * gBattleNav.numNodes + dstNode];
			if ((nextN < 0) || (nextN == lookahead))
				break;
			if (HasDirectDrivablePath(curX, curZ, gBattleNav.nodes[nextN].x, gBattleNav.nodes[nextN].z, curClear))
				lookahead = nextN;
			else
				break;
		}

		/* If very close to lookahead node and next node exists, advance */
		{
			float ndx = gBattleNav.nodes[lookahead].x - curX;
			float ndz = gBattleNav.nodes[lookahead].z - curZ;
			if (((ndx * ndx + ndz * ndz) < (500.0f * 500.0f)) && (lookahead != dstNode))
			{
				int16_t nextN = gBattleNav.nextHop[(long)lookahead * gBattleNav.numNodes + dstNode];
				if (nextN >= 0)
					lookahead = nextN;
			}
		}

		/* If final node reached cleanly, steer toward actual goal coords */
		if ((lookahead == dstNode) &&
			HasDirectDrivablePath(curX, curZ, goalX, goalZ, avoidTar))
		{
			*outX = goalX;
			*outZ = goalZ;
			return(dstNode);
		}
	}

	*outX = gBattleNav.nodes[lookahead].x;
	*outZ = gBattleNav.nodes[lookahead].z;
	return(lookahead);
}


/****************** BATTLENAV_IS SEGMENT CLEAR **********************/

Boolean BattleNav_IsSegmentClear(float x1, float z1, float x2, float z2, Boolean avoidTar)
{
	return(HasDirectDrivablePath(x1, z1, x2, z2, avoidTar));
}


/****************** BATTLENAV_FIND ESCAPE WAYPOINT **********************/

int16_t BattleNav_FindEscapeWaypoint(float curX, float curZ, float threatX, float threatZ,
									 float prevX, float prevZ,
									 float *outX, float *outZ)
{
	int16_t myNode, threatNode, bestGoal = -1;
	float bestScore = -1e18f;
	float curThreatDist;
	uint16_t w;

	if (!gBattleNav.loaded)
	{
		float dx = curX - threatX;
		float dz = curZ - threatZ;
		float len = sqrtf(dx * dx + dz * dz);
		if (len < 1.0f)
		{
			dx = 1.0f;
			dz = 0.0f;
			len = 1.0f;
		}
		*outX = curX + (dx / len) * 2000.0f;
		*outZ = curZ + (dz / len) * 2000.0f;
		return(-1);
	}

	myNode = BattleNav_PointToNode(curX, curZ);
	threatNode = BattleNav_PointToNode(threatX, threatZ);
	if ((myNode < 0) || (threatNode < 0))
	{
		*outX = curX;
		*outZ = curZ;
		return(-1);
	}

	curThreatDist = BattleNav_GetPathDist(myNode, threatNode);

	for (w = 0; w < gBattleNav.numNodes; w++)
	{
		const BattleNavNode *node = &gBattleNav.nodes[w];
		int16_t firstHop;
		float dSelf, dThreat, dHopThreat, score;

		if ((int16_t)w == myNode)
			continue;

		dSelf = BattleNav_GetPathDist(myNode, (int16_t)w);
		dThreat = BattleNav_GetPathDist((int16_t)w, threatNode);
		if ((dSelf > 1e8f) || (dThreat > 1e8f))
			continue;

		/* Avoid picking targets that require driving toward the threat first */
		firstHop = gBattleNav.nextHop[(long)myNode * gBattleNav.numNodes + w];
		if (firstHop < 0)
			continue;

		dHopThreat = BattleNav_GetPathDist(firstHop, threatNode);
		if (dHopThreat < (curThreatDist - 500.0f))
			continue;

		score = dThreat - (0.35f * dSelf);

		if (node->flags & BN_NODE_DEADEND)
			score -= 6000.0f;
		else if (node->flags & BN_NODE_HUB)
			score += 900.0f;

		if (node->clearance < 500.0f)
			score -= 1000.0f;
		else if (node->clearance >= 900.0f)
			score += 400.0f;

		if (BattleNav_GetCellFlags(node->x, node->z) & BN_CELL_HAZARD_TAR)
			score -= 4000.0f;

		/* Mild commitment bonus toward previous escape target to avoid flip-flopping */
		if ((prevX != 0.0f) || (prevZ != 0.0f))
		{
			float pdx = node->x - prevX;
			float pdz = node->z - prevZ;
			if ((pdx * pdx + pdz * pdz) < (2400.0f * 2400.0f))
				score += 1200.0f;
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestGoal = (int16_t)w;
		}
	}

	/* Fallback if every non-approaching candidate was filtered out (cornered) */
	if (bestGoal < 0)
	{
		for (w = 0; w < gBattleNav.numNodes; w++)
		{
			float dThreat = BattleNav_GetPathDist((int16_t)w, threatNode);
			if (dThreat > bestScore && dThreat < 1e8f)
			{
				bestScore = dThreat;
				bestGoal = (int16_t)w;
			}
		}
	}

	if (bestGoal < 0)
	{
		*outX = curX;
		*outZ = curZ;
		return(-1);
	}

	return(BattleNav_GetNextWaypoint(curX, curZ,
									 gBattleNav.nodes[bestGoal].x,
									 gBattleNav.nodes[bestGoal].z,
									 outX, outZ));
}
