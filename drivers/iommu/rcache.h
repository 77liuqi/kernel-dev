
#include <linux/iova.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/bitops.h>
#include <linux/cpu.h>

#include "linux/iova.h"

#define IOVA_RANGE_CACHE_MAX_SIZE 6	/* log of max cached IOVA range size (in pages) */
#define MAX_GLOBAL_MAGS 32	/* magazines per bin */

struct iova_rcache {
	spinlock_t lock;
	unsigned long depot_size;
	struct iova_magazine *depot[MAX_GLOBAL_MAGS];
	struct iova_cpu_rcache __percpu *cpu_rcaches;
};

struct iovad_rcache {
	struct iova_rcache rcaches[IOVA_RANGE_CACHE_MAX_SIZE];	/* IOVA range caches */
	struct hlist_node	cpuhp_dead;
	struct iova_domain *iovad;
};

void
free_iova_fast(struct iovad_rcache *iovad_rcache, unsigned long pfn, unsigned long size);

unsigned long
alloc_iova_fast(struct iovad_rcache *iovad_rcache, unsigned long size,
		unsigned long limit_pfn, bool flush_rcache);


