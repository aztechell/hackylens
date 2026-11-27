"""Verify detection when the script bridge bypasses shared light ownership."""

from pathlib import Path

from conformance.adapters.k210.collect import SCRIPT_BRIDGE_SOURCE, collect
from conformance.mutations import experiment


MUTATIONS = {
    "script-writes-around-common-light-provider": (
        "        result = binding_claim_light(\n"
        "            HK_LIGHTS_CHANNEL_ILLUMINATION, &g_illumination_lights);\n"
        "        if(result == HK_OK)",
        "        result = binding_claim_light(\n"
        "            HK_LIGHTS_CHANNEL_ILLUMINATION, &g_illumination_lights);\n"
        "        if(result == HK_ERR_BUSY)\n"
        "        {\n"
        "            extern void lights_illum_set(uint8_t enabled, uint8_t brightness);\n"
        "            lights_illum_set(1U, (uint8_t)a[0]);\n"
        "        }\n"
        "        if(result == HK_OK)",
        {"native-and-script-share-exclusive-light-authority"},
    ),
}


def main() -> int:
    return experiment.main(__doc__, Path(__file__), SCRIPT_BRIDGE_SOURCE,
                           lambda bundle, source: collect(bundle, source_overrides={SCRIPT_BRIDGE_SOURCE: source}),
                           MUTATIONS)


if __name__ == "__main__":
    raise SystemExit(main())
