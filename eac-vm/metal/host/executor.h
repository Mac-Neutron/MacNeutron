/* The VM host's side of the Metal transport (spec §5.6): the executor behind libkrun's dxmt context (patch 0005). */
#ifndef MN_EXECUTOR_H
#define MN_EXECUTOR_H

/* Registers with libkrun's dxmt context; call before the VM starts. */
void mn_executor_start(void);

#endif
