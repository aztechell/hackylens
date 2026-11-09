#include "board_config.h"

#if HK_BOARD_AUDIO_ES8374
#include "hal_audio.h"
#include "hal_onboard_i2c.h"
#include "hal_shared_buffer.h"
#include "hal_time.h"

#include "dmac.h"
#include "encoding.h"
#include "fpioa.h"
#include "i2s.h"
#include "sysctl.h"

#define CODEC_ADDRESS 0x10U
#define AUDIO_DMA DMAC_CHANNEL3
#define AUDIO_I2S I2S_DEVICE_0
#define AUDIO_SETTLE_US 30000U
#define AUDIO_DRAIN_US 2000U
#define AUDIO_TRANSFER_MARGIN_US 60000U
#define AUDIO_DMA_BLOCK_STATUS (1ULL << 0)
#define AUDIO_DMA_DONE_STATUS (1ULL << 1)
#define AUDIO_DMA_ERROR_STATUS (0xffULL << 5)
#define AUDIO_DMA_LINKED_LIST 3U
#define AUDIO_PLL_LOCK_US 5000U
#define AUDIO_DMA_STOP_US 3000U
#define AUDIO_STREAM_SLOTS 8U
#define AUDIO_IRQ (IRQN_DMA0_INTERRUPT + AUDIO_DMA)

typedef struct
{
    uint8_t reg;
    uint8_t value;
} codec_setting_t;

/* Cube ES8374 setup from Sipeed's MaixPy ES8374 board example. Keep the
 * class-D output disabled and the DAC muted until a playback request. */
static const codec_setting_t codec_setup[] = {
    {0x00, 0x3f}, {0x00, 0x03}, {0x01, 0x7f},
    /* I2S slave, 256 MCLK cycles per sample, 16-bit I2S. */
    {0x0f, 0x04}, {0x06, 0x01}, {0x07, 0x00},
    {0x6f, 0xa0}, {0x72, 0x41}, {0x09, 0x01},
    {0x0c, 0x08}, {0x0d, 0x13}, {0x0e, 0xe0},
    {0x0a, 0x8a}, {0x0b, 0x08}, {0x09, 0x41},
    {0x10, 0x0c}, {0x11, 0x0c}, {0x12, 0x30}, {0x13, 0x20},
    /* Analog mic path and gain. */
    {0x24, 0x08}, {0x21, 0x24}, {0x22, 0x66}, {0x25, 0x00},
    {0x28, 0x70}, {0x26, 0x4e}, {0x27, 0x10}, {0x29, 0x00}, {0x2b, 0x00},
    /* Speaker mixer, class-D clock and fixed -12 dB digital gain. */
    {0x08, 0x21}, {0x00, 0x80}, {0x14, 0x8a}, {0x15, 0x40},
    {0x1a, 0xa0}, {0x1b, 0x19}, {0x1c, 0x90}, {0x1d, 0x12},
    {0x1f, 0x00}, {0x1e, 0x20}, {0x36, 0x20}, {0x37, 0x00},
    {0x38, 24}, {0x6d, 0x60}, {0x71, 0x05}, {0x73, 0x70},
};

typedef enum
{
    AUDIO_IDLE,
    AUDIO_SETTLING,
    AUDIO_TRANSFERRING,
    AUDIO_DRAINING,
} audio_state_t;

/* Use one DMA word per I2S slot. Playback duplicates mono to both slots.
 * The codec's mono ADC is read from the first slot. */
static uint32_t s_dma_storage[AUDIO_STREAM_SLOTS * HK_AUDIO_BLOCK_FRAMES * 2U]
    __attribute__((aligned(64)));
static dmac_lli_item_t s_dma_links[AUDIO_STREAM_SLOTS];
_Static_assert(sizeof(s_dma_storage) >= HK_AUDIO_PCM_MAX_BYTES * sizeof(uint32_t),
               "DMA storage must fit a full Python transfer");
static struct
{
    volatile uint8_t *pcm;
    size_t frames;
    uint64_t deadline_us;
    hk_audio_direction_t direction;
    audio_state_t state;
    uint8_t clock_ready;
    uint8_t codec_ready;
    uint8_t speaker_enabled;
} s_audio;

static uint8_t s_volume = 100U;
static struct
{
    volatile uint32_t complete;
    volatile uint32_t ready;
    volatile uint32_t taken;
    volatile uint32_t last;
    volatile uint32_t frames_done;
    uint32_t observed;
    volatile hk_result_t result;
    volatile uint8_t active;
    volatile uint8_t end;
    volatile uint8_t irq_registered;
    uint16_t frames[AUDIO_STREAM_SLOTS];
} s_stream;

static uint32_t *audio_dma_buffer(void)
{
    return hal_shared_buffer_uncached(s_dma_storage);
}

static volatile dmac_lli_item_t *stream_link(uint32_t block)
{
    dmac_lli_item_t *links = hal_shared_buffer_uncached(s_dma_links);
    return &links[block % AUDIO_STREAM_SLOTS];
}

static uint8_t codec_write(uint8_t reg, uint8_t value)
{
    return hal_onboard_i2c_write(CODEC_ADDRESS, reg, value);
}

static uint8_t codec_configure(void)
{
    for(size_t i = 0; i < sizeof(codec_setup) / sizeof(codec_setup[0]); i++)
    {
        if(!codec_write(codec_setup[i].reg, codec_setup[i].value))
            return 0U;
    }
    return 1U;
}

static uint8_t speaker_disable(void)
{
    uint8_t muted = codec_write(0x36, 0x20);
    uint8_t disabled = codec_write(0x1e, 0x20);

    if(!muted || !disabled)
        s_audio.codec_ready = 0U;
    s_audio.speaker_enabled = !disabled;
    return muted && disabled;
}

static uint8_t speaker_gain(uint8_t percent)
{
    /* Register units are half-decibels. Keep the existing -12 dB ceiling. */
    return (uint8_t)(24U + (100U - percent) * 40U / 100U);
}

hk_result_t hal_audio_set_volume(uint8_t percent)
{
    if(s_audio.speaker_enabled &&
       (!codec_write(0x38, speaker_gain(percent)) ||
        (s_audio.state != AUDIO_SETTLING &&
         !codec_write(0x36, percent ? 0x00U : 0x20U))))
        return HK_ERR_IO;
    s_volume = percent;
    return HK_OK;
}

static hk_result_t audio_prepare_clock(void)
{
    uint64_t deadline;
    sysctl_pll2_t clock;

    if(s_audio.clock_ready)
        return HK_OK;
    sysctl->pll2.pll_out_en2 = 0U;
    sysctl->pll2.pll_pwrd2 = 0U;
    clock = sysctl->pll2;
    /* 26 MHz crystal, x60 then /4. PLL0 and PLL1 are separate. */
    clock.pll_ckin_sel2 = 0U;
    clock.clkr2 = 0U;
    clock.clkf2 = 59U;
    clock.clkod2 = 3U;
    clock.bwadj2 = 59U;
    clock.pll_bypass2 = 0U;
    sysctl->pll2 = clock;
    sysctl->pll2.pll_pwrd2 = 1U;
    deadline = hal_time_us() + 2U;
    while(hal_time_us() < deadline)
        ;
    sysctl->pll2.pll_reset2 = 1U;
    deadline = hal_time_us() + 2U;
    while(hal_time_us() < deadline)
        ;
    sysctl->pll2.pll_reset2 = 0U;
    deadline = hal_time_us() + AUDIO_PLL_LOCK_US;
    while(!(sysctl->pll_lock.pll_lock2 & 1U))
    {
        sysctl->pll_lock.pll_slip_clear2 = 1U;
        if(hal_time_us() >= deadline)
        {
            sysctl->pll2.pll_pwrd2 = 0U;
            return HK_ERR_IO;
        }
    }
    sysctl->pll2.pll_out_en2 = 1U;
    (void)sysctl_clock_enable(SYSCTL_CLOCK_PLL2);
    s_audio.clock_ready = 1U;
    return HK_OK;
}

static void audio_configure_i2s(hk_audio_direction_t direction)
{
    fpioa_set_function(IO_AUDIO_MCLK, FUNC_I2S0_MCLK);
    fpioa_set_function(IO_AUDIO_BCLK, FUNC_I2S0_SCLK);
    fpioa_set_function(IO_AUDIO_LRCLK, FUNC_I2S0_WS);
    fpioa_set_function(IO_AUDIO_RX, FUNC_I2S0_IN_D0);
    fpioa_set_function(IO_AUDIO_TX, FUNC_I2S0_OUT_D2);

    if(direction == HK_AUDIO_PLAY)
    {
        i2s_init(AUDIO_I2S, I2S_TRANSMITTER, 0x30U);
        i2s_tx_channel_config(AUDIO_I2S, I2S_CHANNEL_2, RESOLUTION_16_BIT,
                              SCLK_CYCLES_32, TRIGGER_LEVEL_4, STANDARD_MODE);
    }
    else
    {
        i2s_init(AUDIO_I2S, I2S_RECEIVER, 0x03U);
        i2s_rx_channel_config(AUDIO_I2S, I2S_CHANNEL_0, RESOLUTION_16_BIT,
                              SCLK_CYCLES_32, TRIGGER_LEVEL_4, STANDARD_MODE);
    }
    /* 390 MHz PLL2 divided by 384 for BCLK and 96 for MCLK.
     * BCLK is 64 times LRCLK, giving 15869.14 frames per second. */
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_I2S0, 191);
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_I2S0_M, 47);
    (void)i2s_set_dma_divide_16(AUDIO_I2S, 0U);
}

static void audio_abort_dma(void)
{
    dmac_chen_u_t command = {0};
    command.dmac_chen.ch4_abort = 1U;
    command.dmac_chen.ch4_abort_we = 1U;
    dmac->chen = command.data;
}

static hk_result_t audio_finish(hk_result_t result)
{
    uint8_t was_active = s_audio.state != AUDIO_IDLE;

    s_stream.active = 0U;
    if(s_stream.irq_registered)
    {
        plic_irq_disable(AUDIO_IRQ);
        plic_irq_unregister(AUDIO_IRQ);
        dmac->channel[AUDIO_DMA].intsignal_en = 0U;
        s_stream.irq_registered = 0U;
    }
    if(was_active && !dmac_is_idle(AUDIO_DMA))
    {
        uint64_t deadline = hal_time_us() + AUDIO_DMA_STOP_US;
        audio_abort_dma();
        dmac_channel_disable(AUDIO_DMA);
        /* Leave I2S clocked until any pending DMA handshake has retired. */
        while(!dmac_is_idle(AUDIO_DMA) && hal_time_us() < deadline)
            ;
        if(!dmac_is_idle(AUDIO_DMA) && result == HK_OK)
            result = HK_ERR_IO;
    }
    if(s_audio.speaker_enabled && !speaker_disable() && result == HK_OK)
        result = HK_ERR_IO;
    if(was_active)
    {
        i2s[AUDIO_I2S]->cer = 0U;
        i2s[AUDIO_I2S]->ier = 0U;
    }
    s_audio.state = AUDIO_IDLE;
    s_audio.pcm = NULL;
    return result;
}

static hk_result_t audio_prepare(hk_audio_direction_t direction)
{
    hk_result_t result;

    if(s_audio.state != AUDIO_IDLE || !dmac_is_idle(AUDIO_DMA))
        return HK_ERR_BUSY;
    if(s_audio.speaker_enabled && !speaker_disable())
        return HK_ERR_IO;
    result = audio_prepare_clock();
    if(result != HK_OK)
        return result;
    audio_configure_i2s(direction);
    s_audio.state = AUDIO_SETTLING;
    if(!s_audio.codec_ready)
    {
        if(!codec_configure())
            return audio_finish(HK_ERR_IO);
        s_audio.codec_ready = 1U;
    }
    if(direction == HK_AUDIO_PLAY)
    {
        s_audio.speaker_enabled = 1U;
        if(!codec_write(0x38, speaker_gain(s_volume)) ||
           !codec_write(0x1d, 0x02) || !codec_write(0x1e, 0xa0))
            return audio_finish(HK_ERR_IO);
    }
    s_audio.deadline_us = hal_time_us() + AUDIO_SETTLE_US;
    s_audio.direction = direction;
    return HK_PENDING;
}

static void pack_samples(uint32_t *dma, const volatile uint8_t *pcm, size_t frames)
{
    for(size_t i = 0; i < frames; i++)
    {
        uint32_t sample = pcm[2U * i] | ((uint32_t)pcm[2U * i + 1U] << 8);
        dma[2U * i] = sample;
        dma[2U * i + 1U] = sample;
    }
}

static void unpack_samples(volatile uint8_t *pcm, const uint32_t *dma, size_t frames)
{
    for(size_t i = 0; i < frames; i++)
    {
        pcm[2U * i] = (uint8_t)dma[2U * i];
        pcm[2U * i + 1U] = (uint8_t)(dma[2U * i] >> 8);
    }
}

hk_result_t hal_audio_begin(hk_audio_direction_t direction,
                            volatile uint8_t *pcm, size_t frames)
{
    hk_result_t result = audio_prepare(direction);
    if(result != HK_PENDING)
        return result;
    if(direction == HK_AUDIO_PLAY)
        pack_samples(audio_dma_buffer(), pcm, frames);
    s_audio.frames = frames;
    s_audio.pcm = pcm;
    return HK_PENDING;
}

static void start_dma(uint32_t *dma, size_t frames)
{
    __sync_synchronize();
    if(s_audio.direction == HK_AUDIO_PLAY)
        i2s_send_data_dma(AUDIO_I2S, dma, 2U * frames, AUDIO_DMA);
    else
        i2s_receive_data_dma(AUDIO_I2S, dma, 2U * frames, AUDIO_DMA);
}

static uint32_t *stream_buffer(uint32_t block)
{
    return audio_dma_buffer() + (block % AUDIO_STREAM_SLOTS) * 2U * HK_AUDIO_BLOCK_FRAMES;
}

static dmac_ch_ctl_u_t stream_link_control(uint8_t play)
{
    dmac_ch_ctl_u_t ctl = {0};
    ctl.ch_ctl.sms = DMAC_MASTER1;
    ctl.ch_ctl.dms = DMAC_MASTER2;
    ctl.ch_ctl.sinc = play ? DMAC_ADDR_INCREMENT : DMAC_ADDR_NOCHANGE;
    ctl.ch_ctl.dinc = play ? DMAC_ADDR_NOCHANGE : DMAC_ADDR_INCREMENT;
    ctl.ch_ctl.src_tr_width = DMAC_TRANS_WIDTH_32;
    ctl.ch_ctl.dst_tr_width = DMAC_TRANS_WIDTH_32;
    ctl.ch_ctl.src_msize = DMAC_MSIZE_1;
    ctl.ch_ctl.dst_msize = DMAC_MSIZE_1;
    ctl.ch_ctl.ioc_blktfr = 1U;
    return ctl;
}

static int stream_dma_irq(void *context)
{
    (void)context;
    uint64_t status = dmac->channel[AUDIO_DMA].intstatus;
    dmac->channel[AUDIO_DMA].intclear = status;
    if(!s_stream.active || s_stream.result != HK_PENDING)
        return 0;

    uint8_t play = s_audio.direction == HK_AUDIO_PLAY;
    if(status & AUDIO_DMA_ERROR_STATUS)
        s_stream.result = HK_ERR_IO;
    else if(!play && (i2s[AUDIO_I2S]->channel[I2S_CHANNEL_0].ror & 1U))
        s_stream.result = HK_ERR_OVERFLOW;
    else if(status & AUDIO_DMA_BLOCK_STATUS)
    {
        uint32_t block = s_stream.complete;
        uint32_t complete = block + 1U;
        s_stream.frames_done += play ? s_stream.frames[block % AUDIO_STREAM_SLOTS] :
                                       HK_AUDIO_BLOCK_FRAMES;
        if(play)
            stream_link(block)->ctl = 0U;
        __sync_synchronize();
        s_stream.complete = complete;
        if(play ? complete == s_stream.last : s_stream.end)
            s_stream.result = HK_OK;
        /* Keep the slot that DMA has already started separate from unread PCM. */
        else if(play ? complete == s_stream.ready :
                        complete - s_stream.taken >= AUDIO_STREAM_SLOTS - 1U)
            s_stream.result = HK_ERR_LIMIT;
    }
    if(s_stream.result != HK_PENDING && (!play || s_stream.result != HK_OK))
        audio_abort_dma();
    return 0;
}

static void start_stream_dma(void)
{
    uint8_t play = s_audio.direction == HK_AUDIO_PLAY;
    const uint64_t irq_mask = AUDIO_DMA_BLOCK_STATUS | AUDIO_DMA_ERROR_STATUS;
    dmac_ch_cfg_u_t config = {0};
    config.ch_cfg.src_multblk_type = AUDIO_DMA_LINKED_LIST;
    config.ch_cfg.dst_multblk_type = AUDIO_DMA_LINKED_LIST;
    config.ch_cfg.hs_sel_src = play ? DMAC_HS_SOFTWARE : DMAC_HS_HARDWARE;
    config.ch_cfg.hs_sel_dst = play ? DMAC_HS_HARDWARE : DMAC_HS_SOFTWARE;
    config.ch_cfg.src_per = AUDIO_DMA;
    config.ch_cfg.dst_per = AUDIO_DMA;
    config.ch_cfg.ch_prior = 3U;
    config.ch_cfg.tt_fc = play ? DMAC_MEM2PRF_DMA : DMAC_PRF2MEM_DMA;
    dmac->channel[AUDIO_DMA].cfg = config.data;
    dmac->channel[AUDIO_DMA].ctl = 0U;
    dmac->channel[AUDIO_DMA].llp = (uintptr_t)stream_link(0U);

    sysctl_dma_select((sysctl_dma_channel_t)AUDIO_DMA,
                      play ? SYSCTL_DMA_SELECT_I2S0_TX_REQ : SYSCTL_DMA_SELECT_I2S0_RX_REQ);
    dmac->channel[AUDIO_DMA].intclear = 0xffffffffU;
    dmac->channel[AUDIO_DMA].intstatus_en = irq_mask;
    dmac->channel[AUDIO_DMA].intsignal_en = irq_mask;
    plic_irq_register(AUDIO_IRQ, stream_dma_irq, NULL);
    plic_set_priority(AUDIO_IRQ, 3U);
    s_stream.irq_registered = 1U;
    plic_irq_enable(AUDIO_IRQ);
    __sync_synchronize();
    dmac_channel_enable(AUDIO_DMA);
}

hk_result_t hal_audio_stream_begin(hk_audio_direction_t direction)
{
    hk_result_t result = audio_prepare(direction);
    if(result != HK_PENDING)
        return result;
    if(direction == HK_AUDIO_RECORD)
    {
        i2s[AUDIO_I2S]->irer = 0U;
        i2s[AUDIO_I2S]->channel[I2S_CHANNEL_0].rff = 1U;
    }
    uint8_t play = direction == HK_AUDIO_PLAY;
    dmac_ch_ctl_u_t ctl = stream_link_control(play);
    ctl.ch_ctl.shadowreg_or_lli_valid = !play;
    uintptr_t peripheral = play ? (uintptr_t)&i2s[AUDIO_I2S]->txdma :
                                 (uintptr_t)&i2s[AUDIO_I2S]->rxdma;
    /* The DMA follows this ring directly. An IRQ only retires a completed slot. */
    for(uint32_t block = 0U; block < AUDIO_STREAM_SLOTS; block++)
    {
        uintptr_t memory = (uintptr_t)stream_buffer(block);
        *stream_link(block) = (dmac_lli_item_t){
            .sar = play ? memory : peripheral,
            .dar = play ? peripheral : memory,
            .ch_block_ts = 2U * HK_AUDIO_BLOCK_FRAMES - 1U,
            .llp = (uintptr_t)stream_link(block + 1U),
            .ctl = ctl.data,
        };
    }
    s_stream.complete = 0U;
    s_stream.ready = 0U;
    s_stream.taken = 0U;
    s_stream.last = UINT32_MAX;
    s_stream.frames_done = 0U;
    s_stream.observed = 0U;
    s_stream.result = HK_PENDING;
    s_stream.end = 0U;
    s_stream.active = 1U;
    return HK_PENDING;
}

hk_result_t hal_audio_stream_push(const uint8_t *pcm, size_t frames, uint8_t last)
{
    uint32_t ready = s_stream.ready;
    if(!s_stream.active || s_audio.direction != HK_AUDIO_PLAY || s_stream.last != UINT32_MAX)
        return HK_ERR_INVALID_STATE;
    if(s_stream.result != HK_PENDING)
        return s_stream.result;
    if(ready - s_stream.complete >= AUDIO_STREAM_SLOTS)
        return HK_PENDING;
    pack_samples(stream_buffer(ready), pcm, frames);
    volatile dmac_lli_item_t *link = stream_link(ready);
    link->ch_block_ts = 2U * frames - 1U;
    s_stream.frames[ready % AUDIO_STREAM_SLOTS] = (uint16_t)frames;
    dmac_ch_ctl_u_t ctl = stream_link_control(1U);
    ctl.ch_ctl.shadowreg_or_lli_last = last;
    ctl.ch_ctl.shadowreg_or_lli_valid = 1U;
    /* Publish the descriptor and its software metadata together to the ISR. */
    unsigned long irq_state = clear_csr(mstatus, MSTATUS_MIE);
    hk_result_t result = s_stream.result;
    if(result == HK_PENDING)
    {
        if(last)
            s_stream.last = ready + 1U;
        __sync_synchronize();
        link->ctl = ctl.data;
        s_stream.ready = ready + 1U;
        result = HK_OK;
    }
    __sync_synchronize();
    if(irq_state & MSTATUS_MIE)
        (void)set_csr(mstatus, MSTATUS_MIE);
    return result;
}

hk_result_t hal_audio_stream_take(uint8_t *pcm)
{
    if(!s_stream.active || s_audio.direction != HK_AUDIO_RECORD)
        return HK_ERR_INVALID_STATE;
    if(s_stream.taken == s_stream.complete)
        return HK_PENDING;
    __sync_synchronize();
    unpack_samples(pcm, stream_buffer(s_stream.taken), HK_AUDIO_BLOCK_FRAMES);
    __sync_synchronize();
    /* DMA clears LLI_VALID when it consumes the link. Return the slot only
     * after the samples have been copied out of the ring. */
    dmac_ch_ctl_u_t ctl = stream_link_control(0U);
    ctl.ch_ctl.shadowreg_or_lli_valid = 1U;
    stream_link(s_stream.taken)->ctl = ctl.data;
    __sync_synchronize();
    s_stream.taken++;
    return HK_OK;
}

void hal_audio_stream_end(void)
{
    if(s_stream.active && s_audio.direction == HK_AUDIO_RECORD)
        s_stream.end = 1U;
}

uint32_t hal_audio_stream_frames(void)
{
    return s_stream.frames_done;
}

hk_result_t hal_audio_poll(void)
{
    uint64_t now = hal_time_us();
    uint32_t *dma = audio_dma_buffer();

    if(s_audio.state == AUDIO_IDLE)
        return HK_ERR_INVALID_STATE;
    if(s_audio.state == AUDIO_SETTLING)
    {
        if(now < s_audio.deadline_us)
            return HK_PENDING;
        if(s_stream.active && s_audio.direction == HK_AUDIO_PLAY && !s_stream.ready)
            return HK_PENDING;
        if(s_audio.direction == HK_AUDIO_PLAY && !codec_write(0x36, s_volume ? 0x00U : 0x20U))
            return audio_finish(HK_ERR_IO);
        /* A failed/cancelled transfer cannot reuse the DMA buffer until
         * this dedicated channel has gone idle. The SDK calls below only
         * enter their internal wait once this condition is already met. */
        if(!dmac_is_idle(AUDIO_DMA))
            return audio_finish(HK_ERR_BUSY);
        if(s_stream.active)
        {
            s_audio.state = AUDIO_TRANSFERRING;
            if(s_audio.direction == HK_AUDIO_RECORD)
            {
                (void)i2s[AUDIO_I2S]->channel[I2S_CHANNEL_0].ror;
                if(s_stream.end)
                    return audio_finish(HK_OK);
            }
            s_audio.deadline_us = now;
            start_stream_dma();
            if(s_audio.direction == HK_AUDIO_RECORD)
                i2s[AUDIO_I2S]->irer = 1U;
            return HK_PENDING;
        }
        /* Latch DMA completion for polling without raising a PLIC IRQ. */
        dmac->channel[AUDIO_DMA].intsignal_en = 0U;
        dmac->channel[AUDIO_DMA].intstatus_en = AUDIO_DMA_DONE_STATUS;
        start_dma(dma, s_audio.frames);
        s_audio.state = AUDIO_TRANSFERRING;
        s_audio.deadline_us = now + s_audio.frames * 1000000ULL /
                              HK_AUDIO_RATE_HZ + AUDIO_TRANSFER_MARGIN_US;
        return HK_PENDING;
    }
    if(s_audio.state == AUDIO_DRAINING)
        return now < s_audio.deadline_us ? HK_PENDING : audio_finish(HK_OK);
    if(s_stream.active)
    {
        hk_result_t result = s_stream.result;
        if(result == HK_PENDING)
        {
            if(s_stream.complete != s_stream.observed)
            {
                s_stream.observed = s_stream.complete;
                s_audio.deadline_us = now;
            }
            return now - s_audio.deadline_us < 350000U ? HK_PENDING :
                audio_finish(HK_ERR_DEADLINE_EXCEEDED);
        }
        if(s_audio.direction == HK_AUDIO_RECORD && s_stream.taken != s_stream.complete)
            return HK_PENDING;
        if(result == HK_OK && s_audio.direction == HK_AUDIO_PLAY)
        {
            s_audio.state = AUDIO_DRAINING;
            s_audio.deadline_us = now + AUDIO_DRAIN_US;
            return HK_PENDING;
        }
        return audio_finish(result);
    }
    if(!dmac_is_idle(AUDIO_DMA))
        return now < s_audio.deadline_us ? HK_PENDING :
            audio_finish(HK_ERR_DEADLINE_EXCEEDED);
    if(!dmac_is_done(AUDIO_DMA))
        return audio_finish(HK_ERR_IO);
    if(s_audio.direction == HK_AUDIO_PLAY)
    {
        s_audio.state = AUDIO_DRAINING;
        s_audio.deadline_us = now + AUDIO_DRAIN_US;
        return HK_PENDING;
    }
    __sync_synchronize();
    unpack_samples(s_audio.pcm, dma, s_audio.frames);
    return audio_finish(HK_OK);
}

hk_result_t hal_audio_cancel(void)
{
    return audio_finish(HK_OK);
}
#endif
