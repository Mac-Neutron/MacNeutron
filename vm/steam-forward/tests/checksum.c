/* The forwarding tables' checksums as this build computes them (the handshake's halves): the 64-bit calls' and the
 * WoW64 calls'. An i686 build's WoW64 half must equal the x86-64 one's, which the host computes (build-guest.sh). */
#include <stdio.h>
#include "client.h"
#include "marshal.h"

int main(void) {
    printf("%08x %08x\n", mn_table_checksum(mn_calls, mn_ncalls), mn_table_checksum(mn_calls_wow64, mn_ncalls));
    return 0;
}
