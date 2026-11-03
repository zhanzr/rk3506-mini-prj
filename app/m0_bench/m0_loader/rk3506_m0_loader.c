/*
  rk3506_m0_loader.c - load a flat firmware image into the RK3506
  Cortex-M0 SRAM and release the M0, from a running Linux.

  No DT, no rproc-core dependency, no reflash. Sequence replicates the
  vendor U-Boot fit_standalone_release() (arch/arm/mach-rockchip/rk3506/
  rk3506.c) and nvitya's rk3506_rproc driver:

    1. copy the flat image into the M0 SRAM window (0xFFF84000)
    2. SMC(SIP_MCU_CFG 0x82000028, BUSMCU_0, CODE_START_ADDR, 0xFFF84000)
       - ATF re-maps the SRAM as M0 code TCM and sets the entry point;
         after this the SRAM window is no longer A7-accessible
    3. CRU_GATE_CON5 (0xff9a0814) <- 0x0c000000  (hclk_m0 + swclktck_m0)
    4. GRF_SOC_CON36 (0xff288090) <- 0x0bcd3d80  (time calibration)
    5. PMU_INT_MASK_CON (0xff90000c) <- 0x00060004 (release the M0)

  usage: insmod rk3506_m0_loader.ko fw=/root/m0.bin
  (the M0 keeps running after rmmod; a reboot resets it)
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/err.h>
#include <linux/arm-smccc.h>
#include <linux/clk.h>

#define M0_SRAM_BUS   0xFFF84000u
#define M0_SRAM_SIZE  0x8000u

#define SIP_MCU_CFG   0x82000028u
#define SIP_BUSMCU_0  0x00u
#define SIP_CODE_START 0x01u

/* ATF/TEE diagnostics: version + secure register access */
#define SIP_ATF_VERSION     0x82000001u
#define SIP_ACCESS_REG      0x82000002u
#define SECURE_REG_RD       0x0u
#define SECURE_REG_WR       0x1u

#define CRU_BASE      0xFF9A0000u
#define CRU_GATE_CON5 (CRU_BASE + 0x814)
#define GRF_SOC_CON36 0xFF288090u
#define PMU_INT_MASK_CON 0xFF90000Cu

static char fw_path[256] = "/root/m0.bin";
module_param_string(fw, fw_path, sizeof(fw_path), 0);
MODULE_PARM_DESC(fw, "flat M0 firmware image path (default /root/m0.bin)");

static int __init m0_loader_init(void)
{
    struct arm_smccc_res res;
    void __iomem *sram, *cru, *grf, *pmu;
    struct file *f;
    loff_t pos = 0;
    void *buf;
    int ret;

    sram = ioremap(M0_SRAM_BUS, M0_SRAM_SIZE);
    cru  = ioremap(CRU_BASE & ~0xFFFu, 0x1000);
    grf  = ioremap(GRF_SOC_CON36 & ~0xFFFu, 0x1000);
    pmu  = ioremap(PMU_INT_MASK_CON & ~0xFFFu, 0x1000);
    if (!sram || !cru || !grf || !pmu)
    {
        pr_err("m0_loader: ioremap failed\n");
        ret = -ENOMEM;
        goto err_map;
    }

    /* read the flat firmware image */
    buf = kmalloc(M0_SRAM_SIZE, GFP_KERNEL);
    if (!buf)
    {
        ret = -ENOMEM;
        goto err_map;
    }
    f = filp_open(fw_path, O_RDONLY, 0);
    if (IS_ERR(f))
    {
        pr_err("m0_loader: open %s: %ld\n", fw_path, (long)PTR_ERR(f));
        kfree(buf);
        ret = -ENOENT;
        goto err_map;
    }
    ret = kernel_read(f, buf, M0_SRAM_SIZE, &pos);
    filp_close(f, NULL);
    if (ret <= 0)
    {
        pr_err("m0_loader: read %s failed: %d\n", fw_path, ret);
        kfree(buf);
        ret = -EIO;
        goto err_map;
    }
    pr_info("m0_loader: firmware %s: %d bytes\n", fw_path, ret);

    /* 0. assert the M0 reset (also re-releases cleanly at step 5) */
    {
        void __iomem *pmu0 = ioremap(PMU_INT_MASK_CON & ~0xFFFu, 0x1000);

        writel(0x00060002u, pmu0 + (PMU_INT_MASK_CON & 0xFFF));
        iounmap(pmu0);
    }

    /* 1. copy into the M0 SRAM (A7-side window) + readback verify */
    memcpy_toio(sram, buf, ret);
    kfree(buf);
    pr_info("m0_loader: SRAM readback: %08x %08x %08x %08x\n",
            readl(sram), readl(sram + 4), readl(sram + 8),
            readl(sram + 12));

    /* 2. ATF: re-map SRAM as M0 code TCM + set entry point */
    arm_smccc_smc(SIP_MCU_CFG, SIP_BUSMCU_0, SIP_CODE_START,
                  M0_SRAM_BUS, 0, 0, 0, 0, &res);
    if (res.a0)
    {
        pr_err("m0_loader: SMC failed: %ld\n", res.a0);
        ret = -EIO;
        goto err_map;
    }
    pr_info("m0_loader: SMC ok - SRAM re-mapped as M0 code TCM\n");
    pr_info("m0_loader: SMC ok - SRAM re-mapped as M0 code TCM\n");
    {
        struct arm_smccc_res rres;

        arm_smccc_smc(SIP_ACCESS_REG, 0, 0xFF9A0814u, SECURE_REG_RD,
                      0, 0, 0, 0, &rres);
        pr_info("m0_loader: secure CON5 post-SMC = %08lx\n", rres.a1);
    }

    /* 3. M0 clocks through the CCF, by name - no DT needed.
     * Raw CRU gate pokes fight the kernel clock framework (the gate
     * bit polarity is ambiguous from userspace); the CCF does it
     * right. pclk_mailbox is ungated so the M0 firmware can write
     * the result register. */
    {
        static const char *clk_names[] = {
            "hclk_m0", "stclk_m0", "pclk_mailbox"
        };
        int ci;

        for (ci = 0; ci < 3; ci++)
        {
            struct clk *c = clk_get(NULL, clk_names[ci]);

            if (IS_ERR(c))
            {
                pr_err("m0_loader: clk_get(%s): %ld\n", clk_names[ci],
                       (long)PTR_ERR(c));
                continue;
            }
            clk_prepare_enable(c);
            pr_info("m0_loader: %s enabled\n", clk_names[ci]);
        }
    }

    /* 4. m0 system time calibration */
    writel(0x0bcd3d80, grf + (GRF_SOC_CON36 & 0xFFF));
    /* 5. release: mcu_rst_dis_cfg=1, glb_int_mask_mcu=0 */
    writel(0x00060004, pmu + (PMU_INT_MASK_CON & 0xFFF));

    pr_info("m0_loader: M0 released at 0x%08x - firmware running\n",
            M0_SRAM_BUS);
    iounmap(sram);
    iounmap(cru);
    iounmap(grf);
    iounmap(pmu);
    return 0;

err_map:
    if (sram) iounmap(sram);
    if (cru)  iounmap(cru);
    if (grf)  iounmap(grf);
    if (pmu)  iounmap(pmu);
    return ret;
}

static void __exit m0_loader_exit(void)
{
    pr_info("m0_loader: unloaded (M0 left running)\n");
}

module_init(m0_loader_init);
module_exit(m0_loader_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RK3506 Cortex-M0 flat firmware loader/releaser");
