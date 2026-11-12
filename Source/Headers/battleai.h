//
// battleai.h
// Goal-driven Battle Mode AI (Tag1, Tag2, Survival, CaptureFlag) backed by BattleNav.
//

#pragma once

#include "main.h"

enum
{
	AI_MODE_NONE = 0,
	AI_MODE_CHASE_TARGET,
	AI_MODE_FLEE_THREAT,
	AI_MODE_SEEK_PICKUP,
	AI_MODE_RETURN_FLAG,
	AI_MODE_FETCH_FLAG,
	AI_MODE_INTERCEPT_CARRIER,
	AI_MODE_ROAM_PATROL
};

void BattleAI_UpdateDecision(ObjNode *theCar, short playerNum);
signed char BattleAI_PickUnstuckDir(ObjNode *theCar, short playerNum);
Boolean BattleAI_CalcSteering(ObjNode *theCar, short playerNum,
								OGLVector2D *outPathVec, float *outSteerCmd,
								float *outPathAngle, float *outTurnSharpness,
								Boolean *outWantBrake, Boolean *outWantGas);
