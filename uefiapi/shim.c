#include <err.h>
#include <assert.h>
#include <uefiapi.h>
#include <platform/interrupts.h>

void qgic_init(void)
{
}

void vib_timed_turn_on(const uint32_t vibrate_time)
{
}

void wait_vib_timeout(void)
{
}

void display_image_on_screen(void)
{
}

status_t mask_interrupt(unsigned int vector)
{
	ASSERT(uefiapi.int_mask);
	return uefiapi.int_mask(vector);
}

status_t unmask_interrupt(unsigned int vector)
{
	ASSERT(uefiapi.int_unmask);
	return uefiapi.int_unmask(vector);
}

void register_int_handler(unsigned int vector, int_handler func, void *arg)
{
	ASSERT(uefiapi.int_register_handler);
	uefiapi.int_register_handler(vector, (lkapi_int_handler)func, arg);
}
