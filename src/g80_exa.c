/*
 * Copyright (c) 2007 NVIDIA, Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "g80_type.h"
#include "g80_dma.h"

static void
waitMarker(ScreenPtr pScreen, int marker)
{
    G80Sync(xf86Screens[pScreen->myNum]);
}

static Bool
prepareSolid(PixmapPtr      pPixmap,
             int            alu,
             Pixel          planemask,
             Pixel          fg)
{
    return FALSE;
}

static Bool
checkComposite(int          op,
               PicturePtr   pSrcPicture,
               PicturePtr   pMaskPicture,
               PicturePtr   pDstPicture)
{
    return FALSE;
}

static Bool
prepareCopy(PixmapPtr       pSrcPixmap,
            PixmapPtr       pDstPixmap,
            int             dx,
            int             dy,
            int             alu,
            Pixel           planemask)
{
    return FALSE;
}

Bool G80ExaInit(ScreenPtr pScreen, ScrnInfoPtr pScrn)
{
    G80Ptr pNv = G80PTR(pScrn);
    ExaDriverPtr exa;
    const int pitch = pScrn->displayWidth * (pScrn->bitsPerPixel / 8);

    exa = pNv->exa = exaDriverAlloc();
    if(!exa) return FALSE;

    exa->exa_major         = EXA_VERSION_MAJOR;
    exa->exa_minor         = EXA_VERSION_MINOR;
    exa->memoryBase        = pNv->mem;
    exa->offScreenBase     = pitch * pScrn->virtualY;
    exa->memorySize        = pitch * pNv->offscreenHeight;
    exa->pixmapOffsetAlign = 256;
    exa->pixmapPitchAlign  = 256;
    exa->flags             = EXA_OFFSCREEN_PIXMAPS;
    exa->maxX              = 8192;
    exa->maxY              = 8192;

    /**** Rendering ops ****/
    exa->PrepareSolid     = prepareSolid;
    //exa->Solid            = solid;
    //exa->DoneSolid        = doneSolid;
    exa->PrepareCopy      = prepareCopy;
    //exa->Copy             = copy;
    //exa->DoneCopy         = doneCopy;
    exa->CheckComposite   = checkComposite;
    //exa->PrepareComposite = prepareComposite;
    //exa->Composite        = composite;
    //exa->DoneComposite    = doneComposite;

    exa->WaitMarker       = waitMarker;

    return exaDriverInit(pScreen, exa);
}
