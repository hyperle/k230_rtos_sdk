#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#define open lifecycle_open
#define close lifecycle_close
#define ioctl lifecycle_ioctl
#include "../drivers/gpio/drv_gpio.c"
#undef open
#undef close
#undef ioctl
static fpioa_func_t current_func;
static int fail_func;
static unsigned mode_ioctls;
int lifecycle_open(const char *path, int flags, ...) {(void)path;(void)flags;return 5;}
int lifecycle_close(int fd) {(void)fd;return 0;}
int lifecycle_ioctl(int fd, unsigned long cmd, ...) {
    (void)fd;
    if(cmd==KD_GPIO_IOCTL_SET_MODE) {
        va_list ap;va_start(ap,cmd);gpio_cfg_t *cfg=va_arg(ap,gpio_cfg_t*);va_end(ap);
        assert(cfg->pin==34 && cfg->value==GPIO_DM_INPUT);
        ++mode_ioctls;current_func=GPIO34;
    }
    return 0;
}
int drv_fpioa_get_pin_func(int pin,fpioa_func_t *func) {
    assert(pin==34);if(fail_func)return -1;*func=current_func;return 0;
}
static void create_gpio(drv_gpio_inst_t **inst) {
    current_func=GPIO34;fail_func=0;assert(drv_gpio_inst_create(34,inst)==0);
    (*inst)->curr_mode=GPIO_DM_OUTPUT;
}
int main(void) {
    drv_gpio_inst_t *inst=NULL;
    create_gpio(&inst);
    current_func=IIC1_SCL;
    drv_gpio_inst_destroy(&inst);
    assert(!inst && current_func==IIC1_SCL && !mode_ioctls);
    create_gpio(&inst);
    drv_gpio_inst_destroy(&inst);
    assert(!inst && mode_ioctls==1);
    create_gpio(&inst);
    fail_func=1;
    drv_gpio_inst_destroy(&inst);
    assert(!inst && mode_ioctls==1);
    puts("PASS: old Pin finalizer preserves reclaimed I2C mux; GPIO cleanup retained; failed mux query does not remux");
}
