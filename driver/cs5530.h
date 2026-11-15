#ifndef UNIATA_CS5530_H
#define UNIATA_CS5530_H
BOOLEAN UniataCs5530Init(PHW_DEVICE_EXTENSION extension);
VOID UniataCs5530Timing(PHW_DEVICE_EXTENSION extension, ULONG device,
                        ULONG logicalChannel, ULONG pioMode, ULONG mode);
#endif
