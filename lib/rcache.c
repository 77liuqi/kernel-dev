

#include <linux/iova.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/rcache.h>


struct magazine *magazine_alloc(gfp_t flags, unsigned long size)
{
	BUG_ON(size > 0);
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

int rcache_init(struct rcache *rcache)
{
	struct cpu_rcache *cpu_rcache;
	unsigned int cpu;

	spin_lock_init(&rcache->lock);
	rcache->depot_size = 0;
	rcache->cpu_rcaches = __alloc_percpu(sizeof(*cpu_rcache), cache_line_size());
	if (WARN_ON(!rcache->cpu_rcaches))
		return -ENOMEM;
	for_each_possible_cpu(cpu) {
		cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
		spin_lock_init(&cpu_rcache->lock);
		cpu_rcache->loaded = magazine_alloc(GFP_KERNEL, 0);
		cpu_rcache->prev = magazine_alloc(GFP_KERNEL, 0);
	}

	return 0;
}


