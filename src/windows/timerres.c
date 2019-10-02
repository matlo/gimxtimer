/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "timerres.h"
#include <windows.h>
#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>
#include <gimxtime/include/gtime.h>
#include <gimxcommon/include/gperf.h>

GLOG_GET(GLOG_NAME)

static void (__stdcall *pNtQueryTimerResolution)(PULONG, PULONG, PULONG) = NULL;
static void (__stdcall *pNtSetTimerResolution)(ULONG, BOOL, PULONG) = NULL;

static int nb_users = 0;
static HANDLE hTimer = INVALID_HANDLE_VALUE;
static GPOLL_REGISTER_SOURCE fp_register = NULL;
static GPOLL_REMOVE_SOURCE fp_remove = NULL;
static int (*timer_callback)(unsigned int) = NULL;
static ULONG minimumResolution = 0;
static ULONG currentResolution = 0;
static gtime resolution = 0;
static gtime last = 0;
static LARGE_INTEGER nextTick = { .QuadPart = -1 };

#define MAX_SAMPLES 10000

static struct {
    unsigned int count;
    unsigned int missed;
    unsigned int slices[10];
    DWORD nbcores;
    unsigned int * cores;
} debug = {
};

void timerres_init(void) __attribute__((constructor));
void timerres_init(void) {

    HMODULE hNtdll = GetModuleHandle("ntdll.dll");
    if (hNtdll == INVALID_HANDLE_VALUE) {
        PRINT_ERROR_GETLASTERROR("GetModuleHandle ntdll.dll");
        exit(-1);
    }
    pNtQueryTimerResolution = (void (__stdcall *)(PULONG, PULONG, PULONG))(void (*)(void)) GetProcAddress(hNtdll,
            "NtQueryTimerResolution");
    if (pNtQueryTimerResolution == NULL) {
        PRINT_ERROR_GETLASTERROR("GetProcAddress NtQueryTimerResolution");
        exit(-1);
    }
    pNtSetTimerResolution = (void (__stdcall *)(ULONG, BOOL, PULONG))(void (*)(void)) GetProcAddress(hNtdll, "NtSetTimerResolution");
    if (pNtSetTimerResolution == NULL) {
        PRINT_ERROR_GETLASTERROR("GetProcAddress NtSetTimerResolution");
        exit(-1);
    }

    hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
    if (hTimer == INVALID_HANDLE_VALUE) {
        PRINT_ERROR_GETLASTERROR("CreateWaitableTimer");
        exit(-1);
    }

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

#define SAMPLETYPE \
    struct { \
            gtime now; \
            unsigned int nexp; \
            gtime delta; \
        }

#define NBSAMPLES 20000 // 10s for a resolution of 0.5ms

#define SAMPLEPRINT(SAMPLE) \
    printf("now = "GTIME_FS" nexp = %u delta = "GTIME_FS"\n", SAMPLE.now, SAMPLE.nexp, SAMPLE.delta)

static GPERF_INST(timerres, SAMPLETYPE, NBSAMPLES);

static int read_callback(void * user __attribute__((unused))) {

    gtime now = gtime_gettime();

    if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {
        GPERF_TICK(timerres, now);
    }

    // reset the timer before doing anything else
    BOOL result = SetWaitableTimer(hTimer, &nextTick, 0, NULL, NULL, FALSE);
    DWORD error = GetLastError();

    gtime delta = now - last;
    unsigned int nexp = (delta + resolution / 2) / resolution; // delta can be lower than resolution

    int ret = 0;

    if (nexp > 0) {

        last = now;

        int lret = timer_callback(nexp);
        if (lret < 0) {
            ret = -1;
        } else if (ret != -1 && lret) {
            ret = 1;
        }

        if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {

            ++debug.count;

            debug.cores[GetCurrentProcessorNumber()] += 1;

            if (nexp > 1) {
                unsigned int slice = sizeof(debug.slices) / sizeof(*debug.slices) - 1;
                if (nexp - 2 < slice) {
                    slice = nexp - 2;
                }
                debug.slices[slice] += 1;
                debug.missed += (nexp - 1);
            }
        }
    }

    if (GLOG_LEVEL(GLOG_NAME,TRACE)) {
        GPERF_SAMPLE(timerres).now = now;
        GPERF_SAMPLE(timerres).nexp = nexp;
        GPERF_SAMPLE(timerres).delta = delta;
        GPERF_SAMPLE_INC(timerres);
    }

    if (!result) {
        SetLastError(error);
        PRINT_ERROR_GETLASTERROR("SetWaitableTimer");
        return -1;
    }

    return ret;
}

static int start_timer() {

    if (!SetWaitableTimer(hTimer, &nextTick, 0, NULL, NULL, FALSE)) {
        PRINT_ERROR_GETLASTERROR("SetWaitableTimer");
        return -1;
    }

    DWORD lresult = WaitForSingleObject(hTimer, INFINITE);
    if (lresult == WAIT_FAILED) {
        PRINT_ERROR_GETLASTERROR("WaitForSingleObject");
        return -1;
    }

    last = gtime_gettime();

    if (!SetWaitableTimer(hTimer, &nextTick, 0, NULL, NULL, FALSE)) {
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
            ULONG maximumResolution;
            ULONG previousResolution;
            pNtQueryTimerResolution(&minimumResolution, &maximumResolution, &previousResolution);
            pNtSetTimerResolution(minimumResolution, TRUE, &currentResolution);
            if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {
                printf("Timer resolution: previous=%lu current=%lu\n", previousResolution, currentResolution);
            }
            fp_remove(hTimer);
            if (GLOG_LEVEL(GLOG_NAME,DEBUG) && debug.count) {
                printf("timer count per core: ");
                unsigned int i;
                for (i = 0; i < debug.nbcores; ++i) {
                    printf(" %u", debug.cores[i]);
                }
                printf("\n");
                printf("base timer: count = %u, missed = %u (%.02f%%)\n", debug.count, debug.missed, (double)debug.missed * 100 / (debug.count + debug.missed));
                printf("missed slices: ");
                for (i = 0; i < sizeof(debug.slices) / sizeof(*debug.slices) - 1; ++i) {
                    printf(" [%u] %u", i + 1, debug.slices[i]);
                }
                printf(" [%u+] %u\n", i + 1, debug.slices[i]);
                if (GLOG_LEVEL(GLOG_NAME,TRACE)) {
                    GPERF_SAMPLE_PRINT(timerres, SAMPLEPRINT);
                }
                GPERF_LOG(timerres);
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

        ULONG maximumResolution;
        ULONG previousResolution;
        pNtQueryTimerResolution(&minimumResolution, &maximumResolution, &previousResolution);

        pNtSetTimerResolution(maximumResolution, TRUE, &currentResolution);

        resolution = currentResolution * 100ULL;

        if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {
            printf("Timer resolution: min=%lu max=%lu previous=%lu current=%lu\n", minimumResolution, maximumResolution, previousResolution, currentResolution);
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
