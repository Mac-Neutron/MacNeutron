#!/bin/sh
set -u
B=/mnt/share/bench; M=$HOME/tsomod
export PATH=$HOME/fex/build/Bin:$PATH
tso() { sudo rmmod tso 2>/dev/null; sudo insmod $M/tso.ko set=$1; }
cd $B
for v in "" -DNO_CRT_MATH; do n=gameframe${v:+-nocrt}
  clang++ --target=x86_64-linux-gnu -O2 -static -std=c++17 -pthread $v -o $n.x86_64 gameframe.cpp
  clang++ -O2 -std=c++17 -pthread $v -o $n.arm64 gameframe.cpp
done
out=$B/results-guest-gf.txt; : > $out
run() { cfg=$1; shift; for r in 1 2 3; do "$@" 2>/dev/null | sed -n "s/^frames .* ms_per_frame \([0-9.]*\).*/$cfg $r $n \1/p" >> $out; done; }
for n in gameframe gameframe-nocrt; do
  tso 0; run native ./$n.arm64; run fex_swtso FEX ./$n.x86_64; run fex_notso env FEX_TSOENABLED=0 FEX ./$n.x86_64
  tso 1; run native_tso ./$n.arm64; run fex_hwtso env FEX_BENCH_ASSUME_HWTSO=1 FEX ./$n.x86_64
done
tso 0; cat $out
