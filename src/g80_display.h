Bool G80LoadDetect(ScrnInfoPtr);
Bool G80DispInit(ScrnInfoPtr);
Bool G80DispSetMode(ScrnInfoPtr, DisplayModePtr);
void G80DispShutdown(ScrnInfoPtr);
void G80DispAdjustFrame(G80Ptr pNv, int x, int y);
void G80DispBlankScreen(ScrnInfoPtr, Bool blank);
void G80DispDPMSSet(ScrnInfoPtr, int mode, int flags);
void G80DispShowCursor(G80Ptr, Bool update);
void G80DispHideCursor(G80Ptr, Bool update);
