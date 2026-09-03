#include "hk_board_port.h"

#include "board_config.h"

int main(void)
{
    const hk_board_ops_t *const ops = &hk_board_ops;

    if (ops->early_init == 0 || HK_FLASH_ADDRESS_BYTES != 3U ||
        HK_BOARD_RELEASEABLE != 0U)
    {
        return 1;
    }
    ops->early_init();
    return 0;
}
