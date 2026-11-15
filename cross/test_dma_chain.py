#!/usr/bin/env python3
"""Exercise the production accounting helper with split-request invariants."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
harness = r'''
#include <cstdint>
#include <cassert>
#include <cstdio>
typedef void VOID;
typedef bool BOOLEAN;
struct ATA_REQ { uint32_t WordsTransfered, WordsLeft; };
typedef ATA_REQ* PATA_REQ;
#include "dma_account.h"
static unsigned transfer(uint32_t bytes) {
    ATA_REQ request = {0, 0};
    uint32_t next_lba = 1000, total_sectors = 0;
    for (unsigned n = 1; n <= 16; ++n) {
        uint32_t offset = request.WordsTransfered * 2;
        uint32_t chunk = bytes - offset;
        if (chunk > 63*1024) chunk = 63*1024;
        assert(offset + chunk <= bytes);
        request.WordsLeft = chunk/2;
        total_sectors += chunk/512;
        next_lba += chunk/512;
        UniataAccountDmaTransfer(&request, false);
        if (request.WordsTransfered*2 == bytes) {
            assert(total_sectors == bytes/512);
            assert(next_lba == 1000 + bytes/512);
            return n;
        }
    }
    return 0;
}
int main() {
    assert(transfer(512)==1);
    assert(transfer(63*1024)==1);
#if TEST_FIXED
    assert(transfer(64*1024)==2);
    assert(transfer(63*1024+512)==2);
    assert(transfer(128*1024)==3);
    assert(transfer(255*512)==3);
    assert(transfer(256*1024)==5);
#else
    assert(transfer(64*1024)==0); // reproduce the old endless 126+2 loop
#endif
    ATA_REQ atapi = {100, 20};
    UniataAccountDmaTransfer(&atapi, true);
    assert(atapi.WordsTransfered==20 && atapi.WordsLeft==0);
    printf("PASS DMA_CHAIN_FIX=%d: %s; ATAPI unchanged\n", TEST_FIXED,
        TEST_FIXED ? "split requests complete within bounds" : "original 64-KiB failure reproduced");
}
'''
with tempfile.TemporaryDirectory(prefix='uniata-chain-') as tmp:
    source = Path(tmp)/'test.cpp'
    source.write_text(harness)
    for enabled in (0, 1):
        helper = (root/'driver/dma_account.h').read_text()
        if not enabled:
            helper = helper.replace('request->WordsTransfered +=', 'request->WordsTransfered =')
        (Path(tmp)/'dma_account.h').write_text(helper)
        binary = Path(tmp)/f'test-{enabled}'
        subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror',
                        f'-DTEST_FIXED={enabled}', '-I'+tmp, '-I'+str(root/'driver'),
                        str(source),'-o',str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
