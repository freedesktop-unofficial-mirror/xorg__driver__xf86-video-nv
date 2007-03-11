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

#include <unistd.h>

#include "g80_type.h"
#include "g80_display.h"
#include "g80_output.h"

static void
G80DacSetPClk(xf86OutputPtr output, int pclk)
{
    G80Ptr pNv = G80PTR(output->scrn);
    G80OutputPrivPtr pPriv = output->driver_private;
    const int orOff = 0x800 * pPriv->or;

    pNv->reg[(0x00614280+orOff)/4] = 0;
}

static void
G80DacDPMSSet(xf86OutputPtr output, int mode)
{
    ErrorF("DAC dpms unimplemented\n");
}

static void
G80DacModeSet(xf86OutputPtr output, DisplayModePtr mode,
              DisplayModePtr adjusted_mode)
{
    ScrnInfoPtr pScrn = output->scrn;
    G80OutputPrivPtr pPriv = output->driver_private;
    const int dacOff = 0x80 * pPriv->or;

    if(adjusted_mode)
        ErrorF("DAC%i mode %s -> HEAD%i\n", pPriv->or, adjusted_mode->name, G80CrtcGetHead(output->crtc));

    if(!adjusted_mode) {
        C(0x00000400 + dacOff, 0);
        return;
    }

    C(0x00000400 + dacOff,
        (G80CrtcGetHead(output->crtc) == HEAD0 ? 1 : 2) | 0x40);
    C(0x00000404 + dacOff,
        (adjusted_mode->Flags & V_NHSYNC) ? 1 : 0 |
        (adjusted_mode->Flags & V_NVSYNC) ? 2 : 0);
}

/*
 * Perform DAC load detection to determine if there is a connected display.
 */
static xf86OutputStatus
G80DacDetect(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    G80Ptr pNv = G80PTR(pScrn);
    G80OutputPrivPtr pPriv = output->driver_private;
    const int scrnIndex = pScrn->scrnIndex;
    const int dacOff = 2048 * pPriv->or;
    CARD32 load, tmp;

    xf86DrvMsg(scrnIndex, X_PROBED, "Trying load detection on VGA%i ... ",
            pPriv->or);

    pNv->reg[(0x0061A010+dacOff)/4] = 0x00000001;
    pNv->reg[(0x0061A004+dacOff)/4] = 0x80150000;
    while(pNv->reg[(0x0061A004+dacOff)/4] & 0x80000000);
    tmp = pNv->architecture == 0x50 ? 420 : 340;
    pNv->reg[(0x0061A00C+dacOff)/4] = tmp | 0x100000;
    usleep(4500);
    load = pNv->reg[(0x0061A00C+dacOff)/4];
    pNv->reg[(0x0061A00C+dacOff)/4] = 0;
    pNv->reg[(0x0061A004+dacOff)/4] = 0x80550000;

    // Use this DAC if all three channels show load.
    if((load & 0x38000000) == 0x38000000) {
        xf86ErrorF("found one!\n");
        return XF86OutputStatusConnected;
    }

    xf86ErrorF("nothing.\n");
    return XF86OutputStatusDisconnected;
}

static void
G80DacDestroy(xf86OutputPtr output)
{
    G80OutputDestroy(output);

    xfree(output->driver_private);
    output->driver_private = NULL;
}

static const xf86OutputFuncsRec G80DacOutputFuncs = {
    .dpms = G80DacDPMSSet,
    .save = NULL,
    .restore = NULL,
    .mode_valid = G80OutputModeValid,
    .mode_fixup = G80OutputModeFixup,
    .prepare = G80OutputPrepare,
    .commit = G80OutputCommit,
    .mode_set = G80DacModeSet,
    .detect = G80DacDetect,
    .get_modes = G80OutputGetDDCModes,
    .destroy = G80DacDestroy,
};

xf86OutputPtr
G80CreateDac(ScrnInfoPtr pScrn, ORNum or, int i2cPort)
{
    G80OutputPrivPtr pPriv = xnfcalloc(sizeof(*pPriv), 1);
    xf86OutputPtr output;
    char orName[5];

    if(!pPriv)
        return FALSE;

    snprintf(orName, 5, "VGA%i", or);
    output = xf86OutputCreate(pScrn, &G80DacOutputFuncs, orName);

    pPriv->type = DAC;
    pPriv->or = or;
    pPriv->set_pclk = G80DacSetPClk;
    output->driver_private = pPriv;
    output->interlaceAllowed = TRUE;
    output->doubleScanAllowed = TRUE;

    /* Create an I2C object */
    G80I2CInit(output, i2cPort);

    return output;
}
