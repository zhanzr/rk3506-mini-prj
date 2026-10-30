# CPU frequency and DVFS: why the board runs at 1.2 GHz, not 1.5 GHz

Investigated 2026-10-30. Question: the HD-RK3506G-CORE datasheet claims the
3x Cortex-A7 run "up to 1.5 GHz" (section 2.1, max 1.5 GHz), but the board
runs `armclk` at a fixed **1.2 GHz**. What blocks the higher frequency?

Short answer: **a vendor device-tree gap disables the CPU core-voltage
regulator; the kernel's DVFS/cpufreq stack fails to initialize at boot and
gives up, so the clock stays at the 1.2 GHz value U-Boot left.** The chip
itself exposes OPPs up to 1.6 GHz (grade-dependent).

## Evidence chain (all verified)

### 1. Measured state

- `/sys/kernel/debug/clk/clk_summary`: `armclk` = 1200000000 Hz (fixed),
  `clk_core_pvtpll` = 1.2 GHz. No `/sys/devices/system/cpu/cpufreq` at all.
- dmesg:

```
[    5.436326] cpu cpu0: failed to set opp config
[    5.436343] cpu cpu0: failed to init opp info
[    5.436354] Failed to initialize dvfs info cpu0
```

### 2. The kernel *does* have full DVFS support

- `rk3502.dtsi` (shared RK3502/RK3506 SoC base) defines `cpu0_opp_table`
  with entries 600 M / 800 M / 1008 M / 1200 M / 1296 M / 1416 M /
  1512 M / 1608 M (per-bin voltages, `opp-supported-hw` selects by OTP
  grade), plus a separate J-grade set (see below). CPU nodes reference it
  (`operating-points-v2`), and the live FDT confirms the reference
  (phandle 6) plus `cpu-supply` on `cpu@f00`.
- `drivers/cpufreq/rockchip-cpufreq.c` + `drivers/soc/rockchip/
  rockchip_opp_select.c` are built in (the boot messages above are theirs).

### 3. Root cause: `vdd_arm` (CPU regulator) never registers

`vanxoak-hd-rk3506-evb-v1.dtsi`:

```
vdd_arm: vdd-arm {
    compatible = "pwm-regulator";
    pwms = <&pwm0_4ch_0 0 5000 1>;      /* PWM at ff930000 */
    regulator-min-microvolt = <710000>;
    regulator-max-microvolt = <1207000>;
    regulator-init-microvolt = <1011000>;
    ...
};
&cpu0 { cpu-supply = <&vdd_arm>; };
```

But the board dtsi only enables `&pwm0_4ch_2` (LCD backlight, ff932000).
**`&pwm0_4ch_0` (ff930000) is never set `status = "okay"`** - it stays
disabled from `rk3502.dtsi`. Consequences (verified live):

- `/sys/class/pwm` has only `pwmchip0` (= ff932000, backlight).
- `/sys/class/regulator` lists vcc12v_dc, vcc_sys, vcc3v3_stb, vcc_1v8,
  vcc_mipi_pwren - **no `vdd_arm`**.

`rockchip_cpufreq_cluster_init()` then fails in
`rockchip_opp_set_config()`: `dev_pm_opp_set_config()` cannot get the
"cpu" regulator -> error path `failed to set opp config` ->
`failed to init opp info` -> the driver aborts entirely
(`Failed to initialize dvfs info cpu0`). No retry, no cpufreq, no DVFS.

### 4. Why exactly 1.2 GHz

U-Boot sets up `armclk` before the kernel (u-boot `arch/arm/dts/
rk3506.dtsi` also assigns `pvtpll_core` = 1.2 GHz and carries its own OPP
table). With no kernel cpufreq, the clock stays at the boot value: 1.2 GHz,
which is also the default maximum for the RK3506**J** grade (below).

## Chip grade caveat (from the OPP table)

`rk3502.dtsi` distinguishes grades via `opp-supported-hw` (OTP readout):

- Standard grades (mask `0xf9`): 600 ... 1608 MHz. The 1512 MHz entry is
  what the datasheet's "1.5 GHz" refers to.
- **RK3506J** (mask `0x04`): default OPPs stop at **1200 MHz**. Overdrive
  entries (1296 / 1416 / 1512 MHz) exist but carry `status = "disabled"`
  with an explicit vendor comment: overdrive shortens chip lifetime,
  especially at high temperature; enable manually if you accept that.
- Thermal limit table: kernel drops to 1008 MHz above 85 C and 600 MHz
  above 100 C (`rockchip,temp-freq-table`).

Which grade this RK3506G2 module actually is (J vs normal) is fused in OTP
and not exposed to userspace on this image (`/sys/bus/nvmem` absent), so
the real ceiling can only be confirmed after DVFS works and reads the
OPP `opp-supported-hw` selection.

## What a fix would involve (not applied)

1. Add `&pwm0_4ch_0 { status = "okay"; };` to the board dts (plus pinctrl
   for the PWM pin, per `rk3506-pinctrl*.dtsi`) and rebuild the DTB.
2. Rebuild/reflash (system image work - needs the full SDK; the DTB lives
   in the boot partition of the SD image).
3. Risk: `vdd_arm` is the **CPU core rail** (710-1207 mV PWM buck). If the
   pwm-regulator probe or a cpufreq transition drives a wrong duty cycle,
   the core voltage is wrong - potential instability or worse. U-Boot
   leaves the rail at ~1011 mV (dts `regulator-init-microvolt`), so a
   *failed* kernel probe is harmless (rail untouched), but a *wrongly
   working* one is not. Do this only with serial console attached and a
   known-good SD image to fall back to.
4. Expected outcome if it works: cpufreq with the OTP-selected OPP set;
   1.296-1.512 GHz on a normal-grade chip, 1.2 GHz (or manual overdrive)
   on a J-grade chip; hot board throttles per the temp-freq table.

Until then: the board is effectively a fixed 1.2 GHz platform (see
`docs/dts_adc_notes.md` for the clock facts; CoreMark results in
`app/coremark/README.md` are at this frequency).
