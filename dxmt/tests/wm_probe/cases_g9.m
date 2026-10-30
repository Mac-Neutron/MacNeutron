/* wm_probe's g9 cases (slots.txt's g9; probe.h), recorded from DXMT's C. Capture: 53 the shared manager, 54 startCapture
 * (never writes ret), 55 stopCapture.
 * The recorder here is a target-less one for the MTLCaptureManager class: it answers zero, starts nothing, and prints
 * the message and the descriptor the handler built. The output paths are fixed ones that don't exist (and /tmp, which
 * does and is a directory): wm_tmpdir() is random per run, and the recorder prints an NSURL by absoluteString. */
#include "probe.h"

static const char *const g9_urls[] = {"/wm-probe/capture.gputrace", "/wm-probe/with space/capture.gputrace", "/tmp"};

/* 53: the shared manager; the same object twice; its class. */
static void manager(void) {
    struct unixcall_generic_obj_ret a, b;
    wm_fill(&a, sizeof a);
    wm_own("ret", &a.ret);
    wm_call(53, &a);
    if (a.ret && a.ret != WM_FILL64) wm_val("class", "%s", object_getClassName((id)a.ret));
    wm_fill(&b, sizeof b);
    wm_own("ret", &b.ret);
    wm_call(53, &b);
    wm_eq("same", b.ret, a.ret, "first");
}

/* 54 on the real manager without MTL_CAPTURE_ENABLED: the C never writes ret (the whole block stays 0xA5), and sends
 * startCaptureWithDescriptor:error:nil, which Metal answers however it does on this machine. */
static void start_unsupported(void) {
    struct WMTCaptureInfo info;
    wm_fill(&info, sizeof info);
    info.capture_object = (obj_handle_t)wm_queue;
    info.output_url.ptr = "/wm-probe/unsupported.gputrace";
    info.destination = WMTCaptureDestinationDeveloperTools;
    struct unixcall_mtlcapturemanager_startcapture p;
    wm_fill(&p, sizeof p);
    p.capture_manager = (obj_handle_t)[MTLCaptureManager sharedCaptureManager];
    p.info.ptr = &info;
    wm_call(54, &p);
    wm_bytes("ret", &p.ret, sizeof p.ret);
}

/* 54 on a recording manager: the message, the descriptor (destination, captureObject, outputURL), once per output URL
 * (a path is made a file URL by initFileURLWithPath:, so a space is %20 and an existing directory gets its slash) and
 * per destination, with the capture object a queue the case holds. */
static void start_recorded(void) {
    id rec = wm_rec([MTLCaptureManager class], nil);
    for (unsigned i = 0; i < sizeof g9_urls / sizeof *g9_urls; i++) {
        struct WMTCaptureInfo info;
        wm_fill(&info, sizeof info);
        info.capture_object = (obj_handle_t)rec;
        info.output_url.ptr = g9_urls[i];
        info.destination = i == 1 ? WMTCaptureDestinationGPUTraceDocument : WMTCaptureDestinationDeveloperTools;
        struct unixcall_mtlcapturemanager_startcapture p;
        wm_fill(&p, sizeof p);
        p.capture_manager = (obj_handle_t)rec;
        p.info.ptr = &info;
        wm_call(54, &p);
        wm_bytes("ret", &p.ret, sizeof p.ret);
    }
}

/* 55 on a recording manager. */
static void stop(void) {
    id rec = wm_rec([MTLCaptureManager class], nil);
    struct unixcall_generic_obj_noret p = {.handle = (obj_handle_t)rec};
    wm_call(55, &p);
}

/* 54 and 55 on handle 0: nil sends, ret untouched. */
static void nil_manager(void) {
    struct WMTCaptureInfo info;
    wm_fill(&info, sizeof info);
    info.capture_object = (obj_handle_t)wm_queue;
    info.output_url.ptr = "/wm-probe/nil.gputrace";
    info.destination = WMTCaptureDestinationDeveloperTools;
    struct unixcall_mtlcapturemanager_startcapture p;
    wm_fill(&p, sizeof p);
    p.capture_manager = 0;
    p.info.ptr = &info;
    wm_call(54, &p);
    wm_bytes("ret", &p.ret, sizeof p.ret);
    struct unixcall_generic_obj_noret s = {.handle = 0};
    wm_call(55, &s);
}

const struct wm_case wm_cases_g9[] = {
    {"g9", 53, "manager", 0, manager},
    {"g9", 54, "start-unsupported", 0, start_unsupported},
    {"g9", 54, "start-recorded", 0, start_recorded},
    {"g9", 55, "stop", 0, stop},
    {"g9", 54, "nil", 0, nil_manager},
    {0},
};
