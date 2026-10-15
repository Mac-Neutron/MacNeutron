// Reads ACTLR_EL1 on every CPU and, with set=1, sets Apple's TSO bit (ACTLR_EL1[1]) there; with set=0, clears it.
#include <linux/module.h>
#include <linux/smp.h>
static int set = -1;
module_param(set, int, 0);
static void one(void *unused) {
  u64 v; asm volatile("mrs %0, actlr_el1" : "=r"(v));
  if (set == 1) { v |= 2; asm volatile("msr actlr_el1, %0; isb" :: "r"(v)); }
  if (set == 0) { v &= ~2ULL; asm volatile("msr actlr_el1, %0; isb" :: "r"(v)); }
  asm volatile("mrs %0, actlr_el1" : "=r"(v));
  pr_info("tso: cpu %d actlr_el1 %#llx\n", smp_processor_id(), v);
}
static int __init tso_init(void) { on_each_cpu(one, NULL, 1); return 0; }
static void __exit tso_exit(void) {}
module_init(tso_init); module_exit(tso_exit); MODULE_LICENSE("GPL");
