#include <xaa.h>
#include <exa.h>
#include <xf86.h>
#include <xf86int10.h>
#include <xf86Cursor.h>
#include <xf86DDC.h>
#include <xf86Crtc.h>
#include <xf86int10.h>

typedef enum Head {
    HEAD0 = 0,
    HEAD1
} Head;

typedef enum ORType {
   DAC,
   SOR
} ORType;

typedef enum ORNum {
   DAC0 = 0,
   DAC1 = 1,
   DAC2 = 2,
   SOR0 = 0,
   SOR1 = 1
} ORNum;

typedef enum PanelType {
    TMDS,
    LVDS,
} PanelType;

typedef enum AccelMethod {
    XAA,
    EXA,
} AccelMethod;

typedef struct G80Rec {
    volatile CARD32 *   reg;
    unsigned char *     mem;

    /* Probed data */
    CARD32              architecture;
    CARD32              RamAmountKBytes; /* Total vidmem */
    CARD32              videoRam;        /* Mapped vidmem */
    const unsigned char*table1;
    int                 offscreenHeight;
    struct {
        ORNum           dac;
        ORNum           sor;
        PanelType       panelType;
    } i2cMap[4];

    xf86Int10InfoPtr    int10;
    int                 int10Mode; /* Console mode to restore */

    /* Options */
    OptionInfoPtr       Options;
    Bool                HWCursor;
    Bool                NoAccel;
    AccelMethod         AccelMethod;
    Bool                Dither;

    /* XAA */
    XAAInfoRecPtr       xaa;
    CARD32              currentRop;

    /* EXA */
    ExaDriverPtr        exa;
    ExaOffscreenArea   *exaScreenArea;

    /* DMA command buffer */
    CARD32              dmaPut;
    CARD32              dmaCurrent;
    CARD32              dmaFree;
    CARD32              dmaMax;
    CARD32 *            dmaBase;
    void              (*DMAKickoffCallback)(ScrnInfoPtr);

    CloseScreenProcPtr           CloseScreen;
    ScreenBlockHandlerProcPtr    BlockHandler;
} G80Rec, *G80Ptr;

#define G80PTR(p) ((G80Ptr)((p)->driverPrivate))
