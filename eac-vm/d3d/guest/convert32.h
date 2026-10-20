/* A 32-bit (WoW64) process's Direct3D calls (M4b of the 32-bit games' spec): the i386 proxies are the generated ones
 * compiled for i686, so a call's params members are already 64-bit; what differs is the data they point at whose i386
 * layout isn't the x86-64 one (pointers, SIZE_T, HWND... inside structs) and arrays of interface pointers (4 bytes an
 * element). guest/unix.c's mn_d3d_unix_call32 converts those around the 64-bit call, from gen.py's tables
 * (convert32.c), and refuses a call whose data can't be moved field for field. */
#ifndef MN_D3D_CONVERT32_H
#define MN_D3D_CONVERT32_H
#include <stdint.h>

enum { MN_C32_IN = 1, MN_C32_OUT = 2 };
enum { MN_C32_STRUCT = 1, MN_C32_HANDLES = 2 }; /* a struct (or an array of them); an array of interface pointers */
struct mn_c32_member {
    uint16_t offset;  /* the params member: a u64 holding the caller's 32-bit pointer */
    uint8_t kind, dir;
    uint32_t size64, size32; /* an element's */
    void (*in)(const uint8_t *s32, uint8_t *d64);
    void (*out)(const uint8_t *s64, uint8_t *d32);
    int32_t count_offset; /* the count's member, or -1: one element */
    uint8_t count_width, count_deref; /* its width; 1: the member is a pointer to a uint32_t count */
};
struct mn_c32_call { uint8_t refuse; uint16_t n; const struct mn_c32_member *members; };
extern const struct mn_c32_call mn_c32_calls[]; /* by id: MN_D3D_CALLS entries */
struct mn_c32_struct {
    const char *name;
    uint32_t size32, size64, fields32; /* fields32: the bytes its i386 fields cover */
    void (*in)(const uint8_t *s32, uint8_t *d64);
    void (*out)(const uint8_t *s64, uint8_t *d32);
};
extern const struct mn_c32_struct mn_c32_structs[]; /* every converter, the last NULL-named */
#endif
