
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
};


unsigned long iova_rcache_get(struct iova_domain *iovad,
				     unsigned long size,
				     unsigned long limit_pfn);

bool iova_rcache_insert(struct iova_domain *iovad,
			       unsigned long pfn,
			       unsigned long size);

