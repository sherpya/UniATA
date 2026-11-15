#!/usr/bin/env python3
"""Check the actual packed response and reproduce the old one-byte overwrite."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'driver/scsi.h').read_bytes().decode('latin1')
start = source.index('typedef struct _READ_CAPACITY16_DATA {')
end = source.index(';', source.index('} READ_CAPACITY16_DATA', start)) + 1
structure = source[start:end]
with tempfile.TemporaryDirectory(prefix='uniata-capacity-') as temp:
    for old in (False, True):
        body = structure.replace('Reserved1[19]', 'Reserved1[20]') if old else structure
        text = """
#include <cstdint>
#include <cstring>
#include <cassert>
typedef uint8_t UCHAR;
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
#pragma pack(push, 1)
""" + body + """
#pragma pack(pop)
int main() {
    unsigned char buffer[34];
    memset(buffer, 0xa5, sizeof(buffer));
    memset(buffer + 1, 0, sizeof(READ_CAPACITY16_DATA));
    assert(buffer[0] == 0xa5);
    assert(offsetof(READ_CAPACITY16_DATA, BytesPerBlock) == 8);
    assert(sizeof(READ_CAPACITY16_DATA) == EXPECT_SIZE);
    assert(buffer[33] == EXPECT_GUARD);
}
"""
        path = Path(temp) / 'test.cpp'; path.write_text('#include <cstddef>\n' + text)
        exe = Path(temp) / 'test'
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                        '-DEXPECT_SIZE=' + ('33' if old else '32'),
                        '-DEXPECT_GUARD=' + ('0' if old else '0xa5'),
                        str(path), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('PASS READ CAPACITY(16): 32-byte response, original tail overwrite reproduced')
