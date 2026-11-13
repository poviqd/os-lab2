#ifndef PROCLAUNCH_H
#define PROCLAUNCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
    #endif

    typedef struct {
        intptr_t handle;
        long     pid;
    } pl_process;

    int pl_start(const char *const argv[], pl_process *proc);

    int pl_wait(pl_process *proc, int *exit_code);

    int pl_poll(pl_process *proc, int *exit_code);

    #ifdef __cplusplus
}
#endif

#endif
