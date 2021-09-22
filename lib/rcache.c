

#include <linux/iova.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/rcache.h>


struct magazine *magazine_alloc(gfp_t flags, struct rcache *rcache)
{
	int i, j;

	struct magazine *mag = kzalloc(sizeof(struct magazine) + (rcache->size * MAG_SIZE), flags);
	if (!mag)
		return NULL;
	for (i = 0; i < MAG_SIZE && rcache->size; i++) {
		mag->ptr[i] = kmem_cache_alloc(rcache->kmem_cache, flags);
		if (!mag->ptr[i])
			goto err_out;

	}

	return mag;

err_out:
	for (j = 0; j < i; j++)
		kmem_cache_free(rcache->kmem_cache, mag->ptr[j]);
	kfree(mag);
	return NULL;
}

void magazine_free(struct magazine *mag, struct rcache *rcache)
{
	int i;

	for (i = 0; i < MAG_SIZE && rcache->size; i++)
		kmem_cache_free(rcache->kmem_cache, mag->ptr[i]);

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

int rcache_init(struct rcache *rcache, unsigned int size)
{
	struct cpu_rcache *cpu_rcache;
	unsigned int cpu;

	spin_lock_init(&rcache->lock);
	rcache->depot_size = 0;
	rcache->size = size;
	rcache->cpu_rcaches = __alloc_percpu(sizeof(*cpu_rcache), cache_line_size());
	if (WARN_ON(!rcache->cpu_rcaches))
		return -ENOMEM;
	for_each_possible_cpu(cpu) {
		cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
		spin_lock_init(&cpu_rcache->lock);
		cpu_rcache->loaded = magazine_alloc(GFP_KERNEL, rcache);
		WARN_ON(!cpu_rcache->loaded);
		cpu_rcache->prev = magazine_alloc(GFP_KERNEL, rcache);
		WARN_ON(!cpu_rcache->prev);
	}
	if (size) {
		rcache->kmem_cache = kmem_cache_create("rcache",
					size,
					0, 0,
					NULL);
		if (WARN_ON(!rcache->kmem_cache))
			return -ENOMEM;
	}

	return 0;
}


