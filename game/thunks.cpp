#include "ht.h"

#include "game/ops.h"

namespace wi {

#include "miscgraphics.cpp"

void FillShadowThunk(byte *pbDst, int cbRowDst, int cx, int cy, byte *aclrMap)
{
	Shadow(pbDst, cbRowDst, cx, cy, aclrMap);
}

void FillThunk(byte *pbDst, int cx, int cy, int cbStride, byte bFill)
{
	Fill(pbDst, cx, cy, cbStride, bFill);
}

void LeftToRightBltThunk(byte *pbSrc, int cxSrcStride, byte *pbDst, int cxDstStride, int cx, int cy)
{
	LeftToRightBlt(pbSrc, cxSrcStride, pbDst, cxDstStride, cx, cy);
}

void RightToLeftBltThunk(byte *pbSrc, int cxSrcStride, byte *pbDst, int cxDstStride, int cx, int cy)
{
	RightToLeftBlt(pbSrc, cxSrcStride, pbDst, cxDstStride, cx, cy);
}

void DrawTileMapThunk(byte **ppbMap, int ctx, int cty, byte *pbDst, int cbDstStride, int cxLeftTile, int cyTopTile, int cxRightTile, int cyBottomTile, int ctxInside, int ctyInside, int cxTile, int cyTile)
{
 	DrawTileMap(ppbMap, ctx, cty, pbDst, cbDstStride, cxLeftTile, cyTopTile, cxRightTile, cyBottomTile, ctxInside, ctyInside, cxTile, cyTile);
}

word Compile8Thunk(byte *pb, ScanData *psd, bool fOdd)
{
	return Compile8(pb, psd, fOdd);
}

void DrawDispatchThunk(byte *pb, byte *pbSrc, byte *pbDst, int cbReturn, dword *mpscaiclrSide, byte *mpiclriclrShadow)
{
	DrawDispatch(pb, pbSrc, pbDst, cbReturn, mpscaiclrSide, mpiclriclrShadow);
}

} // namespace wi
