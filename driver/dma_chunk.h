#ifndef UNIATA_DMA_CHUNK_H
#define UNIATA_DMA_CHUNK_H

/* A new ATA chunk has a different data address/length. The cached PRD
 * belongs to the completed chunk and must not short-circuit DMA setup.
 */
static __inline VOID UniataPrepareNextDmaChunk(PATA_REQ request)
{
    request->dma_entries = 0;
}
#endif
