#if 0

#if 0
struct Frame {
	word ibsrEven;
	word ibsrOdd;
};

struct TBitmapSRHeader {
	word cx;
	word cy;
	word cra;
	word ibra;
	word cFrames;
	Frame aframe[1];
};

class TBitmapSR : public TBitmap
{
public:
	TBitmapSR() secTBitmap;
	virtual ~TBitmapSR() secTBitmap;
	virtual bool Init(void *pv, FileMap *pfmap) secTBitmap;
	virtual void BltTo(class DibBitmap *pbmDst, int xDst, int yDst, byte *aclrMap = NULL, Rect *prcSrc = NULL) secTBitmap;
	virtual void GetSize(Size *psiz) secTBitmap;

private:
	FileMap m_fmap;
	TBitmapSRHeader *m_pbmh;
	dword *m_ppfn;
};
#endif

#include "ht.h"

struct ScanRecord {
	word officlr;
	word iraTimes4;
};

struct RunArgs {
	byte op;
	byte cpSrc;
	byte cpArgs;
	byte cpDst;
};


TBitmapSR::TBitmapSR()
{
	m_pbmh = NULL;
	m_ppfn = NULL;
}

TBitmapSR::~TBitmapSR()
{
	gmmgr.DbMemFree(m_ppfn);
	if (m_pbmh != NULL) {
		UnmapFile(&m_fmap);
		m_pbmh = NULL;
	}
}

bool TBitmapSR::Init(void *pv, FileMap *pfmap)
{
	// Init run op function pointers

	TBitmapSRHeader *pbmh = (TBitmapSRHeader *)pv;


	// Done

	m_pbmh = pbmh;
	m_fmap = *pfmap;
	return true;
}


void TBitmapSR::BltTo(DibBitmap *pbmDst, int xDst, int yDst, byte *aclrMap, Rect *prcSrc)
{
	// Get dib dimensions

	Size sizT;
	pbmDst->GetSize(&sizT);

	// Source rect to blt from

	Rect rcSrc;
	if (prcSrc != NULL) {
		rcSrc = *prcSrc;
	} else {
		rcSrc.Set(0, 0, BigWord(m_pbmh->cx), BigWord(m_pbmh->cy));
	}

	// Right and bottom edge clipping

	if (sizT.cx - xDst < rcSrc.Width())
		rcSrc.right = rcSrc.left + sizT.cx - xDst;
	if (sizT.cy - yDst < rcSrc.Height())
		rcSrc.bottom = rcSrc.top + sizT.cy - yDst;

	// Before we much with xDst, get the alignment right

	Frame *pframe = &m_pbmh->aframe[0];
	word ibsr = (xDst & 1) ? BigWord(pframe->ibsrOdd) : BigWord(pframe->ibsrEven);

	// Left and top edge clipping

	if (xDst < 0) {
		rcSrc.left -= xDst;
		xDst = 0;
	}
	if (yDst < 0) {
		rcSrc.top -= yDst;
		yDst = 0;
	}

	// Anything to blt?

	if (rcSrc.IsEmpty())
		return;

#ifdef STATS_DISPLAY
	extern int gcBitmapsDrawn;
	gcBitmapsDrawn++;
#endif

	ScanRecord *psr = (ScanRecord *)((byte *)m_pbmh + ibsr + rcSrc.top * sizeof(ScanRecord));
	int cbRowDst = (sizT.cx + 1) & ~1;
	byte *pbBits = pbmDst->GetBits();

//temp
//	BitmapType *pbmpScreen = WinGetBitmap(WinGetDisplayWindow());
//	pbBits = (byte *)BmpGetBits(pbmpScreen);
//	FillEvenEven8(pbBits, 160, 160, 160, 0);
//end temp

	byte *pbDst = pbBits + yDst * cbRowDst + xDst;

	if (aclrMap == NULL)
		aclrMap = gaclrIdentity;

	if (rcSrc.left == 0) {
		if (rcSrc.Width() != BigWord(m_pbmh->cx)) {
			// Right clip

		} else {
			// No clip
		}
	} else {
		// left clip

	}

}

void TBitmapSR::GetSize(Size *psiz)
{
	psiz->cx = BigWord(m_pbmh->cx);
	psiz->cy = BigWord(m_pbmh->cy);
}

#endif
