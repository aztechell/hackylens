#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "k210_script_runtime_fixture.h"
#include "micropython_runtime.h"

static const char *state_name(micropython_runtime_state_t state)
{
    switch(state)
    {
    case MICROPYTHON_RUNTIME_STARTING: return "starting";
    case MICROPYTHON_RUNTIME_RUNNING: return "running";
    case MICROPYTHON_RUNTIME_STOPPING: return "stopping";
    case MICROPYTHON_RUNTIME_STOPPED: return "stopped";
    case MICROPYTHON_RUNTIME_FINISHED: return "finished";
    case MICROPYTHON_RUNTIME_ERROR: return "error";
    default: return "unknown";
    }
}

static const char *exit_name(micropython_runtime_exit_t reason)
{
    switch(reason)
    {
    case MICROPYTHON_EXIT_NONE: return "none";
    case MICROPYTHON_EXIT_COMPLETE: return "complete";
    case MICROPYTHON_EXIT_REQUESTED: return "requested";
    case MICROPYTHON_EXIT_TIMEOUT: return "timeout";
    case MICROPYTHON_EXIT_EXCEPTION: return "exception";
    case MICROPYTHON_EXIT_BUSY: return "busy";
    case MICROPYTHON_EXIT_INVALID_SOURCE: return "invalid_source";
    case MICROPYTHON_EXIT_CORE1_FAILURE: return "executor_failure";
    default: return "unknown";
    }
}

int hk_test_vm_runtime_trace(int argc, char **argv)
{
    static const char source[] = "x";
    for(int i = 1; i < argc; ++i)
    {
        const char *action = argv[i];
        uint8_t accepted = 1;
        if(strcmp(action, "start-exception") == 0)
        {
            hk_test_vm_configure(source, HK_TEST_VM_THROWS);
            accepted = micropython_runtime_start(source, sizeof(source) - 1, 3);
        }
        else if(strcmp(action, "start-cooperative") == 0)
        {
            hk_test_vm_configure(source, HK_TEST_VM_CHECKS_INTERRUPT);
            accepted = micropython_runtime_start(source, sizeof(source) - 1, 3);
        }
        else if(strcmp(action, "try-start") == 0)
            accepted = micropython_runtime_start("replacement", sizeof("replacement") - 1, 3);
        else if(strcmp(action, "try-invalid-start") == 0)
            accepted = micropython_runtime_start(NULL, 0, 3);
        else if(strcmp(action, "worker-returns") == 0)
            hk_test_vm_return_from_worker();
        else if(strcmp(action, "executor-acknowledges") == 0)
            hk_test_vm_acknowledge_executor();
        else if(strcmp(action, "stop") == 0)
            accepted = micropython_runtime_request_stop();
        else if(strcmp(action, "poll") == 0)
            micropython_runtime_poll();
        else if(strcmp(action, "advance-to-deadline") == 0)
        {
            micropython_runtime_status_t current;
            micropython_runtime_get_status(&current);
            hk_test_vm_set_clock(current.deadline_us);
        }
        else
            return 2;

        micropython_runtime_status_t current;
        micropython_runtime_get_status(&current);
        printf("{\"event\":\"script_runtime\",\"action\":\"%s\",\"accepted\":%s,\"state\":\"%s\","
               "\"exit_reason\":\"%s\",\"run\":%" PRIu32 ",\"cleanup_calls\":%" PRIu32 ","
               "\"prepared_runs\":%" PRIu32 ",\"output_pending\":%" PRIu32 ","
               "\"deadline_us\":%" PRIu64 ",\"executor_acknowledged\":%s}\n",
               action, accepted ? "true" : "false", state_name(current.state), exit_name(current.exit_reason),
               current.run_id, hk_test_vm_cleanup_calls(), hk_test_vm_prepared_runs(), current.output_pending,
               current.deadline_us, hk_test_vm_executor_acknowledged() ? "true" : "false");
    }
    return 0;
}
