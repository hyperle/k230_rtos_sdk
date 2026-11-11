/* Host regression: exercise the actual HAL with a recording ioctl backend. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#define open audit_open
#define close audit_close
#define ioctl audit_ioctl
#include "../drivers/i2c/drv_i2c_master.c"
#undef open
#undef close
#undef ioctl
static unsigned clock_calls, timeout_calls, close_calls;
static unsigned long fail_command;
int audit_open(const char *name, int flags, ...) { (void)name; (void)flags; return 5; }
int audit_close(int fd) { (void)fd; close_calls++; return 0; }
int audit_ioctl(int fd, unsigned long command, ...) {
    (void)fd;
    if(command==RT_I2C_DEV_CTRL_CLK) clock_calls++;
    if(command==RT_I2C_DEV_CTRL_TIMEOUT) timeout_calls++;
    return command==fail_command ? -1 : 0;
}
int main(void) {
    drv_i2c_inst_t *inst=NULL;
    assert(!drv_i2c_inst_create(1,100000,50,34,35,&inst));
    assert(clock_calls==1 && timeout_calls==1);
    assert(inst->freq==100000 && inst->timeout_ms==50);
    assert(!drv_i2c_set_freq(inst,100000) && !drv_i2c_set_timeout(inst,50));
    assert(clock_calls==1 && timeout_calls==1);
    assert(!drv_i2c_set_freq(inst,400000) && inst->freq==400000);
    fail_command=RT_I2C_DEV_CTRL_CLK;
    assert(drv_i2c_set_freq(inst,100000)==-1 && inst->freq==400000);
    drv_i2c_inst_destroy(&inst);
    assert(inst==NULL && close_calls==1 && !hard_i2c_in_use[1]);
    assert(drv_i2c_inst_create(1,100000,50,34,35,&inst)==-1);
    assert(inst==NULL && close_calls==2 && !hard_i2c_in_use[1]);
    fail_command=RT_I2C_DEV_CTRL_TIMEOUT;
    assert(drv_i2c_inst_create(1,100000,50,34,35,&inst)==-1);
    assert(inst==NULL && close_calls==3 && !hard_i2c_in_use[1]);
    fail_command=0;
    assert(!drv_i2c_inst_create(1,100000,50,34,35,&inst));
    assert(inst && hard_i2c_in_use[1]);
    drv_i2c_inst_destroy(&inst);
    assert(inst==NULL && close_calls==4 && !hard_i2c_in_use[1]);
    puts("PASS: first-create clock/timeout, cached setters, failed-change cache, failed-create cleanup, retry");
}
