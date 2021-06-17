// SPDX-License-Identifier: GPL-2.0
/*
 * IOMMU API for ARM architected SMMUv3 implementations.
 *
 * Copyright (C) 2015 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This driver is powered by bad coffee and bombay mix.
 */

#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/bitops.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/dma-iommu.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io-pgtable.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/platform_device.h>

#include <linux/amba/bus.h>

#include "arm-smmu-v3.h"

#define sfx
u32 cmpwait_special(volatile u32 *ptr,
				       u32 ticket_prod)
{
	u32 tmp;

	asm volatile(
	"	sevl\n"
	"	wfe\n"
	"1:"
	"	mov x4,#0x7FFFFFFF\n"
	"	dmb ish\n"
	"	ldxr"  "\t%"  "[tmp], %[v]\n"
	"	and x4,x4,%" "[tmp]\n"
	"	eor	x4, x4, %" "[ticket_prod]\n"
	"	cbz	x4, 2f\n"
	"	wfe\n"
	"	b	1b\n"
	"2:"
	: [tmp] "=&r" (tmp), [v] "+Q" (*(unsigned long *)ptr)
	: [ticket_prod] "r" (ticket_prod));
//	pr_err_once("%s ticket_prod=0x%x tmp=0x%x\n", __func__, ticket_prod, tmp);
	return tmp;
}

