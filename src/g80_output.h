typedef struct G80OutputPrivRec {
    ORType type;
    ORNum or;

    I2CBusPtr i2c;

    void (*set_pclk)(xf86OutputPtr, int pclk);
} G80OutputPrivRec, *G80OutputPrivPtr;

Bool G80I2CInit(xf86OutputPtr, const int port);
void G80OutputSetPClk(xf86OutputPtr, int pclk);
int G80OutputModeValid(xf86OutputPtr, DisplayModePtr);
Bool G80OutputModeFixup(xf86OutputPtr, DisplayModePtr mode, DisplayModePtr adjusted_mode);
void G80OutputPrepare(xf86OutputPtr);
void G80OutputCommit(xf86OutputPtr);
DisplayModePtr G80OutputGetDDCModes(xf86OutputPtr);
void G80OutputDestroy(xf86OutputPtr);
Bool G80CreateOutputs(ScrnInfoPtr);

/* g80_dac.c */
xf86OutputPtr G80CreateDac(ScrnInfoPtr, ORNum, int i2cPort);

/* g80_sor.c */
xf86OutputPtr G80CreateSor(ScrnInfoPtr, ORNum, int i2cPort);
