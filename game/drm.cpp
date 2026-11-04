#include "ht.h"

namespace wi {

// The code is derived from the Owner name. The Owner name is backed up and is the same
// after a cold boot; that's why we use it. The Code is simply a non-obvious binary
// translation of the name into a form that can be validated.

struct Code // code
{
	byte ab[4];
	byte bXor;
	byte bSum;
	byte abHash[2];
};

// DRM key that gets saved to / loaded from prefs

Key gkey;
bool gfDemo = false;

bool GetCode(char *szName, Code *pcode, bool fShowError = true) secDrm;
bool ValidateCode(Code *pcode) secDrm;
void GetKeyFromCode(Code *pcode, Key *pkey) secDrm;
bool CompareKeys(Key *pkeyA, Key *pkeyB) secDrm;
dword HashBytes(byte *pb, int cb) secDrm;
void FillTemplate(char *psz, char *pszFormat, byte *pb) secDrm;
bool ValidateRetailKey(Key *pkey) secDrm;

dword HashBytes(byte *pb, int cb)
{
	dword dwHash = 0;
	while (cb-- != 0)
		dwHash = (dwHash << 5) + dwHash + *pb++;
	return dwHash;
}

int DrmGetRandom(long *plSeed) secDrm;
int DrmGetRandom(long *plSeed)
{
    return (((*plSeed) = (*plSeed) * 214013L + 2531011L) >> 16) & 0x7fff;
}

bool GetCode(char *szName, Code *pcode, bool fShowError)
{
	if (szName == NULL) {
		char szNameT[64];
		if (!HostGetOwnerName(szNameT, sizeof(szNameT), fShowError))
			return false;
		szName = szNameT;
	}

	long lSeed = (long)HashBytes((byte *)szName, (int)strlen(szName));

	// Serialize in endian independent way

	Code code;
	memset(&code, 0, sizeof(code));

	int n;
	for (n = 0; n < 4; n++) {
		code.ab[n] = (byte)DrmGetRandom(&lSeed);
		code.bXor ^= code.ab[n];
		code.bSum += code.ab[n];
	}

	// Hash what we have so far - more validation

	word wHash = (word)(HashBytes(code.ab, 6) >> 16);
	code.abHash[0] = (byte)wHash;
	code.abHash[1] = (byte)(wHash >> 8);

	// Now xor the first 6 with abHash[0]

	byte *pb = (byte *)&code;
	for (n = 0; n < 6; n++)
		*pb++ ^= code.abHash[0];

	// Now xor the first 7 with abHash[1]

	pb = (byte *)&code;
	for (n = 0; n < 7; n++)
		*pb++ ^= code.abHash[1];

	// Done

	*pcode = code;
	Assert(ValidateCode(&code));
	return true;
}

bool ValidateCode(Code *pcode)
{
	Code code = *pcode;

	// Now xor the first 7 with abHash[1]

	byte *pb = (byte *)&code;
	int n;
	for (n = 0; n < 7; n++)
		*pb++ ^= code.abHash[1];

	// Now xor the first 6 with abHash[0]

	pb = (byte *)&code;
	for (n = 0; n < 6; n++)
		*pb++ ^= code.abHash[0];

	// Hash the first 6 and compare to our hash

	word wHash = (word)(HashBytes(code.ab, 6) >> 16);
	if (code.abHash[0] != (byte)wHash)
		return false;
	if (code.abHash[1] != (byte)(wHash >> 8))
		return false;

	// Xor and add the first 4

	byte bXor = 0;
	byte bSum = 0;
	for (n = 0; n < 4; n++) {
		bXor ^= code.ab[n];
		bSum += code.ab[n];
	}
	if (bXor != code.bXor)
		return false;
	if (bSum != code.bSum)
		return false;

	return true;
}

void GetKeyFromCode(Code *pcode, Key *pkey)
{
	byte *abCode = (byte *)pcode;
	int cbCode = sizeof(*pcode);

	// Generate a seed

	long lSeed = 0x29a;
	int n;
	for (n = 0; n < cbCode; n++)
		lSeed = lSeed * abCode[n] + n;

	// Initialize rotors

	byte abR1[256];
	byte abR2[256];
	byte abR3[256];

	for (n = 0; n < 256; n++)
		abR1[n] = n;
	memset(abR2, 0, sizeof(abR2));
	memset(abR3, 0, sizeof(abR3));

	for (n = 0; n < 256; n++) {
		lSeed = 3 * lSeed + abCode[n & 7];
		long lRandom = lSeed % 65521;
		int ibR1 = (lRandom & 255) % (256 - n);
		lRandom >>= 8;
		byte bTemp = abR1[255 - n];
		abR1[255 - n] = abR1[ibR1];
		abR1[ibR1] = bTemp;
		if (abR3[255 - n] == 0) {
			int nT = (lRandom & 255) % (255 - n);
			while (abR3[nT] != 0)
				nT = (nT + 1) % (255 - n);
			abR3[255 - n] = nT & 255;
			abR3[nT] = 255 - n;
		}
	}
	for (n = 0; n < 256; n++)
		abR2[abR1[n]] = n;

	// Encrypt the code into a key

	int nT = 0;
	byte *abKey = (byte *)pkey;
	int cbKey = sizeof(*pkey);
	for (n = 0; n < cbKey; n++) {
		int nSlotR1 = abR1[(n + nT) & 255];
		int nSlotR3 = abR3[nSlotR1 & 255];
		int nSlotR2 = abR2[nSlotR3 & 255];
		abKey[n] = abR2[nSlotR2 & 255] - nT;
		nT = (nT + 1) & 255;
	}
}

bool CompareKeys(Key *pkeyA, Key *pkeyB)
{
	byte *pbA = (byte *)pkeyA;
	byte *pbB = (byte *)pkeyB;
	int cb = sizeof(*pkeyA);
	while (cb-- != 0) {
		if (*pbA++ != *pbB++)
			return false;
	}
	return true;
}

bool ValidateRetailKey(Key *pkey)
{
#ifdef DRM_KEYONLY
	char szT[4];

	// Derive the "name" from the key

	szT[0] = pkey->ab[0];
	long lSeed = (long)szT[0];
	szT[1] = pkey->ab[1] ^ (byte)DrmGetRandom(&lSeed);
	szT[2] = pkey->ab[2] ^ (byte)DrmGetRandom(&lSeed);
	szT[3] = 0;

	// Fill in zeros

	int n;
	for (n = 0; n < sizeof(szT) - 1; n++) {
		while (szT[n] == 0)
			szT[n] = (byte)DrmGetRandom(&lSeed);
	}

	// Get code

	Code code;
	GetCode(szT, &code);

	// Gen full key from this

	Key keyT;
	GetKeyFromCode(&code, &keyT);

	// Compare keys - compares the last 6 bytes because we copy over the first 3

	Key keyReconstructed = *pkey;
	for (n = 0; n < sizeof(szT) - 1; n++)
		keyReconstructed.ab[n] = keyT.ab[n];

	// Now compare these two keys

	return CompareKeys(&keyT, &keyReconstructed);
#else
	return false;
#endif
}

//
// UI
//

class DrmCodeForm : public ShellForm
{
public:
	virtual bool Init(FormMgr *pfrmm, IniReader *pini, word idf) secDrm;
	virtual void OnControlSelected(word idc) secDrm;
};

class DrmKeyForm : public ShellForm
{
public:
	virtual bool Init(FormMgr *pfrmm, IniReader *pini, word idf) secDrm;
	virtual void OnControlSelected(word idc) secDrm;

private:
	void OnBackspace() secDrm;
	void OnEnterNumber(int nNumber) secDrm;
	void ParseKey(Key *pkey) secDrm;
};

bool DrmValidate()
{
#ifndef DRM
	return true;
#else
#ifdef DRM_KEYONLY
	// Validate key

	if (ValidateRetailKey(&gkey))
		return true;

	// Key Invalid, bring up UI

	ShellForm *pfrm = (ShellForm *)gpmfrmm->LoadForm(gpiniForms, kidfDrmKey, new DrmKeyForm());
	if (pfrm == NULL)
		return false;
	bool fSuccess = pfrm->DoModal();
	delete pfrm;
	return fSuccess;

#else

	// If our key is valid, return success

	Code code;
	if (!GetCode(NULL, &code))
		return false;
		
	Key keyT;
	GetKeyFromCode(&code, &keyT);


	// Compare keys

	if (CompareKeys(&gkey, &keyT))
		return true;

	// No valid key; bring up UI

	ShellForm *pfrm = (ShellForm *)gpmfrmm->LoadForm(gpiniForms, kidfDrmCode, new DrmCodeForm());
	if (pfrm == NULL)
		return false;
	gfDemo = !pfrm->DoModal();
	delete pfrm;
	return true;
#endif
#endif
}

void FillTemplate(char *psz, char *pszFormat, byte *pb)
{
	char *pchSrc = pszFormat;
	char *pchDst = psz;

	bool fMSB = true;
	byte bT = 0;
	int cb = (int)strlen(pszFormat) + 1;
	while (cb-- != 0) {
		if (*pchSrc != '?') {
			*pchDst++ = *pchSrc++;
			continue;
		}

		int nNibble = 0;
		if (fMSB) {
			fMSB = false;
			bT = *pb++;
			nNibble = (bT >> 4) & 0x0f;
		} else {
			fMSB = true;
			nNibble = (bT & 0x0f);
		}

		char chT;
		if (nNibble < 0x0a) {
			chT = '0' + nNibble;
		} else {
			chT = 'A' + nNibble - 0x0a;
		}

		*pchDst++ = chT;
		pchSrc++;
	}
}

//
// DrmCodeForm implementation
//

bool DrmCodeForm::Init(FormMgr *pfrmm, IniReader *pini, word idf)
{
	if (!ShellForm::Init(pfrmm, pini, idf))
		return false;

	Code code;
	if (!GetCode(NULL, &code))
		return false;

	char szT[32];
	FillTemplate(szT, "C-\?\?\?\?-\?\?\?\?-\?\?\?\?-\?\?\?\?", (byte *)&code);
	
	LabelControl *plbl = (LabelControl *)GetControlPtr(kidcCode);
	plbl->SetText(szT);

	return true;
}

void DrmCodeForm::OnControlSelected(word idc)
{
	switch (idc) {
	case kidcPlayDemo:
		break;

	case kidcEnterKey:
		{
			ShellForm *pfrm = (ShellForm *)gpmfrmm->LoadForm(gpiniForms, kidfDrmKey, new DrmKeyForm());
			if (pfrm == NULL)
				break;
			bool f = pfrm->DoModal();
			delete pfrm;
			if (f) {
				m_idcLast = kidcOk;
				break;
			}
			return;
		}
		break;
	}

	ShellForm::OnControlSelected(idc);
}

//
// DrmKeyForm implementation
//

bool DrmKeyForm::Init(FormMgr *pfrmm, IniReader *pini, word idf)
{
	return ShellForm::Init(pfrmm, pini, idf);
}

void DrmKeyForm::OnEnterNumber(int nNumber)
{
	char szT[32];
	LabelControl *plbl = (LabelControl *)GetControlPtr(kidcKey);
	strncpyz(szT, plbl->GetText(), sizeof(szT));

	// Find first '?'

	int cch = (int)strlen(szT);
	int n;
	for (n = 0; n < cch; n++) {
		if (szT[n] == '?')
			break;
	}
	if (n == cch)
		return;

	// Put new number there

	char ch = nNumber < 0xa ? '0' + nNumber : 'A' + nNumber - 0xa;
	szT[n] = ch;
	plbl->SetText(szT);
}

void DrmKeyForm::OnBackspace()
{
	char szT[32];
	LabelControl *plbl = (LabelControl *)GetControlPtr(kidcKey);
	strncpyz(szT, plbl->GetText(), sizeof(szT));

	// Find last number

	int cch = (int)strlen(szT);
	int n;
	for (n = cch - 1; n >= 0; n--) {
		if ((szT[n] >= '0' && szT[n] <= '9') || (szT[n] >= 'A' && szT[n] <= 'F'))
			break;
	}
	if (n < 0)
		return;

	// Put '?' there

	szT[n] = '?';
	plbl->SetText(szT);
}

void DrmKeyForm::ParseKey(Key *pkey)
{
	// Init key to 0

	byte *pb = (byte *)pkey;
	int cb = sizeof(*pkey);
	memset(pb, 0, cb);

	// Parse out key

	LabelControl *plbl = (LabelControl *)GetControlPtr(kidcKey);
	const char *psz = plbl->GetText();

	// Find last number

	int cch = (int)strlen(psz);
	int n;
	bool fMSB = true;
	for (n = 0; n < cch; n++) {
		if ((psz[n] >= '0' && psz[n] <= '9') || (psz[n] >= 'A' && psz[n] <= 'F')) {
			int nNibble;
			if (psz[n] >= 'A') {
				nNibble = psz[n] - 'A' + 0xa;
			} else {
				nNibble = psz[n] - '0';
			}
			if (fMSB) {
				fMSB = false;
				*pb = nNibble << 4;
			} else {
				fMSB = true;
				*pb |= nNibble;
				pb++;
			}
		}
	}
}

void DrmKeyForm::OnControlSelected(word idc)
{
	switch (idc) {
	case kidcOk:
		{
			// Valid?

			Key key;
			ParseKey(&key);

#ifdef DRM_KEYONLY
			bool fSuccess = ValidateRetailKey(&key);
#else
			Code code;
			GetCode(NULL, &code);
			Key keyT;
			GetKeyFromCode(&code, &keyT);
			bool fSuccess = CompareKeys(&key, &keyT);
#endif
			if (!fSuccess) {
				HtMessageBox(kfMbWhiteBorder, "License Key", "The key you have entered is invalid!");
				return;
			}

			// Good key. Save prefs.

			HtMessageBox(kfMbWhiteBorder, "License Key", "Thank you.");
			gkey = key;
			ggame.SavePreferences();
		}

		// fall through

	case kidcCancel:
		ShellForm::OnControlSelected(idc);
		break;

	case kidcBackspace:
		OnBackspace();
		break;

	default:
		OnEnterNumber(idc - kidc0);
		break;
	}
}

} // namespace wi
