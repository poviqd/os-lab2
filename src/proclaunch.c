#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#else

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#endif

typedef struct {
    #ifdef _WIN32
    HANDLE handle;
    DWORD pid;
    #else
    pid_t pid;
    #endif
    int done;
    int exit_code;
} pl_proc;

int pl_start(pl_proc *proc, char *const argv[]);

int pl_wait(pl_proc *proc, int *code);
int pl_poll(pl_proc *proc, int *code);


#ifdef _WIN32

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} pl_buf;

static int pl_buf_reserve(pl_buf *b, size_t extra)
{
    if (extra > SIZE_MAX - b->len - 1)
        return -1;

    size_t needed = b->len + extra + 1;
    if (needed <= b->cap)
        return 0;

    size_t cap = b->cap ? b->cap : 64;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) {
            cap = needed;
            break;
        }
        cap *= 2;
    }

    char *data = (char *)realloc(b->data, cap);
    if (!data)
        return -1;

    b->data = data;
    b->cap = cap;
    return 0;
}

static int pl_buf_append_n(pl_buf *b, char c, size_t count)
{
    if (pl_buf_reserve(b, count) < 0)
        return -1;

    memset(b->data + b->len, c, count);
    b->len += count;
    b->data[b->len] = '\0';
    return 0;
}

static int pl_buf_append(pl_buf *b, char c)
{
    return pl_buf_append_n(b, c, 1);
}

static int pl_append_quoted_arg(pl_buf *b, const char *arg)
{
    if (pl_buf_append(b, '"') < 0)
        return -1;

    const char *p = arg;

    while (*p) {
        size_t slashes = 0;
        while (p[slashes] == '\\')
            ++slashes;
        p += slashes;

        if (*p == '"') {
            if (slashes > (SIZE_MAX - 1) / 2 ||
                pl_buf_append_n(b, '\\', slashes * 2 + 1) < 0 ||
                pl_buf_append(b, '"') < 0)
                return -1;
            ++p;
        } else if (*p == '\0') {
            if (slashes > SIZE_MAX / 2 ||
                pl_buf_append_n(b, '\\', slashes * 2) < 0)
                return -1;
        } else {
            if (pl_buf_append_n(b, '\\', slashes) < 0 ||
                pl_buf_append(b, *p) < 0)
                return -1;
            ++p;
        }
    }

    return pl_buf_append(b, '"');
}

static char *pl_make_cmdline(char *const argv[])
{
    pl_buf b = {0};

    for (size_t i = 0; argv[i]; ++i) {
        if ((i && pl_buf_append(&b, ' ') < 0) ||
            pl_append_quoted_arg(&b, argv[i]) < 0) {
            free(b.data);
        return NULL;
            }
    }

    return b.data;
}

int pl_start(pl_proc *proc, char *const argv[])
{
    if (!proc || !argv || !argv[0])
        return -1;

    char *cmd = pl_make_cmdline(argv);
    if (!cmd)
        return -1;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    ZeroMemory(&pi, sizeof pi);
    si.cb = sizeof si;

    BOOL ok = CreateProcessA(
        NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
    );
    free(cmd);

    if (!ok)
        return -1;

    CloseHandle(pi.hThread);

    proc->handle = pi.hProcess;
    proc->pid = pi.dwProcessId;
    proc->done = 0;
    proc->exit_code = 0;
    return 0;
}

static int pl_wait_impl(pl_proc *proc, int nonblock, int *code)
{
    if (!proc)
        return -1;

    if (proc->done) {
        if (code)
            *code = proc->exit_code;
        return 1;
    }

    DWORD result = WaitForSingleObject(
        proc->handle, nonblock ? 0 : INFINITE
    );

    if (result == WAIT_TIMEOUT)
        return 0;
    if (result != WAIT_OBJECT_0)
        return -1;

    DWORD exit_code;
    if (!GetExitCodeProcess(proc->handle, &exit_code))
        return -1;

    CloseHandle(proc->handle);
    proc->handle = NULL;
    proc->exit_code = (int)exit_code;
    proc->done = 1;

    if (code)
        *code = proc->exit_code;
    return 1;
}


#else

int pl_start(pl_proc *proc, char *const argv[])
{
    if (!proc || !argv || !argv[0])
        return -1;

    size_t argc = 0;
    while (argv[argc])
        ++argc;

    char **args = (char **)malloc((argc + 1) * sizeof *args);
    if (!args)
        return -1;

    for (size_t i = 0; i < argc; ++i)
        args[i] = argv[i];
    args[argc] = NULL;

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        free(args);
        return -1;
    }

    int flags = fcntl(pipefd[1], F_GETFD);
    if (flags < 0 || fcntl(pipefd[1], F_SETFD, flags | FD_CLOEXEC) < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        free(args);
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        free(args);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        execvp(args[0], args);

        int child_errno = errno;
        const char *p = (const char *)&child_errno;
        size_t left = sizeof child_errno;

        while (left > 0) {
            ssize_t n = write(pipefd[1], p, left);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            p += n;
            left -= (size_t)n;
        }

        _exit(127);
    }

    free(args);
    close(pipefd[1]);

    int child_errno = 0;
    char *p = (char *)&child_errno;
    size_t received = 0;
    int read_error = 0;

    while (received < sizeof child_errno) {
        ssize_t n = read(pipefd[0], p + received,
                         sizeof child_errno - received);

        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            read_error = errno;
            break;
        }
        if (n == 0)
            break;

        received += (size_t)n;
    }

    close(pipefd[0]);

    if (read_error || received != 0) {
        (void)kill(pid, SIGKILL);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        errno = read_error ? read_error :
        (received == sizeof child_errno ? child_errno : EIO);
        return -1;
    }

    proc->pid = pid;
    proc->done = 0;
    proc->exit_code = 0;
    return 0;
}

static int pl_wait_impl(pl_proc *proc, int nonblock, int *code)
{
    if (!proc)
        return -1;

    if (proc->done) {
        if (code)
            *code = proc->exit_code;
        return 1;
    }

    int status;
    pid_t result;

    do {
        result = waitpid(proc->pid, &status, nonblock ? WNOHANG : 0);
    } while (result < 0 && errno == EINTR);

    if (result < 0)
        return -1;
    if (result == 0)
        return 0;

    if (WIFEXITED(status))
        proc->exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        proc->exit_code = 128 + WTERMSIG(status);
    else
        return -1;

    proc->done = 1;

    if (code)
        *code = proc->exit_code;
    return 1;
}

#endif

int pl_wait(pl_proc *proc, int *code)
{
    return pl_wait_impl(proc, 0, code);
}

int pl_poll(pl_proc *proc, int *code)
{
    return pl_wait_impl(proc, 1, code);
}

static void sleep_ms(unsigned ms)
{
    #ifdef _WIN32
    Sleep((DWORD)ms);
    #else
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000);
    req.tv_nsec = (long)(ms % 1000) * 1000000L;

    while (nanosleep(&req, &req) < 0 && errno == EINTR) {
    }
    #endif
}

static void report_test(const char *name, int passed)
{
    printf("%s %s\n", passed ? "[ OK ]" : "[FAIL]", name);
}

int main(int argc, char *argv[])
{
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    if (argc >= 4 && strcmp(argv[1], "--child") == 0) {
        sleep_ms((unsigned)atoi(argv[3]));

        if (argc == 5 &&
            strcmp(argv[4], "hello \"big\" world") != 0) {
            return 99;
            }

            return atoi(argv[2]);
    }

    char *self = argv[0];
    int failures = 0;

    {
        char *child_argv[] = {
            self, "--child", "42", "1000", NULL
        };

        pl_proc proc = {0};
        int code = -1;
        int iterations = 0;
        int result = -1;

        if (pl_start(&proc, child_argv) == 0) {
            do {
                result = pl_poll(&proc, &code);
                if (result == 0) {
                    ++iterations;
                    sleep_ms(100);
                }
            } while (result == 0);
        }

        int passed = result == 1 && code == 42 && iterations > 3;
        report_test("pl_poll и код завершения 42", passed);
        if (!passed)
            ++failures;
    }

    {
        char *child_argv[] = {
            self, "--child", "7", "300", NULL
        };

        pl_proc proc = {0};
        int code = -1;
        int passed = pl_start(&proc, child_argv) == 0 &&
        pl_wait(&proc, &code) == 1 &&
        code == 7;

        report_test("pl_wait и код завершения 7", passed);
        if (!passed)
            ++failures;
    }

    {
        char *child_argv[] = {
            self, "--child", "0", "0", "hello \"big\" world", NULL
        };

        pl_proc proc = {0};
        int code = -1;
        int passed = pl_start(&proc, child_argv) == 0 &&
        pl_wait(&proc, &code) == 1 &&
        code == 0;

        report_test("аргумент с кавычками и пробелами", passed);
        if (!passed)
            ++failures;
    }

    {
        char *child_argv[] = {
            "no_such_program_12345", NULL
        };

        pl_proc proc = {0};
        int passed = pl_start(&proc, child_argv) == -1;

        if (!passed)
            (void)pl_wait(&proc, NULL);

        report_test("несуществующая программа", passed);
        if (!passed)
            ++failures;
    }

    {
        char *child_argv[] = {
            self, "--child", "300", "0", NULL
        };

        pl_proc proc = {0};
        int code = -1;

        #ifdef _WIN32
        const int expected = 300;
        #else
        const int expected = 44;
        #endif

        int passed = pl_start(&proc, child_argv) == 0 &&
        pl_wait(&proc, &code) == 1 &&
        code == expected;

        report_test("код завершения 300", passed);
        if (!passed)
            ++failures;
    }

    return failures == 0 ? 0 : 1;
}
