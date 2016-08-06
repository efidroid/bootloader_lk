#include <err.h>
#include <assert.h>
#include <uefiapi.h>
#include <platform/interrupts.h>

//
// we don't want to initialize the GIC because UEFI has it's own driver
//
void qgic_init(void)
{
}


//
// the vibrator uses timers, so just don't activate it
//
void vib_timed_turn_on(const uint32_t vibrate_time)
{
}

void wait_vib_timeout(void)
{
}


//
// we don't want to show LK's logo in UEFI
//
void display_image_on_screen(void)
{
}


//
// forward interrupt API's to UEFI's driver
//
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
