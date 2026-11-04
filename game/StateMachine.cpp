#include "game/ht.h"

namespace wi {

// Portions Copyright (C) Steve Rabin, 2000

// StateMachine methods

StateMachine::StateMachine()
{
	m_fForceStateChange = false;
	m_st = m_stNext = kstReservedGlobal;
}

int StateMachine::ProcessStateMachineMessage(State st, Message *pmsg)
{
	return knHandled;
}

// StateMachineMgr methods

StateMachineMgr::StateMachineMgr()
{
	m_pdmsgHead = NULL;
}

StateMachineMgr::~StateMachineMgr()
{
	ClearDelayedMessages();
}

bool StateMachineMgr::Init(TimerMgr *ptimm)
{
	m_ptimm = ptimm;
	ClearDelayedMessages();
	return true;
}

#define knVerStateMachineMgrState 1
bool StateMachineMgr::LoadState(Stream *pstm)
{
	// Version check

	byte nVer = pstm->ReadByte();
	if (nVer != knVerStateMachineMgrState)
		return false;

	// Read msg count

	int cmsgs = pstm->ReadWord();

	// Read in messages, restore delivery time

	long tCurrent = m_ptimm->GetTickCount();
	while (cmsgs-- != 0) {
		Message msg;
		if (pstm->Read(&msg, sizeof(msg)) == 0)
			return false;
		dword ctDelivery = pstm->ReadDword();
		msg.tDelivery = (int)(tCurrent + ctDelivery);
		StoreDelayedMessage(&msg);
	}

	return pstm->IsSuccess();
}

bool StateMachineMgr::SaveState(Stream *pstm)
{
	// Save version

	pstm->WriteByte(knVerStateMachineMgrState);

	// Save count of messages

	int cmsgs = 0;
	DelayedMessage *pdmsg;
	for (pdmsg = m_pdmsgHead; pdmsg != NULL; pdmsg = pdmsg->pdmsgNext)
		cmsgs++;
	pstm->WriteWord(cmsgs);

	// Save messages and time delta remaining for each message.

	long tCurrent = m_ptimm->GetTickCount();
	for (pdmsg = m_pdmsgHead; pdmsg != NULL; pdmsg = pdmsg->pdmsgNext) {
		pstm->Write(&pdmsg->msg, sizeof(pdmsg->msg));
		pstm->WriteDword((int)(pdmsg->msg.tDelivery - tCurrent));
	}

	return pstm->IsSuccess();
}

void StateMachineMgr::ClearDelayedMessages()
{
	DelayedMessage *pdmsg = m_pdmsgHead;
	while (pdmsg != NULL) {
		DelayedMessage *pdmsgNext = pdmsg->pdmsgNext;
		delete pdmsg;
		pdmsg = pdmsgNext;
	}
	m_pdmsgHead = NULL;
}

void StateMachineMgr::SendMsg(Message *pmsg)
{
	pmsg->tDelivery = 0;
	RouteMessage(pmsg);
}

void StateMachineMgr::SendMsg(MessageId mid, StateMachineId smidReceiver)
{
	Message msg;
	memset(&msg, 0, sizeof(msg));

	msg.mid = mid;
	msg.smidReceiver = smidReceiver;
	msg.smidSender = ksmidNull;

	RouteMessage(&msg);
}

void StateMachineMgr::SendMsg(MessageId mid, StateMachineId smidSender, StateMachineId smidReceiver)
{
	Message msg;
	memset(&msg, 0, sizeof(msg));

	msg.mid = mid;
	msg.smidSender = smidSender;
	msg.smidReceiver = smidReceiver;

	RouteMessage(&msg);
}

void StateMachineMgr::PostMsg(Message *pmsg)
{
	pmsg->tDelivery = 0;
	StoreDelayedMessage(pmsg);
}

void StateMachineMgr::PostMsg(MessageId mid, StateMachineId smidSender, StateMachineId smidReceiver)
{
	Message msg;
	memset(&msg, 0, sizeof(msg));

	msg.mid = mid;
	msg.smidSender = smidSender;
	msg.smidReceiver = smidReceiver;
	msg.tDelivery = 0;

	StoreDelayedMessage(&msg);
}

void StateMachineMgr::SendDelayedMsg(Message *pmsg, long tDelay)
{
	pmsg->tDelivery = (int)(m_ptimm->GetTickCount() + tDelay);
	RouteMessage(pmsg);
}

void StateMachineMgr::SendDelayedMsg(MessageId mid, long tDelay, StateMachineId smidSender, StateMachineId smidReceiver)
{
	Message msg;
	memset(&msg, 0, sizeof(msg));

	msg.mid = mid;
	msg.smidSender = smidSender;
	msg.smidReceiver = smidReceiver;
	msg.tDelivery = (int)(m_ptimm->GetTickCount() + tDelay);

	RouteMessage(&msg);
}

void StateMachineMgr::RouteMessage(Message *pmsg)
{
	extern int gcMessagesPerUpdate;
	gcMessagesPerUpdate++;


	StateMachine *psm = GetStateMachine(pmsg->smidReceiver);

	// If receiver doesn't exist anymore, discard the message

	if (psm == NULL)
		return;

	if (pmsg->tDelivery > m_ptimm->GetTickCount()) {

		// This message needs to be stored until it's time to send it
		// UNDONE: this can fail due to out of memory

		StoreDelayedMessage(pmsg);
		return;
	}

    // If SetState() was called outside of ProcessStateMachineMessage()
    // handling, change the state before processing this message so that the
    // message is processed in the correct state

	if (psm->m_fForceStateChange)
		ProcessStateChange(psm);

	// See if this specific state handles this message

#if 0 // def DEBUG_HELPERS
	if (psm->m_fDebug && pmsg->mid != kmidReservedUpdate)
		DebugBreak();
#endif

	int nRet = psm->ProcessMessage(psm->m_st, pmsg);
	if (nRet == knDeleted)
		return;

	// If not, try the "global" handler

	if (nRet == knNotHandled)
		nRet = psm->ProcessMessage(kstReservedGlobal, pmsg);

	// If this state machine has been deleted, no further processing

	if (nRet == knDeleted)
		return;

	// Process state change if a state change occured as part of the previous
	// message handling.

	if (psm->m_fForceStateChange)
		ProcessStateChange(psm);
}

void StateMachineMgr::ProcessStateChange(StateMachine *psm)
{
#ifdef DEBUG
	int cStateChanges = 0;
#endif

	// Check for a state change

	while (psm->m_fForceStateChange) {
		
		// NOTE: Circular logic (state changes causing state changes) could cause
		// an infinite loop here. We don't attempt to protect against this.

#ifdef DEBUG
		Assert(cStateChanges < 3);
		cStateChanges++;
#endif

		// Create a general msg for initializing and cleaning up the state change

		Message msgT;
		msgT.smidSender = msgT.smidReceiver = GetId(psm);
		msgT.tDelivery = 0;

		psm->m_fForceStateChange = false;

		// Let the last state clean-up. 
		// NOTE: this purposefully does NOT bubble up to the Global state handler

		msgT.mid = kmidReservedExit;
		int nRet = psm->ProcessMessage(psm->m_st, &msgT);
		if (nRet == knDeleted)
			return;

		// We don't allow Exit handlers to change the state. 
//		Assert(!psm->m_fForceStateChange);
		psm->m_fForceStateChange = false;

		// Set the new state

		psm->m_st = psm->m_stNext;

		// Let the new state initialize
        // NOTE: this purposefully does NOT bubble up to the Global state
        // handler

		msgT.mid = kmidReservedEnter;
		nRet = psm->ProcessMessage(psm->m_st, &msgT);
		if (nRet == knDeleted)
			return;
	}
}

// This function is called every game tick

void StateMachineMgr::DispatchDelayedMessages()
{
	DelayedMessage **ppdmsg = &m_pdmsgHead;

	while (*ppdmsg != NULL) {
		DelayedMessage *pdmsg = *ppdmsg;
		if (pdmsg->msg.tDelivery <= m_ptimm->GetTickCount()) {
			*ppdmsg = pdmsg->pdmsgNext;
			RouteMessage(&pdmsg->msg);
			delete pdmsg;
		} else {
			ppdmsg = &(*ppdmsg)->pdmsgNext;
		}
	}
}

bool StateMachineMgr::StoreDelayedMessage(Message *pmsg)
{
	// Store this message (in some data structure) for later routing

	// A priority queue would be the ideal data structure (but not required)
	// to store the delayed messages - Check out Mark Nelson's article
	// "Priority Queues and the STL" in the January 1996 Dr. Dobb's Journal
	// http://www.dogma.net/markn/articles/pq_stl/priority.htm

	// Note: In main game loop call SendDelayedMessages() every game 
	// tick to check if it's time to send the stored messages

	DelayedMessage *pdmsg = new DelayedMessage;
	Assert(pdmsg != NULL, "out of memory!");
	if (pdmsg == NULL)
		return false;

	pdmsg->msg = *pmsg;
	pdmsg->pdmsgNext = NULL;

	DelayedMessage **ppdmsg = &m_pdmsgHead;

	while (*ppdmsg != NULL)
		ppdmsg = &(*ppdmsg)->pdmsgNext;
	*ppdmsg = pdmsg;

	return true;
}



} // namespace wi
