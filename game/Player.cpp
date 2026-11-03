#include "ht.h"
#include "game/completemanager.h"
#include "base/md5.h"

namespace wi {

#define knCreditDelta 18	//TUNE:
PlayerMgr gplrm;
Player *gpplrLocal;
Player gplrDummy;

//---------------------------------------------------------------------------
// PlayerMgr Implementation

PlayerMgr::PlayerMgr()
{
	m_aplr = NULL;
	Reset();
}

PlayerMgr::~PlayerMgr()
{
	delete[] m_aplr;
}

void PlayerMgr::Reset()
{
	delete[] m_aplr;
	m_aplr = new Player[kcPlayersMax];
	Assert(m_aplr != NULL, "out of memory!");
	if (m_aplr != NULL)
		memset(m_aplr, 0, sizeof(Player) * kcPlayersMax);
	gpplrLocal = NULL;
}

Player *PlayerMgr::AllocPlayer(word wf)
{
	Player *pplr = m_aplr;
	for (int i = 0; i < kcPlayersMax; i++, pplr++) {
		if (pplr->m_wf & kfPlrInUse)
			continue;

		pplr->Init(i);
		pplr->m_wf |= kfPlrInUse | wf;
		return pplr;
	}

	Assert("this shouldn't happen!");
	return NULL;
}

Player *PlayerMgr::GetPlayer(Side side) 
{
	Player *pplr = m_aplr;
	for (int i = 0; i < kcPlayersMax; i++, pplr++) {
		if (pplr->m_side == side)
			return pplr;
	}

	Assert("No player playing this side???");
	return NULL;
}

Player *PlayerMgr::GetNextPlayer(Player *pplr)
{
	int i = (pplr == NULL) ? 0 : pplr->m_pid + 1;
	pplr = &m_aplr[i];
	for (; i < kcPlayersMax; i++, pplr++) {
		if (pplr->m_wf & kfPlrInUse)
			return pplr;
	}

	return NULL;
}

Player *PlayerMgr::GetNextHumanPlayer(Player *pplr)
{
	int i = pplr == NULL ? 0 : pplr->m_pid + 1;
	pplr = &m_aplr[i];
	for (; i < kcPlayersMax; i++, pplr++) {
		if ((pplr->m_wf & (kfPlrInUse | kfPlrComputer)) == kfPlrInUse)
			return pplr;
	}

	return NULL;
}

int PlayerMgr::GetPlayerCount()
{
	int cplr = 0;
	Player *pplr = m_aplr;
	for (int i = 0; i < kcPlayersMax; i++, pplr++) {
		if (pplr->m_wf & kfPlrInUse)
			cplr++;
	}

	return cplr;
}

bool PlayerMgr::Init(char *pszLevel)
{
	Reset();

	Level *plvl = new Level();
	Assert(plvl != NULL, "out of memory!");
	if (plvl == NULL)
		return false;
	if (!plvl->LoadLevelInfo(pszLevel)) {
		delete plvl;
		return false;
	}

	for (int i = 0; i < kcSides; i++) {
		SideInfo *psidi = plvl->GetSideInfo(i);
		word wf = 0;
		switch (psidi->nIntelligence) {
		case knIntelligenceComputer:
		case knIntelligenceComputerNeutral:
			wf = kfPlrComputer;
			break;

		case knIntelligenceComputerOvermind:
			wf = kfPlrComputer | kfPlrComputerOvermind;
			break;
		}

		Player *pplr = AllocPlayer(wf);

		// The local player is the first human player allocated

		if (wf & kfPlrComputer) {
			pplr->SetHandicap(kfHcapHard);	// life is hard for computers because that's how the levels have been tuned
		} else if (gpplrLocal == NULL) {
			gpplrLocal = pplr;
			gpplrLocal->SetHandicap(gwfHandicap);
		}

		pplr->SetSide(i);
	}

	delete plvl;
	return true;
}

#define knVerPlayerMgrState 1
bool PlayerMgr::LoadState(Stream *pstm)
{
	byte nVer = pstm->ReadByte();
	if (nVer != knVerPlayerMgrState)
		return false;

	Reset();

	int cPlayers = pstm->ReadWord();
	Assert(cPlayers <= kcPlayersMax);

	while (cPlayers-- != 0) {
		Player *pplr = gplrm.AllocPlayer();
		if (pplr == NULL || !pplr->LoadState(pstm))
			return false;
	}
	return true;
}

void PlayerMgr::SetAllies(Player **applr, int cplrs, SideMask sidmAllies)
{
	// Set allies

	for (int n = 0; n < cplrs; n++) {
		applr[n]->SetAllies(sidmAllies);
	}

	// Recalc enemies. Clear first

	Gob *pgobT;
	for (pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {
		if (!(pgobT->GetFlags() & kfGobUnit))
			continue;
		UnitGob *punt = (UnitGob *)pgobT;
		punt->RecalcEnemyNearby(true);
	}

	// Now recalc

	for (pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {
		if (!(pgobT->GetFlags() & kfGobUnit))
			continue;
		UnitGob *punt = (UnitGob *)pgobT;
		punt->RecalcEnemyNearby(false);
	}

	// New allies share what they can see

	RevealSharedVision();
}

bool IsVisionSharedWithLocal(Player *pplr)
{
	if (pplr == NULL || gpplrLocal == NULL)
		return false;
	if (pplr == gpplrLocal)
		return true;
	Side side = pplr->GetSide();
	if (side == ksideNeutral)
		return false;
	return (gpplrLocal->GetAllies() & GetSideMask(side)) != 0 &&
			(pplr->GetAllies() & GetSideMask(gpplrLocal->GetSide())) != 0;
}

// Lifts the fog around every unit and building of the local player's allies.
// Units lift fog as they move, but buildings (and units standing still) only
// when they appear, which may be before the alliance was made.

void PlayerMgr::RevealSharedVision()
{
	if (gpplrLocal == NULL || gsim.GetLevel() == NULL)
		return;
	for (Gob *pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {
		if ((pgobT->GetFlags() & (kfGobUnit | kfGobActive)) != (kfGobUnit | kfGobActive))
			continue;
		UnitGob *punt = (UnitGob *)pgobT;
		if (punt->GetOwner() == gpplrLocal || !IsVisionSharedWithLocal(punt->GetOwner()))
			continue;
		punt->RevealFogAround();
	}
}

bool PlayerMgr::SaveState(Stream *pstm)
{
	pstm->WriteByte(knVerPlayerMgrState);
	pstm->WriteWord(gplrm.GetPlayerCount());
	for (Player *pplr = gplrm.GetNextPlayer(NULL); pplr != NULL; pplr = gplrm.GetNextPlayer(pplr))
		pplr->SaveState(pstm);
	return true;
}

#if 0 // not using anymore
bool PlayerMgr::IsSideInUse(Side side)
{
	Player *pplr = m_aplr;
	for (int i = 0; i < kcPlayersMax; i++, pplr++) {
		if (pplr->m_side == side)
			return true;
	}

	return false;
}
#endif

void PlayerMgr::Update(int cUpdates)
{
	Player *pplr = m_aplr;
	for (int i = 0; i < kcPlayersMax; i++, pplr++) {
		if (pplr->m_wf & kfPlrInUse)
			pplr->Update(cUpdates);
	}
}

int PlayerMgr::GetUnitInstanceCountFromMask(UnitMask um, word wfPlr)
{
	int cUnits = 0;
	Player *pplr = m_aplr;
	for (int i = 0; i < kcPlayersMax; i++, pplr++) {
		// If kfPlrComputer matches in both...
		if (((pplr->m_wf ^ wfPlr) & kfPlrComputer) == 0)
			cUnits += pplr->GetUnitInstanceCountFromMask(um);
	}
	return cUnits;
}


//---------------------------------------------------------------------------
// Player Implementation

Player::Player()
{
	Init(0);
}

Player::~Player()
{
	if (m_szFormalObjectiveInfo != NULL)
		gmmgr.FreePtr(m_szFormalObjectiveInfo);

	for (int i = 0; i < kcFormalObjectivesMax; i++) {
		if (m_aszFormalObjectiveText[i] != NULL)
			gmmgr.FreePtr(m_aszFormalObjectiveText[i]);
		if (m_aszFormalObjectiveStatus[i] != NULL)
			gmmgr.FreePtr(m_aszFormalObjectiveStatus[i]);
	}
}


void Player::Init(Pid pid) 
{
	m_wf = 0;
	m_pid = pid;
	m_nCredits = 0;
	m_nCreditsConsumed = m_nCreditsAcquired = 0;
	m_nDirCredits = 0;
	m_nConsumerCredits = knConsumerMax;
	m_side = ksideNeutral;
	m_wfUpgrades = 0;
	m_nPowerDemand = 0;
	m_nPowerSupply = 0;
	m_szName[0] = 0;
	m_sidmAllies = 0;
	m_umAllowed = kumAll;
	m_szObjective[0] = 0;
	m_tptDiscover.tx = m_tptDiscover.ty = 0;
	m_upgm = 0;
	m_upgmAllowed = kupgmAll;
	m_szFormalObjectiveInfo = NULL;
	m_cUpdatesRepairLast = 0;
	m_cStructsNeedCredits = 0; 
	m_wfHandicap = kfHcapDefault;

	memset(m_acut, 0, ARRAYSIZE(m_acut));
	memset(m_acutBuilt, 0, ARRAYSIZE(m_acutBuilt));

	for (int i = 0; i < kcFormalObjectivesMax; i++) {
		m_aszFormalObjectiveText[i] = NULL;
		m_aszFormalObjectiveStatus[i] = NULL;
	}
}

#define knVerPlayerState 11
bool Player::LoadState(Stream *pstm)
{
	byte nVer = pstm->ReadByte();
	if (nVer != knVerPlayerState)
		return false;
	m_wfUpgrades = pstm->ReadWord();
	m_side = pstm->ReadWord();
	m_sidmAllies = pstm->ReadWord();
	m_sidmDiscovered = pstm->ReadWord();
	m_tptDiscover.tx = pstm->ReadWord();
	m_tptDiscover.ty = pstm->ReadWord();
	pstm->ReadString(m_szName, sizeof(m_szName));
	m_wf = pstm->ReadWord();
	m_pid = pstm->ReadWord();
	m_nCredits = pstm->ReadDword();

	m_nCreditsAcquired = 0;
	m_nCreditsConsumed = 0;
	m_nPowerSupply = (short)pstm->ReadWord();
	m_nPowerDemand = (short)pstm->ReadWord();
	pstm->Read(m_acut, sizeof(m_acut));
	m_umAllowed = pstm->ReadDword();
	pstm->ReadString(m_szObjective, sizeof(m_szObjective));
	m_upgm = pstm->ReadWord();
	m_upgmAllowed = pstm->ReadWord();
	m_cUpdatesRepairLast = pstm->ReadDword();

	// Elaborate b.s. to save memory from dynamic heap on palm

	char szFormalObjectiveInfo[kcchObjectiveInfoMax];
	pstm->ReadString(szFormalObjectiveInfo, sizeof(szFormalObjectiveInfo));
	if (szFormalObjectiveInfo[0] == (char)0xff) {
		m_szFormalObjectiveInfo = NULL;
	} else {
		m_szFormalObjectiveInfo = (char *)gmmgr.AllocPtr(
                strlen(szFormalObjectiveInfo) + 1);
		gmmgr.WritePtr(m_szFormalObjectiveInfo, 0, szFormalObjectiveInfo,
                strlen(szFormalObjectiveInfo) + 1);
	}

	for (int i = 0; i < kcFormalObjectivesMax; i++) {
		char szFormalObjectiveText[kcchObjectiveMax];
		pstm->ReadString(szFormalObjectiveText, sizeof(szFormalObjectiveText));
		if (szFormalObjectiveText[0] == (char)0xff) {
			m_aszFormalObjectiveText[i] = NULL;
		} else {
			m_aszFormalObjectiveText[i] = (char *)gmmgr.AllocPtr(
                    strlen(szFormalObjectiveText) + 1);
			gmmgr.WritePtr(m_aszFormalObjectiveText[i], 0,
                    szFormalObjectiveText, strlen(szFormalObjectiveText) + 1);
		}

		char szFormalObjectiveStatus[kcchObjectiveStatusMax];
		pstm->ReadString(szFormalObjectiveStatus,
                sizeof(szFormalObjectiveStatus));
		if (szFormalObjectiveStatus[0] == (char)0xff) {
			m_aszFormalObjectiveStatus[i] = NULL;
		} else {
			m_aszFormalObjectiveStatus[i] = (char *)gmmgr.AllocPtr(
                    strlen(szFormalObjectiveStatus) + 1);
			gmmgr.WritePtr(m_aszFormalObjectiveStatus[i], 0,
                    szFormalObjectiveStatus,
                    strlen(szFormalObjectiveStatus) + 1);
		}
	}

	m_cmuntKilled = pstm->ReadWord();
	m_cstruKilled = pstm->ReadWord();
	m_cmuntLost = pstm->ReadWord();
	m_cstruLost = pstm->ReadWord();
	m_nTotalCreditsAcquired = pstm->ReadDword();
	m_nTotalCreditsConsumed = pstm->ReadDword();
	m_wfHandicap = pstm->ReadWord();

	// The local player is the first human player allocated

	if (!(m_wf & kfPlrComputer) && gpplrLocal == NULL) {
		gpplrLocal = this;
		gwfHandicap = m_wfHandicap;
	}

	return pstm->IsSuccess();
}

bool Player::SaveState(Stream *pstm)
{
	pstm->WriteByte(knVerPlayerState);
	pstm->WriteWord(m_wfUpgrades);
	pstm->WriteWord(m_side);
	pstm->WriteWord(m_sidmAllies);
	pstm->WriteWord(m_sidmDiscovered);
	pstm->WriteWord(m_tptDiscover.tx);
	pstm->WriteWord(m_tptDiscover.ty);
	pstm->WriteString(m_szName);
	pstm->WriteWord(m_wf);
	pstm->WriteWord(m_pid);
	pstm->WriteDword(m_nCredits);
	pstm->WriteWord(m_nPowerSupply);
	pstm->WriteWord(m_nPowerDemand);
	pstm->Write(m_acut, sizeof(m_acut));
	pstm->WriteDword(m_umAllowed);
	pstm->WriteString(m_szObjective);
	pstm->WriteWord(m_upgm);
	pstm->WriteWord(m_upgmAllowed);
	pstm->WriteDword(m_cUpdatesRepairLast);

	if (m_szFormalObjectiveInfo == NULL) {
		pstm->WriteByte(0xff);
		pstm->WriteByte(0);
	} else {
		pstm->WriteString(m_szFormalObjectiveInfo);
	}

	for (int i = 0; i < kcFormalObjectivesMax; i++) {
		if (m_aszFormalObjectiveText[i] == NULL) {
			pstm->WriteByte(0xff);
			pstm->WriteByte(0);
		} else {
			pstm->WriteString(m_aszFormalObjectiveText[i]);
		}
		if (m_aszFormalObjectiveStatus[i] == NULL) {
			pstm->WriteByte(0xff);
			pstm->WriteByte(0);
		} else {
			pstm->WriteString(m_aszFormalObjectiveStatus[i]);
		}
	}

	pstm->WriteWord(m_cmuntKilled);
	pstm->WriteWord(m_cstruKilled);
	pstm->WriteWord(m_cmuntLost);
	pstm->WriteWord(m_cstruLost);
	pstm->WriteDword(m_nTotalCreditsAcquired);
	pstm->WriteDword(m_nTotalCreditsConsumed);
	pstm->WriteWord(m_wfHandicap);

	return pstm->IsSuccess();
}

void Player::SetUpgrades(word wfUpgrades)
{
	word wfChange = wfUpgrades ^ m_wfUpgrades;
	if (wfChange == 0)
		return;
	m_wfUpgrades = wfUpgrades;

	if (wfUpgrades & kfUpgradeHrc)
		m_upgm |= kupgmAdvancedHRC;
	else
		m_upgm &= ~kupgmAdvancedHRC;
	if (wfUpgrades & kfUpgradeVts)
		m_upgm |= kupgmAdvancedVTS;
	else
		m_upgm &= ~kupgmAdvancedVTS;

	// Tell interested units that they are being upgraded; this gets them to wake up

	for (Gob *pgob = ggobm.GetFirstGob(); pgob != NULL; pgob = ggobm.GetNextGob(pgob)) {
		if (pgob->GetOwner() != this)
			continue;
		if ((pgob->GetFlags() & (kfGobActive | kfGobUnit)) == (kfGobActive | kfGobUnit)) {
			UnitGob *punt = (UnitGob *)pgob;
			MobileUnitBuilderConsts *pmubc = (MobileUnitBuilderConsts *)punt->GetConsts();
			switch (punt->GetType()) {
			case kgtVehicleTransportStation:
				if (wfChange & kfUpgradeVtsInProgress)
					gsmm.SendMsg((m_wfUpgrades & kfUpgradeVtsInProgress) ? kmidBeingUpgraded : kmidUpgradeComplete, punt->GetId());
				break;

			case kgtHumanResourceCenter:
				if (wfChange & kfUpgradeHrcInProgress)
					gsmm.SendMsg((m_wfUpgrades & kfUpgradeHrcInProgress) ? kmidBeingUpgraded : kmidUpgradeComplete, punt->GetId());
				break;
			}
		}
	}
}

void Player::ModifyNeedCreditsCount(int cDelta)
{
	// track how many units are waiting for credits so we can 
	// flash the UI if needed. Keep here so it can be re-updated
	// when loading a save game which happens before the UI is initialized.

	m_cStructsNeedCredits += cDelta;
	Assert((short)m_cStructsNeedCredits >= 0);
}

void Player::AddPowerSupplyAndDemand(int nPowerSupply, int nPowerDemand)
{
	bool fLowBefore = IsPowerLow();
	m_nPowerSupply += nPowerSupply;
	m_nPowerDemand += nPowerDemand;
	bool fLowAfter = IsPowerLow();

	// Notify this player's gobs that want to know that the power situation has changed

	if (fLowBefore != fLowAfter) {
		for (Gob *pgob = ggobm.GetFirstGob(); pgob != NULL; pgob = ggobm.GetNextGob(pgob)) {
			if (pgob->GetOwner() != this)
				continue;
			if ((pgob->GetFlags() & (kfGobActive | kfGobUnit)) == (kfGobActive | kfGobUnit)) {
				UnitGob *punt = (UnitGob *)pgob;
				if (punt->GetConsts()->wf & kfUntcNotifyPowerLowHigh) {
					gsmm.SendMsg(kmidPowerLowHigh, punt->GetId());
				}
			}
		}

		// Notify the of a power change that may affect if the radar is powered

		if (this == gpplrLocal) {
			gpmm->CalcPoweredRadar();
		}
	}
}

void Player::SetName(const char *pszName)
{
	strncpyz(m_szName, (char *)pszName, sizeof(m_szName));
}

UnitMask Player::GetUnitMask()
{
	UnitMask um = 0;

	for (int i = 0; i < kutMax; i++) {
		if (m_acut[i] != 0)
			um |= gapuntc[i]->um;
	}

	return um;
}

int Player::GetUnitActiveCountFromMask(UnitMask um) 
{
	int c = 0;
	for (int i = 0; i < kutMax; i++) {
		if (gapuntc[i]->um & um)
			c += m_acut[i];
	}

	return c;
}

int Player::GetUnitInstanceCountFromMask(UnitMask um)
{
	if (!(m_wf & kfPlrInUse))
		return 0;

	int cUnits = 0;
	for (Gob *pgob = ggobm.GetFirstGob(); pgob != NULL; pgob = ggobm.GetNextGob(pgob)) {
		if (pgob->GetFlags() & kfGobUnit) {
			UnitGob *punt = (UnitGob *)pgob;
			if (punt->GetOwner() == this) {
				if (punt->GetConsts()->um & um)
					cUnits++;
			}
		}
	}
	return cUnits;
}

int Player::GetCreditsDirection()
{
	int nDir = m_nDirCredits;
	m_nDirCredits = 0;
	return nDir;
}

int Player::GetCreditsConsumer()
{
	int nConsumer = m_nConsumerCredits;
	m_nConsumerCredits = knConsumerMax;
	return nConsumer;
}

void Player::SetCredits(int nCredits, bool fAffectTotals, int nConsumer)
{
	Assert(nCredits >= 0);

	if (nConsumer < m_nConsumerCredits)
		m_nConsumerCredits = nConsumer;

	if (nCredits > m_nCredits && m_nCredits != 0) {
		m_nDirCredits = 1;
	} else if (nCredits < m_nCredits && nCredits != 0) {
		if (m_nDirCredits == 0)
			m_nDirCredits = -1;
	}

	int dCredits = nCredits - m_nCredits;
	if (dCredits > 0)
		m_nTotalCreditsAcquired += dCredits;
	else
		m_nTotalCreditsConsumed -= dCredits;

	m_nCredits = nCredits;

	if (this == gpplrLocal && fAffectTotals) {
		if (dCredits < 0)
			m_nCreditsConsumed += -dCredits;
		else if (dCredits > 0)
			m_nCreditsAcquired += dCredits;
	}
}

void Player::SetObjective(char *psz)
{
	strncpyz(m_szObjective, psz, sizeof(m_szObjective));
}

void Player::Update(int cUpdates)
{
	// If auto repair is on, repair all damaged structures owned by this player.
	// TUNE:

#define kcupdRepair 6

	if (m_cUpdatesRepairLast == 0 || abs(m_cUpdatesRepairLast - cUpdates) >= kcupdRepair) {
		m_cUpdatesRepairLast = cUpdates;
		if (m_wf & kfPlrAutoRepair)
			Repair(true);
	}

	// new theory. Play a credit noise every delta credits

	if (m_nCreditsConsumed >= knCreditDelta) {
		m_nCreditsConsumed %= knCreditDelta;
		gsndm.PlaySfx(ksfxGameCreditsDecreasing);
	}

	if (m_nCreditsAcquired >= knCreditDelta) {
		m_nCreditsAcquired %= knCreditDelta;
		gsndm.PlaySfx(ksfxGameCreditsIncreasing);
	}
}

void Player::Repair(bool fOn)
{
	if (fOn)
		m_wf |= kfPlrAutoRepair;
	else
		m_wf &= ~kfPlrAutoRepair;

	for (Gob *pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {
		if (pgobT->GetOwner() != this)
			continue;

		if ((pgobT->GetFlags() & (kfGobStructure | kfGobActive)) != (kfGobStructure | kfGobActive))
			continue;

		StructGob *pstru = (StructGob *)pgobT;

		if (fOn) {
			if (!(pstru->GetUnitFlags() & kfUnitRepairing)) {
				if (pstru->GetHealth() < divfx(pstru->GetConsts()->GetArmorStrength(), itofx(3)))
					pstru->Repair(true);
			}
		} else {
			if (pstru->GetUnitFlags() & kfUnitRepairing)
				pstru->Repair(false);
		}
	}
}

void Player::SetFormalObjectiveText(int iObjective, char *pszText)
{
	if (m_aszFormalObjectiveText[iObjective] != NULL)
		gmmgr.FreePtr(m_aszFormalObjectiveText[iObjective]);
	m_aszFormalObjectiveText[iObjective] = (char *)gmmgr.AllocPtr(strlen(pszText) + 1);
	gmmgr.WritePtr(m_aszFormalObjectiveText[iObjective], 0, pszText, strlen(pszText) + 1);
	SetFormalObjectiveStatus(iObjective, "Incomplete");
}

void Player::SetFormalObjectiveStatus(int iObjective, char *pszText)
{
	if (m_aszFormalObjectiveStatus[iObjective] != NULL)
		gmmgr.FreePtr(m_aszFormalObjectiveStatus[iObjective]);
	m_aszFormalObjectiveStatus[iObjective] = (char *)gmmgr.AllocPtr(strlen(pszText) + 1);
	gmmgr.WritePtr(m_aszFormalObjectiveStatus[iObjective], 0, pszText, strlen(pszText) + 1);
}

void Player::SetFormalObjectiveInfo(char *pszText)
{
	if (m_szFormalObjectiveInfo != NULL)
		gmmgr.FreePtr(m_szFormalObjectiveInfo);
	m_szFormalObjectiveInfo = (char *)gmmgr.AllocPtr(strlen(pszText) + 1);
	gmmgr.WritePtr(m_szFormalObjectiveInfo, 0, pszText, strlen(pszText) + 1);
}

//
// Objectives / Win Summary / Lose Summary form
//

class ObjectivesForm : public ShellForm {
public:
	ObjectivesForm(Player *pplr, int so, bool fForceInfoDisplay) secPlayer;
	void UpdateStatistics() secPlayer;

	// ShellForm overrides

	virtual bool Init(FormMgr *pfrmm, IniReader *pini, word idf) secPlayer;
	virtual void OnControlSelected(word idc) secPlayer;

private:
	Player *m_pplr;
	int m_so;
	static bool s_fStatistics;
};

bool ObjectivesForm::s_fStatistics = false;

ObjectivesForm::ObjectivesForm(Player *pplr, int so, bool fForceInfoDisplay)
{
	m_pplr = pplr;
	m_so = so;
	if (fForceInfoDisplay)
		s_fStatistics = false;
}

bool ObjectivesForm::Init(FormMgr *pfrmm, IniReader *pini, word idf)
{
	if (!ShellForm::Init(pfrmm, pini, idf))
		return false;

	LabelControl *plbl;
	plbl = (LabelControl *)GetControlPtr(kidcMissionTitle);
	char szT[kcbLevelTitle];
	strncpyz(szT, gsim.GetLevel()->GetTitle(), sizeof(szT));
	HtStrupr(szT);
	plbl->SetText(szT);

	for (int i = 0; i < kcFormalObjectivesMax; i++) {
		plbl = (LabelControl *)GetControlPtr(kidcObjectiveText1 + i);
		plbl->SetText(m_pplr->GetFormalObjectiveText(i));
		plbl = (LabelControl *)GetControlPtr(kidcObjectiveStatus1 + i);
		plbl->SetText(m_pplr->GetFormalObjectiveStatus(i));
	}

	UpdateStatistics();

	// Stop the animation. Will anyone people notice?

	m_fAnimate = false;

	return true;
}

void ObjectivesForm::UpdateStatistics()
{
	char szT[100];
	LabelControl *plbl;
	if (m_so == ksoObjectives) {
		plbl = (LabelControl *)GetControlPtr(kidcObjectiveInfo);
		plbl->SetText(m_pplr->GetFormalObjectiveInfo());

		for (int i = 0; i < m_cctl; i++) {
			Control *pctl = m_apctl[i];
			int idc = pctl->GetId();
			if (idc >= kidcPage1 && idc < kidcPage2)
				pctl->Show(!s_fStatistics);
			else if (idc >= kidcPage2 && idc < kidcPage3)
				pctl->Show(s_fStatistics);
		}
	}
	
	plbl = (LabelControl *)GetControlPtr(kidcMobileUnitsKilled);
	itoa(m_pplr->GetEnemyMobileUnitsKilled(), szT, 10);
	plbl->SetText(szT);

	plbl = (LabelControl *)GetControlPtr(kidcStructuresKilled);
	itoa(m_pplr->GetEnemyStructuresKilled(), szT, 10);
	plbl->SetText(szT);

	plbl = (LabelControl *)GetControlPtr(kidcMobileUnitsLost);
	itoa(m_pplr->GetMobileUnitsLost(), szT, 10);
	plbl->SetText(szT);

	plbl = (LabelControl *)GetControlPtr(kidcStructuresLost);
	itoa(m_pplr->GetStructuresLost(), szT, 10);
	plbl->SetText(szT);

	plbl = (LabelControl *)GetControlPtr(kidcCreditsAction);
	sprintf(szT, "%d / %d", m_pplr->GetTotalCreditsAcquired(), m_pplr->GetTotalCreditsConsumed());
	plbl->SetText(szT);

	plbl = (LabelControl *)GetControlPtr(kidcGameTime);
	long t = gsim.GetTickCount();
	int nHour = (int)(t / (100 * 60 * 60));
	t -= nHour * (100L * 60 * 60);
	int nMin = (int)(t / (100 * 60));
	Assert(nMin < 60);
	t -= nMin * (100L * 60);
	int nSec = (int)(t / 100);
	Assert(nSec < 60);
	sprintf(szT, "%02d:%02d:%02d", nHour, nMin, nSec);
	plbl->SetText(szT);
}

void ObjectivesForm::OnControlSelected(word idc)
{
	if (idc == kidcStatistics || idc == kidcInfo) {
		s_fStatistics = !s_fStatistics;
		UpdateStatistics();
	} else {
		ShellForm::OnControlSelected(idc);
	}
}

int Player::ShowObjectives(int so, bool fForceInfoDisplay, bool fAborting)
{
    // The default for losing is knGoAbortLevel. This way, if someone exits
    // while the form is up, knGoAbortLevel will be returned and the game
    // won't be saved. The default for winning, is knGoSuccess. This way,
    // if someone exits while the form is up, the game will be saved, which
    // will mean the mission re-loads and can auto-advance.

	int idf;
    int nGo;

	switch (so) {
	case ksoObjectives:
		idf = kidfObjectives;
        nGo = knGoSuccess;
		break;

	case ksoWinSummary:
		if (m_wf & kfPlrSummaryShown)
			return knGoSuccess;
		m_wf |= kfPlrSummaryShown | kfPlrWinner;
		idf = kidfWinSummary;
        if (!fAborting) {
            // Mark current game complete.
            // Doing it here ensures it is done when the mission is
            // completed.
            gpcptm->MarkComplete(&ggame.GetLastMissionIdentifier());
        }
        nGo = knGoSuccess;
		break;

	case ksoLoseSummary:
		if (m_wf & kfPlrSummaryShown)
			return knGoSuccess;
		m_wf |= kfPlrSummaryShown | kfPlrLoser;
		idf = kidfLoseSummary;
        nGo = knGoAbortLevel;
		break;

	default:
		Assert("Invalid objective screen type passed to ShowObjectives");
		return knGoInitFailure;
	}

lbTryAgain:
	ObjectivesForm *pfrm = (ObjectivesForm *)gpmfrmm->LoadForm(gpiniForms, idf,
            new ObjectivesForm(this, so, fForceInfoDisplay));
	if (pfrm == NULL) {
		Assert("Objectives form failed to load!");
		return knGoInitFailure;
	}

	if (fAborting) {
		LabelControl *plbl;
		plbl = (LabelControl *)pfrm->GetControlPtr(kidcMissionResult);
		if (plbl != NULL)
			plbl->SetText("MISSION ABORTED");
	}

	if (so != ksoObjectives) {
		gsndm.PlaySfx((idf == kidfLoseSummary || fAborting) ? ksfxGameLoseLevel : ksfxGameWinLevel);
    }

	int idc;
	pfrm->DoModal(&idc, true, so == ksoObjectives);
	delete pfrm;

	if (!gevm.IsAppStopping()) {
		switch (idc) {
		case kidcLoadGame:
			{
				Stream *pstm = PickLoadGameStream();
				if (pstm == NULL)
					goto lbTryAgain;

				gpstmSavedGame = pstm;
				nGo = knGoLoadSavedGame;
			}
			break;

		case kidcAbortMission:
		case kidcRestartMission:
			nGo = idc == kidcAbortMission ? knGoAbortLevel : knGoRetryLevel;
			break;
		}
	} else {
        // The game is exiting. If the level was lost, then don't save
        // the re-initialize game, otherwise the user will be able
        // to exit while this form is up, restart the game, and advance
        // to the next level.

        if (nGo != knGoSuccess) {
            ggame.SkipSaveReinitializeGame();
        }
    }

	return nGo;
}

} // namespace wi
