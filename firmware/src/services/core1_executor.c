#include "core1_executor.h"

#include <stddef.h>

#include "../hal/hal_core.h"
#include "../hal/hal_time.h"

#define CORE1_EXECUTOR_START_TIMEOUT_US 250000ULL
#define CORE1_EXECUTOR_IDLE_MS 1U

static volatile uint8_t g_started;
static volatile uint8_t g_online;
static volatile uint8_t g_start_failed;
static volatile uint32_t g_request_ticket;
static volatile uint32_t g_complete_ticket;
static core1_executor_job_fn volatile g_job;
static void *volatile g_context;

static int core1_executor_loop(void *context)
{
    (void)context;
    g_online = 1U;
    __sync_synchronize();

    while(1)
    {
        uint32_t ticket = g_request_ticket;

        if(ticket != g_complete_ticket)
        {
            core1_executor_job_fn job;
            void *job_context;

            __sync_synchronize();
            job = g_job;
            job_context = g_context;
            if(job)
                job(job_context);
            __sync_synchronize();
            g_complete_ticket = ticket;
            continue;
        }
        hal_sleep_ms(CORE1_EXECUTOR_IDLE_MS);
    }

    return 0;
}

uint8_t core1_executor_init(void)
{
    uint64_t deadline;

    if(g_online)
        return 1U;
    if(g_start_failed)
        return 0U;
    if(!g_started)
    {
        g_started = 1U;
        if(hal_core1_start(core1_executor_loop, NULL) != 0)
        {
            g_start_failed = 1U;
            return 0U;
        }
    }

    deadline = hal_time_us() + CORE1_EXECUTOR_START_TIMEOUT_US;
    while(!g_online && hal_time_us() < deadline)
        hal_sleep_ms(1U);
    if(!g_online)
        g_start_failed = 1U;
    return g_online;
}

uint8_t core1_executor_idle(void)
{
    return g_online && g_request_ticket == g_complete_ticket;
}

uint32_t core1_executor_submit(core1_executor_job_fn job, void *context)
{
    uint32_t ticket;

    if(!job || !core1_executor_init() || !core1_executor_idle())
        return 0U;
    ticket = g_request_ticket + 1U;
    if(ticket == 0U)
        ticket = 1U;
    g_job = job;
    g_context = context;
    __sync_synchronize();
    g_request_ticket = ticket;
    return ticket;
}

uint8_t core1_executor_complete(uint32_t ticket)
{
    uint32_t completed;

    if(!ticket)
        return 0U;
    completed = g_complete_ticket;
    __sync_synchronize();
    /*
     * A client may observe its completion after another feature has already
     * submitted and completed a newer job. Treat the monotonically increasing
     * ticket as a wrap-safe sequence, not as an edge that must be acknowledged
     * before the next submission.
     */
    return (int32_t)(completed - ticket) >= 0;
}

uint8_t core1_executor_wait(uint32_t ticket, uint64_t timeout_us)
{
    uint64_t deadline;

    if(!ticket)
        return 0U;
    deadline = hal_time_us() + timeout_us;
    while(!core1_executor_complete(ticket))
    {
        if(hal_time_us() >= deadline)
            return 0U;
        hal_sleep_ms(1U);
    }
    return 1U;
}
