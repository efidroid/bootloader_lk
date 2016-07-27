/* Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Fundation, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <reg.h>
#include <bits.h>
#include <arch/arm.h>
#include <kernel/thread.h>
#include <platform/irqs.h>
#include <platform/iomap.h>
#include <qgic.h>
#include <debug.h>

static struct ihandler handler[NR_IRQS];

/* Intialize distributor */
void qgic_dist_config(uint32_t num_irq)
{
	uint32_t i;

	/* Set each interrupt line to use N-N software model
	 * and edge sensitive, active high
	 */
	for (i = 32; i < num_irq; i += 16)
		writel(0xffffffff, GIC_DIST_CONFIG + i * 4 / 16);

	writel(0xffffffff, GIC_DIST_CONFIG + 4);

	/* Set priority of all interrupts */

	/*
	 * In bootloader we dont care about priority so
	 * setting up equal priorities for all
	 */
	for (i = 0; i < num_irq; i += 4)
		writel(0xa0a0a0a0, GIC_DIST_PRI + i * 4 / 4);

	/* Disabling interrupts */
	for (i = 0; i < num_irq; i += 32)
		writel(0xffffffff, GIC_DIST_ENABLE_CLEAR + i * 4 / 32);

	writel(0x0000ffff, GIC_DIST_ENABLE_SET);
}

/* Initialize QGIC. Called from platform specific init code */
void qgic_init(void)
{
#ifdef WITH_KERNEL_UEFIAPI
	return;
#endif

	qgic_dist_init();
	qgic_cpu_init();
}

/* IRQ handler */
enum handler_return gic_platform_irq(struct arm_iframe *frame)
{
	uint32_t num;
	enum handler_return ret;

	/* Read the interrupt number to be served*/
	num = qgic_read_iar();

	if (num >= NR_IRQS)
		return 0;

	ret = handler[num].func(handler[num].arg);

	/* End of interrupt */
	qgic_write_eoi(num);

	return ret;
}

/* FIQ handler */
void gic_platform_fiq(struct arm_iframe *frame)
{
	PANIC_UNIMPLEMENTED;
}

/* Mask interrupt */
status_t gic_mask_interrupt(unsigned int vector)
{
	uint32_t reg = GIC_DIST_ENABLE_CLEAR + (vector / 32) * 4;
	uint32_t bit = 1 << (vector & 31);

	writel(bit, reg);

	return 0;
}

/* Un-mask interrupt */
status_t gic_unmask_interrupt(unsigned int vector)
{
	uint32_t reg = GIC_DIST_ENABLE_SET + (vector / 32) * 4;
	uint32_t bit = 1 << (vector & 31);

	writel(bit, reg);

	return 0;
}

/* Register interrupt handler */
void gic_register_int_handler(unsigned int vector, int_handler func, void *arg)
{
	ASSERT(vector < NR_IRQS);

	enter_critical_section();
	handler[vector].func = func;
	handler[vector].arg = arg;
	exit_critical_section();
}

void qgic_change_interrupt_cfg(uint32_t spi_number, uint8_t type)
{
	uint32_t register_number, register_address, bit_number, value;
	register_number = spi_number >> 4; // r = n DIV 16
	bit_number = (spi_number % 16) << 1; // b = (n MOD 16) * 2
	value = readl(GIC_DIST_CONFIG + (register_number << 2));
	// there are two bits per register to indicate the level
	if (type == INTERRUPT_LVL_N_TO_N)
		value &= ~(BIT(bit_number)|BIT(bit_number+1)); // 0x0 0x0
	else if (type == INTERRUPT_LVL_1_TO_N)
		value = (value & ~BIT(bit_number+1)) | BIT(bit_number); // 0x0 0x1
	else if (type == INTERRUPT_EDGE_N_TO_N)
		value =  BIT(bit_number+1) | (value & ~BIT(bit_number));// 0x1 0x0
	else if (type == INTERRUPT_EDGE_1_TO_N)
		value |= (BIT(bit_number)|BIT(bit_number+1)); // 0x1 0x1
	else
		dprintf(CRITICAL, "Invalid interrupt type change requested\n");
	register_address = GIC_DIST_CONFIG + (register_number << 2);
	writel(value, register_address);
}
