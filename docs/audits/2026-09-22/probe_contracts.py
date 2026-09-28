"""Audit-only probes. Production sources are compiled unchanged; no hardware I/O."""
from pathlib import Path
import json
import os
import subprocess
import sys
import contextlib
import io

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/audit-2026-09-22"
OUT.mkdir(parents=True, exist_ok=True)
CC = os.environ.get("CC", str(ROOT / "_deps/host-gcc-13.2.0/mingw64/bin/gcc.exe"))

def write(name, content):
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path

def compile_probe(name, sources, includes=()):
    binary = OUT / (name + ".exe")
    command = [CC, "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
               *["-I" + str(p) for p in (OUT / "stubs", ROOT,
                   ROOT / "sdk/include", ROOT / "firmware/include",
                   ROOT / "platforms/k210/hal", *includes)],
               *map(str, sources), "-o", str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    return binary

write("stubs/hk_config.h", "#define HK_MICROPYTHON_WDT_FAULT_INJECTION 0\n")
write("stubs/mp_stub.h", r'''
#ifndef AUDIT_MP_STUB_H
#define AUDIT_MP_STUB_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct { void *ret_val; } nlr_buf_t;
typedef int qstr;
typedef struct { qstr source_name; } mp_lexer_t;
typedef int mp_parse_tree_t;
typedef void *mp_obj_t;
#define MP_QSTR__lt_stdin_gt_ 1
#define MP_PARSE_FILE_INPUT 1
static const int mp_plat_print = 0, mp_type_KeyboardInterrupt = 0;
static inline int nlr_push(nlr_buf_t *n) { (void)n; return 0; }
static inline void nlr_pop(void) {}
static inline mp_lexer_t *mp_lexer_new_from_str_len(qstr q, const char *s, size_t n, int f)
{ static mp_lexer_t l; (void)s; (void)n; (void)f; l.source_name=q; return &l; }
static inline mp_parse_tree_t mp_parse(mp_lexer_t *l, int f) { (void)l; (void)f; return 0; }
static inline mp_obj_t mp_compile(mp_parse_tree_t *t, qstr q, bool b)
{ (void)t; (void)q; (void)b; return NULL; }
static inline void mp_call_function_0(mp_obj_t o) { (void)o; }
static inline void mp_obj_print_exception(const int *p, mp_obj_t o) { (void)p; (void)o; }
static inline void mp_embed_init(void *p, size_t s, void *a) { (void)p; (void)s; (void)a; }
static inline void mp_embed_deinit(void) {}
static inline void mp_stack_set_limit(size_t s) { (void)s; }
static inline void mp_raise_type(const int *p) { (void)p; }
#endif
''')
for name in ("port/micropython_embed.h", "py/compile.h", "py/gc.h", "py/parse.h", "py/runtime.h", "py/stackctrl.h"):
    write("stubs/" + name, '#include "mp_stub.h"\n')

mp = write("micropython_probe.c", r'''
#include <stdio.h>
#include <string.h>
#include "firmware/src/services/micropython_runtime.c"
static core1_executor_job_fn queued;
static uint8_t worker_busy;
static unsigned cleanups, cleanup_while_busy;
uint64_t hal_time_us(void) { return 1000; }
void hal_watchdog_force_reset(uint64_t ms) { (void)ms; }
void hk_screen_request_wake(void) {}
uint8_t core1_executor_init(void) { return 1; }
uint8_t core1_executor_idle(void) { return !worker_busy; }
uint32_t core1_executor_submit(core1_executor_job_fn job, void *ctx)
{ (void)ctx; queued=job; worker_busy=1; return 1; }
uint8_t core1_executor_complete(uint32_t ticket) { (void)ticket; return !worker_busy; }
void micropython_capability_bridge_prepare(uint32_t id) { (void)id; }
hk_result_t micropython_capability_bridge_cleanup(void)
{ cleanups++; if(worker_busy) cleanup_while_busy++; return HK_OK; }
int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "normal";
    micropython_runtime_status_t status;
    unsigned rejected = 0;
    if(!micropython_runtime_start("pass", 4, 1000)) return 2;
    if(strcmp(mode, "busy") == 0)
        rejected = !micropython_runtime_start("pass", 4, 1000);
    else if(strcmp(mode, "invalid") == 0) {
        rejected = !micropython_runtime_start("", 0, 1000);
        micropython_runtime_poll();
    }
    queued(NULL);
    worker_busy=0;
    micropython_runtime_poll();
    micropython_runtime_get_status(&status);
    printf("mode=%s rejected=%u final_state=%u exit_reason=%u cleanups=%u cleanup_while_executor_busy=%u\n",
           mode, rejected, status.state, status.exit_reason, cleanups, cleanup_while_busy);
    return 0;
}
''')
mp_exe = compile_probe("micropython_probe", [mp])
results = {}
for mode in ("normal", "busy", "invalid"):
    results["micropython_" + mode] = subprocess.check_output([str(mp_exe), mode], text=True).strip()

surface = write("surface_probe.c", r'''
#define main original_fixture_main
#include "tests/app_runtime_production_harness.c"
#undef main
static hk_result_t clear_result, lock_result;
static hk_result_t probe_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    hk_display_surface_t pixels;
    (void)ctx;
    clear_result = hk_app_surface_clear(surface, 0xffff);
    lock_result = hk_app_surface_lock(surface, &pixels);
    return HK_OK;
}
int main(void)
{
    hk_app_v2_entry_t entry = s_v2_entry;
    hk_app_t app = s_v2_app;
    hk_result_t poll;
    entry.render = probe_render;
    app.entry = &entry;
    s_fixture.now_us = 100;
    if(app_runtime_integration_initialize() != HK_OK ||
       app_runtime_integration_open(&app, NULL) != HK_OK) return 2;
    poll = app_runtime_integration_poll(100);
    printf("clear=%d lock_after_clear=%d aborted_batches=%u poll=%d\n",
           clear_result, lock_result, s_fixture.display_abort_count, poll);
    return 0;
}
''')
surface_sources = [surface] + [ROOT / name for name in (
    "firmware/src/app_runtime/runtime.c", "firmware/src/app_runtime/surface.c",
    "firmware/src/app_runtime/switch.c", "firmware/src/runtime/app_runtime_integration.c",
    "firmware/src/runtime/hk_main.c", "firmware/src/capabilities/lights.c",
    "tests/lights_normative_fake_backend.c")]
surface_exe = compile_probe("surface_probe", surface_sources, [ROOT / "tests", ROOT / "firmware/assets"])
results["surface_mode"] = subprocess.check_output([str(surface_exe)], text=True).strip()

uart = write("uart_probe.c", r'''
#define main original_fixture_main
#include "tests/external_link_service_harness.c"
#undef main
static void pump(void) {
    for(unsigned i = 0; i < 12; i++) {
        s_now_us += 2000;
        external_link_service_tick();
    }
}
int main(int argc, char **argv)
{
    char info[256];
    size_t size;
    unsigned coalesced = argc > 1 && strcmp(argv[1], "coalesced") == 0;
    external_link_service_init(EXTERNAL_LINK_UART);
    size = hk_link_frame_encode(HK_LINK_PING, 1, NULL, 0, s_uart_rx, sizeof(s_uart_rx));
    s_uart_rx_size = (uint32_t)size;
    if(!coalesced) pump();
    size = hk_link_frame_encode(HK_LINK_PING, 2, NULL, 0,
        s_uart_rx + s_uart_rx_size, sizeof(s_uart_rx) - s_uart_rx_size);
    s_uart_rx_size += (uint32_t)size;
    pump();
    external_link_service_format_info(info, sizeof(info));
    printf("coalesced=%u unread=%u %s", coalesced, s_uart_rx_size, info);
    return 0;
}
''')
sys.path.insert(0, str(ROOT / "tools"))
import gen_board
uart_exe = compile_probe("uart_probe", [uart, ROOT / "firmware/src/services/external_link_service.c",
    ROOT / "firmware/src/services/external_link_protocol.c"],
    [ROOT / "firmware/src/services", ROOT / "firmware/src/core", gen_board.board_config_include_dir()])
for mode in ("sequential", "coalesced"):
    results["uart_" + mode] = subprocess.check_output([str(uart_exe), mode], text=True).strip()

lights = write("native_lights_probe.c", r'''
#define main original_fixture_main
#include "tests/app_runtime_production_harness.c"
#undef main
#include "firmware/src/services/settings_lights.h"
uint8_t settings_led_enabled(void) { return 0; }
uint8_t settings_led_brightness(void) { return 40; }
uint8_t settings_rgb_enabled(void) { return 0; }
uint8_t settings_rgb_red(void) { return 20; }
uint8_t settings_rgb_green(void) { return 30; }
uint8_t settings_rgb_blue(void) { return 40; }
uint8_t settings_screen_brightness(void) { return 90; }
void settings_set_led_brightness(uint8_t v) { (void)v; }
void settings_set_rgb_red(uint8_t v) { (void)v; }
void settings_set_rgb_green(uint8_t v) { (void)v; }
void settings_set_rgb_blue(uint8_t v) { (void)v; }
void settings_set_screen_brightness(uint8_t v) { (void)v; }
static hk_result_t requested;
static hk_result_t request_light(const hk_app_context_t *ctx)
{
    hk_lights_t *lights;
    requested = hk_app_context_lights(ctx, HK_LIGHTS_CHANNEL_ILLUMINATION, &lights);
    return HK_OK;
}
int main(void)
{
    hk_app_v2_entry_t entry = s_v2_entry;
    hk_app_t app = s_v2_app;
    hk_result_t before, after;
    entry.start = request_light;
    app.entry = &entry;
    s_fixture.now_us = 100;
    screen_brightness_apply(); illum_led_apply(); rgb_led_apply();
    if(app_runtime_integration_initialize() != HK_OK ||
       app_runtime_integration_open(&app, NULL) != HK_OK) return 2;
    before = requested;
    (void)app_runtime_integration_close(HK_APP_STOP_SWITCH);
    settings_lights_suspend(HK_LIGHTS_CHANNEL_ILLUMINATION);
    if(app_runtime_integration_open(&app, NULL) != HK_OK) return 3;
    after = requested;
    (void)app_runtime_integration_close(HK_APP_STOP_SWITCH);
    printf("native_with_boot_settings=%d after_private_suspend=%d expected_busy=%d\n",
           before, after, HK_ERR_BUSY);
    return 0;
}
''')
lights_exe = compile_probe("native_lights_probe", [lights, *surface_sources[1:],
    ROOT / "firmware/src/services/settings_lights_apply.c"], [ROOT / "tests", ROOT / "firmware/assets"])
results["native_lights"] = subprocess.check_output([str(lights_exe)], text=True).strip()

camera = write("camera_probe.c", r'''
#include <stdio.h>
#include "firmware/src/drivers/camera_stream.c"
void hal_dvp_set_display_addr(uint32_t a) { (void)a; }
void hal_dvp_start_convert(void) {}
void hal_dvp_stop_capture(void) {}
void hal_dvp_clear_frame_start(void) {}
void hal_dvp_irq_stop(void) {}
void hal_dvp_irq_mask(void) {}
void hal_dvp_irq_unmask(void) {}
uint32_t hal_dvp_status(void) { return 0; }
void hal_dvp_config_rgb565(uint16_t w, uint16_t h, uint32_t a, uint8_t b)
{ (void)w; (void)h; (void)a; (void)b; }
uint8_t hal_dvp_irq_start(hal_dvp_event_callback_t c, void *ctx)
{ (void)c; (void)ctx; return 1; }
static void frame(void)
{
    camera_stream_on_event(HAL_DVP_EVENT_FRAME_START, 0, NULL);
    camera_stream_on_event(HAL_DVP_EVENT_FRAME_START, 0, NULL);
    camera_stream_on_event(HAL_DVP_EVENT_FRAME_FINISH, 0, NULL);
}
int main(void)
{
    camera_stream_frame_t old, current;
    unsigned before, after;
    if(!camera_stream_start(320,240,1)) return 2;
    frame();
    if(!camera_stream_acquire_latest(&old)) return 3;
    camera_stream_stop();
    if(!camera_stream_start(320,240,1)) return 4;
    frame();
    if(!camera_stream_acquire_latest(&current)) return 5;
    before = g_slots[0].state;
    camera_stream_release(old.lease_id);
    after = g_slots[0].state;
    printf("old_lease=%u current_lease=%u before=%u after_stale_release=%u leased=%u free=%u\n",
        old.lease_id, current.lease_id, before, after, CAMERA_SLOT_LEASED, CAMERA_SLOT_FREE);
    return 0;
}
''')
camera_exe = compile_probe("camera_probe", [camera, ROOT / "firmware/src/services/frame_pool.c"],
    [gen_board.board_config_include_dir()])
results["camera_lease"] = subprocess.check_output([str(camera_exe)], text=True).strip()

import build_firmware
for profile, disabled, translation_unit in (
    ("qr-only-camera-consumer", {"camera", "face-detect", "apriltag", "object-detect"},
     "firmware/src/apps/qr_camera/qr_camera_app.c"),
    ("no-camera", {"camera", "qr-camera", "face-detect", "apriltag", "object-detect"},
     "firmware/src/runtime/app_runtime_integration.c"),
):
    stage = OUT / profile
    with contextlib.redirect_stdout(io.StringIO()):
        build_firmware.stage_firmware_sources(stage, disabled)
    check = subprocess.run([CC, "-std=c11", "-fsyntax-only",
        "-I" + str(ROOT / "sdk/include"), "-I" + str(ROOT / "firmware/include"),
        "-I" + str(ROOT / "firmware/assets"), "-I" + str(gen_board.board_config_include_dir()),
        str(stage / translation_unit)], capture_output=True, text=True)
    write(profile + ".log", check.stdout + check.stderr)
    results[profile] = {"compiler_exit": check.returncode,
        "camera_service_header_staged": (stage / "firmware/src/services/camera_light.h").exists(),
        "diagnostic": next((line for line in check.stderr.splitlines() if "fatal error:" in line), check.stderr)}
write("probe-results.json", json.dumps(results, indent=2) + "\n")
print(json.dumps(results, indent=2))
