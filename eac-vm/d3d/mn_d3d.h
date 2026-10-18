/* Direct3D forwarding's records (M2), shared by the guest and the host. A method's record is MN_OP_D3D + its id
 * (manifest.txt, from MN_D3D_FIRST); ids below that are the family's own ops.
 *
 * Status: the method's own result is in its params' ret (an HRESULT as (uint64_t)(uint32_t)); the dispatcher's
 * return is 0; a negative MN_E* for a malformed record (an unknown id, a record shorter than its params); an NTSTATUS
 * of the marshaller's (MN_STATUS_*: a handle that didn't translate, a reply that didn't fit); or an HRESULT when the
 * host refused the call before running it: DXGI_ERROR_DEVICE_REMOVED once the context's device is removed, E_FAIL
 * without native DXMT, E_NOTIMPL for a method the host has no code for. */
#ifndef MN_D3D_MN_D3D_H
#define MN_D3D_MN_D3D_H
#include <stdint.h>

#define MN_OP_D3D 0x10000u
enum {
    MN_D3D_OP_RELEASE = 0, /* u64 handle: the guest's last Release (deferred until the rings that used it pass) */
    MN_D3D_OP_ARENA = 1,   /* Task 7 */
    MN_D3D_OP_NOTIFY = 2,  /* Task 8 */
    MN_D3D_OP_TRACE = 3,   /* u32 id, u32 why (0 unhandled, 1 refused, 2 answered by design: M4's dynamic maps): a
                            * method the guest answered itself, for its trace; never sent */
    MN_D3D_OP_QUERY = 4,   /* struct mn_d3d_query: QueryInterface for an interface at another pointer, a new handle */
    MN_D3D_OP_BLOB = 5,    /* struct mn_d3d_blob: an ID3D10Blob's bytes; reply: u64 its size, then the bytes asked */
    MN_D3D_OP_LISTEN = 6,  /* none: the calling thread sets the events the host posts, for the process's life; never sent */
    MN_D3D_OP_WINDOW = 7,  /* struct mn_d3d_window: a window's state, which native DXMT's window system answers (M3) */
    MN_D3D_OP_DISPLAYS = 8, /* u32 count, then each struct mn_d3d_display and its modes (M3); guest args: below */
    MN_D3D_OP_ACTIONS = 9, /* none; reply: the window actions native DXMT took since the last (struct mn_d3d_action) */
    MN_D3D_OP_DRAWABLES = 10, /* u64 out: the guest's address of the mirror's drawable counts (below); never sent */
    MN_D3D_OP_SHOWN = 11,  /* u64 a guest event, the process's: set (MN_NOTIFY_SET) each time a drawable is shown */
};
/* The fences' mirror (op 2) starts with the context's drawables (M3 Task 4): slot 0 those native DXMT presented, slot
 * 1 those the display showed (Metal's presented handler); the fences' slots follow. A present waits in the guest until
 * no more than one is still to be shown, so the next has a drawable free and the host's nextDrawable doesn't wait on
 * the vCPU: on op 11's event, which the host has the guest's listener set with each one shown. */
#define MN_D3D_MIRROR_DRAWABLES 2
struct mn_d3d_query { uint64_t handle; uint8_t iid[16]; uint64_t out; }; /* reply: struct mn_d3d_query_reply */
/* Ops 7 and 8 (M3): the guest's window and displays as Win32 has them there, pushed before the calls that read them
 * (host/window.c keeps the latest of each, by context). flags: 1 minimised, 2 foreground, 4 a window. A display's
 * modes follow it, `modes` of them, the current one first and then EnumDisplaySettings' list. */
struct mn_d3d_window { uint64_t hwnd, monitor; uint32_t width, height, flags, pad; }; /* flags 8: the process in front */
/* D3D11's and D3D10's initial data and updates (M4 Task 4, rules.txt's own): uint32_t count, pad, then count of these,
 * then the bytes each names (offset from the region's start). */
struct mn_d3d_sub { uint32_t offset, pitch, slice, size; };
/* Op 9 (M3 Task 3): a change to a window or a mode that native DXMT made and the guest makes with its own window
 * system (fork 0083's WMTWsiAction, kind its MN_D3D_ACTION_*); taken during the context's own calls, in order. */
enum {
    MN_D3D_ACTION_SAVE = 1, MN_D3D_ACTION_SET_MODE, MN_D3D_ACTION_ENTER, MN_D3D_ACTION_LEAVE, MN_D3D_ACTION_RESTORE_MODE,
    MN_D3D_ACTION_RESIZE, MN_D3D_ACTION_UPDATE_FULLSCREEN, MN_D3D_ACTION_MINIMIZE,
};
struct mn_d3d_action {
    uint32_t kind, arg;
    uint64_t hwnd, monitor;
    uint32_t width, height, refresh_num, refresh_den, bits_per_pixel, interlaced;
};
struct mn_d3d_actions_args { uint64_t data; uint32_t cap, len; }; /* op 9 from the proxies: into data, len bytes */
struct mn_d3d_display { uint64_t monitor; int32_t left, top, right, bottom; uint16_t name[32]; uint32_t modes, pad; };
struct mn_d3d_mode { uint32_t width, height, refresh_num, refresh_den; };
struct mn_d3d_displays_args { uint64_t data; uint32_t len, pad; }; /* op 8 from the proxies: the record's bytes */
struct mn_d3d_blob { uint64_t handle, offset, len; };
/* Op 2: the context's Direct3D notify queue (MN_NOTIFY_EVENT: a guest event to set) and its fences' mirror, one
 * uint64_t a fence (its completed value, raised by the host: max(slot, value)); a fence's creation names its slot in
 * aux[0], the guest's address of it in aux[1] (the unix side's). */
struct mn_d3d_notify { uint64_t queue_blob, mirror_blob, mirror_len; };

/* Op 1: guest memory (a galloc region, held for the host) the host carves CPU-visible resources from. */
struct mn_d3d_arena { uint64_t blob_id, offset, len; };
/* A creation's status when no arena has room: the reply is the u64 bytes wanted; the guest adds an arena of at least
 * MN_D3D_ARENA_BYTES that size and sends the creation again, up to 8 tries (then E_OUTOFMEMORY). */
#define MN_D3D_NEED_ARENA 0x20000001
#define MN_D3D_ARENA_BYTES (64ull << 20)

/* A guest proxy's start (guest/proxy.h): the marshaller's hook reads the handle there. */
struct mn_d3d_proxy_hdr { const void *vtbl; uint32_t magic; int32_t iface; uint64_t handle; };
#define MN_D3D_PROXY_MAGIC 0x4433444eu /* "ND3D" */
/* The unix side's call (guest/unix.c): id, and the params (a method's struct, or a family op's below). */
/* A creation's reply names the interfaces its object answers in a block of 2 * MN_D3D_MASK_WORDS + 1 words (each
 * params struct's <out>_ifaces[9]): answered (a bit by mn_d3d_ifaces index), answered at another pointer, then the
 * handle the host gave the object before (0: none). 256 interfaces (M4: D3D11 and D3D10 take the model past 128). */
#define MN_D3D_MASK_WORDS 4
/* Op 4's reply (M4): the companion's whole interfaces block, as a creation's (D3D11's, D3D10's and DXGI's objects answer
 * each other at other pointers) */
struct mn_d3d_query_reply { int32_t hr, pad; uint64_t ifaces[2 * MN_D3D_MASK_WORDS + 1]; };
struct mn_d3d_args { uint32_t id, flags; uint64_t params; };
/* A command list's method on a list another thread called last (M3 Task 10, SMITE 2's: one thread opens a list and
 * stamps its start, another records and closes it): sent with MN_REC_SUBMIT, which first runs every other ring of the
 * context, that thread's records for the list among them. */
#define MN_D3D_ARG_HANDOFF 1u
/* A piece of a dynamic resource's memory (IMTLD3D11ContextExt2::GetPieces's, M4 Task 8), as the host answers it: the
 * arena's (blob, offset), which the unix side makes the guest's address in blob (0: none, not in an arena). */
struct mn_d3d_piece { uint64_t id, blob, offset; uint32_t row_pitch, depth_pitch; };
struct mn_d3d_query_args {
    uint64_t handle; uint8_t iid[16]; uint64_t out; int32_t hr, pad;
    uint64_t ifaces[2 * MN_D3D_MASK_WORDS + 1];
};
struct mn_d3d_blob_args { uint64_t handle, dst, cap, size; }; /* the blob's bytes into dst (at most cap), its size */

#define MN_D3D_E_FAIL ((int32_t)0x80004005)
#define MN_D3D_E_NOTIMPL ((int32_t)0x80004001)
#define MN_D3D_E_NOINTERFACE ((int32_t)0x80004002)
#define MN_D3D_DEVICE_REMOVED ((int32_t)0x887A0005)

#endif
