// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HiSilicon SPI Nor Flash Controller Driver
 *
 * Copyright (c) 2015-2016 HiSilicon Technologies Co., Ltd.
 */
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
	void *buffer;
	dma_addr_t dma_buffer;

	struct spi_nor	*nor[HIFMC_MAX_CHIP_NUM];
	u32 num_chip;
};

static inline int hisi_spi_hi16xx_nor_wait_op_finish(struct hifmc_host *host)
{
	pr_err("%s host=%pS\n", __func__, host);
	return 0;
}

__maybe_unused static int hisi_spi_hi16xx_nor_get_if_type(enum spi_nor_protocol proto)
{
	enum hifmc_iftype if_type;

	switch (proto) {
	case SNOR_PROTO_1_1_2:
		if_type = IF_TYPE_DUAL;
		break;
	case SNOR_PROTO_1_2_2:
		if_type = IF_TYPE_DIO;
		break;
	case SNOR_PROTO_1_1_4:
		if_type = IF_TYPE_QUAD;
		break;
	case SNOR_PROTO_1_4_4:
		if_type = IF_TYPE_QIO;
		break;
	case SNOR_PROTO_1_1_1:
	default:
		if_type = IF_TYPE_STD;
		break;
	}

	return if_type;
}

static void hisi_spi_hi16xx_nor_init(struct hifmc_host *host)
{
	pr_err("%s host=%pS\n", __func__, host);
}

static int hisi_spi_hi16xx_nor_prep(struct spi_nor *nor, enum spi_nor_ops ops)
{
//	pr_err("%s nor=%pS ops=%x\n", __func__, nor, ops);

	return 0;
}

static void hisi_spi_hi16xx_nor_unprep(struct spi_nor *nor, enum spi_nor_ops ops)
{
//	pr_err("%s nor=%pS ops=%d\n", __func__, nor, ops);
}

__maybe_unused static int hisi_spi_hi16xx_nor_op_reg(struct spi_nor *nor,
				u8 opcode, int len, u8 optype)
{
	pr_err("%s nor=%pS\n", __func__, nor);

	return 0;
}

static int hisi_spi_hi16xx_nor_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf,
		int len)
{
	struct hifmc_priv *priv = nor->priv;
	struct hifmc_host *host = priv->host;
	u32 config, ins, addr, version;
	__le32 cmd_buf0, cmd_buf1, cmd_buf2, cmd_buf3,  cmd_buf[5];
	int i;
	static int count;

	config = readl(host->regbase + CMD_CONFIG);
	ins = readl(host->regbase + CMD_INS);
	addr = readl(host->regbase + CMD_ADDR);
	version = readl(host->regbase + VERSION);
	cmd_buf0 = readl(host->regbase + CMD_DATABUF(0));
	cmd_buf1 = readl(host->regbase + CMD_DATABUF(1));


//	pr_err("%s nor=%pS opcode=0x%x buf=%pS len=%d host=%pS count=%d\n", __func__, nor, opcode, buf, len, host, count);
//	pr_err("%s1 nor=%pS config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x cmd_buf1=0x%x\n",
//		__func__, nor, config, ins, addr, version, cmd_buf0, cmd_buf1);

	config &= ~CMD_CONFIG_DATA_CNT_MSK & ~CMD_CONFIG_CMD_CS_SEL_MSK &
			~CMD_CONFIG_CMD_ADDR_EN_MSK & ~CMD_CONFIG_CMD_RW_MSK;
	config |= ((len +1 )<< CMD_CONFIG_DATA_CNT_OFF) | CMD_CONFIG_CMD_DATA_EN_MSK |
			CMD_CONFIG_CMD_START_MSK;

	writel(opcode, host->regbase + CMD_INS);

//	pr_err("%s2 nor=%pS config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x\n",
//		__func__, nor, config, ins, addr, version, cmd_buf0);

	writel(config, host->regbase + CMD_CONFIG);

	msleep(100);

	config = readl(host->regbase + CMD_CONFIG);
//	pr_err("%s3 nor=%pS config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x\n",
//		__func__, nor, config, ins, addr, version, cmd_buf0);

	cmd_buf0 = readl(host->regbase + CMD_DATABUF(0));
	cmd_buf1 = readl(host->regbase + CMD_DATABUF(1));
	cmd_buf2 = readl(host->regbase + CMD_DATABUF(2));
	cmd_buf3 = readl(host->regbase + CMD_DATABUF(3));
	cmd_buf[0] = le32_to_cpu(cmd_buf0);
	cmd_buf[1] = le32_to_cpu(cmd_buf1);
	cmd_buf[2] = le32_to_cpu(cmd_buf2);
	cmd_buf[3] = le32_to_cpu(cmd_buf3);

//	pr_err("%s4 nor=%pS config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x cmd_buf[0]=0x%x\n",
//		__func__, nor, config, ins, addr, version, cmd_buf0, cmd_buf[0]);

	for (i = 0; i<len;i++) {
		u8 *byte = (u8 *)&cmd_buf[0];

		//pr_err("%s byte %d=0x%x", __func__, i, byte[i]);
		*buf = byte[i];
		buf++;
	}

	count++;

	return 0;
}

static int hisi_spi_hi16xx_nor_write_reg(struct spi_nor *nor, u8 opcode,
				u8 *buf, int len)
{
	pr_err("%s nor=%pS\n", __func__, nor);

	return 0;
}

__maybe_unused static int hisi_spi_hi16xx_nor_dma_transfer(struct spi_nor *nor, loff_t start_off,
		dma_addr_t dma_buf, size_t len, u8 op_type)
{
	pr_err("%s nor=%pS\n", __func__, nor);
	return 0;
}

#define MAX_CMD_WORD 4

static ssize_t hisi_spi_hi16xx_nor_read(struct spi_nor *nor, loff_t from, size_t len,
		u_char *read_buf)
{
	struct hifmc_priv *priv = nor->priv;
	struct hifmc_host *host = priv->host;
	u32 config, ins, addr, version, cmd_buf0, cmd_buf1;
	int i;
	static int count;
	int remaining = len;
	//WARN_ON_ONCE(1);
	config = readl(host->regbase + CMD_CONFIG);
	ins = readl(host->regbase + CMD_INS);
	addr = readl(host->regbase + CMD_ADDR);
	version = readl(host->regbase + VERSION);
	cmd_buf0 = readl(host->regbase + CMD_DATABUF(0));
	cmd_buf1 = readl(host->regbase + CMD_DATABUF(1));
	
	
	pr_err("%s nor=%pS read_buf=%pS len=%ld host=%pS count=%d read opcode=0x%x addr=0x%x read_dummy=%x\n", __func__, nor, read_buf, len, host, count, nor->read_opcode, addr, nor->read_dummy);
	//	pr_err("%s1 nor=%pS config=0x%x ins=0x%x addr=0x%x version=0x%x cmd_buf0=0x%x cmd_buf1=0x%x\n",
	//		__func__, nor, config, ins, addr, version, cmd_buf0, cmd_buf1);

	if (count >= 1)
		return -1;

	do {
		int read_len;

		if (remaining > MAX_CMD_WORD * 4)
			read_len = MAX_CMD_WORD * 4;
		else
			read_len = remaining;

		remaining -= read_len;

		config &= ~CMD_CONFIG_DATA_CNT_MSK & ~CMD_CONFIG_CMD_CS_SEL_MSK &
				~CMD_CONFIG_CMD_DATA_EN_OFF;
		config |= ((read_len + 1) << CMD_CONFIG_DATA_CNT_OFF) | CMD_CONFIG_CMD_DATA_EN_MSK |
			    CMD_CONFIG_CMD_ADDR_EN_MSK |
				(nor->read_dummy / 8) << CMD_CONFIG_CMD_DUMMY_CNT_OFF |
				CMD_CONFIG_CMD_START_MSK | CMD_CONFIG_CMD_RW_MSK;// 1: READ
		writel(from, host->regbase + CMD_ADDR);
		writel(nor->read_opcode, host->regbase + CMD_INS);
		writel(config, host->regbase + CMD_CONFIG);

		pr_err("%s1 nor=%pS read_buf=%pS len=%ld host=%pS count=%d config=0x%x read_len=%x remaining=%d\n", __func__, nor, read_buf, len, host, count, config, read_len, remaining);

sleep:
		msleep(100);

		config = readl(host->regbase + CMD_CONFIG);
		addr = readl(host->regbase + CMD_ADDR);

		if (config & CMD_CONFIG_CMD_START_MSK)
			goto sleep;

		pr_err("%s2 nor=%pS read_buf=%pS len=%ld host=%pS count=%d config=0x%x addr=0x%x\n", __func__, nor, read_buf, len, host, count, config, addr);

		for (i=0;i<2;i++) {
			u32 cmd_bufx = readl(host->regbase + CMD_DATABUF(i));
			u32 cmd_bufy = __swab32(cmd_bufx);
			u8 *ptr = (u8 *)&cmd_bufx;
			u8 aa, bb, cc, dd;

			pr_err("%s3.0 i=%d cmd_bufx=0x%x cmd_bufy=0x%x\n", __func__, i, cmd_bufx, cmd_bufy);

			*read_buf = aa = ptr[0];read_buf++;
			*read_buf = bb = ptr[1];read_buf++;
			*read_buf = cc = ptr[2];read_buf++;
			*read_buf = dd = ptr[3];read_buf++;
			
			pr_err("%s3.1 i=%d cmd_bufx=0x%x [%02x %02x %02x %02x]\n", __func__, i, cmd_bufx, aa, bb, cc, dd);
		}
	}while (0);
	pr_err("%s out nor=%pS returning len=%ld\n", __func__, nor, len);
	return len;
}

static ssize_t hisi_spi_hi16xx_nor_write(struct spi_nor *nor, loff_t to,
			size_t len, const u_char *write_buf)
{
	pr_err("%s nor=%pS\n", __func__, nor);

	return 0;
}

/**
 * Get spi flash device information and register it as a mtd device.
 */
static int hisi_spi_hi16xx_nor_register(struct device_node *np,
				struct hifmc_host *host)
{
	const struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_READ_FAST |
			SNOR_HWCAPS_READ_1_1_2 |
			SNOR_HWCAPS_READ_1_1_4 |
			SNOR_HWCAPS_PP,
	};
	struct device *dev = host->dev;
	struct spi_nor *nor;
	struct hifmc_priv *priv;
	struct mtd_info *mtd;
	int ret;

	nor = devm_kzalloc(dev, sizeof(*nor), GFP_KERNEL);
	if (!nor)
		return -ENOMEM;

	nor->dev = dev;
	spi_nor_set_flash_node(nor, np);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_property_read_u32(np, "reg", &priv->chipselect);
	if (ret) {
		dev_err(dev, "There's no reg property for %pOF\n",
			np);
		return ret;
	}

//	ret = of_property_read_u32(np, "spi-max-frequency",
//			&priv->clkrate);
//	if (ret) {
//		dev_err(dev, "There's no spi-max-frequency property for %pOF\n",
//			np);
//		return ret;
//	}
	priv->host = host;
	nor->priv = priv;

	nor->prepare = hisi_spi_hi16xx_nor_prep;
	nor->unprepare = hisi_spi_hi16xx_nor_unprep;
	nor->read_reg = hisi_spi_hi16xx_nor_read_reg;
	nor->write_reg = hisi_spi_hi16xx_nor_write_reg;
	nor->read = hisi_spi_hi16xx_nor_read;
	nor->write = hisi_spi_hi16xx_nor_write;
	nor->erase = NULL;

	pr_err("%s nor=%pS host=%pS\n", __func__, nor, host);
	
	ret = spi_nor_scan(nor, NULL, &hwcaps);
	if (ret)
		return ret;

	mtd = &nor->mtd;
	mtd->name = np->name;
	ret = mtd_device_register(mtd, NULL, 0);
	if (ret)
		return ret;

	host->nor[host->num_chip] = nor;
	host->num_chip++;
	return 0;
}

static void hisi_spi_hi16xx_nor_unregister_all(struct hifmc_host *host)
{
	int i;

	for (i = 0; i < host->num_chip; i++)
		mtd_device_unregister(&host->nor[i]->mtd);
}

static int hisi_spi_hi16xx_nor_register_all(struct hifmc_host *host)
{
	struct device *dev = host->dev;
	struct device_node *np;
	int ret;

	for_each_available_child_of_node(dev->of_node, np) {
		ret = hisi_spi_hi16xx_nor_register(np, host);
		if (ret)
			goto fail;

		if (host->num_chip == HIFMC_MAX_CHIP_NUM) {
			dev_warn(dev, "Flash device number exceeds the maximum chipselect number\n");
			break;
		}
	}

	return 0;

fail:
	hisi_spi_hi16xx_nor_unregister_all(host);
	return ret;
}

static int hisi_spi_hi16xx_nor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct hifmc_host *host;
	int ret;

	dev_err(dev, "%s\n", __func__);
	
	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	platform_set_drvdata(pdev, host);
	host->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg");
	host->regbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->regbase))
		return PTR_ERR(host->regbase);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "memory");
	host->iobase = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->iobase))
		return PTR_ERR(host->iobase);


	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_warn(dev, "Unable to set dma mask\n");
		return ret;
	}

	host->buffer = dmam_alloc_coherent(dev, HIFMC_DMA_MAX_LEN,
			&host->dma_buffer, GFP_KERNEL);
	if (!host->buffer)
		return -ENOMEM;

	mutex_init(&host->lock);
	hisi_spi_hi16xx_nor_init(host);
	ret = hisi_spi_hi16xx_nor_register_all(host);

	return ret;
}

static int hisi_spi_hi16xx_nor_remove(struct platform_device *pdev)
{
	struct hifmc_host *host = platform_get_drvdata(pdev);

	hisi_spi_hi16xx_nor_unregister_all(host);
	mutex_destroy(&host->lock);
	return 0;
}

static const struct of_device_id hisi_spi_hi16xx_nor_dt_ids[] = {
	{ .compatible = "hisilicon,sfc-hi16xx"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hisi_spi_hi16xx_nor_dt_ids);

static struct platform_driver hisi_spi_hi16xx_nor_driver = {
	.driver = {
		.name	= "hisi-sfc-hi16xx",
		.of_match_table = hisi_spi_hi16xx_nor_dt_ids,
	},
	.probe	= hisi_spi_hi16xx_nor_probe,
	.remove	= hisi_spi_hi16xx_nor_remove,
};
module_platform_driver(hisi_spi_hi16xx_nor_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("HiSilicon SPI Nor Flash Controller Driver");
