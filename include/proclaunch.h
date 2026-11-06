#ifndef PROCLAUNCH_H
#define PROCLAUNCH_H
 
#include <stdint.h>
 
#ifdef __cplusplus
extern "C" {
#endif
 
typedef struct {
    intptr_t handle;   /* Windows: HANDLE процесса */
    long     pid;      /* PID процесса */
} pl_process;
 
/* Запустить в фоне. argv завершается NULL. 0 - успех, -1 - ошибка. */
int pl_start(const char *const argv[], pl_process *proc);
 
/* Дождаться завершения, код - в *exit_code. 0 - успех, -1 - ошибка. */
int pl_wait(pl_process *proc, int *exit_code);
 
/* Без ожидания: 1 - завершился, 0 - работает, -1 - ошибка. */
int pl_poll(pl_process *proc, int *exit_code);
 
#ifdef __cplusplus
}
#endif
#endif
