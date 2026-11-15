#ifndef UNIATA_DMA_ACCOUNT_H
#define UNIATA_DMA_ACCOUNT_H

/* ATA_REQ::WordsTransfered is cumulative across a split ATA request.
 * Keep legacy ATAPI accounting separate: its interrupt protocol differs.
 */
static __inline VOID UniataAccountDmaTransfer(PATA_REQ request, BOOLEAN atapi)
{
    if (!atapi)
        request->WordsTransfered += request->WordsLeft;
    else
        request->WordsTransfered = request->WordsLeft;
    request->WordsLeft = 0;
}
#endif
