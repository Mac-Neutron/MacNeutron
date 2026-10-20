/* M4b: gen.py's i386 converters (convert32.c). For every struct a 32-bit process's calls carry with a layout that
 * differs: an i386 value converted to x86-64 and back is the same bytes (padding left as it was), and converted back
 * into a zeroed buffer it covers exactly its fields' bytes (gen.py's count, from the layout: no field dropped on the
 * way). gen.py's i386 layouts themselves are checked against llvm-mingw's i686 headers by layout32.cpp. Prints one line
 * per struct that fails, then a summary; exits 1 on any.
 *   cc -I guest -I <gen dir> tests/convert32-test.c <gen dir>/convert32.c */
#include <stdio.h>
#include <string.h>
#include "convert32.h"

int main(void) {
    int failures = 0, n = 0;
    for (const struct mn_c32_struct *c = mn_c32_structs; c->name; c++, n++) {
        uint8_t in32[4096], out64[4096], back32[4096], zero32[4096] = {0};
        for (uint32_t i = 0; i < c->size32; i++) in32[i] = (uint8_t)(i % 255 + 1); /* never 0 */
        memcpy(back32, in32, c->size32);
        c->in(in32, out64);
        c->out(out64, back32);
        c->out(out64, zero32);
        uint32_t covered = 0, same = 1;
        for (uint32_t i = 0; i < c->size32; i++) {
            covered += zero32[i] != 0;
            same &= !zero32[i] || zero32[i] == in32[i];
        }
        if (memcmp(in32, back32, c->size32) || covered != c->fields32 || !same) {
            printf("FAIL %s (%u -> %u bytes): roundtrip %s, %u of %u field bytes back\n", c->name, c->size32, c->size64,
                   memcmp(in32, back32, c->size32) ? "differs" : "ok", covered, c->fields32);
            failures++;
        }
    }
    printf(failures ? "convert32-test: %d of %d failed\n" : "convert32-test: %d structs ok\n", failures ? failures : n, n);
    return failures != 0;
}
