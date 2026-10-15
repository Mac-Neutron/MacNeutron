#!/bin/sh
# Runs the benchmark matrix in the guest, 3 rounds; results to /mnt/share/bench/results-guest.txt
set -u
B=/mnt/share/bench
M=$HOME/tsomod
export PATH=$HOME/fex/build/Bin:$PATH
mkdir -p $M && cp /mnt/share/tso/tso.c /mnt/share/tso/Kbuild $M/ && make -s -C /lib/modules/$(uname -r)/build M=$M modules >/dev/null 2>&1
tso() { sudo rmmod tso 2>/dev/null; sudo insmod $M/tso.ko set=$1; }
out=$B/results-guest.txt; : > $out
run() {  # run <config> <cmd...>
  cfg=$1; shift
  for r in 1 2 3; do
    "$@" 2>/dev/null | sed -n "s/^row /$cfg $r /p; s/^frames .* ms_per_frame \([0-9.]*\).*/$cfg $r gameframe_ms \1/p" >> $out
  done
}
cd $B
tso 0; run native ./gameframe.arm64
tso 1; run native_tso ./gameframe.arm64
tso 0
run fex_swtso FEX ./x64-bench.x86_64; run fex_swtso FEX ./gameframe.x86_64
run fex_notso env FEX_TSOENABLED=0 FEX ./x64-bench.x86_64; run fex_notso env FEX_TSOENABLED=0 FEX ./gameframe.x86_64
tso 1
run fex_hwtso env FEX_BENCH_ASSUME_HWTSO=1 FEX ./x64-bench.x86_64; run fex_hwtso env FEX_BENCH_ASSUME_HWTSO=1 FEX ./gameframe.x86_64
tso 0
echo done
