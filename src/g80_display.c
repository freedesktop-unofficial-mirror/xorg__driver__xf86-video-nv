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

#include <float.h>
#include <math.h>
#include <strings.h>
#include <unistd.h>

#include "g80_type.h"
#include "g80_display.h"
#include "g80_output.h"

#define DPMS_SERVER
#include <X11/extensions/dpms.h>

typedef struct G80CrtcPrivRec {
    Head head;
    int pclk; /* Target pixel clock in kHz */
} G80CrtcPrivRec, *G80CrtcPrivPtr;

/*
 * PLL calculation.  pclk is in kHz.
 */
static void
G80CalcPLL(float pclk, int *pNA, int *pMA, int *pNB, int *pMB, int *pP)
{
    const float refclk = 27000.0f;
    const float minVcoA = 100000;
    const float maxVcoA = 400000;
    const float minVcoB = 600000;
    float maxVcoB = 1400000;
    const float minUA = 2000;
    const float maxUA = 400000;
    const float minUB = 50000;
    const float maxUB = 200000;
    const int minNA = 1, maxNA = 255;
    const int minNB = 1, maxNB = 31;
    const int minMA = 1, maxMA = 255;
    const int minMB = 1, maxMB = 31;
    const int minP = 0, maxP = 6;
    int lowP, highP;
    float vcoB;

    int na, ma, nb, mb, p;
    float bestError = FLT_MAX;

    *pNA = *pMA = *pNB = *pMB = *pP = 0;

    if(maxVcoB < pclk + pclk / 200)
        maxVcoB = pclk + pclk / 200;
    if(minVcoB / (1 << maxP) > pclk)
        pclk = minVcoB / (1 << maxP);

    vcoB = maxVcoB - maxVcoB / 200;
    lowP = minP;
    vcoB /= 1 << (lowP + 1);

    while(pclk <= vcoB && lowP < maxP)
    {
        vcoB /= 2;
        lowP++;
    }

    vcoB = maxVcoB + maxVcoB / 200;
    highP = lowP;
    vcoB /= 1 << (highP + 1);

    while(pclk <= vcoB && highP < maxP)
    {
        vcoB /= 2;
        highP++;
    }

    for(p = lowP; p <= highP; p++)
    {
        for(ma = minMA; ma <= maxMA; ma++)
        {
            if(refclk / ma < minUA)
                break;
            else if(refclk / ma > maxUA)
                continue;

            for(na = minNA; na <= maxNA; na++)
            {
                if(refclk * na / ma < minVcoA || refclk * na / ma > maxVcoA)
                    continue;

                for(mb = minMB; mb <= maxMB; mb++)
                {
                    if(refclk * na / ma / mb < minUB)
                        break;
                    else if(refclk * na / ma / mb > maxUB)
                        continue;

                    nb = rint(pclk * (1 << p) * (ma / (float)na) * mb / refclk);

                    if(nb > maxNB)
                        break;
                    else if(nb < minNB)
                        continue;
                    else
                    {
                        float freq = refclk * (na / (float)ma) * (nb / (float)mb) / (1 << p);
                        float error = fabsf(pclk - freq);
                        if(error < bestError) {
                            *pNA = na;
                            *pMA = ma;
                            *pNB = nb;
                            *pMB = mb;
                            *pP = p;
                            bestError = error;
                        }
                    }
                }
            }
        }
    }
}

static void
G80CrtcSetPClk(xf86CrtcPtr crtc)
{
    G80Ptr pNv = G80PTR(crtc->scrn);
    G80CrtcPrivPtr pPriv = crtc->driver_private;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
    const int headOff = 0x800 * pPriv->head;
    int lo_n, lo_m, hi_n, hi_m, p, i;
    CARD32 lo = pNv->reg[(0x00614104+headOff)/4];
    CARD32 hi = pNv->reg[(0x00614108+headOff)/4];

    pNv->reg[(0x00614100+headOff)/4] = 0x10000610;
    lo &= 0xff00ff00;
    hi &= 0x8000ff00;

    G80CalcPLL(pPriv->pclk, &lo_n, &lo_m, &hi_n, &hi_m, &p);

    lo |= (lo_m << 16) | lo_n;
    hi |= (p << 28) | (hi_m << 16) | hi_n;
    pNv->reg[(0x00614104+headOff)/4] = lo;
    pNv->reg[(0x00614108+headOff)/4] = hi;
    pNv->reg[(0x00614200+headOff)/4] = 0;

    for(i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];

        if(output->crtc != crtc)
            continue;
        G80OutputSetPClk(output, pPriv->pclk);
    }
}

void
G80DispCommand(ScrnInfoPtr pScrn, CARD32 addr, CARD32 data)
{
    G80Ptr pNv = G80PTR(pScrn);

    pNv->reg[0x00610304/4] = data;
    pNv->reg[0x00610300/4] = addr | 0x80010001;

    while(pNv->reg[0x00610300/4] & 0x80000000) {
        const int super = ffs((pNv->reg[0x00610024/4] >> 4) & 7);

        if(super) {
            if(super == 2) {
                xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
                const CARD32 r = pNv->reg[0x00610030/4];
                int i;

                for(i = 0; i < xf86_config->num_crtc; i++)
                {
                    xf86CrtcPtr crtc = xf86_config->crtc[i];
                    G80CrtcPrivPtr pPriv = crtc->driver_private;

                    if(r & (0x200 << pPriv->head))
                        G80CrtcSetPClk(crtc);
                }
            }

            pNv->reg[0x00610024/4] = 8 << super;
            pNv->reg[0x00610030/4] = 0x80000000;
        }
    }
}

Head
G80CrtcGetHead(xf86CrtcPtr crtc)
{
    G80CrtcPrivPtr pPriv = crtc->driver_private;
    return pPriv->head;
}

Bool
G80DispPreInit(ScrnInfoPtr pScrn)
{
    G80Ptr pNv = G80PTR(pScrn);

    pNv->reg[0x00610184/4] = pNv->reg[0x00614004/4];
    pNv->reg[0x00610190/4] = pNv->reg[0x00616100/4];
    pNv->reg[0x006101a0/4] = pNv->reg[0x00616900/4];
    pNv->reg[0x00610194/4] = pNv->reg[0x00616104/4];
    pNv->reg[0x006101a4/4] = pNv->reg[0x00616904/4];
    pNv->reg[0x00610198/4] = pNv->reg[0x00616108/4];
    pNv->reg[0x006101a8/4] = pNv->reg[0x00616908/4];
    pNv->reg[0x0061019C/4] = pNv->reg[0x0061610C/4];
    pNv->reg[0x006101ac/4] = pNv->reg[0x0061690c/4];
    pNv->reg[0x006101D0/4] = pNv->reg[0x0061A000/4];
    pNv->reg[0x006101D4/4] = pNv->reg[0x0061A800/4];
    pNv->reg[0x006101D8/4] = pNv->reg[0x0061B000/4];
    pNv->reg[0x006101E0/4] = pNv->reg[0x0061C000/4];
    pNv->reg[0x006101E4/4] = pNv->reg[0x0061C800/4];
    pNv->reg[0x0061c00c/4] = 0x03010700;
    pNv->reg[0x0061c010/4] = 0x0000152f;
    pNv->reg[0x0061c014/4] = 0x00000000;
    pNv->reg[0x0061c018/4] = 0x00245af8;
    pNv->reg[0x0061c80c/4] = 0x03010700;
    pNv->reg[0x0061c810/4] = 0x0000152f;
    pNv->reg[0x0061c814/4] = 0x00000000;
    pNv->reg[0x0061c818/4] = 0x00245af8;
    pNv->reg[0x0061A004/4] = 0x80550000;
    pNv->reg[0x0061A010/4] = 0x00000001;
    pNv->reg[0x0061A804/4] = 0x80550000;
    pNv->reg[0x0061A810/4] = 0x00000001;
    pNv->reg[0x0061B004/4] = 0x80550000;
    pNv->reg[0x0061B010/4] = 0x00000001;

    return TRUE;
}

Bool
G80DispInit(ScrnInfoPtr pScrn)
{
    G80Ptr pNv = G80PTR(pScrn);

    if(pNv->reg[0x00610024/4] & 0x100) {
        pNv->reg[0x00610024/4] = 0x100;
        pNv->reg[0x006194E8/4] &= ~1;
        while(pNv->reg[0x006194E8/4] & 2);
    }

    pNv->reg[0x00610200/4] = 0x2b00;
    while((pNv->reg[0x00610200/4] & 0x1e0000) != 0);
    pNv->reg[0x00610300/4] = 1;
    pNv->reg[0x00610200/4] = 0x1000b03;
    while(!(pNv->reg[0x00610200/4] & 0x40000000));

    C(0x00000084, 0);
    C(0x00000088, 0);
    C(0x00000874, 0);
    C(0x00000800, 0);
    C(0x00000810, 0);
    C(0x0000082C, 0);

    return TRUE;
}

void
G80DispShutdown(ScrnInfoPtr pScrn)
{
    G80Ptr pNv = G80PTR(pScrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    for(i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];

        G80CrtcBlankScreen(crtc, TRUE);
    }

    C(0x00000080, 0);

    for(i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];

        if(crtc->enabled) {
            const CARD32 mask = 4 << G80CrtcGetHead(crtc);

            pNv->reg[0x00610024/4] = mask;
            while(!(pNv->reg[0x00610024/4] & mask));
        }
    }

    pNv->reg[0x00610200/4] = 0;
    pNv->reg[0x00610300/4] = 0;
    while((pNv->reg[0x00610200/4] & 0x1e0000) != 0);
}

static Bool
G80CrtcModeFixup(xf86CrtcPtr crtc,
                 DisplayModePtr mode, DisplayModePtr adjusted_mode)
{
    // TODO: Fix up the mode here
    return TRUE;
}

static void
G80CrtcModeSet(xf86CrtcPtr crtc, DisplayModePtr mode,
               DisplayModePtr adjusted_mode, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    G80CrtcPrivPtr pPriv = crtc->driver_private;
    const int HDisplay = mode->HDisplay, VDisplay = mode->VDisplay;
    const int headOff = 0x400 * G80CrtcGetHead(crtc);
    int interlaceDiv, fudge;

    // TODO: Use adjusted_mode and fix it up in G80CrtcModeFixup
    pPriv->pclk = mode->Clock;

    /* Magic mode timing fudge factor */
    fudge = ((mode->Flags & V_INTERLACE) && (mode->Flags & V_DBLSCAN)) ? 2 : 1;
    interlaceDiv = (mode->Flags & V_INTERLACE) ? 2 : 1;

    C(0x00000804 + headOff, mode->Clock | 0x800000);
    C(0x00000808 + headOff, (mode->Flags & V_INTERLACE) ? 2 : 0);
    C(0x00000810 + headOff, 0);
    C(0x0000082C + headOff, 0);
    C(0x00000814 + headOff, mode->CrtcVTotal << 16 | mode->CrtcHTotal);
    C(0x00000818 + headOff,
        ((mode->CrtcVSyncEnd - mode->CrtcVSyncStart) / interlaceDiv - 1) << 16 |
        (mode->CrtcHSyncEnd - mode->CrtcHSyncStart - 1));
    C(0x0000081C + headOff,
        ((mode->CrtcVBlankEnd - mode->CrtcVSyncStart) / interlaceDiv - fudge) << 16 |
        (mode->CrtcHBlankEnd - mode->CrtcHSyncStart - 1));
    C(0x00000820 + headOff,
        ((mode->CrtcVTotal - mode->CrtcVSyncStart + mode->CrtcVBlankStart) / interlaceDiv - fudge) << 16 |
        (mode->CrtcHTotal - mode->CrtcHSyncStart + mode->CrtcHBlankStart - 1));
    if(mode->Flags & V_INTERLACE) {
        C(0x00000824 + headOff,
            ((mode->CrtcVTotal + mode->CrtcVBlankEnd - mode->CrtcVSyncStart) / 2 - 2) << 16 |
            ((2*mode->CrtcVTotal - mode->CrtcVSyncStart + mode->CrtcVBlankStart) / 2 - 2));
    }
    C(0x00000868 + headOff, pScrn->virtualY << 16 | pScrn->virtualX);
    C(0x0000086C + headOff, pScrn->displayWidth * (pScrn->bitsPerPixel / 8) | 0x100000);
    switch(pScrn->depth) {
        case  8: C(0x00000870 + headOff, 0x1E00); break;
        case 15: C(0x00000870 + headOff, 0xE900); break;
        case 16: C(0x00000870 + headOff, 0xE800); break;
        case 24: C(0x00000870 + headOff, 0xCF00); break;
    }
    C(0x000008A0 + headOff, 0);
    if((mode->Flags & V_DBLSCAN) || (mode->Flags & V_INTERLACE) ||
       mode->CrtcHDisplay != HDisplay || mode->CrtcVDisplay != VDisplay) {
        C(0x000008A4 + headOff, 9);
    } else {
        C(0x000008A4 + headOff, 0);
    }
    C(0x000008A8 + headOff, 0x40000);
    C(0x000008C0 + headOff, y << 16 | x);
    C(0x000008C8 + headOff, VDisplay << 16 | HDisplay);
    C(0x000008D4 + headOff, 0);
    C(0x000008D8 + headOff, mode->CrtcVDisplay << 16 | mode->CrtcHDisplay);
    C(0x000008DC + headOff, mode->CrtcVDisplay << 16 | mode->CrtcHDisplay);

    G80CrtcBlankScreen(crtc, FALSE);
}

void
G80CrtcBlankScreen(xf86CrtcPtr crtc, Bool blank)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    G80Ptr pNv = G80PTR(pScrn);
    const int headOff = 0x400 * G80CrtcGetHead(crtc);

    if(blank) {
        // G80DispHideCursor(pNv, FALSE);

        C(0x00000840 + headOff, 0);
        C(0x00000844 + headOff, 0);
        if(pNv->architecture != 0x50)
            C(0x0000085C + headOff, 0);
        C(0x00000874 + headOff, 0);
        if(pNv->architecture != 0x50)
            C(0x0000089C + headOff, 0);
    } else {
        C(0x00000860 + headOff, 0);
        C(0x00000864 + headOff, 0);
        pNv->reg[0x00610380/4] = 0;
        pNv->reg[0x00610384/4] = pNv->RamAmountKBytes * 1024 - 1;
        pNv->reg[0x00610388/4] = 0x150000;
        pNv->reg[0x0061038C/4] = 0;
        C(0x00000884 + headOff, (pNv->videoRam << 2) - 0x40);
        if(pNv->architecture != 0x50)
            C(0x0000089C + headOff, 1);
        // if(pNv->cursorVisible)
        //     G80DispShowCursor(pNv, FALSE);
        C(0x00000840 + headOff, pScrn->depth == 8 ? 0x80000000 : 0xc0000000);
        C(0x00000844 + headOff, (pNv->videoRam * 1024 - 0x5000) >> 8);
        if(pNv->architecture != 0x50)
            C(0x0000085C + headOff, 1);
        C(0x00000874 + headOff, 1);
    }
}

void
G80CrtcDPMSSet(xf86CrtcPtr crtc, int mode)
{
    ErrorF("CRTC dpms unimplemented\n");
#if 0
    G80Ptr pNv = G80PTR(pScrn);
    const int off = 0x800 * pNv->or;
    CARD32 tmp;

    /*
     * DPMSModeOn       everything on
     * DPMSModeStandby  hsync disabled, vsync enabled
     * DPMSModeSuspend  hsync enabled, vsync disabled
     * DPMSModeOff      sync disabled
     */
    switch(pNv->orType) {
    case DAC:
        while(pNv->reg[(0x0061A004+off)/4] & 0x80000000);

        tmp = pNv->reg[(0x0061A004+off)/4];
        tmp &= ~0x7f;
        tmp |= 0x80000000;

        if(mode == DPMSModeStandby || mode == DPMSModeOff)
            tmp |= 1;
        if(mode == DPMSModeSuspend || mode == DPMSModeOff)
            tmp |= 4;
        if(mode != DPMSModeOn)
            tmp |= 0x10;
        if(mode == DPMSModeOff)
            tmp |= 0x40;

        pNv->reg[(0x0061A004+off)/4] = tmp;

        break;

    case SOR:
        while(pNv->reg[(0x0061C004+off)/4] & 0x80000000);

        tmp = pNv->reg[(0x0061C004+off)/4];
        tmp |= 0x80000000;

        if(mode == DPMSModeOn)
            tmp |= 1;
        else
            tmp &= ~1;

        pNv->reg[(0x0061C004+off)/4] = tmp;

        break;
    }
#endif
}

/******************************** Cursor stuff ********************************/
void G80CrtcShowCursor(xf86CrtcPtr crtc, Bool update)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    const int headOff = 0x400 * G80CrtcGetHead(crtc);

    C(0x00000880 + headOff, 0x85000000);
    if(update) C(0x00000080, 0);
}

void G80CrtcHideCursor(xf86CrtcPtr crtc, Bool update)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    const int headOff = 0x400 * G80CrtcGetHead(crtc);

    C(0x00000880 + headOff, 0x5000000);
    if(update) C(0x00000080, 0);
}

void G80CrtcSetCursorPosition(xf86CrtcPtr crtc, int x, int y)
{
    G80Ptr pNv = G80PTR(crtc->scrn);
    const int headOff = 0x647000 + 0x1000*G80CrtcGetHead(crtc);

    x &= 0xffff;
    y &= 0xffff;
    pNv->reg[(0x84 + headOff)/4] = y << 16 | x;
    pNv->reg[(0x80 + headOff)/4] = 0;
}

/******************************** CRTC stuff ********************************/

static Bool
G80CrtcLock(xf86CrtcPtr crtc)
{
    return FALSE;
}

static void
G80CrtcPrepare(xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    ErrorF("Outputs:\n");
    for(i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];

        if(output->crtc) {
            G80CrtcPrivPtr pPriv = output->crtc->driver_private;
            ErrorF("\t%s -> HEAD%i\n", output->name, pPriv->head);
        } else {
            ErrorF("\t%s disconnected\n", output->name);
            output->funcs->mode_set(output, NULL, NULL);
        }
    }
}

static void
G80CrtcCommit(xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
    int i, crtc_mask = 0;

    /* If any heads are unused, blank them */
    for(i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];

        if(output->crtc)
            /* XXXagp: This assumes that xf86_config->crtc[i] is HEADi */
            crtc_mask |= 1 << G80CrtcGetHead(output->crtc);
    }

    for(i = 0; i < xf86_config->num_crtc; i++)
        if(!((1 << i) & crtc_mask))
            G80CrtcBlankScreen(xf86_config->crtc[i], TRUE);

    C(0x00000080, 0);
}

static const xf86CrtcFuncsRec g80_crtc_funcs = {
    .dpms = G80CrtcDPMSSet,
    .save = NULL,
    .restore = NULL,
    .lock = G80CrtcLock,
    .unlock = NULL,
    .mode_fixup = G80CrtcModeFixup,
    .prepare = G80CrtcPrepare,
    .mode_set = G80CrtcModeSet,
    // .gamma_set = G80DispGammaSet,
    .commit = G80CrtcCommit,
    .shadow_create = NULL,
    .shadow_destroy = NULL,
    .destroy = NULL,
};

void
G80DispCreateCrtcs(ScrnInfoPtr pScrn)
{
    Head head;
    xf86CrtcPtr crtc;
    G80CrtcPrivPtr g80_crtc;

    /* Create a "crtc" object for each head */
    for(head = HEAD0; head <= HEAD1; head++) {
        crtc = xf86CrtcCreate(pScrn, &g80_crtc_funcs);
        if(!crtc) return;

        g80_crtc = xnfcalloc(sizeof(*g80_crtc), 1);
        g80_crtc->head = head;
        crtc->driver_private = g80_crtc;
    }
}
