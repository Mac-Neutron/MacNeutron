/* The C that macneutron-vm.swift calls: libkrun's v2 API, the Metal transport's executor, its winemetal handler
 * (milestone 4b), the guest's keyboard and mouse (input.c) and its sound (audio.c). */
#include "libkrun.h"
#include "input.h"
#include "audio.h"
#include "../guest/mn-audio.h"
#include "../metal/host/executor.h"
#include "../metal/wm/host/dispatch.h"
#include "../metal/wm/host/loader.h"
