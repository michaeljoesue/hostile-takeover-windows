#include "ht.h"

namespace wi {

//
// WallGob implementation
//
// Map walls used to be terrain only: drawn by the FogMap, never hit, never
// destroyed. Each wall tile is now a 1-tile structure with an owner (its
// team colour) and health. Levels can list walls in GameObjects
// (kgtWall,side,tx,ty,0,health%) like any structure; wall tiles a level
// doesn't list become neutral walls at full health.
//
// Walls are never picked as a target automatically (mobile units are not
// told about structures, the Overmind and hunts skip them); they're only
// hit when a player orders an attack on them. Below half health they use
// the damaged pictures (strip 1); destroyed, the tile becomes open terrain
// and the neighbouring walls change shape.
//

static StructConsts gConsts;

bool WallGob::InitClass(IniReader *pini)
{
	gConsts.gt = kgtWall;
	gConsts.ut = kutWall;
	gConsts.umPrerequisites = 0;

	// Sound effects

	gConsts.sfxAbortRepair = ksfxNothing;
	gConsts.sfxRepair = ksfxNothing;
	gConsts.sfxDamaged = ksfxNothing;
	gConsts.sfxSelect = ksfxNothing;
	gConsts.sfxDestroyed = ksfxMachineGunTowerDestroyed;
	gConsts.sfxImpact = ksfxNothing;

	return StructGob::InitClass(&gConsts, pini);
}

void WallGob::ExitClass()
{
	StructGob::ExitClass(&gConsts);
}

WallGob::WallGob() : StructGob(&gConsts)
{
}

static bool IsWallTile(TerrainMap *ptrmap, int tx, int ty)
{
	if (tx < 0 || ty < 0 || tx >= ptrmap->GetWidth() || ty >= ptrmap->GetHeight())
		return false;
	return ptrmap->GetTerrainType(tx, ty) == kttWall;
}

static WallGob *GetWallGob(int tx, int ty)
{
	TerrainMap *ptrmap = gsim.GetLevel()->GetTerrainMap();
	if (tx < 0 || ty < 0 || tx >= ptrmap->GetWidth() || ty >= ptrmap->GetHeight())
		return NULL;
	Gob *pgob = ggobm.GetShadowGob(tx, ty);
	if (pgob == NULL || pgob->GetType() != kgtWall)
		return NULL;
	return (WallGob *)pgob;
}

// Frame = which neighbours are walls (1 up, 2 right, 4 down, 8 left), as
// the FogMap drew them; strip 0 healthy, 1 below half health

void WallGob::UpdateShape()
{
	TerrainMap *ptrmap = gsim.GetLevel()->GetTerrainMap();
	int tx = TcFromWc(m_wx);
	int ty = TcFromWc(m_wy);
	int ifrm = 0;
	if (IsWallTile(ptrmap, tx, ty - 1))
		ifrm |= 1;
	if (IsWallTile(ptrmap, tx + 1, ty))
		ifrm |= 2;
	if (IsWallTile(ptrmap, tx, ty + 1))
		ifrm |= 4;
	if (IsWallTile(ptrmap, tx - 1, ty))
		ifrm |= 8;
	int nStrip = m_fxHealth * 2 < m_pstruc->GetArmorStrength() ? 1 : 0;
	if (nStrip >= m_puntc->panid->GetStripCount())
		nStrip = 0;
	if (m_ani.GetStrip() != nStrip || m_ani.GetFrame() != ifrm || !(m_ani.GetFlags() & kfAniDone))
		StartAnimation(&m_ani, nStrip, ifrm, kfAniDone);
}

bool WallGob::Init(WCoord wx, WCoord wy, Player *pplr, fix fxHealth, dword ff, const char *pszName)
{
	if (pplr == NULL)
		pplr = gplrm.GetPlayer(ksideNeutral);

	// Walls are never built
	ff &= ~kfGobBeingBuilt;

	if (!StructGob::Init(wx, wy, pplr, fxHealth, ff, pszName))
		return false;

	// A wall listed in GameObjects on a tile the terrain doesn't mark as
	// a wall still blocks units

	gsim.GetLevel()->GetTerrainMap()->SetTerrainType(TcFromWc(m_wx), TcFromWc(m_wy), kttWall);
	UpdateShape();
	return true;
}

// From the level's GameObjects. A second wall listed on the same tile is
// left out (not added to the game; the loader doesn't keep it)

bool WallGob::Init(IniReader *pini, FindProp *pfind, const char *pszName)
{
	int side, tx, ty;
	if (pini->GetPropertyValue(pfind, "%*d ,%d ,%d ,%d", &side, &tx, &ty) == 3 && GetWallGob(tx, ty) != NULL)
		return true;
	return UnitGob::Init(pini, pfind, pszName);
}

void WallGob::Delete()
{
	int tx = TcFromWc(m_wx);
	int ty = TcFromWc(m_wy);

	// The tile becomes open terrain

	gsim.GetLevel()->GetTerrainMap()->SetTerrainType(tx, ty, kttOpen);
	StructGob::Delete();

	// The walls next to it lose an arm

	static const int s_adx[4] = { 0, 1, 0, -1 };
	static const int s_ady[4] = { -1, 0, 1, 0 };
	for (int i = 0; i < 4; i++) {
		WallGob *pwall = GetWallGob(tx + s_adx[i], ty + s_ady[i]);
		if (pwall != NULL)
			pwall->UpdateShape();
	}
}

void WallGob::SetHealth(fix fxHealth)
{
	// Not StructGob::SetHealth: that switches to looping idle animations

	UnitGob::SetHealth(fxHealth);
	UpdateShape();
}

bool WallGob::IsTakeoverable(Player *pplr)
{
	return false;
}

void WallGob::PopupMenu()
{
	// Nothing to do with a wall: no repair, no recycling
}

bool WallGob::LoadState(Stream *pstm)
{
	if (!StructGob::LoadState(pstm))
		return false;
	UpdateShape();
	return true;
}

// New mission: every wall tile without a wall from the level's GameObjects
// becomes a neutral wall at full health

void WallGob::CreateMissingWalls()
{
	TerrainMap *ptrmap = gsim.GetLevel()->GetTerrainMap();
	Player *pplrNeutral = gplrm.GetPlayer(ksideNeutral);
	for (int ty = 0; ty < ptrmap->GetHeight(); ty++) {
		for (int tx = 0; tx < ptrmap->GetWidth(); tx++) {
			if (ptrmap->GetTerrainType(tx, ty) != kttWall)
				continue;
			if (GetWallGob(tx, ty) != NULL)
				continue;
			WallGob *pwall = (WallGob *)CreateGob(kgtWall);
			if (pwall == NULL)
				return;
			if (!pwall->Init(WcFromTc(tx), WcFromTc(ty), pplrNeutral, 0, 0, NULL))
				delete pwall;
		}
	}

	// Shapes were set as the walls appeared; set them again now that all
	// of them are there

	for (Gob *pgob = ggobm.GetFirstGob(); pgob != NULL; pgob = ggobm.GetNextGob(pgob)) {
		if (pgob->GetType() == kgtWall)
			((WallGob *)pgob)->UpdateShape();
	}
}

// Loaded game: wall tiles whose wall was destroyed are open terrain again
// (the terrain map itself is reloaded from the level)

void WallGob::OpenTilesWithoutWalls()
{
	TerrainMap *ptrmap = gsim.GetLevel()->GetTerrainMap();
	for (int ty = 0; ty < ptrmap->GetHeight(); ty++) {
		for (int tx = 0; tx < ptrmap->GetWidth(); tx++) {
			if (ptrmap->GetTerrainType(tx, ty) == kttWall && GetWallGob(tx, ty) == NULL)
				ptrmap->SetTerrainType(tx, ty, kttOpen);
		}
	}
	for (Gob *pgob = ggobm.GetFirstGob(); pgob != NULL; pgob = ggobm.GetNextGob(pgob)) {
		if (pgob->GetType() == kgtWall)
			((WallGob *)pgob)->UpdateShape();
	}
}



int WallGob::ProcessStateMachineMessage(State st, Message *pmsg)
{
BeginStateMachine
	//-----------------------------------------------------------------------

	State(kstIdle)
		OnEnter
			UpdateShape();

		// The frame is the shape: don't animate
		OnUpdate
			DefUpdate();

	//-----------------------------------------------------------------------

	State(kstDying)
		OnEnter
			// Like StructGob's, but walls have no destroyed strip: the
			// damaged wall shows under the explosion until it's removed

			if (m_ff & kfGobActive)
				Deactivate();
			gsndm.PlaySfx(m_pstruc->sfxDestroyed, m_wx, m_wy);
			gsmm.SendDelayedMsg(kmidDelete, 400, m_gid, m_gid);
			Gob *pgobExpl = CreateAnimGob(m_wx + kwcTileHalf, m_wy + kwcTileHalf,
					kfAnmDeleteWhenDone | kfAnmSmokeFireLayer, "sexplosion.anir", NULL);
			if (pgobExpl != NULL)
				pgobExpl->SetOwner(m_pplr);

		OnUpdate

		OnMsg(kmidDelete)
			Delete();
			return knDeleted;

		OnMsg(kmidSpawnSmoke)
			return knNotHandled;

		// Eat all other messages (e.g., kmidHit, kmidNearbyAllyHit)

		DiscardMsgs

	//-----------------------------------------------------------------------

            return knHandled;
        }
    } else {
        return (int)StructGob::ProcessStateMachineMessage(st, pmsg);
    }
    return (int)StructGob::ProcessStateMachineMessage(st, pmsg);
}

} // namespace wi
