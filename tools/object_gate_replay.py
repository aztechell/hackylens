"""Build an isolated K210 image with deterministic VOC20 model inputs."""

import argparse
import json
from pathlib import Path

import build_firmware as firmware


def replace_once(path, old, new):
    source = path.read_text(encoding="utf-8")
    if source.count(old) != 1:
        raise ValueError(f"expected one replay insertion point in {path}")
    path.write_text(source.replace(old, new), encoding="utf-8")


def install_replay(stage, fixture):
    source = stage / "firmware/src"
    camera = source / "services/camera_ai_input.c"
    asset = json.dumps(json.dumps(fixture.as_posix()))
    fixture_assembly = f"""
#include <stdio.h>
__asm__ (
    ".pushsection .rodata\\n"
    ".balign 64\\n"
    ".global object_gate_fixture\\n"
    "object_gate_fixture:\\n"
    ".incbin " {asset} "\\n"
    ".popsection\\n");
extern const uint8_t object_gate_fixture[];
"""
    replay_code = """
enum { REPLAY_W = 144, REPLAY_H = 180, MODEL_W = 320, MODEL_H = 240 };
static uint32_t s_fixture_frame, s_inferences;
static uint64_t s_measured_kpu_us;
static uint8_t s_replay;

uint8_t object_gate_replay_command(const char *cmd)
{
    if(strcmp(cmd, "HKOGREPLAY ON") == 0)
    {
        s_replay = 1U;
        s_fixture_frame = s_inferences = 0;
        s_measured_kpu_us = 0;
        puts("HKOGREPLAY ON");
    }
    else if(strcmp(cmd, "HKOGREPLAY OFF") == 0)
    {
        s_replay = 0U;
        puts("HKOGREPLAY OFF");
    }
    else if(strcmp(cmd, "HKOGKPU") == 0)
    {
        printf("HKOGKPU %lu %llu\\r\\n", (unsigned long)s_inferences,
               (unsigned long long)s_measured_kpu_us);
    }
    else
        return 0U;
    return 1U;
}

void object_gate_replay_inference(uint32_t duration_us)
{
    if(s_replay && s_inferences++ >= 4U)
        s_measured_kpu_us += duration_us;
}

static void object_gate_replay_input(void)
{
    if(!s_replay || g_width != MODEL_W || g_height != MODEL_H)
        return;
    uint8_t *input = camera_ai_uncached();
    memset(input, 32, g_input_bytes);
    const uint32_t step = s_fixture_frame++ % 12U;
    if(step < 3U || step >= 9U)
        return;
    for(unsigned plane = 0; plane < 3; ++plane)
        for(unsigned y = 0; y < REPLAY_H; ++y)
            memcpy(input + (plane * MODEL_H + y + 30U) * MODEL_W + 88U,
                   object_gate_fixture + (plane * REPLAY_H + y) * REPLAY_W, REPLAY_W);
}

"""
    marker = "uint8_t camera_ai_input_take("
    replace_once(camera, marker, fixture_assembly + replay_code + marker)
    replace_once(camera, "    if(sequence)\n        *sequence = g_sequence;",
                 "    object_gate_replay_input();\n    if(sequence)\n        *sequence = g_sequence;")

    detector = source / "services/object_detect_detector.c"
    marker = "    if(ai_model_runtime_get_output(&g_runtime, 0U, &output, &bytes) != 0)"
    replace_once(detector, marker,
                 "    extern void object_gate_replay_inference(uint32_t duration_us);\n"
                 "    object_gate_replay_inference(g_runtime.last_inference_us);\n" + marker)

    controller = source / "controllers/debug_controller.c"
    marker = "void debug_uart_handle_command(const char *cmd)\n{"
    replace_once(controller, marker,
                 "extern uint8_t object_gate_replay_command(const char *cmd);\n\n"
                 + marker + "\n    if(object_gate_replay_command(cmd))\n        return;")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path, help="144x180 RGB888 image as three consecutive color planes")
    parser.add_argument("--board", default="sipeed-maix-cube",
                        choices=("sipeed-maix-cube", "huskylens-sen0305"))
    args = parser.parse_args()
    fixture = args.fixture.resolve(strict=True)
    if fixture.stat().st_size != 144 * 180 * 3:
        parser.error("fixture must contain exactly three 144x180 byte planes")
    stage_target = firmware.stage_target

    def stage_with_replay(*args, **kwargs):
        stage = stage_target(*args, **kwargs)
        install_replay(stage, fixture)
        return stage

    firmware.stage_target = stage_with_replay
    firmware.TARGETS["full"] = dict(firmware.TARGETS["full"], output="hackylens-object-gate.bin",
                                    build_dir="sdk-object-gate")
    build = ["full", "--board", args.board, "--disable-app", "sky-glide",
             "--require-app", "object-gate"]
    return firmware.main(build)


if __name__ == "__main__":
    raise SystemExit(main())
