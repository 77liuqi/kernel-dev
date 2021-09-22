
#ifndef _RCACHE_H_
#define _RCACHE_H_

#define MAX_GLOBAL_MAGS 32	/* magazines per bin */

#define MAG_SIZE 128


struct magazine {
	unsigned long size;
	union {
		unsigned long val[MAG_SIZE];
		void *ptr[MAG_SIZE];
	};
};

struct cpu_rcache {
	spinlock_t lock;
	struct magazine *loaded;
	struct magazine *prev;
};

struct rcache {
	spinlock_t lock;
	unsigned int size;
	unsigned long depot_size;
	struct magazine *depot[MAX_GLOBAL_MAGS];
	struct cpu_rcache __percpu *cpu_rcaches;
	struct kmem_cache *kmem_cache;
};


void magazine_free(struct magazine *mag, struct rcache *rcache);

int rcache_init(struct rcache *rcache, unsigned int);

unsigned long rcache_get(struct rcache *rcache,
				       unsigned long limit_pfn);

bool rcache_insert(struct rcache *rcache,
				 unsigned long iova_pfn);

#endif

