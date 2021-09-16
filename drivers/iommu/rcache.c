
#include <linux/iova.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/bitops.h>
#include <linux/cpu.h>


struct iommu_dma_iova_domain {
	struct iova_domain	iovad;
	struct iova_rcache rcaches[IOVA_RANGE_CACHE_MAX_SIZE];	/* IOVA range caches */
	struct hlist_node	cpuhp_dead;
};

static void init_iova_rcaches(struct iova_domain *iovad);
static void free_cpu_cached_iovas(unsigned int cpu, struct iova_domain *iovad);
static void free_iova_rcaches(struct iova_domain *iovad);

static int iova_cpuhp_dead(unsigned int cpu, struct hlist_node *node)
{
	struct iova_domain *iovad;

	iovad = hlist_entry_safe(node, struct iova_domain, cpuhp_dead);

	free_cpu_cached_iovas(cpu, iovad);
	return 0;
}

static void free_global_cached_iovas(struct iova_domain *iovad);


/*
 * Magazine caches for IOVA ranges.  For an introduction to magazines,
 * see the USENIX 2001 paper "Magazines and Vmem: Extending the Slab
 * Allocator to Many CPUs and Arbitrary Resources" by Bonwick and Adams.
 * For simplicity, we use a static magazine size and don't implement the
 * dynamic size tuning described in the paper.
 */

#define IOVA_MAG_SIZE 128

struct iova_magazine {
	unsigned long size;
	unsigned long pfns[IOVA_MAG_SIZE];
};

struct iova_cpu_rcache {
	spinlock_t lock;
	struct iova_magazine *loaded;
	struct iova_magazine *prev;
};

static struct iova_magazine *iova_magazine_alloc(gfp_t flags)
{
	return kzalloc(sizeof(struct iova_magazine), flags);
}

static void iova_magazine_free(struct iova_magazine *mag)
{
	kfree(mag);
}

static void
iova_magazine_free_pfns(struct iova_magazine *mag, struct iova_domain *iovad)
{
	unsigned long flags;
	int i;

	if (!mag)
		return;

	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);

	for (i = 0 ; i < mag->size; ++i) {
		struct iova *iova = private_find_iova(iovad, mag->pfns[i]);

		if (WARN_ON(!iova))
			continue;

		remove_iova(iovad, iova);
		free_iova_mem(iova);
	}

	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);

	mag->size = 0;
}

static bool iova_magazine_full(struct iova_magazine *mag)
{
	return (mag && mag->size == IOVA_MAG_SIZE);
}

static bool iova_magazine_empty(struct iova_magazine *mag)
{
	return (!mag || mag->size == 0);
}

static unsigned long iova_magazine_pop(struct iova_magazine *mag,
				       unsigned long limit_pfn)
{
	int i;
	unsigned long pfn;

	BUG_ON(iova_magazine_empty(mag));

	/* Only fall back to the rbtree if we have no suitable pfns at all */
	for (i = mag->size - 1; mag->pfns[i] > limit_pfn; i--)
		if (i == 0)
			return 0;

	/* Swap it to pop it */
	pfn = mag->pfns[i];
	mag->pfns[i] = mag->pfns[--mag->size];

	return pfn;
}

static void iova_magazine_push(struct iova_magazine *mag, unsigned long pfn)
{
	BUG_ON(iova_magazine_full(mag));

	mag->pfns[mag->size++] = pfn;
}

static void init_iova_rcaches(struct iova_domain *iovad)
{
	struct iova_cpu_rcache *cpu_rcache;
	struct iova_rcache *rcache;
	unsigned int cpu;
	int i;

	for (i = 0; i < IOVA_RANGE_CACHE_MAX_SIZE; ++i) {
		rcache = &iovad->rcaches[i];
		spin_lock_init(&rcache->lock);
		rcache->depot_size = 0;
		rcache->cpu_rcaches = __alloc_percpu(sizeof(*cpu_rcache), cache_line_size());
		if (WARN_ON(!rcache->cpu_rcaches))
			continue;
		for_each_possible_cpu(cpu) {
			cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
			spin_lock_init(&cpu_rcache->lock);
			cpu_rcache->loaded = iova_magazine_alloc(GFP_KERNEL);
			cpu_rcache->prev = iova_magazine_alloc(GFP_KERNEL);
		}
	}
}

/*
 * Try inserting IOVA range starting with 'iova_pfn' into 'rcache', and
 * return true on success.  Can fail if rcache is full and we can't free
 * space, and free_iova() (our only caller) will then return the IOVA
 * range to the rbtree instead.
 */
static bool __iova_rcache_insert(struct iova_domain *iovad,
				 struct iova_rcache *rcache,
				 unsigned long iova_pfn)
{
	struct iova_magazine *mag_to_free = NULL;
	struct iova_cpu_rcache *cpu_rcache;
	bool can_insert = false;
	unsigned long flags;

	cpu_rcache = raw_cpu_ptr(rcache->cpu_rcaches);
	spin_lock_irqsave(&cpu_rcache->lock, flags);

	if (!iova_magazine_full(cpu_rcache->loaded)) {
		can_insert = true;
	} else if (!iova_magazine_full(cpu_rcache->prev)) {
		swap(cpu_rcache->prev, cpu_rcache->loaded);
		can_insert = true;
	} else {
		struct iova_magazine *new_mag = iova_magazine_alloc(GFP_ATOMIC);

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
		iova_magazine_push(cpu_rcache->loaded, iova_pfn);

	spin_unlock_irqrestore(&cpu_rcache->lock, flags);

	if (mag_to_free) {
		iova_magazine_free_pfns(mag_to_free, iovad);
		iova_magazine_free(mag_to_free);
	}

	return can_insert;
}

static bool iova_rcache_insert(struct iova_domain *iovad, unsigned long pfn,
			       unsigned long size)
{
	unsigned int log_size = order_base_2(size);

	if (log_size >= IOVA_RANGE_CACHE_MAX_SIZE)
		return false;

	return __iova_rcache_insert(iovad, &iovad->rcaches[log_size], pfn);
}

/*
 * Caller wants to allocate a new IOVA range from 'rcache'.  If we can
 * satisfy the request, return a matching non-NULL range and remove
 * it from the 'rcache'.
 */
static unsigned long __iova_rcache_get(struct iova_rcache *rcache,
				       unsigned long limit_pfn)
{
	struct iova_cpu_rcache *cpu_rcache;
	unsigned long iova_pfn = 0;
	bool has_pfn = false;
	unsigned long flags;

	cpu_rcache = raw_cpu_ptr(rcache->cpu_rcaches);
	spin_lock_irqsave(&cpu_rcache->lock, flags);

	if (!iova_magazine_empty(cpu_rcache->loaded)) {
		has_pfn = true;
	} else if (!iova_magazine_empty(cpu_rcache->prev)) {
		swap(cpu_rcache->prev, cpu_rcache->loaded);
		has_pfn = true;
	} else {
		spin_lock(&rcache->lock);
		if (rcache->depot_size > 0) {
			iova_magazine_free(cpu_rcache->loaded);
			cpu_rcache->loaded = rcache->depot[--rcache->depot_size];
			has_pfn = true;
		}
		spin_unlock(&rcache->lock);
	}

	if (has_pfn)
		iova_pfn = iova_magazine_pop(cpu_rcache->loaded, limit_pfn);

	spin_unlock_irqrestore(&cpu_rcache->lock, flags);

	return iova_pfn;
}

/*
 * Try to satisfy IOVA allocation range from rcache.  Fail if requested
 * size is too big or the DMA limit we are given isn't satisfied by the
 * top element in the magazine.
 */
static unsigned long iova_rcache_get(struct iova_domain *iovad,
				     unsigned long size,
				     unsigned long limit_pfn)
{
	unsigned int log_size = order_base_2(size);

	if (log_size >= IOVA_RANGE_CACHE_MAX_SIZE)
		return 0;

	return __iova_rcache_get(&iovad->rcaches[log_size], limit_pfn - size);
}

/*
 * free rcache data structures.
 */
static void free_iova_rcaches(struct iova_domain *iovad)
{
	struct iova_rcache *rcache;
	struct iova_cpu_rcache *cpu_rcache;
	unsigned int cpu;
	int i, j;

	for (i = 0; i < IOVA_RANGE_CACHE_MAX_SIZE; ++i) {
		rcache = &iovad->rcaches[i];
		for_each_possible_cpu(cpu) {
			cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
			iova_magazine_free(cpu_rcache->loaded);
			iova_magazine_free(cpu_rcache->prev);
		}
		free_percpu(rcache->cpu_rcaches);
		for (j = 0; j < rcache->depot_size; ++j)
			iova_magazine_free(rcache->depot[j]);
	}
}

/*
 * free all the IOVA ranges cached by a cpu (used when cpu is unplugged)
 */
static void free_cpu_cached_iovas(unsigned int cpu, struct iova_domain *iovad)
{
	struct iova_cpu_rcache *cpu_rcache;
	struct iova_rcache *rcache;
	unsigned long flags;
	int i;

	for (i = 0; i < IOVA_RANGE_CACHE_MAX_SIZE; ++i) {
		rcache = &iovad->rcaches[i];
		cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
		spin_lock_irqsave(&cpu_rcache->lock, flags);
		iova_magazine_free_pfns(cpu_rcache->loaded, iovad);
		iova_magazine_free_pfns(cpu_rcache->prev, iovad);
		spin_unlock_irqrestore(&cpu_rcache->lock, flags);
	}
}

/*
 * free all the IOVA ranges of global cache
 */
static void free_global_cached_iovas(struct iova_domain *iovad)
{
	struct iova_rcache *rcache;
	unsigned long flags;
	int i, j;

	for (i = 0; i < IOVA_RANGE_CACHE_MAX_SIZE; ++i) {
		rcache = &iovad->rcaches[i];
		spin_lock_irqsave(&rcache->lock, flags);
		for (j = 0; j < rcache->depot_size; ++j) {
			iova_magazine_free_pfns(rcache->depot[j], iovad);
			iova_magazine_free(rcache->depot[j]);
		}
		rcache->depot_size = 0;
		spin_unlock_irqrestore(&rcache->lock, flags);
	}
}


