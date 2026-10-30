/* wm_probe's swizzles (probe.h, wm_swizzles): what a handler gives MetalFX's descriptors and a function's constant
 * values, which have no getters to read back. Installed on the first wm_swizzles(true), on the class of a real
 * instance (a private subclass may override the public class's method); printing only while on. */
#include "probe.h"
#import <MetalFX/MetalFX.h>
#include <objc/runtime.h>

static bool on;
static IMP orig[4];

static id dumped(id self, SEL cmd, id device, unsigned which) {
    if (on) {
        wm_line("swz@%s %s", wm_thread(), sel_getName(cmd));
        wm_dump_as(self, "desc.");
    }
    return ((id(*)(id, SEL, id))orig[which])(self, cmd, device);
}
static id temporal(id self, SEL cmd, id device) { return dumped(self, cmd, device, 0); }
static id spatial(id self, SEL cmd, id device) { return dumped(self, cmd, device, 1); }
static id interpolator(id self, SEL cmd, id device) { return dumped(self, cmd, device, 2); }

/* A constant's size by MTLDataType: the scalar and vector types (components times the scalar's size), else 0. */
static unsigned type_size(MTLDataType t) {
    static const struct {
        unsigned first, last, scalar;
    } r[] = {{MTLDataTypeFloat, MTLDataTypeFloat4, 4}, {MTLDataTypeHalf, MTLDataTypeHalf4, 2},
             {MTLDataTypeInt, MTLDataTypeInt4, 4},     {MTLDataTypeUInt, MTLDataTypeUInt4, 4},
             {MTLDataTypeShort, MTLDataTypeShort4, 2}, {MTLDataTypeUShort, MTLDataTypeUShort4, 2},
             {MTLDataTypeChar, MTLDataTypeChar4, 1},   {MTLDataTypeUChar, MTLDataTypeUChar4, 1},
             {MTLDataTypeBool, MTLDataTypeBool4, 1},   {MTLDataTypeLong, MTLDataTypeLong4, 8},
             {MTLDataTypeULong, MTLDataTypeULong4, 8}};
    for (unsigned i = 0; i < sizeof r / sizeof *r; i++)
        if (t >= r[i].first && t <= r[i].last) return (unsigned)(t - r[i].first + 1) * r[i].scalar;
    return 0;
}

static void set_constant(id self, SEL cmd, const void *value, MTLDataType type, NSUInteger index) {
    if (on) {
        unsigned n = value ? type_size(type) : 0;
        char bytes[2 * 32 + 1] = "";
        for (unsigned i = 0; i < n && i < 32; i++) snprintf(bytes + 2 * i, 3, "%02x", ((const uint8_t *)value)[i]);
        wm_line("swz@%s %s setConstantValue=%s type=%lu atIndex=%lu", wm_thread(), sel_getName(cmd),
                value ? (n ? bytes : "?") : "null", (unsigned long)type, (unsigned long)index);
    }
    ((void (*)(id, SEL, const void *, MTLDataType, NSUInteger))orig[3])(self, cmd, value, type, index);
}

static void swizzle(id instance, SEL sel, IMP imp, unsigned which) {
    Method m = class_getInstanceMethod(object_getClass(instance), sel);
    if (m) orig[which] = method_setImplementation(m, imp);
    else fprintf(stderr, "wm_probe: no -[%s %s] to swizzle\n", object_getClassName(instance), sel_getName(sel));
    [instance release];
}

void wm_swizzles(bool v) {
    static bool installed;
    if (v && !installed) {
        installed = true;
        swizzle([[MTLFXTemporalScalerDescriptor alloc] init], @selector(newTemporalScalerWithDevice:), (IMP)temporal, 0);
        swizzle([[MTLFXSpatialScalerDescriptor alloc] init], @selector(newSpatialScalerWithDevice:), (IMP)spatial, 1);
        swizzle([[MTLFXFrameInterpolatorDescriptor alloc] init], @selector(newFrameInterpolatorWithDevice:),
                (IMP)interpolator, 2);
        swizzle([[MTLFunctionConstantValues alloc] init], @selector(setConstantValue:type:atIndex:), (IMP)set_constant, 3);
    }
    on = v;
}
