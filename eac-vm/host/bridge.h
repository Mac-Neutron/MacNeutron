/* The C that macneutron-vm.swift calls: libkrun's v2 API and trap blobs (patch 0009), the Metal transport's executor,
 * its winemetal handler (milestone 4b), the guest's keyboard and mouse (input.c), its sound (audio.c) and its TPM
 * (tpm.c). */
#include "libkrun.h"
#include "libkrun_trap.h"
#include "input.h"
#include "audio.h"
#include "tpm.h"
#include "../guest/mn-audio.h"
#include "../metal/host/executor.h"
#include "../metal/wm/host/dispatch.h"
#include "../metal/wm/host/loader.h"
