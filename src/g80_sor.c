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

#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#include <X11/Xatom.h>

#include "g80_type.h"
#include "g80_display.h"
#include "g80_output.h"

static void
G80SorSetPClk(xf86OutputPtr output, int pclk)
{
    G80Ptr pNv = G80PTR(output->scrn);
    G80OutputPrivPtr pPriv = output->driver_private;
    const int orOff = 0x800 * pPriv->or;
    const int limit = 165000;

    pNv->reg[(0x00614300+orOff)/4] = (pclk > limit) ? 0x101 : 0;
}

static void
G80SorDPMSSet(xf86OutputPtr output, int mode)
{
    G80Ptr pNv = G80PTR(output->scrn);
    G80OutputPrivPtr pPriv = output->driver_private;
    const int off = 0x800 * pPriv->or;
    CARD32 tmp;

    while(pNv->reg[(0x0061C004+off)/4] & 0x80000000);

    tmp = pNv->reg[(0x0061C004+off)/4];
    tmp |= 0x80000000;

    if(mode == DPMSModeOn)
        tmp |= 1;
    else
        tmp &= ~1;

    pNv->reg[(0x0061C004+off)/4] = tmp;
    while((pNv->reg[(0x61C030+off)/4] & 0x10000000));
}

static int
G80TMDSModeValid(xf86OutputPtr output, DisplayModePtr mode)
{
    // Disable dual-link modes until I can find a way to make them work
    // reliably.
    if (mode->Clock > 165000)
        return MODE_CLOCK_HIGH;

    return G80OutputModeValid(output, mode);
}

static int
G80LVDSModeValid(xf86OutputPtr output, DisplayModePtr mode)
{
    G80OutputPrivPtr pPriv = output->driver_private;
    DisplayModePtr native = pPriv->nativeMode;

    // Ignore modes larger than the native res.
    if (mode->HDisplay > native->HDisplay || mode->VDisplay > native->VDisplay)
        return MODE_PANEL;

    return G80OutputModeValid(output, mode);
}

static void
G80SorModeSet(xf86OutputPtr output, DisplayModePtr mode,
              DisplayModePtr adjusted_mode)
{
    ScrnInfoPtr pScrn = output->scrn;
    G80OutputPrivPtr pPriv = output->driver_private;
    const int sorOff = 0x40 * pPriv->or;
    CARD32 type;

    if(!adjusted_mode) {
        /* Disconnect the SOR */
        C(0x00000600 + sorOff, 0);
        return;
    }

    if(pPriv->panelType == LVDS)
        type = 0;
    else if(adjusted_mode->Clock > 165000)
        type = 0x500;
    else
        type = 0x100;

    // This wouldn't be necessary, but the server is stupid and calls
    // G80SorDPMSSet after the output is disconnected, even though the hardware
    // turns it off automatically.
    G80SorDPMSSet(output, DPMSModeOn);

    C(0x00000600 + sorOff,
        (G80CrtcGetHead(output->crtc) == HEAD0 ? 1 : 2) |
        type |
        ((adjusted_mode->Flags & V_NHSYNC) ? 0x1000 : 0) |
        ((adjusted_mode->Flags & V_NVSYNC) ? 0x2000 : 0));
}

static xf86OutputStatus
G80SorDetect(xf86OutputPtr output)
{
    G80OutputPrivPtr pPriv = output->driver_private;

    /* Assume physical status isn't going to change before the BlockHandler */
    if(pPriv->cached_status != XF86OutputStatusUnknown)
        return pPriv->cached_status;

    G80OutputPartnersDetect(pPriv->partner, output, pPriv->i2c);
    return pPriv->cached_status;
}

static xf86OutputStatus
G80SorLVDSDetect(xf86OutputPtr output)
{
    /* Assume LVDS is always connected */
    return XF86OutputStatusConnected;
}

static void
G80SorDestroy(xf86OutputPtr output)
{
    G80OutputPrivPtr pPriv = output->driver_private;

    G80OutputDestroy(output);

    if(pPriv->nativeMode) {
        if(pPriv->nativeMode->name)
            xfree(pPriv->nativeMode->name);
        xfree(pPriv->nativeMode);
    }

    xfree(output->driver_private);
    output->driver_private = NULL;
}

/******************** LVDS ********************/
static Bool
G80SorModeFixupScale(xf86OutputPtr output, DisplayModePtr mode,
                     DisplayModePtr adjusted_mode)
{
    G80OutputPrivPtr pPriv = output->driver_private;
    DisplayModePtr native = pPriv->nativeMode;

    // Stash the saved mode timings in adjusted_mode
    adjusted_mode->Clock = native->Clock;
    adjusted_mode->Flags = native->Flags;
    adjusted_mode->CrtcHDisplay = native->CrtcHDisplay;
    adjusted_mode->CrtcHBlankStart = native->CrtcHBlankStart;
    adjusted_mode->CrtcHSyncStart = native->CrtcHSyncStart;
    adjusted_mode->CrtcHSyncEnd = native->CrtcHSyncEnd;
    adjusted_mode->CrtcHBlankEnd = native->CrtcHBlankEnd;
    adjusted_mode->CrtcHTotal = native->CrtcHTotal;
    adjusted_mode->CrtcHSkew = native->CrtcHSkew;
    adjusted_mode->CrtcVDisplay = native->CrtcVDisplay;
    adjusted_mode->CrtcVBlankStart = native->CrtcVBlankStart;
    adjusted_mode->CrtcVSyncStart = native->CrtcVSyncStart;
    adjusted_mode->CrtcVSyncEnd = native->CrtcVSyncEnd;
    adjusted_mode->CrtcVBlankEnd = native->CrtcVBlankEnd;
    adjusted_mode->CrtcVTotal = native->CrtcVTotal;
    adjusted_mode->CrtcHAdjusted = native->CrtcHAdjusted;
    adjusted_mode->CrtcVAdjusted = native->CrtcVAdjusted;

    // This mode is already "fixed"
    G80CrtcSkipModeFixup(output->crtc);

    return TRUE;
}

static DisplayModePtr
G80SorGetLVDSModes(xf86OutputPtr output)
{
    G80OutputPrivPtr pPriv = output->driver_private;
    return xf86DuplicateMode(pPriv->nativeMode);
}

#ifdef RANDR_12_INTERFACE
#define MAKE_ATOM(a) MakeAtom((a), sizeof(a) - 1, TRUE);

struct property {
    Atom atom;
    INT32 range[2];
};

static struct {
    struct property dither;
} properties;

static void
G80SorCreateResources(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    G80Ptr pNv = G80PTR(pScrn);
    int data, err;

    properties.dither.atom = MAKE_ATOM("dither");
    properties.dither.range[0] = 0;
    properties.dither.range[1] = 1;
    err = RRConfigureOutputProperty(output->randr_output,
                                    properties.dither.atom, FALSE, TRUE, FALSE,
                                    2, properties.dither.range);
    if(err)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to configure dithering property for %s: error %d\n",
                   output->name, err);

    // Set the default value
    data = pNv->Dither;
    err = RRChangeOutputProperty(output->randr_output, properties.dither.atom,
                                 XA_INTEGER, 32, PropModeReplace, 1, &data,
                                 FALSE, FALSE);
    if(err)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to set dithering property for %s: error %d\n",
                   output->name, err);
}

static Bool
G80SorSetProperty(xf86OutputPtr output, Atom prop, RRPropertyValuePtr val)
{
    if(prop == properties.dither.atom) {
        INT32 i;

        if(val->type != XA_INTEGER || val->format != 32 || val->size != 1)
            return FALSE;

        i = *(INT32*)val->data;
        if(i < properties.dither.range[0] || i > properties.dither.range[1])
            return FALSE;

        G80CrtcSetDither(output->crtc, i, TRUE);
    }

    return TRUE;
}
#endif // RANDR_12_INTERFACE

static const xf86OutputFuncsRec G80SorTMDSOutputFuncs = {
    .dpms = G80SorDPMSSet,
    .save = NULL,
    .restore = NULL,
    .mode_valid = G80TMDSModeValid,
    .mode_fixup = G80OutputModeFixup,
    .prepare = G80OutputPrepare,
    .commit = G80OutputCommit,
    .mode_set = G80SorModeSet,
    .detect = G80SorDetect,
    .get_modes = G80OutputGetDDCModes,
#ifdef RANDR_12_INTERFACE
    .create_resources = G80SorCreateResources,
    .set_property = G80SorSetProperty,
#endif
    .destroy = G80SorDestroy,
};

static const xf86OutputFuncsRec G80SorLVDSOutputFuncs = {
    .dpms = G80SorDPMSSet,
    .save = NULL,
    .restore = NULL,
    .mode_valid = G80LVDSModeValid,
    .mode_fixup = G80SorModeFixupScale,
    .prepare = G80OutputPrepare,
    .commit = G80OutputCommit,
    .mode_set = G80SorModeSet,
    .detect = G80SorLVDSDetect,
    .get_modes = G80SorGetLVDSModes,
#ifdef RANDR_12_INTERFACE
    .create_resources = G80SorCreateResources,
    .set_property = G80SorSetProperty,
#endif
    .destroy = G80SorDestroy,
};

static DisplayModePtr
GetLVDSNativeMode(G80Ptr pNv)
{
    DisplayModePtr mode = xnfcalloc(1, sizeof(DisplayModeRec));
    const CARD32 size = pNv->reg[0x00610B4C/4];
    const int width = size & 0x3fff;
    const int height = (size >> 16) & 0x3fff;

    mode->HDisplay = mode->CrtcHDisplay = width;
    mode->VDisplay = mode->CrtcVDisplay = height;
    mode->Clock           = pNv->reg[0x610AD4/4] & 0x3fffff;
    mode->CrtcHBlankStart = pNv->reg[0x610AFC/4];
    mode->CrtcHSyncEnd    = pNv->reg[0x610B04/4];
    mode->CrtcHBlankEnd   = pNv->reg[0x610AE8/4];
    mode->CrtcHTotal      = pNv->reg[0x610AF4/4];

    mode->next = mode->prev = NULL;
    mode->status = MODE_OK;
    mode->type = M_T_DRIVER | M_T_PREFERRED;

    xf86SetModeDefaultName(mode);

    return mode;
}

xf86OutputPtr
G80CreateSor(ScrnInfoPtr pScrn, ORNum or, PanelType panelType)
{
    G80Ptr pNv = G80PTR(pScrn);
    G80OutputPrivPtr pPriv = xnfcalloc(sizeof(*pPriv), 1);
    const int off = 0x800 * or;
    xf86OutputPtr output;
    char orName[5];
    const xf86OutputFuncsRec *funcs;

    if(!pPriv)
        return FALSE;

    if(panelType == LVDS) {
        strcpy(orName, "LVDS");
        funcs = &G80SorLVDSOutputFuncs;
    } else {
        snprintf(orName, 5, "DVI%d", or);
        pNv->reg[(0x61C00C+off)/4] = 0x03010700;
        pNv->reg[(0x61C010+off)/4] = 0x0000152f;
        pNv->reg[(0x61C014+off)/4] = 0x00000000;
        pNv->reg[(0x61C018+off)/4] = 0x00245af8;
        funcs = &G80SorTMDSOutputFuncs;
    }

    output = xf86OutputCreate(pScrn, funcs, orName);

    pPriv->type = SOR;
    pPriv->or = or;
    pPriv->panelType = panelType;
    pPriv->cached_status = XF86OutputStatusUnknown;
    if(panelType == TMDS)
        pPriv->set_pclk = G80SorSetPClk;
    output->driver_private = pPriv;
    output->interlaceAllowed = TRUE;
    output->doubleScanAllowed = TRUE;

    if(panelType == LVDS) {
        pPriv->nativeMode = GetLVDSNativeMode(pNv);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s native size %dx%d\n",
                   orName, pPriv->nativeMode->HDisplay,
                   pPriv->nativeMode->VDisplay);
    }

    return output;
}
