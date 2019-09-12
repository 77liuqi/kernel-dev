// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SPI Nor Flash Controller Driver
 *
 * Copyright (c) 2015-2016 HiSilicon Technologies Co., Ltd.
 */
 #define DEBUG 1
#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define GLOBAL_CFG (0x100)

#define BUS_CFG1 (0x200)
#define BUS_CFG2 (0x204)
#define BUS_FLASH_SIZE (0x210)

#define HIFMC_DMA_MAX_LEN		(4096)

#define VERSION (0x1f8)

#define CMD_CONFIG (0x300)
#define CMD_CONFIG_DATA_CNT_OFF 9
#define CMD_CONFIG_DATA_CNT_MSK (0xff << CMD_CONFIG_DATA_CNT_OFF)
#define CMD_CONFIG_CMD_RW_OFF 8
#define CMD_CONFIG_CMD_RW_MSK (1 << CMD_CONFIG_CMD_RW_OFF)
#define CMD_CONFIG_CMD_DATA_EN_OFF 7
#define CMD_CONFIG_CMD_DATA_EN_MSK (1 << CMD_CONFIG_CMD_DATA_EN_OFF)
#define CMD_CONFIG_CMD_DUMMY_CNT_OFF 4
#define CMD_CONFIG_CMD_DUMMY_CNT_MSK (0x7 << CMD_CONFIG_CMD_DUMMY_CNT_OFF)
#define CMD_CONFIG_CMD_ADDR_EN_OFF 3
#define CMD_CONFIG_CMD_ADDR_EN_MSK (1 << CMD_CONFIG_CMD_ADDR_EN_OFF)
#define CMD_CONFIG_CMD_CS_SEL_OFF 1
#define CMD_CONFIG_CMD_CS_SEL_MSK (1 << CMD_CONFIG_CMD_CS_SEL_OFF)
#define CMD_CONFIG_CMD_START_OFF 0
#define CMD_CONFIG_CMD_START_MSK (1 << CMD_CONFIG_CMD_START_OFF)
#define CMD_INS (0x308)
#define CMD_ADDR (0x30c)
#define CMD_DATABUF(x) (0x400 + (x * 4))



#define MAX_CMD_DWORD 8

enum hifmc_iftype {
	IF_TYPE_STD,
	IF_TYPE_DUAL,
	IF_TYPE_DIO,
	IF_TYPE_QUAD,
	IF_TYPE_QIO,
};

struct hifmc_priv {
	u32 chipselect;
//	u32 clkrate;
	struct hifmc_host *host;
};

#define HIFMC_MAX_CHIP_NUM		2
struct hifmc_host {
	struct device *dev;
	struct mutex lock;

	void __iomem *regbase;
	void __iomem *iobase;
//	struct clk *clk;
//	void *buffer;
	dma_addr_t dma_buffer;

	u32 num_chip;
};
#define FMC_WAIT_TIMEOUT_US		1000000
#define FMC_WAIT_POLL_INTERVAL_US		10

static int hisi_spi_hi16xx_spi_wait_cmd_idle(struct hifmc_host *host)
{
	u32 reg;

	return readl_poll_timeout(host->regbase + CMD_CONFIG, reg,
		!(reg & CMD_CONFIG_CMD_START_MSK), FMC_WAIT_POLL_INTERVAL_US, FMC_WAIT_TIMEOUT_US);
}

static void hisi_spi_hi16xx_spi_init(struct hifmc_host *host)
{
	pr_err("%s host=%pS\n", __func__, host);
}

void hisi_spi_hi16xx_spi_memcpy_from_databuf(struct hifmc_host *host, u8 *to, unsigned int len)
{
	int i;

	for (i = 0; i < roundup(len, 4) / 4; i++) {
		u32 val = readl_relaxed(host->regbase + CMD_DATABUF(i));
		int j;

		for (j = 0; j < 4 && (j + (i * 4) < len); to++, val >>= 8, j++)
			*to = val & 0xff;
	}
}

 void hisi_spi_hi16xx_spi_memcpy_to_databuf(struct hifmc_host *host, u8 *from, unsigned int len)
{
	int i;

	for (i = 0; i < roundup(len, 4) / 4; i++) {
		u32 val = 0;
		int j;

		for (j = 0; j < 4 && (j + (i * 4) < len); from++, j++)
			val |= *from << (j * 8);
		writel_relaxed(val, host->regbase + CMD_DATABUF(i));
	}
}

static int hisi_spi_hi16xx_spi_read_reg(struct hifmc_host *host, u8 opcode, u8 *buf,
		unsigned int len, u8 chip_select)
{
	u32 bus_cfg1, bus_cfg2, global_cfg;
	int res;
	int bus_flash_size;
	u32 config;
	int i;

	bus_cfg1 = readl(host->regbase + BUS_CFG1);
	bus_cfg2 = readl(host->regbase + BUS_CFG2);
	bus_flash_size = readl(host->regbase + BUS_FLASH_SIZE);
	global_cfg = readl(host->regbase + GLOBAL_CFG);

	pr_debug("%s opcode=0x%x buf=%pS len=%d bus_cfg1=0x%x bus_cfg2=0x%x bus_flash_size=0x%x global_cfg=0x%x\n",
		__func__, opcode, buf, len, bus_cfg1, bus_cfg2, bus_flash_size, global_cfg);


	config = ((len +1 )<< CMD_CONFIG_DATA_CNT_OFF) | CMD_CONFIG_CMD_DATA_EN_MSK |
			CMD_CONFIG_CMD_START_MSK | chip_select << CMD_CONFIG_CMD_CS_SEL_OFF
			 | CMD_CONFIG_CMD_RW_MSK;

	writel(opcode, host->regbase + CMD_INS);

//	pr_err("%s2 config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x\n",
//		__func__, config, ins, addr, version, cmd_buf0);

	writel(config, host->regbase + CMD_CONFIG);
	res = hisi_spi_hi16xx_spi_wait_cmd_idle(host);
	if (res)
		return res;

//	pr_err("%s3 config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x\n",
//		__func__, config, ins, addr, version, cmd_buf0);
	hisi_spi_hi16xx_spi_memcpy_from_databuf(host, buf, len);
	for (i=0;i<len;i++)
		pr_debug("%s buf[%d]=0x%x CMD_BUF(%d)=0x%x\n", __func__, i, buf[i], i, readl(host->regbase + CMD_DATABUF(i)));

	pr_debug("%s4 config=0x%x opcode=0x%x\n",
		__func__, config, opcode);

	return 0;
}



static int hisi_spi_hi16xx_spi_write_reg(struct hifmc_host *host, u8 opcode, const u8 *buf,
		unsigned int len, u8 chip_select)
{
	int i;
	u32 erase_addr = 0;
	u32 config;

	pr_debug("%s opcode=0x%x buf=%pS len=%d chip_select=%d\n",
		__func__, opcode, buf, len, chip_select);

	if (opcode != 0x6 && opcode != 0x20 && opcode != 0x4) {
		pr_err("%s1 opcode=0x%x buf=%pS len=%d chip_select=%d rejected as opcode not supported yet\n",
			__func__, opcode, buf, len, chip_select);

		return -ENOTSUPP;
	}
	
	if (len > 0) {
		//pr_err("%s2 opcode=0x%x buf=%pS len=%d chip_select=%d rejected as len not supported yet\n",
	//		__func__, opcode, buf, len, chip_select);

		for (i = 0; i < len; i++) {
			erase_addr |= buf[i] << ((len - i - 1) *8);
		}

		pr_debug("%s2.0 opcode=0x%x erase_addr=0x%x\n",
			__func__, opcode, erase_addr);
//	[	34.487982] hisi_spi_hi16xx_spi_write_reg2 opcode=0x20 buf=0xffff801f97bd4800 len=3 chip_select=0 rejected as len not supported yet
//	[	34.499798] hisi_spi_hi16xx_spi_write_reg2.0 opcode=0x20 buf[0]=0xe4
//	[	34.506144] hisi_spi_hi16xx_spi_write_reg2.0 opcode=0x20 buf[1]=0x0
//	[	34.512405] hisi_spi_hi16xx_spi_write_reg2.0 opcode=0x20 buf[2]=0x0


	//	return -ENOTSUPP;
	}

	config = ((len +1 )<< CMD_CONFIG_DATA_CNT_OFF) | 
			CMD_CONFIG_CMD_START_MSK | chip_select << CMD_CONFIG_CMD_CS_SEL_OFF;

	if (opcode == 0x20) {
		writel(erase_addr, host->regbase + CMD_ADDR);
		config |= CMD_CONFIG_CMD_ADDR_EN_MSK;
	}


	writel(opcode, host->regbase + CMD_INS);
	
//	pr_err("%s2 config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x\n",
//		__func__, config, ins, addr, version, cmd_buf0);

	writel(config, host->regbase + CMD_CONFIG);


	return hisi_spi_hi16xx_spi_wait_cmd_idle(host);
}

static int hisi_spi_hi16xx_spi_read(struct hifmc_host *host, u64 from, unsigned int len,
		void *buf, u8 opcode, unsigned int dummy_bytes, u8 chip_select)
{
	u32 config;
	int res;

	
	dev_dbg(host->dev, "%s buf=%pS len=%u host=%pS read opcode=0x%x chip_select=%d\n", __func__, 
		buf, len, host, opcode, chip_select);
	//	pr_err("%s1 spi=%pS config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x cmd_buf1=0x%x\n",
	//		__func__, spi, config, ins, addr, version, cmd_buf0, cmd_buf1);

	if (len & 0x3) {
		pr_err("%s1 len=%u unsupported\n",
			__func__, len);
	}


	config = ((len + 1) << CMD_CONFIG_DATA_CNT_OFF) | CMD_CONFIG_CMD_DATA_EN_MSK |
		    CMD_CONFIG_CMD_ADDR_EN_MSK |
			(dummy_bytes / 8) << CMD_CONFIG_CMD_DUMMY_CNT_OFF |
			CMD_CONFIG_CMD_START_MSK | CMD_CONFIG_CMD_RW_MSK;// 1: READ
	writel(from, host->regbase + CMD_ADDR);
	writel(opcode, host->regbase + CMD_INS);
	writel(config, host->regbase + CMD_CONFIG);


	dev_dbg(host->dev, "%s2 buf=%pS len=%u config=0x%x\n", __func__, buf, len, config);

	res = hisi_spi_hi16xx_spi_wait_cmd_idle(host);
	if (res)
		return res;

	dev_dbg(host->dev, "%s3 buf=%pS len=%u host=%pS config=0x%x\n", __func__, buf, len, host, config);

	hisi_spi_hi16xx_spi_memcpy_from_databuf(host, buf, len);

	dev_dbg(host->dev, "%s out returning len=%u\n", __func__, len);
	return 0;
}

static int hisi_spi_hi16xx_spi_write(struct hifmc_host *host, u64 from, unsigned int len,
		void *buf, u8 opcode, int dummy_bytes, u8 chip_select)
{
	u32 config;

	dev_dbg(host->dev, "%s write_buf=%pS len=%u write_opcode=0x%x chip_select=%d from=0x%llx dummy_bytes=%d\n", __func__, 
		buf, len, opcode, chip_select, from, dummy_bytes);

	if (opcode != 0x2) {
		pr_err("%s1 opcode=0x%x buf=%pS len=%u chip_select=%d rejected as opcode not supported yet\n",
			__func__, opcode, buf, len, chip_select);

		return -ENOTSUPP;
	}
	
	if (len == 0 || len & 3) {
		pr_err("%s2 opcode=0x%x buf=%pS len=%u chip_select=%d rejected as len not supported yet\n",
			__func__, opcode, buf, len, chip_select);

		return -ENOTSUPP;
	}

	config = readl(host->regbase + CMD_CONFIG);

//	pr_err("%s2.2 res=%d rdsr=0x%x\n", __func__, res, rdsr);

//	pr_err_ratelimited("%s buf=%pS len=%u host=%pS count=%d read opcode=0x%x addr=0x%x chip_select=%d\n", __func__, 
//		buf, len, host, count, opcode, addr, chip_select);
	//	pr_err("%s1 spi=%pS config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x cmd_buf1=0x%x\n",
	//		__func__, spi, config, ins, addr, version, cmd_buf0, cmd_buf1);

	config = ((len + 1) << CMD_CONFIG_DATA_CNT_OFF) | CMD_CONFIG_CMD_DATA_EN_MSK |
			CMD_CONFIG_CMD_ADDR_EN_MSK |
			(dummy_bytes / 8) << CMD_CONFIG_CMD_DUMMY_CNT_OFF |
			CMD_CONFIG_CMD_START_MSK;
	writel(from, host->regbase + CMD_ADDR);
	writel(opcode, host->regbase + CMD_INS);

	hisi_spi_hi16xx_spi_memcpy_to_databuf(host, buf, len);

	writel(config, host->regbase + CMD_CONFIG);

	dev_dbg(host->dev, "%s4\n", __func__);
	
	return hisi_spi_hi16xx_spi_wait_cmd_idle(host);
}


__maybe_unused static int hi16xx_spi_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	//struct hifmc_host *host = spi_controller_get_devdata(mem->spi->master);

	if (op->addr.nbytes == 0 && op->addr.buswidth == 0 && op->addr.val == 0) {
	} else {
	//	int was = op->data.nbytes;
	//	pr_err("%s [cmd opcode=0x%x buswidth=0x%x, addr.nbytes=%d buswidth=%d val=0x%llx, dummy nbytes=%d buswidth=%d, data.buswidth=%d buf=%pS nbytes=%d (was %d)] adjusting\n", __func__,  
	//		op->cmd.opcode, op->cmd.buswidth, op->addr.nbytes, op->addr.buswidth, op->addr.val, op->dummy.nbytes, op->dummy.buswidth, op->data.buswidth, op->data.buf.in, op->data.nbytes, was);
		if (op->data.nbytes > MAX_CMD_DWORD * 4)
			op->data.nbytes = MAX_CMD_DWORD * 4;
	}

	return 0;
}

__maybe_unused static bool hi16xx_spi_supports_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
//	struct hifmc_host *host = spi_controller_get_devdata(mem->spi->master);

	//pr_err("%s mem=%pS op=%pS [] host=%pS\n", __func__, mem, op, host);

	return true;
}

static int hi16xx_spi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct hifmc_host *host = spi_controller_get_devdata(mem->spi->master);
	struct spi_device *spi = mem->spi;
	u8 chip_select = spi->chip_select;

//	pr_err("%s mem=%pS [cmd opcode=0x%x buswidth=0x%x] host=%pS\n", __func__, mem, 
//		op->cmd.opcode, op->cmd.buswidth, host);
//	pr_err("%s1 mem=%pS [addr nbytes=0x%x buswidth=0x%x val=0x%llx] host=%pS\n", __func__, mem, 
//		op->addr.nbytes, op->addr.buswidth, op->addr.val, host);
//	pr_err("%s2 mem=%pS [dummy buswidth=0x%x buswidth=0x%x] host=%pS\n", __func__, mem, 
//		op->dummy.nbytes, op->dummy.buswidth, host);
//	pr_err("%s3 mem=%pS [data nbytes=0x%x buf in=0x%pS out=%pS dir=%d] host=%pS\n", __func__, mem, 
//		op->data.nbytes, op->data.buf.in, op->data.buf.out, op->data.dir, host);

	if (op->addr.nbytes == 0 && op->addr.buswidth == 0 && op->addr.val == 0) {
		/* Read/write Reg */
		switch (op->data.dir) {
		case SPI_MEM_DATA_IN:
			return hisi_spi_hi16xx_spi_read_reg(host, op->cmd.opcode, op->data.buf.in, op->data.nbytes, chip_select);
		case SPI_MEM_DATA_OUT:
			return hisi_spi_hi16xx_spi_write_reg(host, op->cmd.opcode, op->data.buf.out, op->data.nbytes, chip_select);
		default:
			break;
		}
	} else {
		/* Read/write */
		switch (op->data.dir) {
		case SPI_MEM_DATA_IN:
			return hisi_spi_hi16xx_spi_read(host, op->addr.val, op->data.nbytes, op->data.buf.in, op->cmd.opcode, op->dummy.nbytes*8, chip_select);
		case SPI_MEM_DATA_OUT:
		//	pr_err("%s [cmd opcode=0x%x buswidth=0x%x, addr.nbytes=%d buswidth=%d val=0x%llx, dummy nbytes=%d buswidth=%d, data.buswidth=%d buf=%pS nbytes=%d]\n", __func__,  
		//	op->cmd.opcode, op->cmd.buswidth, op->addr.nbytes, op->addr.buswidth, op->addr.val, op->dummy.nbytes, op->dummy.buswidth, op->data.buswidth, op->data.buf.in, op->data.nbytes);
		
			return hisi_spi_hi16xx_spi_write(host, op->addr.val, op->data.nbytes, op->data.buf.in, op->cmd.opcode, op->dummy.nbytes*8, chip_select);
		default:
			break;
		}
	}
	
	pr_err("%s not support [cmd opcode=0x%x buswidth=0x%x addr.val=0x%llx]\n", __func__, 
		op->cmd.opcode, op->cmd.buswidth, op->addr.val);
	return -ENOTSUPP;
}
__maybe_unused static const char *hi16xx_spi_get_name(struct spi_mem *mem)
{
	struct hifmc_host *host = spi_controller_get_devdata(mem->spi->master);

	pr_err("%s mem=%pS host=%pS\n", __func__, mem, host);

	return "snake";
}


static const struct spi_controller_mem_ops hi16xx_spi_mem_ops = {
	.adjust_op_size = hi16xx_spi_adjust_op_size,
//	.supports_op = hi16xx_spi_supports_op,
	.exec_op = hi16xx_spi_exec_op,
//	.get_name = hi16xx_spi_get_name,
};

void alloc_fake_spi_chip(struct device *host, struct spi_controller *ctlr, int cs)
{
	
	struct spi_device *spi = spi_alloc_device(ctlr);
	struct device *spi_dev;
	const char *compatible = "jedec,spi-nor", *p;
	dev_err(host, "%s creating fake SPI NOR device cs=%d\n", __func__, cs);
	if (!spi) {
		dev_err(host, "failed to allocate SPI device for \n");
	}
	//	int cplen;
	
	//	compatible = of_get_property(node, "compatible", &cplen);
	//	if (!compatible || strlen(compatible) > cplen)
	//		return -ENODEV;
	p = strchr(compatible, ',');
	strlcpy(spi->modalias, p ? p + 1 : compatible, sizeof(spi->modalias));
	
	spi_dev = &spi->dev;
	
	spi_dev->parent = host;
	spi->max_speed_hz	= 48000000;
	
	spi->chip_select = cs;
	
	if (spi_add_device(spi)) {
		dev_err(host, "failed to add SPI device from ACPI\n");
		spi_dev_put(spi);
	}

}

static int hisi_spi_hi16xx_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct hifmc_host *host;
	struct spi_controller *ctlr;
	struct device_node *np = dev->of_node;
	int version;
	int ret;

	dev_err(dev, "%s np=%pS\n", __func__, np);

	ctlr = spi_alloc_master(&pdev->dev, sizeof(*host));
	if (!ctlr)
		return -ENOMEM;
	
	ctlr->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD |
			  SPI_TX_DUAL | SPI_TX_QUAD;

	dev_err(dev, "%s2\n", __func__);

	host = spi_controller_get_devdata(ctlr);
	host->dev = dev;

	platform_set_drvdata(pdev, host);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg");
	host->regbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->regbase))
		return PTR_ERR(host->regbase);
	version = readl(host->regbase + VERSION);
	dev_err(dev, "%s host->regbase=%p\n", __func__, host->regbase);
	dev_err(dev, "%s version=0x%x\n", __func__, version);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "memory");
	host->iobase = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->iobase))
		return PTR_ERR(host->iobase);


	#ifdef hos_buffer
	host->buffer = dmam_alloc_coherent(dev, HIFMC_DMA_MAX_LEN,
			&host->dma_buffer, GFP_KERNEL);
	if (!host->buffer)
		return -ENOMEM;
	#endif

	mutex_init(&host->lock);
	hisi_spi_hi16xx_spi_init(host);
	ctlr->dev.of_node = np;

	ctlr->bus_num = -1;
	ctlr->num_chipselect = 1;
	ctlr->mem_ops = &hi16xx_spi_mem_ops;

	ret = devm_spi_register_controller(dev, ctlr);
	dev_err(dev, "%s devm_spi_register_controller ret=%d\n", __func__, ret);


	if (!acpi_disabled) {
		int chip_sel;
		for (chip_sel=0;chip_sel<ctlr->num_chipselect;chip_sel++)
			alloc_fake_spi_chip(dev, ctlr, chip_sel);
	}

	return ret;
}

static int hisi_spi_hi16xx_spi_remove(struct platform_device *pdev)
{
	struct hifmc_host *host = platform_get_drvdata(pdev);

//	hisi_spi_hi16xx_spi_unregister_all(host);
	mutex_destroy(&host->lock);
	return 0;
}

static const struct of_device_id hisi_spi_hi16xx_spi_dt_ids[] = {
	{ .compatible = "hisilicon,sfc-hi16xx"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hisi_spi_hi16xx_spi_dt_ids);

static const struct acpi_device_id hisi_spi_hi16xx_spi_acpi_ids[] = {
	{"HISI0999", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, hisi_spi_hi16xx_spi_acpi_ids);

#define HI16XX_SFC_NAME "hisi-sfc-hi16xx"




static const struct platform_device_id hi16xx_sfc_match[] = {
	{ HI16XX_SFC_NAME, 0},
	{ }
};
MODULE_DEVICE_TABLE(platform, hi16xx_sfc_match);


static struct platform_driver hisi_spi_hi16xx_spi_driver = {
	.id_table = hi16xx_sfc_match,
	.driver = {
		.name	= HI16XX_SFC_NAME,
		.of_match_table = hisi_spi_hi16xx_spi_dt_ids,
		.acpi_match_table = ACPI_PTR(hisi_spi_hi16xx_spi_acpi_ids),
	},
	.probe	= hisi_spi_hi16xx_spi_probe,
	.remove	= hisi_spi_hi16xx_spi_remove,
};
//module_platform_driver(hisi_spi_hi16xx_spi_driver);

static struct resource hi16xx_sfc_hi1616_resources[] = {
	{
		/* irq */
		.flags          = IORESOURCE_MEM,
		.name = "reg",
		.start = 0xa6000000,
		.end = 0xa6000000 + 0x10000 -1,
	},
	{
		/* irq */
		.flags          = IORESOURCE_MEM,
		.name = "memory",
		.start = 0xa4000000,
		.end = 0xa4000000 + 0x10000 -1,
	},
};


static struct platform_device hi1616_spi_dev = {
	.name = HI16XX_SFC_NAME,
	.id = -1,
	.resource = hi16xx_sfc_hi1616_resources,
	.num_resources = ARRAY_SIZE(hi16xx_sfc_hi1616_resources)
};
	
static struct resource hi16xx_sfc_hi1620_resources[] = {
	{
		/* irq */
		.flags          = IORESOURCE_MEM,
		.name = "reg",
		.start = 0x206200000,
		.end = 0x206200000 + 0x10000 -1,
	},
	{
		/* irq */
		.flags          = IORESOURCE_MEM,
		.name = "memory",
		.start = 0x206250000,
		.end = 0x206250000 + 0x10000 -1,
	},
};


static struct platform_device hi1620_spi_dev = {
	.name = HI16XX_SFC_NAME,
	.id = -1,
	.resource = hi16xx_sfc_hi1620_resources,
	.num_resources = ARRAY_SIZE(hi16xx_sfc_hi1620_resources)
};

#define read_cpuid(reg)			read_sysreg_s(SYS_ ## reg)

static int __init hisi_spi_hi16xx_spi_module_init(void)
{
	int ret;
	u32 midr = read_cpuid_id();

	pr_err("%s acpi_disabled=%d midr=0x%x\n", __func__, acpi_disabled, midr);

	ret = platform_driver_register(&hisi_spi_hi16xx_spi_driver);
	if (ret)
		return ret;

	if (acpi_disabled)
		return 0;

	switch (midr) {
	case 0x410fd082:
	ret = platform_device_register(&hi1616_spi_dev);
	if (ret) {
		pr_err("%s could not register hi1616_spi_dev pdev ret=%d\n", __func__, ret);
		return ret;
	}
	break;
	case 0x480fd010:
	ret = platform_device_register(&hi1620_spi_dev);
	if (ret) {
		pr_err("%s could not register hi1620_spi_dev pdev ret=%d\n", __func__, ret);
		return ret;
	}
	break;
	default:
		break;
	}
	
	pr_err("%s registered pdev\n", __func__);
	
	return 0;	
}
module_init(hisi_spi_hi16xx_spi_module_init);

static void __exit hisi_spi_hi16xx_spi_module_remove(void)
{

}
module_exit(hisi_spi_hi16xx_spi_module_remove);



MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HiSilicon SPI spi Flash Controller Driver");
