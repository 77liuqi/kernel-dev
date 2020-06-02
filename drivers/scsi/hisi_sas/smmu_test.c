#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/dmapool.h>
#include <linux/iopoll.h>
#include <linux/lcm.h>
#include <linux/libata.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/timer.h>
#include <scsi/sas_ata.h>
#include <scsi/libsas.h>
#include <linux/kthread.h>

static int ways = 64;
module_param(ways, int, S_IRUGO);

static int seconds = 20;
module_param(seconds, int, S_IRUGO);

int mappings[NR_CPUS];
struct semaphore sem[NR_CPUS];


extern struct device *get_zip_dev(void);

#define COMPLETIONS_SIZE 50

static noinline dma_addr_t test_mapsingle(struct device *dev, void *buf, int size)
{
	dma_addr_t dma_addr = dma_map_single(dev, buf, size, DMA_TO_DEVICE);
	return dma_addr;
}

static noinline void test_unmapsingle(struct device *dev, void *buf, int size, dma_addr_t dma_addr)
{
	dma_unmap_single(dev, dma_addr, size, DMA_TO_DEVICE);
}

static noinline void test_memcpy(void *out, void *in, int size)
{  
	memcpy(out, in, size);
}

extern struct device *hisi_sas_dev;

static int testthread(void *data)
{  
	unsigned long stop = jiffies +seconds*HZ;
	struct device *dev = hisi_sas_dev;
	char *inputs[COMPLETIONS_SIZE];
	char *outputs[COMPLETIONS_SIZE];
	dma_addr_t dma_addr[COMPLETIONS_SIZE];
	int i, cpu = smp_processor_id();
	struct semaphore *sem = data;

	for (i = 0; i < COMPLETIONS_SIZE; i++) {
		inputs[i] = kzalloc(4096, GFP_KERNEL);
		if (!inputs[i])
			return -ENOMEM;
	}

	for (i = 0; i < COMPLETIONS_SIZE; i++) {
		outputs[i] = kzalloc(4096, GFP_KERNEL);
		if (!outputs[i])
			return -ENOMEM;
	}

	while (time_before(jiffies, stop)) {
		for (i = 0; i < COMPLETIONS_SIZE; i++) {
			dma_addr[i] = test_mapsingle(dev, inputs[i], 4096);
			test_memcpy(outputs[i], inputs[i], 4096);
		}
		for (i = 0; i < COMPLETIONS_SIZE; i++) {
			test_unmapsingle(dev, inputs[i], 4096, dma_addr[i]);
		}
		mappings[cpu] += COMPLETIONS_SIZE;
	}

	for (i = 0; i < COMPLETIONS_SIZE; i++) {
		kfree(outputs[i]);
		kfree(inputs[i]);
	}

	up(sem);

	return 0;
}  

void smmu_test_core(int cpus)
{
	struct task_struct *tsk;
	int i;
	int total_mappings = 0;

	ways = cpus;

	for(i=0;i<ways;i++) {
		mappings[i] = 0;
		tsk = kthread_create_on_cpu(testthread, &sem[i], i,  "map_test");

		if (IS_ERR(tsk))
			printk(KERN_ERR "create test thread failed\n");
		wake_up_process(tsk);
	}

	for(i=0;i<ways;i++) {
		down(&sem[i]);
		total_mappings += mappings[i];
	}

	printk(KERN_ERR "finished total_mappings=%d (per way=%d) (rate=%d per second per cpu) ways=%d\n", 
	total_mappings, total_mappings / ways, total_mappings / (seconds* ways), ways);

}
EXPORT_SYMBOL(smmu_test_core);


static int __init test_init(void)
{
	int i;

	for(i=0;i<NR_CPUS;i++)
		sema_init(&sem[i], 0);

	return 0;
}
  
static void __exit test_exit(void)
{
}

module_init(test_init);
module_exit(test_exit);
MODULE_LICENSE("GPL");
