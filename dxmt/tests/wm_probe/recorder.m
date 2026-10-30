/* wm_probe's recorder (an NSProxy printing every message it gets) and descriptor dump (probe.h). */
#include "probe.h"
#include <CommonCrypto/CommonDigest.h>
#include <objc/runtime.h>

NSMutableArray *wm__dump_keys(void);

static NSString *hex(const void *p, size_t n) {
    NSMutableString *s = [NSMutableString stringWithCapacity:n * 2];
    for (size_t i = 0; i < n; i++) [s appendFormat:@"%02x", ((const uint8_t *)p)[i]];
    return s;
}

static NSString *quoted(NSString *s) {
    NSMutableString *q = [NSMutableString stringWithString:@"\""];
    for (NSUInteger i = 0; i < s.length; i++) {
        unichar c = [s characterAtIndex:i];
        if (c == '"' || c == '\\') [q appendFormat:@"\\%C", c];
        else if (c == '\n') [q appendString:@"\\n"];
        else if (c == '\r') [q appendString:@"\\r"];
        else if (c == '\t') [q appendString:@"\\t"];
        else [q appendFormat:@"%C", c];
    }
    [q appendString:@"\""];
    return q;
}

/* NSGetSizeAndAlignment, 0 for an encoding it can't size. */
static NSUInteger size_of(const char *t) {
    NSUInteger n = 0;
    @try {
        NSGetSizeAndAlignment(t, &n, NULL);
    } @catch (NSException *x) {
        n = 0;
    }
    return n;
}

static bool is_mtl(id o) { return o && ![o isProxy] && !strncmp(object_getClassName(o), "MTL", 3); }
/* Descriptors and options objects (MTLCompileOptions): what winemetal builds and hands Metal. MTLFunctionConstantValues
 * has no getters; the swizzle prints it. */
static bool is_descriptor(id o) {
    return is_mtl(o) && (strstr(object_getClassName(o), "Descriptor") || strstr(object_getClassName(o), "Options"));
}

/* An object as a value. A descriptor goes to dumps (path, object) when given, and prints as its class. */
static NSString *fmt_obj(id o, NSString *path, NSMutableArray *dumps) {
    if (!o) return @"nil";
    if ([o isProxy]) return @(wm_hs((uint64_t)o));
    if (dumps && is_descriptor(o)) {
        [dumps addObject:@[ path, o ]];
        return @(object_getClassName(o));
    }
    if ([o isKindOfClass:[NSString class]]) return quoted(o);
    if ([o isKindOfClass:[NSURL class]]) return quoted([o absoluteString]);
    if ([o isKindOfClass:[NSNumber class]]) {
        NSNumber *n = o;
        switch (n.objCType[0]) {
        case 'f': case 'd': return [NSString stringWithFormat:@"%a", n.doubleValue];
        case 'C': case 'S': case 'I': case 'L': case 'Q': return [NSString stringWithFormat:@"%llu", n.unsignedLongLongValue];
        default: return [NSString stringWithFormat:@"%lld", n.longLongValue];
        }
    }
    if ([o isKindOfClass:[NSData class]]) {
        NSData *d = o;
        uint8_t sum[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(d.bytes, (CC_LONG)d.length, sum);
        return [NSString stringWithFormat:@"sha256:%@/%lu", hex(sum, sizeof sum), (unsigned long)d.length];
    }
    if ([o isKindOfClass:[NSArray class]]) {
        NSMutableArray *a = [NSMutableArray array];
        NSUInteger i = 0;
        for (id e in o) [a addObject:fmt_obj(e, [NSString stringWithFormat:@"%@[%lu]", path, (unsigned long)i++], dumps)];
        return [NSString stringWithFormat:@"[%@]", [a componentsJoinedByString:@","]];
    }
    if ([o isKindOfClass:[NSDictionary class]]) {
        NSMutableArray *a = [NSMutableArray array];
        NSArray *keys = [[o allKeys] sortedArrayUsingComparator:^NSComparisonResult(id x, id y) {
          return [[x description] compare:[y description]];
        }];
        for (id k in keys)
            [a addObject:[NSString stringWithFormat:@"%@:%@", fmt_obj(k, path, nil),
                                                    fmt_obj(o[k], [NSString stringWithFormat:@"%@.%@", path, k], dumps)]];
        return [NSString stringWithFormat:@"{%@}", [a componentsJoinedByString:@","]];
    }
    if ([o isKindOfClass:[NSValue class]]) {
        NSUInteger size = size_of([o objCType]);
        void *b = calloc(1, size ? size : 1);
        [o getValue:b size:size];
        NSString *s = [NSString stringWithFormat:@"{%@}", hex(b, size)];
        free(b);
        return s;
    }
    return @(wm_hs((uint64_t)o));
}

/* ---- the dump ---- */

static void dump_obj(id o, NSString *prefix, int depth, NSMutableSet *seen, NSMutableArray *lines);

static void dump_value(id v, NSString *path, bool vertex, int depth, NSMutableSet *seen, NSMutableArray *lines) {
    if (!is_mtl(v) || [v isKindOfClass:[NSArray class]]) {
        [lines addObject:[NSString stringWithFormat:@"%@=%@", path, fmt_obj(v, path, nil)]];
        return;
    }
    if (depth >= 3 || [seen containsObject:[NSValue valueWithNonretainedObject:v]]) {
        [lines addObject:[NSString stringWithFormat:@"%@=%s", path, object_getClassName(v)]];
        return;
    }
    if ([v respondsToSelector:@selector(objectAtIndexedSubscript:)]) {
        [seen addObject:[NSValue valueWithNonretainedObject:v]];
        [lines addObject:[NSString stringWithFormat:@"%@=%s", path, object_getClassName(v)]];
        for (unsigned i = 0; i < (vertex ? 31u : 8u); i++) {
            NSString *p = [NSString stringWithFormat:@"%@[%u]", path, i];
            id e;
            @try {
                e = [v objectAtIndexedSubscript:i];
            } @catch (NSException *x) {
                [lines addObject:[p stringByAppendingString:@"=!"]];
                continue;
            }
            dump_value(e, p, false, depth + 1, seen, lines);
        }
        return;
    }
    dump_obj(v, path, depth + 1, seen, lines);
}

static void dump_obj(id o, NSString *path, int depth, NSMutableSet *seen, NSMutableArray *lines) {
    [seen addObject:[NSValue valueWithNonretainedObject:o]];
    if (path.length) [lines addObject:[NSString stringWithFormat:@"%@=%s", path, object_getClassName(o)]];
    NSString *prefix = path.length ? [path stringByAppendingString:@"."] : @"";
    NSMutableSet *names = [NSMutableSet set];
    for (Class c = object_getClass(o); c && c != [NSObject class]; c = class_getSuperclass(c)) {
        unsigned n = 0;
        objc_property_t *props = class_copyPropertyList(c, &n);
        for (unsigned i = 0; i < n; i++) [names addObject:@(property_getName(props[i]))];
        free(props);
    }
    for (NSString *skip in @[ @"description", @"debugDescription", @"hash", @"superclass" ]) [names removeObject:skip];
    NSMutableArray *private =
        [NSMutableArray arrayWithArray:@[ @"logicOperationEnabled", @"logicOperation", @"compressionMode" ]];
    [private addObjectsFromArray:wm__dump_keys()];
    for (NSString *k in private) {
        if ([names containsObject:k]) continue;
        @try {
            [o valueForKey:k];
            [names addObject:k];
        } @catch (NSException *x) {
        }
    }
    bool vertex = strstr(object_getClassName(o), "VertexDescriptor") != NULL;
    for (NSString *k in [names.allObjects sortedArrayUsingSelector:@selector(compare:)]) {
        NSString *p = [prefix stringByAppendingString:k];
        id v;
        @try {
            v = [o valueForKey:k];
        } @catch (NSException *x) {
            [lines addObject:[p stringByAppendingString:@"=!"]];
            continue;
        }
        dump_value(v, p, vertex, depth, seen, lines);
    }
}

void wm_dump_as(id d, const char *prefix) {
    NSString *p = @(prefix);
    if (!d) {
        wm_line("dump %s=nil", prefix);
        return;
    }
    NSMutableArray *lines = [NSMutableArray array];
    if (p.length && [p hasSuffix:@"."]) p = [p substringToIndex:p.length - 1];
    dump_obj(d, p, 0, [NSMutableSet set], lines);
    [lines sortUsingSelector:@selector(compare:)];
    for (NSString *l in lines) wm_line("dump %s", l.UTF8String);
}

void wm_dump(id d) { wm_dump_as(d, ""); }

void wm_dump_key(const char *key) { [wm__dump_keys() addObject:@(key)]; }

/* ---- the recorder ---- */

@interface WMRecorder : NSProxy {
  @public
    id target;
    Class cls;
    Protocol *proto;
    NSMutableDictionary *canned;
}
@end

static const char *skip_qualifiers(const char *t) {
    while (*t && strchr("rnNoORV", *t)) t++;
    return t;
}

static NSString *fmt_int(const char *t, const void *b) {
    switch (*t) {
    case 'c': return [NSString stringWithFormat:@"%d", *(const int8_t *)b];
    case 's': return [NSString stringWithFormat:@"%d", *(const int16_t *)b];
    case 'i': return [NSString stringWithFormat:@"%d", *(const int32_t *)b];
    case 'l': case 'q': return [NSString stringWithFormat:@"%lld", *(const long long *)b];
    case 'C': case 'B': return [NSString stringWithFormat:@"%u", *(const uint8_t *)b];
    case 'S': return [NSString stringWithFormat:@"%u", *(const uint16_t *)b];
    case 'I': return [NSString stringWithFormat:@"%u", *(const uint32_t *)b];
    case 'L': case 'Q': return [NSString stringWithFormat:@"%llu", *(const unsigned long long *)b];
    case 'f': return [NSString stringWithFormat:@"%a", *(const float *)b];
    case 'd': return [NSString stringWithFormat:@"%a", *(const double *)b];
    default: return nil;
    }
}

/* The element count of a pointer argument: the selector's count: (elements), length: (bytes) or withRange:
 * (NSRange.length) argument; -1 when it has none. */
static long pointee_count(NSInvocation *inv, NSArray *pieces, bool *bytes) {
    NSMethodSignature *sig = inv.methodSignature;
    for (NSUInteger j = 0; j < pieces.count && j + 2 < sig.numberOfArguments; j++) {
        NSString *pc = pieces[j];
        if ([pc isEqualToString:@"count"] || [pc isEqualToString:@"length"]) {
            unsigned long long n = 0;
            [inv getArgument:&n atIndex:j + 2];
            *bytes = [pc isEqualToString:@"length"];
            return (long)n;
        }
        if ([pc isEqualToString:@"withRange"]) {
            NSRange r;
            [inv getArgument:&r atIndex:j + 2];
            *bytes = false;
            return (long)r.length;
        }
    }
    return -1;
}

static NSString *fmt_arg(NSInvocation *inv, NSUInteger i, NSArray *pieces, NSString *path, NSMutableArray *dumps) {
    const char *t = skip_qualifiers([inv.methodSignature getArgumentTypeAtIndex:i]);
    NSUInteger size = size_of(t);
    uint8_t *b = calloc(1, size > 16 ? size : 16);
    [inv getArgument:b atIndex:i];
    NSString *s;
    switch (*t) {
    case '@':
        s = t[1] == '?' ? @"block" : fmt_obj(*(id *)b, path, dumps);
        break;
    case '#': s = @(class_getName(*(Class *)b) ?: "nil"); break;
    case ':': s = @(sel_getName(*(SEL *)b)); break;
    case '*': s = *(char **)b ? quoted(@(*(char **)b)) : @"null"; break;
    case '{': s = hex(b, size); break;
    case '^': {
        const uint8_t *p = *(const uint8_t **)b;
        bool bytes = false;
        long n = p ? pointee_count(inv, pieces, &bytes) : -1;
        const char *e = skip_qualifiers(t + 1);
        NSUInteger esize = 1;
        if (!bytes && *e != 'v') esize = size_of(e) ?: 1;
        if (!p) s = @"null";
        else if (n < 0) s = @"nonnull";
        else if (bytes || *e == 'v' || *e == '{') s = hex(p, (size_t)n * esize);
        else {
            NSMutableArray *a = [NSMutableArray array];
            for (long k = 0; k < n; k++) {
                const void *el = p + (size_t)k * esize;
                [a addObject:*e == '@' ? fmt_obj(*(id const *)el, [NSString stringWithFormat:@"%@[%ld]", path, k], dumps)
                                       : (fmt_int(e, el) ?: hex(el, esize))];
            }
            s = [NSString stringWithFormat:@"[%@]", [a componentsJoinedByString:@","]];
        }
        break;
    }
    default:
        s = fmt_int(t, b) ?: [NSString stringWithFormat:@"?%s", t];
    }
    free(b);
    return s;
}

static void print_message(NSInvocation *inv) {
    NSString *sel = NSStringFromSelector(inv.selector);
    NSArray *pieces = [sel componentsSeparatedByString:@":"];
    NSMutableString *line = [NSMutableString stringWithFormat:@"msg@%s %@", wm_thread(), sel];
    NSMutableArray *dumps = [NSMutableArray array];
    for (NSUInteger i = 2; i < inv.methodSignature.numberOfArguments; i++) {
        NSString *name = i - 2 < pieces.count && [pieces[i - 2] length]
                             ? pieces[i - 2]
                             : [NSString stringWithFormat:@"arg%lu", (unsigned long)(i - 2)];
        [line appendFormat:@" %@=%@", name, fmt_arg(inv, i, pieces, name, dumps)];
    }
    wm_line("%s", line.UTF8String);
    for (NSArray *d in dumps) wm_dump_as(d[1], [d[0] stringByAppendingString:@"."].UTF8String);
}

@implementation WMRecorder
- (void)dealloc {
    [target release];
    [canned release];
    [super dealloc];
}
- (NSMethodSignature *)methodSignatureForSelector:(SEL)s {
    if (target) {
        NSMethodSignature *m = [target methodSignatureForSelector:s];
        if (m) return m;
    }
    const char *types = NULL;
    if (proto) {
        struct objc_method_description d = protocol_getMethodDescription(proto, s, YES, YES);
        if (!d.types) d = protocol_getMethodDescription(proto, s, NO, YES);
        types = d.types;
    }
    Class c = cls ? cls : target ? object_getClass(target) : Nil;
    if (!types && c) {
        Method m = class_getInstanceMethod(c, s);
        if (m) types = method_getTypeEncoding(m);
    }
    return types ? [NSMethodSignature signatureWithObjCTypes:types] : nil;
}
- (void)forwardInvocation:(NSInvocation *)inv {
    print_message(inv);
    if (target) {
        [inv invokeWithTarget:target];
        return;
    }
    NSMethodSignature *sig = inv.methodSignature;
    id v = canned[NSStringFromSelector(inv.selector)];
    if (v && *skip_qualifiers(sig.methodReturnType) == '@') {
        [inv setReturnValue:&v];
    } else if (sig.methodReturnLength) {
        void *z = calloc(1, sig.methodReturnLength);
        [inv setReturnValue:z];
        free(z);
    }
}
- (BOOL)respondsToSelector:(SEL)s {
    wm_line("msg@%s respondsToSelector: respondsToSelector=%s", wm_thread(), sel_getName(s));
    return target ? [target respondsToSelector:s] : [self methodSignatureForSelector:s] != nil;
}
- (BOOL)conformsToProtocol:(Protocol *)p {
    wm_line("msg@%s conformsToProtocol: conformsToProtocol=%s", wm_thread(), protocol_getName(p));
    if (target) return [target conformsToProtocol:p];
    return (proto && protocol_conformsToProtocol(proto, p)) || (cls && class_conformsToProtocol(cls, p));
}
- (BOOL)isKindOfClass:(Class)c {
    wm_line("msg@%s isKindOfClass: isKindOfClass=%s", wm_thread(), class_getName(c));
    if (target) return [target isKindOfClass:c];
    return cls && [cls isSubclassOfClass:c];
}
@end

id wm_rec(id target, Protocol *protocol) {
    WMRecorder *r = [WMRecorder alloc];
    if (target && object_isClass(target)) r->cls = target;
    else r->target = [target retain];
    r->proto = protocol;
    r->canned = [[NSMutableDictionary alloc] init];
    return [r autorelease];
}

void wm_rec_ret(id recorder, SEL selector, id value) {
    [((WMRecorder *)recorder)->canned setObject:value forKey:NSStringFromSelector(selector)];
}
