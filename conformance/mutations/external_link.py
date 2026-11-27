"""Check that fixture traces expose violations in connector validation and quiescence."""

from pathlib import Path

from conformance.adapters.k210.collect import LINK_PROVIDER_SOURCE, collect
from conformance.mutations import experiment


MUTATIONS = {
    "publish-cancel-before-peripheral-quiescence": (
        "    if(operation->kind == HK_EXTERNAL_LINK_OP_I2C_TRANSFER)\n"
        "        hal_external_i2c_stop();\n"
        "    else if(result != HK_OK && state->uart_baud != 0U)",
        "    if(operation->kind == HK_EXTERNAL_LINK_OP_I2C_TRANSFER)\n"
        "    {\n"
        "        if(result != HK_ERR_CANCELLED)\n"
        "            hal_external_i2c_stop();\n"
        "    }\n"
        "    else if(result != HK_OK && state->uart_baud != 0U)",
        {"cancelled-transfer-releases-peripheral-borrow"},
    ),
    "touch-uart-before-baud-validation": (
        "    if(!config || config->baud < K210_EXTERNAL_UART_MIN_BAUD ||\n"
        "       config->baud > K210_EXTERNAL_UART_MAX_BAUD)\n"
        "        return HK_ERR_INVALID_ARGUMENT;",
        "    if(!config || config->baud < K210_EXTERNAL_UART_MIN_BAUD ||\n"
        "       config->baud > K210_EXTERNAL_UART_MAX_BAUD)\n"
        "    {\n"
        "        if(config)\n"
        "            hal_external_uart_init(config->baud, uart_receive, state);\n"
        "        return HK_ERR_INVALID_ARGUMENT;\n"
        "    }",
        {"connector-claims-and-validation-before-effects"},
    ),
}


def main() -> int:
    return experiment.main(__doc__, Path(__file__), LINK_PROVIDER_SOURCE,
                           lambda bundle, source: collect(bundle, source_overrides={LINK_PROVIDER_SOURCE: source}),
                           MUTATIONS)


if __name__ == "__main__":
    raise SystemExit(main())
