/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "timerres.h"
#include <windows.h>
#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>

GLOG_GET(GLOG_NAME)

static void (__stdcall *pNtQueryTimerResolution)(PULONG, PULONG, PULONG) = NULL;
static void (__stdcall *pNtSetTimerResolution)(ULONG, BOOL, PULONG) = NULL;

static int nb_users = 0;
static HANDLE hTimer = INVALID_HANDLE_VALUE;
static GPOLL_REGISTER_SOURCE fp_register = NULL;
static GPOLL_REMOVE_SOURCE fp_remove = NULL;
static int (*timer_callback)(unsigned int) = NULL;
static ULONG currentResolution = 0;
static LARGE_INTEGER last = {};
static LARGE_INTEGER next = {};

static LARGE_INTEGER freq = { 0 };

static struct {
    unsigned int count;
    unsigned int missed;
    unsigned int slices[10];
    DWORD nbcores;
    unsigned int * cores;
} debug = {
};

static inline LARGE_INTEGER timerres_get_time() {
    LARGE_INTEGER tnow;
    QueryPerformanceCounter(&tnow);
    tnow.QuadPart = tnow.QuadPart * 10000000ULL / freq.QuadPart;
    return tnow;
}

void timerres_init(void) __attribute__((constructor));
void timerres_init(void) {

    HMODULE hNtdll = GetModuleHandle("ntdll.dll");
    if (hNtdll == INVALID_HANDLE_VALUE) {
        PRINT_ERROR_GETLASTERROR("GetModuleHandle ntdll.dll");
        exit(-1);
    }
    pNtQueryTimerResolution = (void (__stdcall *)(PULONG, PULONG, PULONG)) GetProcAddress(hNtdll,
            "NtQueryTimerResolution");
    if (pNtQueryTimerResolution == NULL) {
        PRINT_ERROR_GETLASTERROR("GetProcAddress NtQueryTimerResolution");
        exit(-1);
    }
    pNtSetTimerResolution = (void (__stdcall *)(ULONG, BOOL, PULONG)) GetProcAddress(hNtdll, "NtSetTimerResolution");
    if (pNtSetTimerResolution == NULL) {
        PRINT_ERROR_GETLASTERROR("GetProcAddress NtSetTimerResolution");
        exit(-1);
    }

    hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
    if (hTimer == INVALID_HANDLE_VALUE) {
        PRINT_ERROR_GETLASTERROR("CreateWaitableTimer");
        exit(-1);
    }

    QueryPerformanceFrequency(&freq);

    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    debug.nbcores = sysinfo.dwNumberOfProcessors;
    debug.cores = calloc(debug.nbcores, sizeof(*debug.cores));
    if (debug.cores == NULL) {
        PRINT_ERROR_ALLOC_FAILED("calloc");
        exit(-1);
    }
}

void timerres_quit(void) __attribute__((destructor));
void timerres_quit(void) {

    free(debug.cores);

    CloseHandle(hTimer);
}

static int close_callback(void * user __attribute__((unused))) {

    return -1;
}

#define unlikely(x)    __builtin_expect(!!(x), 0)

// Warning: preemption may happen anytime, and timer_callback may take some time.
// Reset the timer until no period elapsed.
static int read_callback(void * user __attribute__((unused))) {

    ++debug.count;

    int ret = 0;

    LARGE_INTEGER now = timerres_get_time();
    LONGLONG delta = now.QuadPart - last.QuadPart;
    unsigned int nexp = delta / currentResolution;
    if (nexp > 0) {
        int lret = timer_callback(nexp);
        if (lret < 0) {
            ret = -1;
        } else if (ret != -1 && lret) {
            ret = 1;
        }
        last.QuadPart += nexp * currentResolution;
        next.QuadPart += nexp * currentResolution;
    }
    LARGE_INTEGER li = { .QuadPart = now.QuadPart - next.QuadPart };
    if (unlikely(!SetWaitableTimer(hTimer, &li, 0, NULL, NULL, FALSE))) {
        PRINT_ERROR_GETLASTERROR("SetWaitableTimer");
        return -1;
    }

    if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {

        debug.cores[GetCurrentProcessorNumber()] += 1;

        if (nexp > 1) {
            unsigned int slice = sizeof(debug.slices) / sizeof(*debug.slices) - 1;
            if (nexp - 2 < slice) {
                slice = nexp - 2;
            }
            debug.slices[slice] += 1;
            debug.missed += (nexp - 1);
        }

        if (GLOG_LEVEL(GLOG_NAME,TRACE)) {
            LARGE_INTEGER end = timerres_get_time();
            printf("--- delta = %I64d nexp = %u next = %I64d time = %I64d\n", delta, nexp, li.QuadPart, end.QuadPart - now.QuadPart);
        }
    }

    return ret;
}

static int start_timer() {

    LARGE_INTEGER li = { .QuadPart = -1 };
    if (!SetWaitableTimer(hTimer, &li, 0, NULL, NULL, FALSE)) {
        PRINT_ERROR_GETLASTERROR("SetWaitableTimer");
        return -1;
    }

    DWORD lresult = WaitForSingleObject(hTimer, 1);
    if (lresult == WAIT_FAILED) {
        PRINT_ERROR_GETLASTERROR("WaitForSingleObject");
        return -1;
    }

    last = timerres_get_time();

    next.QuadPart = last.QuadPart + currentResolution;

    li.QuadPart = -currentResolution;
    if (!SetWaitableTimer(hTimer, &li, 0, NULL, NULL, FALSE)) {
        PRINT_ERROR_GETLASTERROR("SetWaitableTimer");
        return -1;
    }

    GPOLL_CALLBACKS callbacks = {
      .fp_read = read_callback,
      .fp_write = NULL,
      .fp_close = close_callback,
    };
    int ret = fp_register(hTimer, 0, &callbacks);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

void timerres_end() {

    if (nb_users > 0) {
        --nb_users;
        if (nb_users == 0) {
            pNtSetTimerResolution(0, FALSE, &currentResolution);
            fp_remove(hTimer);
            if (GLOG_LEVEL(GLOG_NAME,DEBUG) && debug.count) {
                printf("base timer: count = %u, missed = %u (%.02f%%)\n", debug.count, debug.missed, (double)debug.missed * 100 / (debug.count + debug.missed));
                printf("timer count per core: ");
                unsigned int i;
                for (i = 0; i < debug.nbcores; ++i) {
                    printf(" %u", debug.cores[i]);
                }
                printf("\n");
                printf("missed slices: ");
                for (i = 0; i < sizeof(debug.slices) / sizeof(*debug.slices) - 1; ++i) {
                    printf(" [%u] %u", i + 1, debug.slices[i]);
                }
                printf(" [%u+] %u\n", i + 1, debug.slices[i]);
            }
        }
    }
}

#define XSTR(s) STR(s)
#define STR(s) #s

#define CHECK_FUNCTION(FUNCTION) \
    do { \
        if (FUNCTION == NULL) { \
            PRINT_ERROR_OTHER(XSTR(FUNCTION)" is NULL"); \
            return -1; \
        } \
    } while (0)

unsigned int timerres_begin(const GPOLL_INTERFACE * poll_interface, TIMERRES_CALLBACK timer_cb) {

    CHECK_FUNCTION (poll_interface->fp_register);
    CHECK_FUNCTION (poll_interface->fp_remove);
    CHECK_FUNCTION (timer_cb);

    // TODO MLA: warn if register / remove functions change
    // on Windows function pointers to gpoll_register/remove inside and outside the dll do not match.

    int ret = 0;

    ++nb_users;

    if (nb_users == 1) {

        ULONG minimumResolution, maximumResolution;
        pNtQueryTimerResolution(&minimumResolution, &maximumResolution, &currentResolution);

        pNtSetTimerResolution(maximumResolution, TRUE, &currentResolution);

        if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {
            printf("Timer resolution: min=%lu max=%lu current=%lu\n", minimumResolution, maximumResolution, currentResolution);
        }

        timer_callback = timer_cb;
        fp_register = poll_interface->fp_register;
        fp_remove = poll_interface->fp_remove;
        if (start_timer() < 0) {
            ret = -1;
        }
    }

    if (ret == -1) {
        timerres_end();
    }

    return currentResolution;
}
