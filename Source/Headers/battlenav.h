//
// battlenav.h
// Pre-computed arena navigation mesh & APSP routing for Battle Mode CPUs.
//

#pragma once

#include "main.h"

#define BATTLENAV_MAGIC				"CMNV"
#define BATTLENAV_VERSION			1

enum
{
	BN_CELL_WALKABLE	= (1 << 0),
	BN_CELL_HAZARD_TAR	= (1 << 1),
	BN_CELL_STEEP		= (1 << 2),
	BN_CELL_BLOCKED		= (1 << 3),
};

enum
{
	BN_NODE_SPAWN		= (1 << 0),
	BN_NODE_BASE		= (1 << 1),
	BN_NODE_TORCH		= (1 << 2),
	BN_NODE_PICKUP		= (1 << 3),
	BN_NODE_HUB			= (1 << 4),
	BN_NODE_DEADEND		= (1 << 5),
};

typedef struct BattleNavNode
{
	float		x;
	float		y;
	float		z;
	float		clearance;
	uint16_t	flags;
	uint16_t	degree;
} BattleNavNode;

typedef struct BattleNavPickup
{
	float		x;
	float		z;
	uint16_t	itemType;
	int16_t		nodeIdx;
	uint8_t		powSubtype;
	uint8_t		reserved[3];
} BattleNavPickup;

typedef struct BattleNavData
{
	Boolean			loaded;
	uint16_t		gridW;
	uint16_t		gridH;
	float			cellSize;
	float			worldMinX;
	float			worldMinZ;
	float			worldMaxX;
	float			worldMaxZ;
	uint16_t		numNodes;
	uint16_t		numPickups;
	int16_t			baseNode[2];
	float			baseX[2];
	float			baseZ[2];
	uint8_t			*cellFlags;		/* gridW * gridH */
	int16_t			*cellNode;		/* gridW * gridH */
	BattleNavNode	*nodes;			/* numNodes */
	BattleNavPickup	*pickups;		/* numPickups */
	int16_t			*nextHop;		/* numNodes * numNodes: [src * numNodes + dst] */
	uint16_t		*distEighth;	/* numNodes * numNodes: cost / 8.0f (0xffff = unreachable) */
} BattleNavData;

extern BattleNavData gBattleNav;

Boolean BattleNav_LoadForTrack(short trackNum);
void BattleNav_Dispose(void);

int16_t BattleNav_PointToNode(float x, float z);
uint8_t BattleNav_GetCellFlags(float x, float z);
Boolean BattleNav_IsWalkablePoint(float x, float z);
float BattleNav_GetPathDist(int16_t fromNode, int16_t toNode);
int16_t BattleNav_GetNextWaypoint(float curX, float curZ, float goalX, float goalZ,
								  float *outX, float *outZ);
int16_t BattleNav_FindEscapeWaypoint(float curX, float curZ, float threatX, float threatZ,
									 float prevX, float prevZ,
									 float *outX, float *outZ);
Boolean BattleNav_IsSegmentClear(float x1, float z1, float x2, float z2, Boolean avoidTar);
