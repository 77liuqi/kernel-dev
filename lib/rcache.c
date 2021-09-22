

#include <linux/iova.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/rcache.h>


struct magazine *magazine_alloc(gfp_t flags, struct rcache *rcache)
{
	
#ifdef removing

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
#else
	return kzalloc(sizeof(struct magazine), flags);

#endif

#ifdef removing

err_out:
	for (j = 0; j < i; j++)
		kmem_cache_free(rcache->kmem_cache, mag->ptr[j]);
	kfree(mag);
	return NULL;
#endif
}

void magazine_free(struct magazine *mag, struct rcache *rcache)
{
	#ifdef removing
	int i;
	pr_err("%s mag=%pS size=%lu rcache=%pS\n", __func__, mag, mag->size, rcache);
	for (i = 0; i < MAG_SIZE && rcache->size; i++)
		kmem_cache_free(rcache->kmem_cache, mag->ptr[i]);
	#endif

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
	#ifdef removing
	if (size) {
		rcache->kmem_cache = kmem_cache_create("rcache",
					size,
					0, 0,
					NULL);
		if (WARN_ON(!rcache->kmem_cache))
			return -ENOMEM;
	}
	#endif

	return 0;
}

static void magazine_push(struct magazine *mag, unsigned long pfn)
{
	BUG_ON(magazine_full(mag));

	mag->mem[mag->size++].val = pfn;
}

/*
 * Try inserting IOVA range starting with 'iova_pfn' into 'rcache', and
 * return true on success.  Can fail if rcache is full and we can't free
 * space, and free_iova() (our only caller) will then return the IOVA
 * range to the rbtree instead.
 */
bool rcache_insert(struct rcache *rcache,
				 unsigned long iova_pfn)
{
	struct magazine *mag_to_free = NULL;
	struct cpu_rcache *cpu_rcache;
	bool can_insert = false;
	unsigned long flags;

	cpu_rcache = raw_cpu_ptr(rcache->cpu_rcaches);
	spin_lock_irqsave(&cpu_rcache->lock, flags);

	if (!magazine_full(cpu_rcache->loaded)) {
		can_insert = true;
	} else if (!magazine_full(cpu_rcache->prev)) {
		swap(cpu_rcache->prev, cpu_rcache->loaded);
		can_insert = true;
	} else {
		struct magazine *new_mag = magazine_alloc(GFP_ATOMIC, rcache);

		if (new_mag) {
			spin_lock(&rcache->lock);
			if (rcache->depot_size < MAX_GLOBAL_MAGS) {
				rcache->depot[rcache->depot_size++] =
						cpu_rcache->loaded;
			} else {
				mag_to_free = cpu_rcache->loaded;
			}
			spin_unlock(&rcache->lock);

			cpu_rcache->loaded = new_mag;
			can_insert = true;
		}
	}

	if (can_insert)
		magazine_push(cpu_rcache->loaded, iova_pfn);

	spin_unlock_irqrestore(&cpu_rcache->lock, flags);

	if (mag_to_free) {
		// fixme
		//iova_magazine_free_pfns(mag_to_free, iovad);
		magazine_free(mag_to_free, rcache);
	}

	return can_insert;
}

static unsigned long magazine_pop(struct magazine *mag,
				       unsigned long limit_pfn)
{
	int i;
	unsigned long pfn;

	BUG_ON(magazine_empty(mag));

	/* Only fall back to the rbtree if we have no suitable pfns at all */
	for (i = mag->size - 1; mag->mem[i].val > limit_pfn; i--)
		if (i == 0)
			return 0;

	/* Swap it to pop it */
	pfn = mag->mem[i].val;
	mag->mem[i].val = mag->mem[--mag->size].val;

	return pfn;
}

/*
 * Caller wants to allocate a new IOVA range from 'rcache'.  If we can
 * satisfy the request, return a matching non-NULL range and remove
 * it from the 'rcache'.
 */
unsigned long rcache_get(struct rcache *rcache,
				       unsigned long limit_pfn)
{
	struct cpu_rcache *cpu_rcache;
	unsigned long iova_pfn = 0;
	bool has_pfn = false;
	unsigned long flags;

	cpu_rcache = raw_cpu_ptr(rcache->cpu_rcaches);
	spin_lock_irqsave(&cpu_rcache->lock, flags);

	if (!magazine_empty(cpu_rcache->loaded)) {
		has_pfn = true;
	} else if (!magazine_empty(cpu_rcache->prev)) {
		swap(cpu_rcache->prev, cpu_rcache->loaded);
		has_pfn = true;
	} else {
		spin_lock(&rcache->lock);
		if (rcache->depot_size > 0) {
			magazine_free(cpu_rcache->loaded, rcache);
			cpu_rcache->loaded = rcache->depot[--rcache->depot_size];
			has_pfn = true;
		}
		spin_unlock(&rcache->lock);
	}

	if (has_pfn)
		iova_pfn = magazine_pop(cpu_rcache->loaded, limit_pfn);

	spin_unlock_irqrestore(&cpu_rcache->lock, flags);

	return iova_pfn;
}


