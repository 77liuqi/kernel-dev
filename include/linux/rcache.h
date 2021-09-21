
#ifndef _RCACHE_H_
#define _RCACHE_H_

#define MAX_GLOBAL_MAGS 32	/* magazines per bin */

#define MAG_SIZE 128

struct magazine {
	unsigned long size;
	unsigned char mem[][MAG_SIZE];
};

struct cpu_rcache {
	spinlock_t lock;
	struct magazine *loaded;
	struct magazine *prev;
};

struct magazine *magazine_alloc(gfp_t flags);

void magazine_free(struct magazine *mag);

bool magazine_full(struct magazine *mag);

bool magazine_empty(struct magazine *mag);

struct rcache {
	spinlock_t lock;
	unsigned long depot_size;
	struct magazine *depot[MAX_GLOBAL_MAGS];
	struct cpu_rcache __percpu *cpu_rcaches;
};

#endif

