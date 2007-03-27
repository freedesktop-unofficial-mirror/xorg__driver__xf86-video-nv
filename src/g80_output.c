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

#include <strings.h>
#include <xf86DDC.h>

#include "g80_type.h"
#include "g80_ddc.h"
#include "g80_output.h"

static Bool G80ReadPortMapping(int scrnIndex, G80Ptr pNv)
{
    unsigned char *table2;
    unsigned char headerSize, entries;
    int i;
    CARD16 a;
    CARD32 b;

    /* Clear the i2c map to invalid */
    for(i = 0; i < 4; i++)
        pNv->i2cMap[i].dac = pNv->i2cMap[i].sor = -1;

    if(*(CARD16*)pNv->table1 != 0xaa55) goto fail;

    a = *(CARD16*)(pNv->table1 + 0x36);
    table2 = (unsigned char*)pNv->table1 + a;

    if(table2[0] != 0x40) goto fail;

    b = *(CARD32*)(table2 + 6);
    if(b != 0x4edcbdcb) goto fail;

    headerSize = table2[1];
    entries = table2[2];

    for(i = 0; i < entries; i++) {
        CARD32 type, port;
        ORNum or;

        b = *(CARD32*)&table2[headerSize + 8*i];
        type = b & 0xf;
        port = (b >> 4) & 0xf;
        or = ffs((b >> 24) & 0xf) - 1;

        if(type < 4 && port != 0xf) {
            switch(type) {
                case 0: /* CRT */
                case 1: /* TV */
                    if(pNv->i2cMap[port].dac != -1) {
                        xf86DrvMsg(scrnIndex, X_WARNING,
                                   "DDC routing table corrupt!  DAC %i -> %i "
                                   "for port %i\n",
                                   or, pNv->i2cMap[port].dac, port);
                    }
                    pNv->i2cMap[port].dac = or;
                    break;
                case 2: /* TMDS */
                case 3: /* LVDS */
                    if(pNv->i2cMap[port].sor != -1)
                        xf86DrvMsg(scrnIndex, X_WARNING,
                                   "DDC routing table corrupt!  SOR %i -> %i "
                                   "for port %i\n",
                                   or, pNv->i2cMap[port].sor, port);
                    pNv->i2cMap[port].sor = or;
                    break;
            }
        }
    }

    xf86DrvMsg(scrnIndex, X_PROBED, "I2C map:\n");
    for(i = 0; i < 4; i++) {
        if(pNv->i2cMap[i].dac != -1)
            xf86DrvMsg(scrnIndex, X_PROBED, "  Bus %i -> DAC%i\n", i, pNv->i2cMap[i].dac);
        if(pNv->i2cMap[i].sor != -1)
            xf86DrvMsg(scrnIndex, X_PROBED, "  Bus %i -> SOR%i\n", i, pNv->i2cMap[i].sor);
    }

    return TRUE;

fail:
    xf86DrvMsg(scrnIndex, X_ERROR, "Couldn't find the DDC routing table.  "
               "Mode setting will probably fail!\n");
    return FALSE;
}

static void G80_I2CPutBits(I2CBusPtr b, int clock, int data)
{
    G80Ptr pNv = G80PTR(xf86Screens[b->scrnIndex]);
    const int off = b->DriverPrivate.val * 0x18;

    pNv->reg[(0x0000E138+off)/4] = 4 | clock | data << 1;
}

static void G80_I2CGetBits(I2CBusPtr b, int *clock, int *data)
{
    G80Ptr pNv = G80PTR(xf86Screens[b->scrnIndex]);
    const int off = b->DriverPrivate.val * 0x18;
    unsigned char val;

    val = pNv->reg[(0x0000E138+off)/4];
    *clock = !!(val & 1);
    *data = !!(val & 2);
}

Bool
G80I2CInit(xf86OutputPtr output, const int port)
{
    G80OutputPrivPtr pPriv = output->driver_private;
    I2CBusPtr i2c;

    /* Allocate the I2C bus structure */
    i2c = xf86CreateI2CBusRec();
    if(!i2c) return FALSE;

    i2c->BusName = output->name;
    i2c->scrnIndex = output->scrn->scrnIndex;
    i2c->I2CPutBits = G80_I2CPutBits;
    i2c->I2CGetBits = G80_I2CGetBits;
    i2c->ByteTimeout = 2200; /* VESA DDC spec 3 p. 43 (+10 %) */
    i2c->StartTimeout = 550;
    i2c->BitTimeout = 40;
    i2c->ByteTimeout = 40;
    i2c->AcknTimeout = 40;
    i2c->DriverPrivate.val = port;

    if(xf86I2CBusInit(i2c)) {
        pPriv->i2c = i2c;
        return TRUE;
    } else {
        xfree(i2c);
        return FALSE;
    }
}

void
G80OutputSetPClk(xf86OutputPtr output, int pclk)
{
    G80OutputPrivPtr pPriv = output->driver_private;
    pPriv->set_pclk(output, pclk);
}

int
G80OutputModeValid(xf86OutputPtr output, DisplayModePtr mode)
{
    if(mode->Clock > 400000 || mode->Clock < 25000)
        return MODE_CLOCK_RANGE;

    return MODE_OK;
}

Bool
G80OutputModeFixup(xf86OutputPtr output, DisplayModePtr mode,
                   DisplayModePtr adjusted_mode)
{
    return TRUE;
}

void
G80OutputPrepare(xf86OutputPtr output)
{
}

void
G80OutputCommit(xf86OutputPtr output)
{
}

DisplayModePtr
G80OutputGetDDCModes(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    G80Ptr pNv = G80PTR(pScrn);
    G80OutputPrivPtr pPriv = output->driver_private;
    I2CBusPtr i2c = pPriv->i2c;
    xf86MonPtr monInfo = NULL;
    DisplayModePtr modes;
    const int bus = i2c->DriverPrivate.val, off = bus * 0x18;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "Probing for EDID on I2C bus %i...\n", bus);
    pNv->reg[(0x0000E138+off)/4] = 7;
    monInfo = xf86OutputGetEDID(output, i2c);
    pNv->reg[(0x0000E138+off)/4] = 3;

    xf86OutputSetEDID(output, monInfo);
    modes = xf86OutputGetEDIDModes(output);

    if(monInfo) {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                "DDC detected a %s:\n", monInfo->features.input_type ?
                "DFP" : "CRT");
        xf86PrintEDID(monInfo);
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  ... none found\n");
    }

    return modes;
}

void
G80OutputDestroy(xf86OutputPtr output)
{
    G80OutputPrivPtr pPriv = output->driver_private;

    xf86DestroyI2CBusRec(pPriv->i2c, TRUE, TRUE);
    pPriv->i2c = NULL;
}

Bool
G80CreateOutputs(ScrnInfoPtr pScrn)
{
    G80Ptr pNv = G80PTR(pScrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    if(!G80ReadPortMapping(pScrn->scrnIndex, pNv))
        return FALSE;

    /* For each DDC port, create an output for the attached ORs */
    for(i = 0; i < 4; i++) {
        if(pNv->i2cMap[i].dac != -1)
            G80CreateDac(pScrn, pNv->i2cMap[i].dac, i);
        if(pNv->i2cMap[i].sor != -1)
            G80CreateSor(pScrn, pNv->i2cMap[i].sor, i);
    }

    /* For each output, set the crtc and clone masks */
    for(i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];

        /* Any output can connect to any head */
        output->possible_crtcs = 0x3;
        output->possible_clones = 0;
    }

    return TRUE;
}

