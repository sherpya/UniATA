#ifndef UNIATA_CS5536_H
#define UNIATA_CS5536_H
BOOLEAN UniataCs5536Init(PHW_DEVICE_EXTENSION extension);
BOOLEAN UniataCs5536SetMode(PHW_DEVICE_EXTENSION extension, ULONG device,
                           ULONG channel, ULONG pioMode, ULONG mode);
#endif
