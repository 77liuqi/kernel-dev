

#include <linux/iova.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/rcache.h>


struct magazine *magazine_alloc(gfp_t flags)
{
	return kzalloc(sizeof(struct magazine), flags);
}

void magazine_free(struct magazine *mag)
{
	kfree(mag);
}

bool magazine_full(struct magazine *mag)
{
	return (mag && mag->size == MAG_SIZE);
}

bool magazine_empty(struct magazine *mag)
{
	return (!mag || mag->size == 0);
}

