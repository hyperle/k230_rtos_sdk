"""Compile the production VM cleanup guard; check cleanup before VM destruction."""
from pathlib import Path
import os
import subprocess
import tempfile

CORE = Path(__file__).resolve().parents[2] / 'port/core'
SOURCE = Path(os.environ.get('SENSOR_MAIN_SOURCE', str(CORE / 'main.c')))
source = SOURCE.read_text()
start = source.index('static bool stop_sensors_before_vm_reset(void)')
brace = source.index('{', start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
function = source[start:end]
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
typedef uintptr_t mp_obj_t;
typedef struct { int unused; } mp_map_t;
typedef struct { mp_obj_t value; } mp_map_elem_t;
typedef struct { mp_map_t map; } module_dict_t;
static module_dict_t mp_loaded_modules_dict;
#define MP_STATE_VM(field) field
#define MP_OBJ_NEW_QSTR(value) ((mp_obj_t)(value))
#define MP_MAP_LOOKUP 0
#define MP_QSTR_Sensor 1
#define MP_QSTR_deinit 2
static const int mp_plat_print = 0;
typedef struct { void *ret_val; } nlr_buf_t;
static jmp_buf jump_buffer;
#define nlr_push(nlr) (((nlr)->ret_val = (void *)19), setjmp(jump_buffer))
static void nlr_pop(void) {}
static int loaded, cleanup_error, attribute_error, cleanup_calls, exceptions;
static int qstr_from_str(const char *name) {
    assert(!strcmp(name, "media.sensor"));
    return 3;
}
static mp_map_elem_t *mp_map_lookup(mp_map_t *map, mp_obj_t key, int kind) {
    static mp_map_elem_t result = { 7 };
    assert(map == &mp_loaded_modules_dict.map && key == 3 && kind == MP_MAP_LOOKUP);
    return loaded ? &result : 0;
}
static mp_obj_t mp_load_attr(mp_obj_t object, int attr) {
    if (attribute_error) longjmp(jump_buffer, 1);
    assert((object == 7 && attr == MP_QSTR_Sensor) || (object == 8 && attr == MP_QSTR_deinit));
    return object + 1;
}
static void mp_call_function_0(mp_obj_t object) {
    assert(object == 9); ++cleanup_calls;
    if (cleanup_error) longjmp(jump_buffer, 1);
}
static void mp_obj_print_exception(const int *print, mp_obj_t exception) {
    assert(print == &mp_plat_print && exception == 19); ++exceptions;
}
'''
checks = r'''
int main(void) {
    assert(stop_sensors_before_vm_reset());
    assert(!cleanup_calls && !exceptions);
    loaded = 1;
    assert(stop_sensors_before_vm_reset());
    assert(cleanup_calls == 1 && !exceptions);
    cleanup_error = 1;
    assert(!stop_sensors_before_vm_reset());
    assert(cleanup_calls == 2 && exceptions == 1);
    cleanup_error = 0;
    assert(stop_sensors_before_vm_reset());
    assert(cleanup_calls == 3 && exceptions == 1);
    attribute_error = 1;
    assert(!stop_sensors_before_vm_reset());
    assert(cleanup_calls == 3 && exceptions == 2);
    return 0;
}
'''
# Check the actual main() ordering and failure branch: live VM, stopped workers,
# GIL reacquired, no pool/GC/VM destruction before the guard has succeeded.
shutdown = source.index('mp_thread_shutdown_workers();', source.index('main_thread_exit:'))
guard = source.index('if (!stop_sensors_before_vm_reset())', shutdown)
assert shutdown < guard < source.index('py_media_vbmgmt_deinit_pre();', guard)
assert guard < source.index('gc_sweep_all();', guard) < source.index('mp_deinit();', guard)
if not os.environ.get('SENSOR_GIL_RUNTIME_ONLY'):
    assert 'MP_THREAD_GIL_ENTER();' not in source[shutdown:guard]
failure_end = source.index('\n    lv_deinit();', guard)
failure_branch = source[guard:failure_end]
assert 'goto service_repl;' in failure_branch
assert 'mp_thread_set_main_ready(true);' in failure_branch
assert 'system_set_exiting_flag(false);' in failure_branch
assert 'gc_sweep_all' not in failure_branch and 'mp_deinit' not in failure_branch
with tempfile.TemporaryDirectory(prefix='sensor-reset-guard-') as directory:
    cfile, binary = Path(directory) / 'test.c', Path(directory) / 'test'
    cfile.write_text(preamble + function + '\n' + checks)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
# Compile the actual worker shutdown function together with the actual main.c
# cleanup fragment. Model a nonrecursive GIL, not an assumed ownership state.
thread_source = (CORE / 'mpthreadport.c').read_text()
thread_start = thread_source.index('void mp_thread_shutdown_workers(void)')
thread_brace = thread_source.index('{', thread_start)
depth, thread_end = 1, thread_brace + 1
while depth:
    depth += (thread_source[thread_end] == '{') - (thread_source[thread_end] == '}')
    thread_end += 1
worker_function = thread_source[thread_start:thread_end]
fragment_start = source.rfind('    #if MICROPY_PY_THREAD', source.index('main_thread_exit:'), shutdown)
fragment = source[fragment_start:failure_end]
thread_model = """
#include <errno.h>
#define MICROPY_PY_THREAD 1
#define MICROPY_PY_BLUETOOTH 0
#define CONFIG_ENABLE_NETWORK_RT_WLAN 0
typedef int pthread_t;
typedef struct entry { pthread_t id; struct entry *next; } mp_thread_entry_t;
static mp_thread_entry_t main_entry = { 1, 0 }, worker_entry = { 2, 0 };
static mp_thread_entry_t *thread_list = &main_entry;
static bool deinit_waiting;
static int thread_finished_sem, gil_held, worker_waits, ready, deferred;
static pthread_t pthread_self(void) { return 1; }
static bool thread_id_equal(pthread_t a, pthread_t b) { return a == b; }
static void thread_list_lock(void) {}
static void thread_list_unlock(void) {}
static int sem_wait(int *semaphore) {
    assert(semaphore == &thread_finished_sem);
    main_entry.next = 0; ++worker_waits; return 0;
}
static void thread_fatal_error(const char *what, int error) {
    (void)what; (void)error; assert(0);
}
static void enter_gil(void) { assert(!gil_held); gil_held = 1; }
#define MP_THREAD_GIL_ENTER() enter_gil()
#define MP_THREAD_GIL_EXIT() do { assert(gil_held); gil_held = 0; } while (0)
static void RtspServer_DeInitAll(void) {}
static void mp_hal_stdout_tx_str(const char *text) { assert(text); ++deferred; }
static void system_set_exiting_flag(bool exiting) { assert(!exiting); }
static void repl_interrupt_store(bool interrupt) { assert(!interrupt); }
static void ide_dbg_clear_soft_reset_request(void) {}
static void mp_thread_set_main_ready(bool value) { ready = value; }
"""
thread_checks = """
static int real_cleanup_tail(void) {
""" + fragment + """
    return 1;
service_repl:
    return 0;
}
int main(void) {
    for (int with_worker = 0; with_worker < 2; ++with_worker) {
        for (int fail = 0; fail < 2; ++fail) {
            main_entry.next = with_worker ? &worker_entry : 0;
            deinit_waiting = false;
            gil_held = worker_waits = ready = deferred = 0;
            loaded = 1; cleanup_error = fail; attribute_error = 0;
            assert(real_cleanup_tail() == !fail);
            assert(gil_held == 1); /* Both normal teardown and retained VM own it. */
            assert(deinit_waiting && worker_waits == with_worker);
            assert(deferred == fail && ready == fail);
            MP_THREAD_GIL_EXIT();
        }
    }
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='sensor-reset-real-gil-') as directory:
    cfile, binary = Path(directory) / 'test.c', Path(directory) / 'test'
    cfile.write_text(preamble + function + '\n' + thread_model + worker_function + thread_checks)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Production VM cleanup guard: 5 cases; real worker/GIL handoff: 4 cases PASS')
