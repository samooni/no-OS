/***************************************************************************//**
 *   @file   ad9361.c
 *   @brief  Implementation of AD9361 Driver.
********************************************************************************
 * Copyright 2014(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/
#include <malloc.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "ad9361.h"
#include "platform.h"
#include "util.h"

/******************************************************************************/
/********************** Macros and Constants Definitions **********************/
/******************************************************************************/
#define DEBUG

const char *ad9361_ensm_states[] = {
	"sleep", "", "", "", "", "alert", "tx", "tx flush",
	"rx", "rx_flush", "fdd", "fdd_flush"
};

/**
 * SPI multiple bytes register read.
 * @param spi
 * @param reg The register address.
 * @param rbuf The data buffer.
 * @param num The number of bytes to read.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_spi_readm(struct spi_device *spi, uint32_t reg,
	uint8_t *rbuf, uint32_t num)
{
	uint8_t buf[2];
	int32_t ret;
	uint16_t cmd;

	if (num > MAX_MBYTE_SPI)
		return -EINVAL;

	cmd = AD_READ | AD_CNT(num) | AD_ADDR(reg);
	buf[0] = cmd >> 8;
	buf[1] = cmd & 0xFF;

	ret = spi_write_then_read(spi, &buf[0], 2, rbuf, num);
	if (ret < 0) {
		dev_err(&spi->dev, "Read Error %"PRId32, ret);
		return ret;
	}
#ifdef _DEBUG
	{
		int32_t i;
		for (i = 0; i < num; i++)
			dev_dbg(&spi->dev, "%s: reg 0x%"PRIX32" val 0x%X",
			__func__, reg--, rbuf[i]);
	}
#endif

	return 0;
}

/**
 * SPI register read.
 * @param spi
 * @param reg The register address.
 * @return The register value or negative error code in case of failure.
 */
int32_t ad9361_spi_read(struct spi_device *spi, uint32_t reg)
{
	uint8_t buf;
	int32_t ret;

	ret = ad9361_spi_readm(spi, reg, &buf, 1);
	if (ret < 0)
		return ret;

	return buf;
}

/**
 * SPI register bits read.
 * @param spi
 * @param reg The register address.
 * @param mask The bits mask.
 * @param offset The mask offset.
 * @return The bits value or negative error code in case of failure.
 */
static int32_t __ad9361_spi_readf(struct spi_device *spi, uint32_t reg,
	uint32_t mask, uint32_t offset)
{
	uint8_t buf;
	int32_t ret;

	if (!mask)
		return -EINVAL;

	ret = ad9361_spi_readm(spi, reg, &buf, 1);
	if (ret < 0)
		return ret;

	buf &= mask;
	buf >>= offset;

	return buf;
}

/**
 * SPI register bits read.
 * @param spi
 * @param reg The register address.
 * @param mask The bits mask.
 * @return The bits value or negative error code in case of failure.
 */
#define ad9361_spi_readf(spi, reg, mask) \
	__ad9361_spi_readf(spi, reg, mask, __ffs(mask))

/**
 * SPI register write.
 * @param spi
 * @param reg The register address.
 * @param val The value of the register.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_spi_write(struct spi_device *spi,
	uint32_t reg, uint32_t val)
{
	uint8_t buf[3];
	int32_t ret;
	uint16_t cmd;

	cmd = AD_WRITE | AD_CNT(1) | AD_ADDR(reg);
	buf[0] = cmd >> 8;
	buf[1] = cmd & 0xFF;
	buf[2] = val;

	ret = spi_write_then_read(spi, buf, 3, NULL, 0);
	if (ret < 0) {
		dev_err(&spi->dev, "Write Error %"PRId32, ret);
		return ret;
	}

#ifdef _DEBUG
	dev_dbg(&spi->dev, "%s: reg 0x%"PRIX32" val 0x%X", __func__, reg, buf[2]);
#endif

	return 0;
}

/**
 * SPI register bits write.
 * @param spi
 * @param reg The register address.
 * @param mask The bits mask.
 * @param offset The mask offset.
 * @param val The bits value.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t __ad9361_spi_writef(struct spi_device *spi, uint32_t reg,
	uint32_t mask, uint32_t offset, uint32_t val)
{
	uint8_t buf;
	int32_t ret;

	if (!mask)
		return -EINVAL;

	ret = ad9361_spi_readm(spi, reg, &buf, 1);
	if (ret < 0)
		return ret;

	buf &= ~mask;
	buf |= ((val << offset) & mask);

	return ad9361_spi_write(spi, reg, buf);
}

/**
 * SPI register bits write.
 * @param spi
 * @param reg The register address.
 * @param mask The bits mask.
 * @param val The bits value.
 * @return 0 in case of success, negative error code otherwise.
 */
#define ad9361_spi_writef(spi, reg, mask, val) \
	__ad9361_spi_writef(spi, reg, mask, __ffs(mask), val)

/**
 * SPI multiple bytes register write.
 * @param spi
 * @param reg The register address.
 * @param tbuf The data buffer.
 * @param num The number of bytes to read.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_spi_writem(struct spi_device *spi,
	uint32_t reg, uint8_t *tbuf, uint32_t num)
{
	uint8_t buf[10];
	int32_t ret;
	uint16_t cmd;

	if (num > MAX_MBYTE_SPI)
		return -EINVAL;

	cmd = AD_WRITE | AD_CNT(num) | AD_ADDR(reg);
	buf[0] = cmd >> 8;
	buf[1] = cmd & 0xFF;

	memcpy(&buf[2], tbuf, num);

	ret = spi_write_then_read(spi, buf, num + 2, NULL, 0);
	if (ret < 0) {
		dev_err(&spi->dev, "Write Error %"PRId32, ret);
		return ret;
	}

#ifdef _DEBUG
	{
		int32_t i;
		for (i = 0; i < num; i++)
			dev_dbg(&spi->dev, "Reg 0x%"PRIX32" val 0x%X", reg--, tbuf[i]);
	}
#endif

	return 0;
}

/**
 * Find optimal value.
 * @param field
 * @param ret_start
 * @return The optimal delay in case of success, negative error code otherwise.
 */
static int32_t ad9361_find_opt(uint8_t *field, uint32_t size, uint32_t *ret_start)
{
	int32_t i, cnt = 0, max_cnt = 0, start, max_start = 0;

	for(i = 0, start = -1; i < (int64_t)size; i++) {
		if (field[i] == 0) {
			if (start == -1)
				start = i;
			cnt++;
		} else {
			if (cnt > max_cnt) {
				max_cnt = cnt;
				max_start = start;
			}
			start = -1;
			cnt = 0;
		}
	}

	if (cnt > max_cnt) {
		max_cnt = cnt;
		max_start = start;
	}

	*ret_start = max_start;

	return max_cnt;
}

/**
 * AD9361 Device Reset
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_reset(struct ad9361_rf_phy *phy)
{
	if (gpio_is_valid(phy->pdata->gpio_resetb)) {
		gpio_set_value(phy->pdata->gpio_resetb, 0);
		mdelay(1);
		gpio_set_value(phy->pdata->gpio_resetb, 1);
		mdelay(1);
		dev_dbg(&phy->spi->dev, "%s: by GPIO", __func__);
		return 0;
	}
	else {
		ad9361_spi_write(phy->spi, REG_SPI_CONF, SOFT_RESET | _SOFT_RESET); /* RESET */
		ad9361_spi_write(phy->spi, REG_SPI_CONF, 0x0);
		dev_dbg(&phy->spi->dev, "%s: by SPI", __func__);
		return 0;
	}

	return -ENODEV;
}

/**
 * HDL loopback enable/disable.
 * @param phy The AD9361 state structure.
 * @param enable Enable/disable option.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_hdl_loopback(struct ad9361_rf_phy *phy, bool enable)
{
	struct axiadc_converter *conv = phy->adc_conv;
	struct axiadc_state *st = phy->adc_state;
	int32_t reg, addr, chan;

	uint32_t version = axiadc_read(st, 0x4000);

	/* Still there but implemented a bit different */
	if (PCORE_VERSION_MAJOR(version) > 7)
		addr = 0x4418;
	else
		addr = 0x4414;

	for (chan = 0; chan < conv->chip_info->num_channels; chan++) {
		reg = axiadc_read(st, addr + (chan) * 0x40);

		if (PCORE_VERSION_MAJOR(version) > 7) {
		/* FIXME: May cause problems if DMA is selected */
			if (enable)
				reg = 0x8;
			else
				reg = 0x0;
		} else {
		/* DAC_LB_ENB If set enables loopback of receive data */
			if (enable)
				reg |= BIT(1);
			else
				reg &= ~BIT(1);
		}
		axiadc_write(st, addr + (chan) * 0x40, reg);
	}

	return 0;
}

/**
 * BIST loopback mode.
 * @param phy The AD9361 state structure.
 * @param mode BIST loopback mode.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_bist_loopback(struct ad9361_rf_phy *phy, int32_t mode)
{
	uint32_t sp_hd, reg;

	dev_dbg(&phy->spi->dev, "%s: mode %"PRId32, __func__, mode);

	reg = ad9361_spi_read(phy->spi, REG_OBSERVE_CONFIG);

	phy->bist_loopback_mode = mode;

	switch (mode) {
	case 0:
		ad9361_hdl_loopback(phy, false);
		reg &= ~(DATA_PORT_SP_HD_LOOP_TEST_OE |
			DATA_PORT_LOOP_TEST_ENABLE);
		return ad9361_spi_write(phy->spi, REG_OBSERVE_CONFIG, reg);
	case 1:
		/* loopback (AD9361 internal) TX->RX */
		ad9361_hdl_loopback(phy, false);
		sp_hd = ad9361_spi_read(phy->spi, REG_PARALLEL_PORT_CONF_3);
		if ((sp_hd & SINGLE_PORT_MODE) && (sp_hd & HALF_DUPLEX_MODE))
			reg |= DATA_PORT_SP_HD_LOOP_TEST_OE;
		else
			reg &= ~DATA_PORT_SP_HD_LOOP_TEST_OE;

		reg |= DATA_PORT_LOOP_TEST_ENABLE;

		return ad9361_spi_write(phy->spi, REG_OBSERVE_CONFIG, reg);
	case 2:
		/* loopback (FPGA internal) RX->TX */
		ad9361_hdl_loopback(phy, true);
		reg &= ~(DATA_PORT_SP_HD_LOOP_TEST_OE |
			DATA_PORT_LOOP_TEST_ENABLE);
		return ad9361_spi_write(phy->spi, REG_OBSERVE_CONFIG, reg);
	default:
		return -EINVAL;
	}
}

/**
 * Get BIST loopback mode.
 * @param phy The AD9361 state structure.
 * @param mode BIST loopback mode.
 * @return 0 in case of success, negative error code otherwise.
 */
void ad9361_get_bist_loopback(struct ad9361_rf_phy *phy, int32_t *mode)
{
	*mode = phy->bist_loopback_mode;
}

/**
 * BIST mode.
 * @param phy The AD9361 state structure.
 * @param mode Bist mode.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_bist_prbs(struct ad9361_rf_phy *phy, enum ad9361_bist_mode mode)
{
	uint32_t reg = 0;

	dev_dbg(&phy->spi->dev, "%s: mode %d", __func__, mode);

	phy->bist_prbs_mode = mode;

	switch (mode) {
	case BIST_DISABLE:
		reg = 0;
		break;
	case BIST_INJ_TX:
		reg = BIST_CTRL_POINT(0) | BIST_ENABLE;
		break;
	case BIST_INJ_RX:
		reg = BIST_CTRL_POINT(2) | BIST_ENABLE;
		break;
	};

	return ad9361_spi_write(phy->spi, REG_BIST_CONFIG, reg);
}

/**
 * Get BIST mode settings.
 * @param phy The AD9361 state structure.
 * @param mode Bist mode.
 * @return 0 in case of success, negative error code otherwise.
 */
void ad9361_get_bist_prbs(struct ad9361_rf_phy *phy, enum ad9361_bist_mode *mode)
{
	*mode = phy->bist_prbs_mode;
}

/**
 * BIST tone.
 * @param phy The AD9361 state structure.
 * @param mode Bist tone mode.
 * @param freq_Hz Bist tone frequency.
 * @param level_dB Bist tone level.
 * @param mask Bist reg mask.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_bist_tone(struct ad9361_rf_phy *phy,
						 enum ad9361_bist_mode mode, uint32_t freq_Hz,
						 uint32_t level_dB, uint32_t mask)
{
	uint32_t clk = 0;
	uint32_t reg = 0, reg1, reg_mask;

	dev_dbg(&phy->spi->dev, "%s: mode %d", __func__, mode);

	phy->bist_tone_mode = mode;
	phy->bist_tone_freq_Hz = freq_Hz;
	phy->bist_tone_level_dB = level_dB;
	phy->bist_tone_mask = mask;

	switch (mode) {
	case BIST_DISABLE:
		reg = 0;
		break;
	case BIST_INJ_TX:
		clk = clk_get_rate(phy, phy->ref_clk_scale[TX_SAMPL_CLK]);
		reg = BIST_CTRL_POINT(0) | BIST_ENABLE;
		break;
	case BIST_INJ_RX:
		clk = clk_get_rate(phy, phy->ref_clk_scale[RX_SAMPL_CLK]);
		reg = BIST_CTRL_POINT(2) | BIST_ENABLE;
		break;
	};

	reg |= TONE_PRBS;
	reg |= TONE_LEVEL(level_dB / 6);

	if (freq_Hz < 4) {
		reg |= TONE_FREQ(freq_Hz);
	}
	else {
		if (clk)
			reg |= TONE_FREQ(DIV_ROUND_CLOSEST(freq_Hz * 32, clk) - 1);
	}

	reg_mask = BIST_MASK_CHANNEL_1_I_DATA | BIST_MASK_CHANNEL_1_Q_DATA |
		BIST_MASK_CHANNEL_2_I_DATA | BIST_MASK_CHANNEL_2_Q_DATA;

	reg1 = ((mask << 2) & reg_mask);
	ad9361_spi_write(phy->spi, REG_BIST_AND_DATA_PORT_TEST_CONFIG, reg1);

	return ad9361_spi_write(phy->spi, REG_BIST_CONFIG, reg);
}

/**
 * Get BIST tone settings.
 * @param phy The AD9361 state structure.
 * @param mode Bist tone mode.
 * @param freq_Hz Bist tone frequency.
 * @param level_dB Bist tone level.
 * @param mask Bist reg mask.
 * @return 0 in case of success, negative error code otherwise.
 */
void ad9361_get_bist_tone(struct ad9361_rf_phy *phy,
						 enum ad9361_bist_mode *mode, uint32_t *freq_Hz,
						 uint32_t *level_dB, uint32_t *mask)
{
	*mode = phy->bist_tone_mode;
	*freq_Hz = phy->bist_tone_freq_Hz;
	*level_dB = phy->bist_tone_level_dB;
	*mask = phy->bist_tone_mask;
}

/**
 * Digital interface timing analysis.
 * @param phy The AD9361 state structure.
 * @param buf The buffer.
 * @param buflen The buffer length.
 * @return The size in case of success, negative error code otherwise.
 */
ssize_t ad9361_dig_interface_timing_analysis(struct ad9361_rf_phy *phy,
	char *buf, int32_t buflen)
{
	struct axiadc_state *st = phy->adc_state;
	int32_t ret, i, j, chan, len = 0;
	uint8_t field[16][16];
	uint8_t rx;

	rx = ad9361_spi_read(phy->spi, REG_RX_CLOCK_DATA_DELAY);

	ad9361_bist_prbs(phy, BIST_INJ_RX);

	for (i = 0; i < 16; i++) {
		for (j = 0; j < 16; j++) {
			ad9361_spi_write(phy->spi, REG_RX_CLOCK_DATA_DELAY,
				DATA_CLK_DELAY(j) | RX_DATA_DELAY(i));
			for (chan = 0; chan < 4; chan++)
				axiadc_write(st, ADI_REG_CHAN_STATUS(chan),
				ADI_PN_ERR | ADI_PN_OOS);

			mdelay(1);

			if (axiadc_read(st, ADI_REG_STATUS) & ADI_STATUS) {
				for (chan = 0, ret = 0; chan < 4; chan++)
					ret |= axiadc_read(st, ADI_REG_CHAN_STATUS(chan));
			}
			else {
				ret = 1;
			}

			field[i][j] = ret;
		}
	}

	ad9361_spi_write(phy->spi, REG_RX_CLOCK_DATA_DELAY, rx);

	ad9361_bist_prbs(phy, BIST_DISABLE);

	len += snprintf(buf + len, buflen, "CLK: %"PRIu32" Hz 'o' = PASS\n",
		clk_get_rate(phy, phy->ref_clk_scale[RX_SAMPL_CLK]));
	len += snprintf(buf + len, buflen, "DC");
	for (i = 0; i < 16; i++)
		len += snprintf(buf + len, buflen, "%"PRIx32":", i);
	len += snprintf(buf + len, buflen, "\n");

	for (i = 0; i < 16; i++) {
		len += snprintf(buf + len, buflen, "%"PRIx32":", i);
		for (j = 0; j < 16; j++) {
			len += snprintf(buf + len, buflen, "%c ",
				(field[i][j] ? '.' : 'o'));
		}
		len += snprintf(buf + len, buflen, "\n");
	}
	len += snprintf(buf + len, buflen, "\n");

	return len;
}

/**
 * Check the calibration done bit.
 * @param phy The AD9361 state structure.
 * @param reg The register address.
 * @param mask The bit mask.
 * @param done_state The done state [0,1].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_check_cal_done(struct ad9361_rf_phy *phy, uint32_t reg,
	uint32_t mask, bool done_state)
{
	uint32_t timeout = 5000; /* RFDC_CAL can take long */
	uint32_t state;

	do {
		state = ad9361_spi_readf(phy->spi, reg, mask);
		if (state == done_state)
			return 0;

		if (reg == REG_CALIBRATION_CTRL)
			udelay(1200);
		else
			udelay(120);
	} while (timeout--);

	dev_err(&phy->spi->dev, "Calibration TIMEOUT (0x%"PRIX32", 0x%"PRIX32")", reg, mask);

	return -ETIMEDOUT;
}

/**
 * Run an AD9361 calibration and check the calibration done bit.
 * @param phy The AD9361 state structure.
 * @param mask The calibration bit mask[RX_BB_TUNE_CAL, TX_BB_TUNE_CAL,
 *             RX_QUAD_CAL, TX_QUAD_CAL, RX_GAIN_STEP_CAL, TXMON_CAL,
 *             RFDC_CAL, BBDC_CAL].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_run_calibration(struct ad9361_rf_phy *phy, uint32_t mask)
{
	int32_t ret = ad9361_spi_write(phy->spi, REG_CALIBRATION_CTRL, mask);
	if (ret < 0)
		return ret;

	dev_dbg(&phy->spi->dev, "%s: CAL Mask 0x%"PRIx32, __func__, mask);

	return ad9361_check_cal_done(phy, REG_CALIBRATION_CTRL, mask, 0);
}

/**
 * Choose the right RX gain table index for the selected frequency.
 * @param freq The frequency value [Hz].
 * @return The index to the RX gain table.
 */
static enum rx_gain_table_name ad9361_gt_tableindex(uint64_t freq)
{
	if (freq <= 1300000000ULL)
		return TBL_200_1300_MHZ;

	if (freq <= 4000000000ULL)
		return TBL_1300_4000_MHZ;

	return TBL_4000_6000_MHZ;
}

/**
 * Shift the real frequency value, so it fits type unsigned long
 * Note: PLL operates between 47 .. 6000 MHz which is > 2^32.
 * @param freq The frequency value [Hz].
 * @return The shifted frequency value.
 */
uint32_t ad9361_to_clk(uint64_t freq)
{
	return (uint32_t)(freq >> 1);
}

/**
 * Shift back the frequency value, so it reflects the real value.
 * Note: PLL operates between 47 .. 6000 MHz which is > 2^32.
 * @param freq The frequency value [Hz].
 * @return The shifted frequency value.
 */
uint64_t ad9361_from_clk(uint32_t freq)
{
	return ((uint64_t)freq << 1);
}

/**
 * Load the gain table for the selected frequency range and receiver.
 * @param phy The AD9361 state structure.
 * @param freq The frequency value [Hz].
 * @param dest The destination [GT_RX1, GT_RX2].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_load_gt(struct ad9361_rf_phy *phy, uint64_t freq, uint32_t dest)
{
	struct spi_device *spi = phy->spi;
	const uint8_t(*tab)[3];
	enum rx_gain_table_name band;
	uint32_t index_max, i;

	dev_dbg(&phy->spi->dev, "%s: frequency %llu", __func__, freq);

	band = ad9361_gt_tableindex(freq);

	dev_dbg(&phy->spi->dev, "%s: frequency %llu (band %d)",
		__func__, freq, band);

	/* check if table is present */
	if (phy->current_table == band)
		return 0;

	ad9361_spi_writef(spi, REG_AGC_CONFIG_2,
		AGC_USE_FULL_GAIN_TABLE, !phy->pdata->split_gt);

	if (phy->pdata->split_gt) {
		tab = &split_gain_table[band][0];
		index_max = SIZE_SPLIT_TABLE;
	}
	else {
		tab = &full_gain_table[band][0];
		index_max = SIZE_FULL_TABLE;
	}

	ad9361_spi_write(spi, REG_GAIN_TABLE_CONFIG, START_GAIN_TABLE_CLOCK |
		RECEIVER_SELECT(dest)); /* Start Gain Table Clock */

	for (i = 0; i < index_max; i++) {
		ad9361_spi_write(spi, REG_GAIN_TABLE_ADDRESS, i); /* Gain Table Index */
		ad9361_spi_write(spi, REG_GAIN_TABLE_WRITE_DATA1, tab[i][0]); /* Ext LNA, Int LNA, & Mixer Gain Word */
		ad9361_spi_write(spi, REG_GAIN_TABLE_WRITE_DATA2, tab[i][1]); /* TIA & LPF Word */
		ad9361_spi_write(spi, REG_GAIN_TABLE_WRITE_DATA3, tab[i][2]); /* DC Cal bit & Dig Gain Word */
		ad9361_spi_write(spi, REG_GAIN_TABLE_CONFIG,
			START_GAIN_TABLE_CLOCK |
			WRITE_GAIN_TABLE |
			RECEIVER_SELECT(dest)); /* Gain Table Index */
		ad9361_spi_write(spi, REG_GAIN_TABLE_READ_DATA1, 0); /* Dummy Write to delay 3 ADCCLK/16 cycles */
		ad9361_spi_write(spi, REG_GAIN_TABLE_READ_DATA1, 0); /* Dummy Write to delay ~1u */
	}

	ad9361_spi_write(spi, REG_GAIN_TABLE_CONFIG, START_GAIN_TABLE_CLOCK |
		RECEIVER_SELECT(dest)); /* Clear Write Bit */
	ad9361_spi_write(spi, REG_GAIN_TABLE_READ_DATA1, 0); /* Dummy Write to delay ~1u */
	ad9361_spi_write(spi, REG_GAIN_TABLE_READ_DATA1, 0); /* Dummy Write to delay ~1u */
	ad9361_spi_write(spi, REG_GAIN_TABLE_CONFIG, 0); /* Stop Gain Table Clock */

	phy->current_table = band;

	return 0;
}

/**
 * Setup the external low-noise amplifier (LNA).
 * @param phy The AD9361 state structure.
 * @param ctrl Pointer to eLNA control structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_setup_ext_lna(struct ad9361_rf_phy *phy,
struct elna_control *ctrl)
{
	ad9361_spi_writef(phy->spi, REG_EXTERNAL_LNA_CTRL, EXTERNAL_LNA1_CTRL,
		ctrl->elna_1_control_en);

	ad9361_spi_writef(phy->spi, REG_EXTERNAL_LNA_CTRL, EXTERNAL_LNA2_CTRL,
		ctrl->elna_2_control_en);

	ad9361_spi_write(phy->spi, REG_EXT_LNA_HIGH_GAIN,
		EXT_LNA_HIGH_GAIN(ctrl->gain_mdB / 500));

	return ad9361_spi_write(phy->spi, REG_EXT_LNA_LOW_GAIN,
		EXT_LNA_LOW_GAIN(ctrl->bypass_loss_mdB / 500));
}

/**
 * Set the clock output mode.
 * @param phy The AD9361 state structure.
 * @param mode The clock output mode [].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_clkout_control(struct ad9361_rf_phy *phy,
enum ad9361_clkout mode)
{
	if (mode == CLKOUT_DISABLE)
		return ad9361_spi_writef(phy->spi, REG_BBPLL, CLKOUT_ENABLE, 0);

	return ad9361_spi_writef(phy->spi, REG_BBPLL,
		CLKOUT_ENABLE | CLKOUT_SELECT(~0),
		((mode - 1) << 1) | 0x1);
}

/**
 * Load the Gm Sub Table.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_load_mixer_gm_subtable(struct ad9361_rf_phy *phy)
{
	int32_t i, addr;
	dev_dbg(&phy->spi->dev, "%s", __func__);

	ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_CONFIG,
		START_GM_SUB_TABLE_CLOCK); /* Start Clock */

	for (i = 0, addr = ARRAY_SIZE(gm_st_ctrl); i < (int64_t)ARRAY_SIZE(gm_st_ctrl); i++) {
		ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_ADDRESS, --addr); /* Gain Table Index */
		ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_BIAS_WRITE, 0); /* Bias */
		ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_GAIN_WRITE, gm_st_gain[i]); /* Gain */
		ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_CTRL_WRITE, gm_st_ctrl[i]); /* Control */
		ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_CONFIG,
			WRITE_GM_SUB_TABLE | START_GM_SUB_TABLE_CLOCK); /* Write Words */
		ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_GAIN_READ, 0); /* Dummy Delay */
		ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_GAIN_READ, 0); /* Dummy Delay */
	}

	ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_CONFIG, START_GM_SUB_TABLE_CLOCK); /* Clear Write */
	ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_GAIN_READ, 0); /* Dummy Delay */
	ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_GAIN_READ, 0); /* Dummy Delay */
	ad9361_spi_write(phy->spi, REG_GM_SUB_TABLE_CONFIG, 0); /* Stop Clock */

	return 0;
}

/**
 * Set the attenuation for the selected TX channels.
 * @param phy The AD9361 state structure.
 * @param atten_mdb Attenuation value [mdB].
 * @param tx1 Set true, the attenuation of the TX1 will be affected.
 * @param tx2 Set true, the attenuation of the TX2 will be affected.
 * @param immed Set true, an immediate update will take place.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_set_tx_atten(struct ad9361_rf_phy *phy, uint32_t atten_mdb,
	bool tx1, bool tx2, bool immed)
{
	uint8_t buf[2];
	int32_t ret = 0;

	dev_dbg(&phy->spi->dev, "%s : attenuation %"PRIu32" mdB tx1=%d tx2=%d",
		__func__, atten_mdb, tx1, tx2);

	if (atten_mdb > 89750) /* 89.75 dB */
		return -EINVAL;

	atten_mdb /= 250; /* Scale to 0.25dB / LSB */

	buf[0] = atten_mdb >> 8;
	buf[1] = atten_mdb & 0xFF;

	ad9361_spi_writef(phy->spi, REG_TX2_DIG_ATTEN,
		IMMEDIATELY_UPDATE_TPC_ATTEN, 0);

	if (tx1)
		ret = ad9361_spi_writem(phy->spi, REG_TX1_ATTEN_1, buf, 2);

	if (tx2)
		ret = ad9361_spi_writem(phy->spi, REG_TX2_ATTEN_1, buf, 2);

	if (immed)
		ad9361_spi_writef(phy->spi, REG_TX2_DIG_ATTEN,
		IMMEDIATELY_UPDATE_TPC_ATTEN, 1);

	return ret;
}

/**
 * Get the attenuation for the selected TX channel.
 * @param phy The AD9361 state structure.
 * @param tx_num The selected channel [1, 2].
 * @return The attenuation value [mdB] or negative error code in case of failure.
 */
int32_t ad9361_get_tx_atten(struct ad9361_rf_phy *phy, uint32_t tx_num)
{
	uint8_t buf[2];
	int32_t ret = 0;
	uint32_t code;

	ret = ad9361_spi_readm(phy->spi, (tx_num == 1) ?
	REG_TX1_ATTEN_1 : REG_TX2_ATTEN_1, buf, 2);

	if (ret < 0)
		return ret;

	code = (buf[0] << 8) | buf[1];

	code *= 250;

	return code;
}

/**
 * Choose the right RF VCO table index for the selected frequency.
 * @param freq The frequency value [Hz].
 * @return The index from the RF VCO table.
 */
static uint32_t ad9361_rfvco_tableindex(uint32_t freq)
{
	if (freq < 50000000UL)
		return LUT_FTDD_40;

	if (freq <= 70000000UL)
		return LUT_FTDD_60;

	return LUT_FTDD_80;
}

/**
 * Initialize the RFPLL VCO.
 * @param phy The AD9361 state structure.
 * @param tx Set true for TX_RFPLL.
 * @param vco_freq The VCO frequency [Hz].
 * @param ref_clk The reference clock frequency [Hz].
 * @return 0 in case of success
 */
static int32_t ad9361_rfpll_vco_init(struct ad9361_rf_phy *phy,
	bool tx, uint64_t vco_freq,
	uint32_t ref_clk)
{
	struct spi_device *spi = phy->spi;
	const struct SynthLUT(*tab);
	int32_t i = 0;
	uint32_t range, offs = 0;

	range = ad9361_rfvco_tableindex(ref_clk);

	dev_dbg(&phy->spi->dev, "%s : vco_freq %llu : ref_clk %"PRIu32" : range %"PRIu32,
		__func__, vco_freq, ref_clk, range);

	do_div(&vco_freq, 1000000UL); /* vco_freq in MHz */

	if (phy->pdata->fdd || phy->pdata->tdd_use_fdd_tables) {
		tab = &SynthLUT_FDD[range][0];
	}
	else {
		tab = &SynthLUT_TDD[range][0];
	}

	if (tx)
		offs = REG_TX_VCO_OUTPUT - REG_RX_VCO_OUTPUT;

	while (i < SYNTH_LUT_SIZE && tab[i].VCO_MHz > vco_freq)
		i++;

	dev_dbg(&phy->spi->dev, "%s : freq %d MHz : index %"PRId32,
		__func__, tab[i].VCO_MHz, i);

	ad9361_spi_write(spi, REG_RX_VCO_OUTPUT + offs,
		VCO_OUTPUT_LEVEL(tab[i].VCO_Output_Level) |
		PORB_VCO_LOGIC);
	ad9361_spi_writef(spi, REG_RX_ALC_VARACTOR + offs,
		VCO_VARACTOR(~0), tab[i].VCO_Varactor);
	ad9361_spi_write(spi, REG_RX_VCO_BIAS_1 + offs,
		VCO_BIAS_REF(tab[i].VCO_Bias_Ref) |
		VCO_BIAS_TCF(tab[i].VCO_Bias_Tcf));

	ad9361_spi_write(spi, REG_RX_FORCE_VCO_TUNE_1 + offs,
		VCO_CAL_OFFSET(tab[i].VCO_Cal_Offset));
	ad9361_spi_write(spi, REG_RX_VCO_VARACTOR_CTRL_1 + offs,
		VCO_VARACTOR_REFERENCE(
		tab[i].VCO_Varactor_Reference));

	ad9361_spi_write(spi, REG_RX_VCO_CAL_REF + offs, VCO_CAL_REF_TCF(0));

	ad9361_spi_write(spi, REG_RX_VCO_VARACTOR_CTRL_0 + offs,
		VCO_VARACTOR_OFFSET(0) |
		VCO_VARACTOR_REFERENCE_TCF(7));

	ad9361_spi_writef(spi, REG_RX_CP_CURRENT + offs, CHARGE_PUMP_CURRENT(~0),
		tab[i].Charge_Pump_Current);
	ad9361_spi_write(spi, REG_RX_LOOP_FILTER_1 + offs,
		LOOP_FILTER_C2(tab[i].LF_C2) |
		LOOP_FILTER_C1(tab[i].LF_C1));
	ad9361_spi_write(spi, REG_RX_LOOP_FILTER_2 + offs,
		LOOP_FILTER_R1(tab[i].LF_R1) |
		LOOP_FILTER_C3(tab[i].LF_C3));
	ad9361_spi_write(spi, REG_RX_LOOP_FILTER_3 + offs,
		LOOP_FILTER_R3(tab[i].LF_R3));

	return 0;
}

/**
 * Get the current gain in Split Gain Table Mode
 * @param phy The AD9361 state structure.
 * @param idx_reg Register base address for the selected receiver
 * @param rx_gain  A rf_rx_gain struct to store the RF gain.
 * @return 0 in case of success,
 */
static int32_t ad9361_get_split_table_gain(struct ad9361_rf_phy *phy, uint32_t idx_reg,
struct rf_rx_gain *rx_gain)
{
	struct spi_device *spi = phy->spi;
	uint32_t val, tbl_addr;
	int32_t rc = 0;

	rx_gain->fgt_lmt_index = ad9361_spi_readf(spi, idx_reg,
		FULL_TABLE_GAIN_INDEX(~0));
	tbl_addr = ad9361_spi_read(spi, REG_GAIN_TABLE_ADDRESS);

	ad9361_spi_write(spi, REG_GAIN_TABLE_ADDRESS, rx_gain->fgt_lmt_index);

	val = ad9361_spi_read(spi, REG_GAIN_TABLE_READ_DATA1);
	rx_gain->lna_index = TO_LNA_GAIN(val);
	rx_gain->mixer_index = TO_MIXER_GM_GAIN(val);

	rx_gain->tia_index = ad9361_spi_readf(spi, REG_GAIN_TABLE_READ_DATA2, TIA_GAIN);

	rx_gain->lmt_gain = lna_table[rx_gain->lna_index] +
		mixer_table[rx_gain->mixer_index] +
		tia_table[rx_gain->tia_index];

	ad9361_spi_write(spi, REG_GAIN_TABLE_ADDRESS, tbl_addr);

	/* Read LPF Index */
	rx_gain->lpf_gain = ad9361_spi_readf(spi, idx_reg + 1, LPF_GAIN_RX(~0));

	/* Read Digital Gain */
	rx_gain->digital_gain = ad9361_spi_readf(spi, idx_reg + 2,
		DIGITAL_GAIN_RX(~0));

	rx_gain->gain_db = rx_gain->lmt_gain + rx_gain->lpf_gain +
		rx_gain->digital_gain;
	return rc;
}

/**
 * Get the current gain in Full Gain Table Mode
 * @param phy The AD9361 state structure.
 * @param idx_reg Register base address for the selected receiver
 * @param rx_gain A rf_rx_gain struct to store the RF gain.
 * @return 0 in case of success
 */
static int32_t ad9361_get_full_table_gain(struct ad9361_rf_phy *phy, uint32_t idx_reg,
struct rf_rx_gain *rx_gain)
{
	struct spi_device *spi = phy->spi;
	int32_t val;
	enum rx_gain_table_name tbl;
	struct rx_gain_info *gain_info;
	int32_t rc = 0, rx_gain_db;

	tbl = ad9361_gt_tableindex(
		ad9361_from_clk(clk_get_rate(phy, phy->ref_clk_scale[RX_RFPLL])));

	rx_gain->fgt_lmt_index = val = ad9361_spi_readf(spi, idx_reg,
		FULL_TABLE_GAIN_INDEX(~0));
	gain_info = &phy->rx_gain[tbl];
	if (val > gain_info->idx_step_offset) {
		val = val - gain_info->idx_step_offset;
		rx_gain_db = gain_info->starting_gain_db +
			((val)* gain_info->gain_step_db);
	}
	else {
		rx_gain_db = gain_info->starting_gain_db;
	}

	/* Read Digital Gain */
	rx_gain->digital_gain = ad9361_spi_readf(spi, idx_reg + 2,
		DIGITAL_GAIN_RX(~0));

	rx_gain->gain_db = rx_gain_db;

	return rc;
}

/**
 * Get current RX gain for the selected channel.
 * @param phy The AD9361 state structure.
 * @param rx_id The desired channel number (0, 1).
 * @param rx_gain A rf_rx_gain struct to store the RF gain.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_get_rx_gain(struct ad9361_rf_phy *phy,
	uint32_t rx_id, struct rf_rx_gain *rx_gain)
{
	struct spi_device *spi = phy->spi;
	uint32_t val, idx_reg;
	uint8_t gain_ctl_shift, rx_enable_mask;
	uint8_t fast_atk_shift;
	int32_t rc = 0;

	if (rx_id == 1) {
		gain_ctl_shift = RX1_GAIN_CTRL_SHIFT;
		idx_reg = REG_GAIN_RX1;
		rx_enable_mask = RX_CHANNEL_ENABLE(RX_1);
		fast_atk_shift = RX1_FAST_ATK_SHIFT;

	}
	else if (rx_id == 2) {
		gain_ctl_shift = RX2_GAIN_CTRL_SHIFT;
		idx_reg = REG_GAIN_RX2;
		rx_enable_mask = RX_CHANNEL_ENABLE(RX_2);
		fast_atk_shift = RX2_FAST_ATK_SHIFT;
	}
	else {
		dev_err(dev, "Unknown Rx path %"PRIu32, rx_id);
		rc = -EINVAL;
		goto out;
	}

	val = ad9361_spi_readf(spi, REG_RX_ENABLE_FILTER_CTRL, rx_enable_mask);

	if (!val) {
		dev_dbg(dev, "Rx%"PRIu32" is not enabled", rx_gain->ant);
		rc = -EAGAIN;
		goto out;
	}

	val = ad9361_spi_read(spi, REG_AGC_CONFIG_1);

	val = (val >> gain_ctl_shift) & RX_GAIN_CTL_MASK;

	if (val == RX_GAIN_CTL_AGC_FAST_ATK) {
		/* In fast attack mode check whether Fast attack state machine
		* has locked gain, if not then we can not read gain.
		*/
		val = ad9361_spi_read(spi, REG_FAST_ATTACK_STATE);
		val = (val >> fast_atk_shift) & FAST_ATK_MASK;
		if (val != FAST_ATK_GAIN_LOCKED) {
			dev_warn(dev, "Failed to read gain, state m/c at %"PRIx32,
				val);
			rc = -EAGAIN;
			goto out;
		}
	}

	if (phy->pdata->split_gt)
		rc = ad9361_get_split_table_gain(phy, idx_reg, rx_gain);
	else
		rc = ad9361_get_full_table_gain(phy, idx_reg, rx_gain);

out:
	return rc;
}

/**
 * Force Enable State Machine (ENSM) to the desired state (internally used only).
 * @param phy The AD9361 state structure.
 * @param ensm_state The ENSM state [ENSM_STATE_SLEEP_WAIT, ENSM_STATE_ALERT,
 *                   ENSM_STATE_TX, ENSM_STATE_TX_FLUSH, ENSM_STATE_RX,
 *                   ENSM_STATE_RX_FLUSH, ENSM_STATE_FDD, ENSM_STATE_FDD_FLUSH].
 * @return None.
 */
void ad9361_ensm_force_state(struct ad9361_rf_phy *phy, uint8_t ensm_state)
{
	struct spi_device *spi = phy->spi;
	uint8_t dev_ensm_state;
	int32_t rc;
	uint32_t val;

	dev_ensm_state = ad9361_spi_readf(spi, REG_STATE, ENSM_STATE(~0));

	phy->prev_ensm_state = dev_ensm_state;

	if (dev_ensm_state == ensm_state) {
		dev_dbg(dev, "Nothing to do, device is already in %d state",
			ensm_state);
		goto out;
	}

	dev_dbg(dev, "Device is in %x state, forcing to %x", dev_ensm_state,
		ensm_state);

	val = ad9361_spi_read(spi, REG_ENSM_CONFIG_1);

	/* Enable control through SPI writes, and take out from
	* Alert
	*/
	if (val & ENABLE_ENSM_PIN_CTRL) {
		val &= ~ENABLE_ENSM_PIN_CTRL;
		phy->ensm_pin_ctl_en = true;
	}
	else {
		phy->ensm_pin_ctl_en = false;
	}

	if (dev_ensm_state)
		val &= ~(TO_ALERT);

	switch (ensm_state) {

	case ENSM_STATE_TX:
		val |= FORCE_TX_ON;
		break;
	case ENSM_STATE_RX:
		val |= FORCE_RX_ON;
		break;
	case ENSM_STATE_FDD:
		val |= (FORCE_TX_ON | FORCE_RX_ON);
		break;
	case ENSM_STATE_ALERT:
		val &= ~(FORCE_TX_ON | FORCE_RX_ON);
		val |= TO_ALERT | FORCE_ALERT_STATE;
		break;
	default:
		dev_err(dev, "No handling for forcing %d ensm state",
			ensm_state);
		goto out;
	}

	ad9361_spi_write(spi, REG_ENSM_CONFIG_1, TO_ALERT | FORCE_ALERT_STATE);

	rc = ad9361_spi_write(spi, REG_ENSM_CONFIG_1, val);
	if (rc)
		dev_err(dev, "Failed to restore state");

out:
	return;

}

/**
 * Restore the previous Enable State Machine (ENSM) state.
 * @param phy The AD9361 state structure.
 * @return None.
 */
static void ad9361_ensm_restore_prev_state(struct ad9361_rf_phy *phy)
{
	struct spi_device *spi = phy->spi;
	int32_t rc;
	uint32_t val;

	val = ad9361_spi_read(spi, REG_ENSM_CONFIG_1);

	/* We are restoring state only, so clear State bits first
	* which might have set while forcing a particular state
	*/
	val &= ~(FORCE_TX_ON | FORCE_RX_ON |
		TO_ALERT | FORCE_ALERT_STATE);

	switch (phy->prev_ensm_state) {

	case ENSM_STATE_TX:
		val |= FORCE_TX_ON;
		break;
	case ENSM_STATE_RX:
		val |= FORCE_RX_ON;
		break;
	case ENSM_STATE_FDD:
		val |= (FORCE_TX_ON | FORCE_RX_ON);
		break;
	case ENSM_STATE_ALERT:
		val |= TO_ALERT;
		break;
	case ENSM_STATE_INVALID:
		dev_dbg(dev, "No need to restore, ENSM state wasn't saved");
		goto out;
	default:
		dev_dbg(dev, "Could not restore to %d ENSM state",
			phy->prev_ensm_state);
		goto out;
	}

	ad9361_spi_write(spi, REG_ENSM_CONFIG_1, TO_ALERT | FORCE_ALERT_STATE);

	rc = ad9361_spi_write(spi, REG_ENSM_CONFIG_1, val);
	if (rc) {
		dev_err(dev, "Failed to write ENSM_CONFIG_1");
		goto out;
	}

	if (phy->ensm_pin_ctl_en) {
		val |= ENABLE_ENSM_PIN_CTRL;
		rc = ad9361_spi_write(spi, REG_ENSM_CONFIG_1, val);
		if (rc)
			dev_err(dev, "Failed to write ENSM_CONFIG_1");
	}

out:
	return;
}

/**
 * Set gain in Split Gain Table Mode (used only in Manual Gain Control Mode).
 * @param phy The AD9361 state structure.
 * @param idx_reg Register base address for the selected receiver
 * @param rx_gain The rf_rx_gain struct containing the RF gain.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t set_split_table_gain(struct ad9361_rf_phy *phy, uint32_t idx_reg,
struct rf_rx_gain *rx_gain)
{
	struct spi_device *spi = phy->spi;
	int32_t rc = 0;

	if ((rx_gain->fgt_lmt_index > MAX_LMT_INDEX) ||
		(rx_gain->lpf_gain > MAX_LPF_GAIN) ||
		(rx_gain->digital_gain > MAX_DIG_GAIN)) {
		dev_err(dev, "LMT_INDEX missing or greater than max value %d",
			MAX_LMT_INDEX);
		dev_err(dev, "LPF_GAIN missing or greater than max value %d",
			MAX_LPF_GAIN);
		dev_err(dev, "DIGITAL_GAIN cannot be more than %d",
			MAX_DIG_GAIN);
		rc = -EINVAL;
		goto out;
	}
	if (rx_gain->gain_db > 0)
		dev_dbg(dev, "Ignoring rx_gain value in split table mode.");
	if (rx_gain->fgt_lmt_index == 0 && rx_gain->lpf_gain == 0 &&
		rx_gain->digital_gain == 0) {
		dev_err(dev,
			"In split table mode, All LMT/LPF/digital gains cannot be 0");
		rc = -EINVAL;
		goto out;
	}

	ad9361_spi_writef(spi, idx_reg, RX_FULL_TBL_IDX_MASK, rx_gain->fgt_lmt_index);
	ad9361_spi_writef(spi, idx_reg + 1, RX_LPF_IDX_MASK, rx_gain->lpf_gain);

	if (phy->pdata->gain_ctrl.dig_gain_en) {
		ad9361_spi_writef(spi, idx_reg + 2, RX_DIGITAL_IDX_MASK, rx_gain->digital_gain);

	}
	else if (rx_gain->digital_gain > 0) {
		dev_err(dev, "Digital gain is disabled and cannot be set");
	}
out:
	return rc;
}

/**
 * Set gain in Full Gain Table Mode (used only in Manual Gain Control Mode).
 * @param phy The AD9361 state structure.
 * @param idx_reg Register base address for the selected receiver
 * @param rx_gain The rf_rx_gain struct containing the RF gain.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t set_full_table_gain(struct ad9361_rf_phy *phy, uint32_t idx_reg,
struct rf_rx_gain *rx_gain)
{
	struct spi_device *spi = phy->spi;
	enum rx_gain_table_name tbl;
	struct rx_gain_info *gain_info;
	uint32_t val;
	int32_t rc = 0;

	if (rx_gain->fgt_lmt_index != (uint32_t)~0 || (int64_t)rx_gain->lpf_gain != (uint32_t)~0 ||
		rx_gain->digital_gain > 0)
		dev_dbg(dev,
		"Ignoring lmt/lpf/digital gains in Single Table mode");

	tbl = ad9361_gt_tableindex(
		ad9361_from_clk(clk_get_rate(phy, phy->ref_clk_scale[RX_RFPLL])));

	gain_info = &phy->rx_gain[tbl];
	if ((rx_gain->gain_db < gain_info->starting_gain_db) ||
		(rx_gain->gain_db > gain_info->max_gain_db)) {

		dev_err(dev, "Invalid gain %"PRId32", supported range [%"PRId32" - %"PRId32"]",
			rx_gain->gain_db, gain_info->starting_gain_db,
			gain_info->max_gain_db);
		rc = -EINVAL;
		goto out;

	}

	val = ((rx_gain->gain_db - gain_info->starting_gain_db) /
		gain_info->gain_step_db) + gain_info->idx_step_offset;
	ad9361_spi_writef(spi, idx_reg, RX_FULL_TBL_IDX_MASK, val);

out:
	return rc;
}

/**
 * Set the RX gain for the selected channel.
 * @param phy The AD9361 state structure.
 * @param rx_id The desired channel number (0, 1).
 * @param rx_gain The rf_rx_gain struct containing the RF gain.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_set_rx_gain(struct ad9361_rf_phy *phy,
	uint32_t rx_id, struct rf_rx_gain *rx_gain)
{
	struct spi_device *spi = phy->spi;
	uint32_t val, idx_reg;
	uint8_t gain_ctl_shift;
	int32_t rc = 0;

	if (rx_id == 1) {
		gain_ctl_shift = RX1_GAIN_CTRL_SHIFT;
		idx_reg = REG_RX1_MANUAL_LMT_FULL_GAIN;

	}
	else if (rx_id == 2) {
		gain_ctl_shift = RX2_GAIN_CTRL_SHIFT;
		idx_reg = REG_RX2_MANUAL_LMT_FULL_GAIN;
	}
	else {
		dev_err(dev, "Unknown Rx path %"PRIu32, rx_id);
		rc = -EINVAL;
		goto out;

	}

	val = ad9361_spi_read(spi, REG_AGC_CONFIG_1);
	val = (val >> gain_ctl_shift) & RX_GAIN_CTL_MASK;

	if (val != RX_GAIN_CTL_MGC) {
		dev_dbg(dev, "Rx gain can be set in MGC mode only");
		goto out;
	}

	if (phy->pdata->split_gt)
		rc = set_split_table_gain(phy, idx_reg, rx_gain);
	else
		rc = set_full_table_gain(phy, idx_reg, rx_gain);

	if (rc) {
		dev_err(dev, "Unable to write gain tbl idx reg: %"PRIu32, idx_reg);
		goto out;
	}

out:
	return rc;

}

/**
 * Initialize the rx_gain_info structure.
 * @param rx_gain The rx_gain_info structure pointer.
 * @param type Either Full or Split Table
 * @param starting_gain The starting gain value.
 * @param max_gain The maximum gain value.
 * @param gain_step The gain step.
 * @param max_idx The max table size.
 * @param idx_offset Offset in the table where linear progression starts
 * @return None
 */
static void ad9361_init_gain_info(struct rx_gain_info *rx_gain,
enum rx_gain_table_type type, int32_t starting_gain,
	int32_t max_gain, int32_t gain_step, int32_t max_idx, int32_t idx_offset)
{
	rx_gain->tbl_type = type;
	rx_gain->starting_gain_db = starting_gain;
	rx_gain->max_gain_db = max_gain;
	rx_gain->gain_step_db = gain_step;
	rx_gain->max_idx = max_idx;
	rx_gain->idx_step_offset = idx_offset;
}

/**
 * Initialize the gain table information.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_init_gain_tables(struct ad9361_rf_phy *phy)
{
	struct rx_gain_info *rx_gain;

	/* Intialize Meta data according to default gain tables
	* of AD9631. Changing/Writing of gain tables is not
	* supported yet.
	*/
	rx_gain = &phy->rx_gain[TBL_200_1300_MHZ];
	ad9361_init_gain_info(rx_gain, RXGAIN_FULL_TBL, 1, 77, 1,
		SIZE_FULL_TABLE, 0);

	rx_gain = &phy->rx_gain[TBL_1300_4000_MHZ];
	ad9361_init_gain_info(rx_gain, RXGAIN_FULL_TBL, -4, 71, 1,
		SIZE_FULL_TABLE, 1);

	rx_gain = &phy->rx_gain[TBL_4000_6000_MHZ];
	ad9361_init_gain_info(rx_gain, RXGAIN_FULL_TBL, -10, 62, 1,
		SIZE_FULL_TABLE, 4);

	return 0;
}

/**
 * Enable/disable the desired TX channel.
 * @param phy The AD9361 state structure.
 * @param tx_if The desired channel number [1, 2].
 * @param enable Enable/disable option.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_en_dis_tx(struct ad9361_rf_phy *phy, uint32_t tx_if, uint32_t enable)
{
	if (tx_if == 2 && !phy->pdata->rx2tx2 && enable)
		return -EINVAL;

	return ad9361_spi_writef(phy->spi, REG_TX_ENABLE_FILTER_CTRL,
		TX_CHANNEL_ENABLE(tx_if), enable);
}

/**
 * Enable/disable the desired RX channel.
 * @param phy The AD9361 state structure.
 * @param tx_if The desired channel number [1, 2].
 * @param enable Enable/disable option.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_en_dis_rx(struct ad9361_rf_phy *phy, uint32_t rx_if, uint32_t enable)
{
	if (rx_if == 2 && !phy->pdata->rx2tx2 && enable)
		return -EINVAL;

	return ad9361_spi_writef(phy->spi, REG_RX_ENABLE_FILTER_CTRL,
		RX_CHANNEL_ENABLE(rx_if), enable);
}

/**
 * Update the Gain Control.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_gc_update(struct ad9361_rf_phy *phy)
{
	struct spi_device *spi = phy->spi;
	uint32_t clkrf;
	uint32_t reg, delay_lna, settling_delay, dec_pow_meas_dur;
	int32_t ret;

	clkrf = clk_get_rate(phy, phy->ref_clk_scale[CLKRF_CLK]);
	delay_lna = phy->pdata->elna_ctrl.settling_delay_ns;

	/*
	 * AGC Attack Delay (us)=ceiling((((0.2+Delay_LNA)*ClkRF+14))/(2*ClkRF))+1
	 * ClkRF in MHz, delay in us
	 */

	reg = (200 * delay_lna) / 2 + (14000000UL / (clkrf / 500U));
	reg = DIV_ROUND_UP(reg, 1000UL) +
		phy->pdata->gain_ctrl.agc_attack_delay_extra_margin_us;
	reg = clamp_t(uint8_t, reg, 0U, 31U);
	ret = ad9361_spi_writef(spi, REG_AGC_ATTACK_DELAY,
			  AGC_ATTACK_DELAY(~0), reg);

	/*
	 * Peak Overload Wait Time (ClkRF cycles)=ceiling((0.1+Delay_LNA) *clkRF+1)
	 */

	reg = (delay_lna + 100UL) * (clkrf / 1000UL);
	reg = DIV_ROUND_UP(reg, 1000000UL) + 1;
	reg = clamp_t(uint8_t, reg, 0U, 31U);
	ret |= ad9361_spi_writef(spi, REG_PEAK_WAIT_TIME,
			  PEAK_OVERLOAD_WAIT_TIME(~0), reg);

	/*
	 * Settling Delay in 0x111.  Applies to all gain control modes:
	 * 0x111[D4:D0]= ceiling(((0.2+Delay_LNA)*clkRF
	dodebug = false;+14)/2)
	 */

	reg = (delay_lna + 200UL) * (clkrf / 2000UL);
	reg = DIV_ROUND_UP(reg, 1000000UL) + 7;
	reg = settling_delay = clamp_t(uint8_t, reg, 0U, 31U);
	ret |= ad9361_spi_writef(spi, REG_FAST_CONFIG_2_SETTLING_DELAY,
			 SETTLING_DELAY(~0), reg);

	/*
	 * Gain Update Counter [15:0]= round((((time*ClkRF-0x111[D4:D0]*2)-2))/2)
	 */
	reg = phy->pdata->gain_ctrl.gain_update_interval_us * (clkrf / 1000UL) -
		settling_delay * 2000UL - 2000UL;

	reg = DIV_ROUND_CLOSEST(reg, 2000UL);
	reg = clamp_t(uint32_t, reg, 0U, 131071UL);

	if (phy->agc_mode[0] == RF_GAIN_FASTATTACK_AGC ||
		phy->agc_mode[1] == RF_GAIN_FASTATTACK_AGC) {
		dec_pow_meas_dur =
			phy->pdata->gain_ctrl.f_agc_dec_pow_measuremnt_duration;
	} else {

		dec_pow_meas_dur = phy->pdata->gain_ctrl.dec_pow_measuremnt_duration;

		if (((reg * 2) / dec_pow_meas_dur) < 2) {
			dec_pow_meas_dur = reg;
		}
	}

	/* Power Measurement Duration */
	ad9361_spi_writef(spi, REG_DEC_POWER_MEASURE_DURATION_0,
			  DEC_POWER_MEASUREMENT_DURATION(~0),
			  ilog2(dec_pow_meas_dur / 16));


	ret |= ad9361_spi_writef(spi, REG_DIGITAL_SAT_COUNTER,
			  DOUBLE_GAIN_COUNTER,  reg > 65535);

	if (reg > 65535)
		reg /= 2;

	ret |= ad9361_spi_write(spi, REG_GAIN_UPDATE_COUNTER1, reg & 0xFF);
	ret |= ad9361_spi_write(spi, REG_GAIN_UPDATE_COUNTER2, reg >> 8);

	/*
	 * Fast AGC State Wait Time - Energy Detect Count
	 */

	reg = DIV_ROUND_CLOSEST(phy->pdata->gain_ctrl.f_agc_state_wait_time_ns *
				1000, clkrf / 1000UL);
	reg = clamp_t(uint32_t, reg, 0U, 31U);
	ret |= ad9361_spi_writef(spi, REG_FAST_ENERGY_DETECT_COUNT,
			  ENERGY_DETECT_COUNT(~0),  reg);

	return ret;
}

/**
 * Set the gain control mode.
 * @param phy The AD9361 state structure.
 * @param rf_gain_ctrl A rf_gain_ctrl struct that contains the the desired
 *        channel information and the gain control mode.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_set_gain_ctrl_mode(struct ad9361_rf_phy *phy,
		struct rf_gain_ctrl *gain_ctrl)
{
	struct spi_device *spi = phy->spi;
	int32_t rc = 0;
	uint32_t gain_ctl_shift, mode;
	uint8_t val;

	rc = ad9361_spi_readm(spi, REG_AGC_CONFIG_1, &val, 1);
	if (rc) {
		dev_err(dev, "Unable to read AGC config1 register: %x",
			REG_AGC_CONFIG_1);
		goto out;
	}

	switch (gain_ctrl->mode) {
	case RF_GAIN_MGC:
		mode = RX_GAIN_CTL_MGC;
		break;
	case RF_GAIN_FASTATTACK_AGC:
		mode = RX_GAIN_CTL_AGC_FAST_ATK;
		break;
	case RF_GAIN_SLOWATTACK_AGC:
		mode = RX_GAIN_CTL_AGC_SLOW_ATK;
		break;
	case RF_GAIN_HYBRID_AGC:
		mode = RX_GAIN_CTL_AGC_SLOW_ATK_HYBD;
		break;
	default:
		rc = -EINVAL;
		goto out;
	}

	if (gain_ctrl->ant == 1) {
		gain_ctl_shift = RX1_GAIN_CTRL_SHIFT;
	}
	else if (gain_ctrl->ant == 2) {
		gain_ctl_shift = RX2_GAIN_CTRL_SHIFT;
	}
	else {
		dev_err(dev, "Unknown Rx path %"PRIu32, gain_ctrl->ant);
		rc = -EINVAL;
		goto out;
	}

	rc = ad9361_en_dis_rx(phy, gain_ctrl->ant, RX_DISABLE);
	if (rc) {
		dev_err(dev, "Unable to disable rx%"PRIu32, gain_ctrl->ant);
		goto out;
	}

	val &= ~(RX_GAIN_CTL_MASK << gain_ctl_shift);
	val |= mode << gain_ctl_shift;
	if (mode == RX_GAIN_CTL_AGC_SLOW_ATK_HYBD)
		val |= SLOW_ATTACK_HYBRID_MODE;
	else
		val &= ~SLOW_ATTACK_HYBRID_MODE;

	rc = ad9361_spi_write(spi, REG_AGC_CONFIG_1, val);
	if (rc) {
		dev_err(dev, "Unable to write AGC config1 register: %x",
			REG_AGC_CONFIG_1);
		goto out;
	}

	ad9361_en_dis_rx(phy, gain_ctrl->ant, RX_ENABLE);
	rc = ad9361_gc_update(phy);
out:
	return rc;
}

/**
 * Get the RSSI.
 * @param phy The AD9361 state structure.
 * @param rssi A rf_rssi struct to store the RSSI.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_read_rssi(struct ad9361_rf_phy *phy, struct rf_rssi *rssi)
{
	struct spi_device *spi = phy->spi;
	uint8_t reg_val_buf[6];
	int32_t rc;

	rc = ad9361_spi_readm(spi, REG_PREAMBLE_LSB,
		reg_val_buf, ARRAY_SIZE(reg_val_buf));
	if (rssi->ant == 1) {
		rssi->symbol = RSSI_RESOLUTION *
			((reg_val_buf[5] << RSSI_LSB_SHIFT) +
			(reg_val_buf[1] & RSSI_LSB_MASK1));
		rssi->preamble = RSSI_RESOLUTION *
			((reg_val_buf[4] << RSSI_LSB_SHIFT) +
			(reg_val_buf[0] & RSSI_LSB_MASK1));
	}
	else if (rssi->ant == 2) {
		rssi->symbol = RSSI_RESOLUTION *
			((reg_val_buf[3] << RSSI_LSB_SHIFT) +
			((reg_val_buf[1] & RSSI_LSB_MASK2) >> 1));
		rssi->preamble = RSSI_RESOLUTION *
			((reg_val_buf[2] << RSSI_LSB_SHIFT) +
			((reg_val_buf[0] & RSSI_LSB_MASK2) >> 1));
	}
	else
		rc = -EFAULT;

	rssi->multiplier = RSSI_MULTIPLIER;

	return rc;
}

/**
 * Setup the RX ADC.
 * @param phy The AD9361 state structure.
 * @param bbpll_freq The BBPLL frequency [Hz].
 * @param adc_sampl_freq_Hz The ADC sampling frequency [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_rx_adc_setup(struct ad9361_rf_phy *phy, uint32_t bbpll_freq,
	uint32_t adc_sampl_freq_Hz)
{

	uint32_t scale_snr_1e3, maxsnr, sqrt_inv_rc_tconst_1e3, tmp_1e3,
		scaled_adc_clk_1e6, inv_scaled_adc_clk_1e3, sqrt_term_1e3,
		min_sqrt_term_1e3, bb_bw_Hz;
	uint64_t tmp, invrc_tconst_1e6;
	uint8_t data[40];
	uint32_t i;
	int32_t ret;

	uint8_t c3_msb = ad9361_spi_read(phy->spi, REG_RX_BBF_C3_MSB);
	uint8_t c3_lsb = ad9361_spi_read(phy->spi, REG_RX_BBF_C3_LSB);
	uint8_t r2346 = ad9361_spi_read(phy->spi, REG_RX_BBF_R2346);

	/*
	* BBBW = (BBPLL / RxTuneDiv) * ln(2) / (1.4 * 2PI )
	* We assume ad9361_rx_bb_analog_filter_calib() is always run prior
	*/

	tmp = bbpll_freq * 10000ULL;
	do_div(&tmp, 126906UL * phy->rxbbf_div);
	bb_bw_Hz = tmp;

	dev_dbg(&phy->spi->dev, "%s : BBBW %"PRIu32" : ADCfreq %"PRIu32,
		__func__, bb_bw_Hz, adc_sampl_freq_Hz);

	dev_dbg(&phy->spi->dev, "c3_msb 0x%X : c3_lsb 0x%X : r2346 0x%X : ",
		c3_msb, c3_lsb, r2346);

	bb_bw_Hz = clamp(bb_bw_Hz, 200000UL, 28000000UL);

	if (adc_sampl_freq_Hz < 80000000)
		scale_snr_1e3 = 1000;
	else
		scale_snr_1e3 = 1585; /* pow(10, scale_snr_dB/10); */

	if (bb_bw_Hz >= 18000000) {
		invrc_tconst_1e6 = (160975ULL * r2346 *
			(160 * c3_msb + 10 * c3_lsb + 140) *
			(bb_bw_Hz)* (1000 + (10 * (bb_bw_Hz - 18000000) / 1000000)));

		do_div(&invrc_tconst_1e6, 1000UL);

	}
	else {
		invrc_tconst_1e6 = (160975ULL * r2346 *
			(160 * c3_msb + 10 * c3_lsb + 140) *
			(bb_bw_Hz));
	}

	do_div(&invrc_tconst_1e6, 1000000000UL);

	if (invrc_tconst_1e6 > ULONG_MAX)
		dev_err(&phy->spi->dev, "invrc_tconst_1e6 > ULONG_MAX");

	sqrt_inv_rc_tconst_1e3 = int_sqrt((uint32_t)invrc_tconst_1e6);
	maxsnr = 640 / 160;
	scaled_adc_clk_1e6 = DIV_ROUND_CLOSEST(adc_sampl_freq_Hz, 640);
	inv_scaled_adc_clk_1e3 = DIV_ROUND_CLOSEST(640000000,
		DIV_ROUND_CLOSEST(adc_sampl_freq_Hz, 1000));
	tmp_1e3 = DIV_ROUND_CLOSEST(980000 + 20 * max_t(uint32_t, 1000U,
		DIV_ROUND_CLOSEST(inv_scaled_adc_clk_1e3, maxsnr)), 1000);
	sqrt_term_1e3 = int_sqrt(scaled_adc_clk_1e6);
	min_sqrt_term_1e3 = min_t(uint32_t, 1000U,
		int_sqrt(maxsnr * scaled_adc_clk_1e6));

	dev_dbg(&phy->spi->dev, "invrc_tconst_1e6 %llu, sqrt_inv_rc_tconst_1e3 %"PRIu32,
		invrc_tconst_1e6, sqrt_inv_rc_tconst_1e3);
	dev_dbg(&phy->spi->dev, "scaled_adc_clk_1e6 %"PRIu32", inv_scaled_adc_clk_1e3 %"PRIu32,
		scaled_adc_clk_1e6, inv_scaled_adc_clk_1e3);
	dev_dbg(&phy->spi->dev, "tmp_1e3 %"PRIu32", sqrt_term_1e3 %"PRIu32", min_sqrt_term_1e3 %"PRIu32,
		tmp_1e3, sqrt_term_1e3, min_sqrt_term_1e3);

	data[0] = 0;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0x24;
	data[4] = 0x24;
	data[5] = 0;
	data[6] = 0;

	tmp = -50000000 + 8ULL * scale_snr_1e3 * sqrt_inv_rc_tconst_1e3 *
		min_sqrt_term_1e3;
	do_div(&tmp, 100000000UL);
	data[7] = min_t(uint64_t, 124U, tmp);

	tmp = (invrc_tconst_1e6 >> 1) + 20 * inv_scaled_adc_clk_1e3 *
		data[7] / 80 * 1000ULL;
	do_div(&tmp, invrc_tconst_1e6);
	data[8] = min_t(uint64_t, 255U, tmp);

	tmp = (-500000 + 77ULL * sqrt_inv_rc_tconst_1e3 * min_sqrt_term_1e3);
	do_div(&tmp, 1000000UL);
	data[10] = min_t(uint64_t, 127U, tmp);

	data[9] = min_t(uint32_t, 127U, ((800 * data[10]) / 1000));
	tmp = ((invrc_tconst_1e6 >> 1) + (20 * inv_scaled_adc_clk_1e3 *
		data[10] * 1000ULL));
	do_div(&tmp, invrc_tconst_1e6 * 77);
	data[11] = min_t(uint64_t, 255U, tmp);
	data[12] = min_t(uint32_t, 127U, (-500000 + 80 * sqrt_inv_rc_tconst_1e3 *
		min_sqrt_term_1e3) / 1000000UL);

	tmp = -3 * (long)(invrc_tconst_1e6 >> 1) + inv_scaled_adc_clk_1e3 *
		data[12] * (1000ULL * 20 / 80);
	do_div(&tmp, invrc_tconst_1e6);
	data[13] = min_t(uint64_t, 255, tmp);

	data[14] = 21 * (inv_scaled_adc_clk_1e3 / 10000);
	data[15] = min_t(uint32_t, 127U, (500 + 1025 * data[7]) / 1000);
	data[16] = min_t(uint32_t, 127U, (data[15] * tmp_1e3) / 1000);
	data[17] = data[15];
	data[18] = min_t(uint32_t, 127U, (500 + 975 * data[10]) / 1000);
	data[19] = min_t(uint32_t, 127U, (data[18] * tmp_1e3) / 1000);
	data[20] = data[18];
	data[21] = min_t(uint32_t, 127U, (500 + 975 * data[12]) / 1000);
	data[22] = min_t(uint32_t, 127, (data[21] * tmp_1e3) / 1000);
	data[23] = data[21];
	data[24] = 0x2E;
	data[25] = (128 + min_t(uint32_t, 63000U, DIV_ROUND_CLOSEST(63 *
		scaled_adc_clk_1e6, 1000)) / 1000);
	data[26] = min_t(uint32_t, 63U, 63 * scaled_adc_clk_1e6 / 1000000 *
		(920 + 80 * inv_scaled_adc_clk_1e3 / 1000) / 1000);
	data[27] = min_t(uint32_t, 63, (32 * sqrt_term_1e3) / 1000);
	data[28] = data[25];
	data[29] = data[26];
	data[30] = data[27];
	data[31] = data[25];
	data[32] = data[26];
	data[33] = min_t(uint32_t, 63U, 63 * sqrt_term_1e3 / 1000);
	data[34] = min_t(uint32_t, 127U, 64 * sqrt_term_1e3 / 1000);
	data[35] = 0x40;
	data[36] = 0x40;
	data[37] = 0x2C;
	data[38] = 0x00;
	data[39] = 0x00;

	for (i = 0; i < 40; i++) {
		ret = ad9361_spi_write(phy->spi, 0x200 + i, data[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * Perform a RX TIA calibration.
 * @param phy The AD9361 state structure.
 * @param bb_bw_Hz The baseband bandwidth [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_rx_tia_calib(struct ad9361_rf_phy *phy, uint32_t bb_bw_Hz)
{
	uint32_t Cbbf, R2346;
	uint64_t CTIA_fF;

	uint8_t reg1EB = ad9361_spi_read(phy->spi, REG_RX_BBF_C3_MSB);
	uint8_t reg1EC = ad9361_spi_read(phy->spi, REG_RX_BBF_C3_LSB);
	uint8_t reg1E6 = ad9361_spi_read(phy->spi, REG_RX_BBF_R2346);
	uint8_t reg1DB, reg1DF, reg1DD, reg1DC, reg1DE, temp;

	dev_dbg(&phy->spi->dev, "%s : bb_bw_Hz %"PRIu32,
		__func__, bb_bw_Hz);

	bb_bw_Hz = clamp(bb_bw_Hz, 200000UL, 20000000UL);

	Cbbf = (reg1EB * 160) + (reg1EC * 10) + 140; /* fF */
	R2346 = 18300 * RX_BBF_R2346(reg1E6);

	CTIA_fF = Cbbf * R2346 * 560ULL;
	do_div(&CTIA_fF, 3500000UL);

	if (bb_bw_Hz <= 3000000UL)
		reg1DB = 0xE0;
	else if (bb_bw_Hz <= 10000000UL)
		reg1DB = 0x60;
	else
		reg1DB = 0x20;

	if (CTIA_fF > 2920ULL) {
		reg1DC = 0x40;
		reg1DE = 0x40;
		temp = min(127U, DIV_ROUND_CLOSEST((uint32_t)CTIA_fF - 400, 320U));
		reg1DD = temp;
		reg1DF = temp;
	}
	else {
		temp = DIV_ROUND_CLOSEST((uint32_t)CTIA_fF - 400, 40U) + 0x40;
		reg1DC = temp;
		reg1DE = temp;
		reg1DD = 0;
		reg1DF = 0;
	}

	ad9361_spi_write(phy->spi, REG_RX_TIA_CONFIG, reg1DB);
	ad9361_spi_write(phy->spi, REG_TIA1_C_LSB, reg1DC);
	ad9361_spi_write(phy->spi, REG_TIA1_C_MSB, reg1DD);
	ad9361_spi_write(phy->spi, REG_TIA2_C_LSB, reg1DE);
	ad9361_spi_write(phy->spi, REG_TIA2_C_MSB, reg1DF);

	return 0;
}

/**
 * Perform a baseband RX analog filter calibration.
 * @param phy The AD9361 state structure.
 * @param rx_bb_bw The baseband bandwidth [Hz].
 * @param bbpll_freq The BBPLL frequency [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_rx_bb_analog_filter_calib(struct ad9361_rf_phy *phy,
	uint32_t rx_bb_bw,
	uint32_t bbpll_freq)
{
	uint32_t target;
	uint8_t tmp;
	int32_t ret;

	dev_dbg(&phy->spi->dev, "%s : rx_bb_bw %"PRIu32" bbpll_freq %"PRIu32,
		__func__, rx_bb_bw, bbpll_freq);

	rx_bb_bw = clamp(rx_bb_bw, 200000UL, 28000000UL);

	/* 1.4 * BBBW * 2PI / ln(2) */
	target = 126906UL * (rx_bb_bw / 10000UL);
	phy->rxbbf_div = min_t(uint32_t, 511UL, DIV_ROUND_UP(bbpll_freq, target));

	/* Set RX baseband filter divide value */
	ad9361_spi_write(phy->spi, REG_RX_BBF_TUNE_DIVIDE, phy->rxbbf_div);
	ad9361_spi_writef(phy->spi, REG_RX_BBF_TUNE_CONFIG, BIT(0), phy->rxbbf_div >> 8);

	/* Write the BBBW into registers 0x1FB and 0x1FC */
	ad9361_spi_write(phy->spi, REG_RX_BBBW_MHZ, rx_bb_bw / 1000000UL);

	tmp = DIV_ROUND_CLOSEST((rx_bb_bw % 1000000UL) * 128, 1000000UL);
	ad9361_spi_write(phy->spi, REG_RX_BBBW_KHZ, min_t(uint8_t, 127, tmp));

	ad9361_spi_write(phy->spi, REG_RX_MIX_LO_CM, RX_MIX_LO_CM(0x3F)); /* Set Rx Mix LO CM */
	ad9361_spi_write(phy->spi, REG_RX_MIX_GM_CONFIG, RX_MIX_GM_PLOAD(3)); /* Set GM common mode */

	/* Enable the RX BBF tune circuit by writing 0x1E2=0x02 and 0x1E3=0x02 */
	ad9361_spi_write(phy->spi, REG_RX1_TUNE_CTRL, RX1_TUNE_RESAMPLE);
	ad9361_spi_write(phy->spi, REG_RX2_TUNE_CTRL, RX2_TUNE_RESAMPLE);

	/* Start the RX Baseband Filter calibration in register 0x016[7] */
	/* Calibration is complete when register 0x016[7] self clears */
	ret = ad9361_run_calibration(phy, RX_BB_TUNE_CAL);

	/* Disable the RX baseband filter tune circuit, write 0x1E2=3, 0x1E3=3 */
	ad9361_spi_write(phy->spi, REG_RX1_TUNE_CTRL,
		RX1_TUNE_RESAMPLE | RX1_PD_TUNE);
	ad9361_spi_write(phy->spi, REG_RX2_TUNE_CTRL,
		RX2_TUNE_RESAMPLE | RX2_PD_TUNE);

	return ret;
}

/**
 * Perform a baseband TX analog filter calibration.
 * @param phy The AD9361 state structure.
 * @param tx_bb_bw The baseband bandwidth [Hz].
 * @param bbpll_freq The BBPLL frequency [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_tx_bb_analog_filter_calib(struct ad9361_rf_phy *phy,
	uint32_t tx_bb_bw,
	uint32_t bbpll_freq)
{
	uint32_t target, txbbf_div;
	int32_t ret;

	dev_dbg(&phy->spi->dev, "%s : tx_bb_bw %"PRIu32" bbpll_freq %"PRIu32,
		__func__, tx_bb_bw, bbpll_freq);

	tx_bb_bw = clamp(tx_bb_bw, 625000UL, 20000000UL);

	/* 1.6 * BBBW * 2PI / ln(2) */
	target = 145036 * (tx_bb_bw / 10000UL);
	txbbf_div = min_t(uint32_t, 511UL, DIV_ROUND_UP(bbpll_freq, target));

	/* Set TX baseband filter divide value */
	ad9361_spi_write(phy->spi, REG_TX_BBF_TUNE_DIVIDER, txbbf_div);
	ad9361_spi_writef(phy->spi, REG_TX_BBF_TUNE_MODE,
		TX_BBF_TUNE_DIVIDER, txbbf_div >> 8);

	/* Enable the TX baseband filter tune circuit by setting 0x0CA=0x22. */
	ad9361_spi_write(phy->spi, REG_TX_TUNE_CTRL, TUNER_RESAMPLE | TUNE_CTRL(1));

	/* Start the TX Baseband Filter calibration in register 0x016[6] */
	/* Calibration is complete when register 0x016[] self clears */
	ret = ad9361_run_calibration(phy, TX_BB_TUNE_CAL);

	/* Disable the TX baseband filter tune circuit by writing 0x0CA=0x26. */
	ad9361_spi_write(phy->spi, REG_TX_TUNE_CTRL,
		TUNER_RESAMPLE | TUNE_CTRL(1) | PD_TUNE);

	return ret;
}

/**
 * Perform a baseband TX secondary filter calibration.
 * @param phy The AD9361 state structure.
 * @param tx_rf_bw The RF bandwidth [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_tx_bb_second_filter_calib(struct ad9361_rf_phy *phy,
	uint32_t tx_bb_bw)
{
	uint64_t cap;
	uint32_t corner, res, div;
	uint32_t reg_conf, reg_res;
	int32_t ret, i;

	dev_dbg(&phy->spi->dev, "%s : tx_bb_bw %"PRIu32,
		__func__, tx_bb_bw);

	tx_bb_bw = clamp(tx_bb_bw, 530000UL, 20000000UL);

	/* BBBW * 5PI */
	corner = 15708 * (tx_bb_bw / 10000UL);

	for (i = 0, res = 1; i < 4; i++) {
		div = corner * res;
		cap = (500000000ULL) + (div >> 1);
		do_div(&cap, div);
		cap -= 12ULL;
		if (cap < 64ULL)
			break;

		res <<= 1;
	}

	if (cap > 63ULL)
		cap = 63ULL;

	if (tx_bb_bw <= 4500000UL)
		reg_conf = 0x59;
	else if (tx_bb_bw <= 12000000UL)
		reg_conf = 0x56;
	else
		reg_conf = 0x57;

	switch (res) {
	case 1:
		reg_res = 0x0C;
		break;
	case 2:
		reg_res = 0x04;
		break;
	case 4:
		reg_res = 0x03;
		break;
	case 8:
		reg_res = 0x01;
		break;
	default:
		reg_res = 0x01;
	}

	ret = ad9361_spi_write(phy->spi, REG_CONFIG0, reg_conf);
	ret |= ad9361_spi_write(phy->spi, REG_RESISTOR, reg_res);
	ret |= ad9361_spi_write(phy->spi, REG_CAPACITOR, (uint8_t)cap);

	return ret;
}

/**
 * Perform a RF synthesizer charge pump calibration.
 * @param phy The AD9361 state structure.
 * @param ref_clk_hz The reference clock rate [Hz].
 * @param tx The Synthesizer TX = 1, RX = 0.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_txrx_synth_cp_calib(struct ad9361_rf_phy *phy,
	uint32_t ref_clk_hz, bool tx)
{
	uint32_t offs = tx ? 0x40 : 0;
	uint32_t vco_cal_cnt;
	dev_dbg(&phy->spi->dev, "%s : ref_clk_hz %"PRIu32" : is_tx %d",
		__func__, ref_clk_hz, tx);

	ad9361_spi_write(phy->spi, REG_RX_CP_LEVEL_DETECT + offs, 0x17);
	ad9361_spi_write(phy->spi, REG_RX_DSM_SETUP_1 + offs, 0x0);

	ad9361_spi_write(phy->spi, REG_RX_LO_GEN_POWER_MODE + offs, 0x00);
	ad9361_spi_write(phy->spi, REG_RX_VCO_LDO + offs, 0x0B);
	ad9361_spi_write(phy->spi, REG_RX_VCO_PD_OVERRIDES + offs, 0x02);
	ad9361_spi_write(phy->spi, REG_RX_CP_CURRENT + offs, 0x80);
	ad9361_spi_write(phy->spi, REG_RX_CP_CONFIG + offs, 0x00);

	/* see Table 70 Example Calibration Times for RF VCO Cal */
	if (phy->pdata->fdd || phy->pdata->tdd_use_fdd_tables) {
		vco_cal_cnt = VCO_CAL_EN | VCO_CAL_COUNT(3) | FB_CLOCK_ADV(2);
	}
	else {
		if (ref_clk_hz > 40000000UL)
			vco_cal_cnt = VCO_CAL_EN | VCO_CAL_COUNT(1) |
			FB_CLOCK_ADV(2);
		else
			vco_cal_cnt = VCO_CAL_EN | VCO_CAL_COUNT(0) |
			FB_CLOCK_ADV(2);
	}

	ad9361_spi_write(phy->spi, REG_RX_VCO_CAL + offs, vco_cal_cnt);

	/* Enable FDD mode during calibrations */

	if (!phy->pdata->fdd)
		ad9361_spi_write(phy->spi, REG_PARALLEL_PORT_CONF_3, LVDS_MODE);

	ad9361_spi_write(phy->spi, REG_ENSM_CONFIG_2, DUAL_SYNTH_MODE);
	ad9361_spi_write(phy->spi, REG_ENSM_CONFIG_1,
		FORCE_ALERT_STATE |
		TO_ALERT);
	ad9361_spi_write(phy->spi, REG_ENSM_MODE, FDD_MODE);

	ad9361_spi_write(phy->spi, REG_RX_CP_CONFIG + offs, CP_CAL_ENABLE);

	return ad9361_check_cal_done(phy, REG_RX_CAL_STATUS + offs,
		CP_CAL_VALID, 1);
}

/**
 * Perform a baseband DC offset calibration.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_bb_dc_offset_calib(struct ad9361_rf_phy *phy)
{
	dev_dbg(&phy->spi->dev, "%s", __func__);

	ad9361_spi_write(phy->spi, REG_BB_DC_OFFSET_COUNT, 0x3F);
	ad9361_spi_write(phy->spi, REG_BB_DC_OFFSET_SHIFT, BB_DC_M_SHIFT(0xF));
	ad9361_spi_write(phy->spi, REG_BB_DC_OFFSET_ATTEN, BB_DC_OFFSET_ATTEN(1));

	return ad9361_run_calibration(phy, BBDC_CAL);
}

/**
 * Perform a RF DC offset calibration.
 * @param phy The AD9361 state structure.
 * @param ref_clk_hz The RX LO frequency [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_rf_dc_offset_calib(struct ad9361_rf_phy *phy,
	uint64_t rx_freq)
{
	struct spi_device *spi = phy->spi;

	dev_dbg(&phy->spi->dev, "%s : rx_freq %llu",
		__func__, rx_freq);

	ad9361_spi_write(spi, REG_WAIT_COUNT, 0x20);

	if (rx_freq <= 4000000000ULL) {
		ad9361_spi_write(spi, REG_RF_DC_OFFSET_COUNT,
			phy->pdata->rf_dc_offset_count_low);
		ad9361_spi_write(spi, REG_RF_DC_OFFSET_CONFIG_1,
			RF_DC_CALIBRATION_COUNT(4) | DAC_FS(2));
		ad9361_spi_write(spi, REG_RF_DC_OFFSET_ATTEN,
			RF_DC_OFFSET_ATTEN(
			phy->pdata->dc_offset_attenuation_low));
	}
	else {
		ad9361_spi_write(spi, REG_RF_DC_OFFSET_COUNT,
			phy->pdata->rf_dc_offset_count_high);
		ad9361_spi_write(spi, REG_RF_DC_OFFSET_CONFIG_1,
			RF_DC_CALIBRATION_COUNT(4) | DAC_FS(3));
		ad9361_spi_write(spi, REG_RF_DC_OFFSET_ATTEN,
			RF_DC_OFFSET_ATTEN(
			phy->pdata->dc_offset_attenuation_high));
	}

	ad9361_spi_write(spi, REG_DC_OFFSET_CONFIG2,
		USE_WAIT_COUNTER_FOR_RF_DC_INIT_CAL |
		DC_OFFSET_UPDATE(3));

	if (phy->pdata->rx1rx2_phase_inversion_en ||
		(phy->pdata->port_ctrl.pp_conf[1] & INVERT_RX2)) {
		ad9361_spi_write(spi, REG_INVERT_BITS,
				INVERT_RX1_RF_DC_CGOUT_WORD);
	} else {
		ad9361_spi_write(spi, REG_INVERT_BITS,
				INVERT_RX1_RF_DC_CGOUT_WORD |
				INVERT_RX2_RF_DC_CGOUT_WORD);
	}

	return ad9361_run_calibration(phy, RFDC_CAL);
}

/**
 * Update RF bandwidth.
 * @param phy The AD9361 state structure.
 * @param rf_rx_bw RF RX bandwidth [Hz].
 * @param rf_tx_bw RF TX bandwidth [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t __ad9361_update_rf_bandwidth(struct ad9361_rf_phy *phy,
		uint32_t rf_rx_bw, uint32_t rf_tx_bw)
{
	uint32_t real_rx_bandwidth = rf_rx_bw / 2;
	uint32_t real_tx_bandwidth = rf_tx_bw / 2;
	uint32_t bbpll_freq;
	int32_t ret;

	dev_dbg(&phy->spi->dev, "%s: %"PRIu32" %"PRIu32,
		__func__, rf_rx_bw, rf_tx_bw);

	bbpll_freq = clk_get_rate(phy, phy->ref_clk_scale[BBPLL_CLK]);

	ret = ad9361_rx_bb_analog_filter_calib(phy,
				real_rx_bandwidth,
				bbpll_freq);
	if (ret < 0)
		return ret;

	ret = ad9361_tx_bb_analog_filter_calib(phy,
				real_tx_bandwidth,
				bbpll_freq);
	if (ret < 0)
		return ret;

	ret = ad9361_rx_tia_calib(phy, real_rx_bandwidth);
	if (ret < 0)
		return ret;

	ret = ad9361_tx_bb_second_filter_calib(phy, real_tx_bandwidth);
	if (ret < 0)
		return ret;

	ret = ad9361_rx_adc_setup(phy,
				bbpll_freq,
				clk_get_rate(phy, phy->ref_clk_scale[ADC_CLK]));
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * Loop through all possible phase offsets in case the QUAD CAL doesn't converge.
 * @param phy The AD9361 state structure.
 * @param rxnco_word Rx NCO word.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_tx_quad_phase_search(struct ad9361_rf_phy *phy, uint32_t rxnco_word)
{
	int32_t i, ret;
	uint8_t field[64];
	uint32_t val, start;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	for (i = 0; i < (int64_t)(ARRAY_SIZE(field) / 2); i++) {

		ad9361_spi_write(phy->spi, REG_QUAD_CAL_NCO_FREQ_PHASE_OFFSET,
			RX_NCO_FREQ(rxnco_word) | RX_NCO_PHASE_OFFSET(i));

		ret =  ad9361_run_calibration(phy, TX_QUAD_CAL);
		if (ret < 0)
			return ret;

		/* Handle 360/0 wrap around */
		val = ad9361_spi_read(phy->spi, REG_QUAD_CAL_STATUS_TX1);
		field[i] = field[i + 32] = !((val & TX1_LO_CONV) && (val & TX1_SSB_CONV));
	}

	ret = ad9361_find_opt(field, ARRAY_SIZE(field), &start);

#ifdef _DEBUG
	for (i = 0; i < 64; i++) {
		printk("%c", (field[i] ? '#' : 'o'));
	}
#ifdef WIN32
	printk(" RX_NCO_PHASE_OFFSET(%d) \n", (start + ret / 2) & 0x1F);
#else
	printk(" RX_NCO_PHASE_OFFSET(%"PRIu32") \n", (start + ret / 2) & 0x1F);
#endif
#endif

	ad9361_spi_write(phy->spi, REG_QUAD_CAL_NCO_FREQ_PHASE_OFFSET,
		RX_NCO_FREQ(rxnco_word) |
		RX_NCO_PHASE_OFFSET((start + ret / 2) & 0x1F));

	ad9361_run_calibration(phy, TX_QUAD_CAL);
	/* REVISIT: sometimes we need to do it twice */
	ret = ad9361_run_calibration(phy, TX_QUAD_CAL);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * Perform a TX quadrature calibration.
 * @param phy The AD9361 state structure.
 * @param bw The bandwidth [Hz].
 * @param rx_phase The optional RX phase value overwrite (set to zero).
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_tx_quad_calib(struct ad9361_rf_phy *phy,
	uint32_t bw_rx, uint32_t bw_tx,
	int32_t rx_phase)
{
	struct spi_device *spi = phy->spi;
	uint32_t clktf, clkrf;
	int32_t txnco_word, rxnco_word, txnco_freq, ret;
	uint8_t __rx_phase = 0, reg_inv_bits, val;
	const uint8_t(*tab)[3];
	uint32_t index_max, i, lpf_tia_mask;
	/*
	* Find NCO frequency that matches this equation:
	* BW / 4 = Rx NCO freq = Tx NCO freq:
	* Rx NCO = ClkRF * (rxNCO <1:0> + 1) / 32
	* Tx NCO = ClkTF * (txNCO <1:0> + 1) / 32
	*/

	clkrf = clk_get_rate(phy, phy->ref_clk_scale[CLKRF_CLK]);
	clktf = clk_get_rate(phy, phy->ref_clk_scale[CLKTF_CLK]);

	dev_dbg(&phy->spi->dev, "%s : bw_tx %"PRIu32" clkrf %"PRIu32" clktf %"PRIu32,
		__func__, bw_tx, clkrf, clktf);

	txnco_word = DIV_ROUND_CLOSEST(bw_tx * 8, clktf) - 1;
	txnco_word = clamp_t(int, txnco_word, 0, 3);
	rxnco_word = txnco_word;

	dev_dbg(dev, "Tx NCO frequency: %"PRIu32" (BW/4: %"PRIu32") txnco_word %"PRId32,
			clktf * (txnco_word + 1) / 32, bw_tx / 4, txnco_word);

	if (clkrf == (2 * clktf)) {
		__rx_phase = 0x0E;
		switch (txnco_word) {
		case 0:
			txnco_word++;
			break;
		case 1:
			rxnco_word--;
			break;
		case 2:
			rxnco_word -= 2;
			txnco_word--;
			break;
		case 3:
			rxnco_word -= 2;	/* REVISIT */
			__rx_phase = 0x08;
			break;
		}
	}
	else if (clkrf == clktf) {
		switch (txnco_word) {
		case 0:
		case 3:
			__rx_phase = 0x15;
			break;
		case 2:
			__rx_phase = 0x1F;
			break;
		case 1:
			if (ad9361_spi_readf(spi,
				REG_TX_ENABLE_FILTER_CTRL, 0x3F) == 0x22)
				__rx_phase = 0x15; 	/* REVISIT */
			else
				__rx_phase = 0x1A;
			break;
		}
	}
	else
		dev_err(dev, "Unhandled case in %s line %d clkrf %"PRIu32" clktf %"PRIu32,
		__func__, __LINE__, clkrf, clktf);


	if (rx_phase >= 0)
		__rx_phase = rx_phase;

	txnco_freq = clktf * (txnco_word + 1) / 32;

	if (txnco_freq > (int64_t)(bw_rx / 4) || txnco_freq > (int64_t)(bw_tx / 4)) {
		/* Make sure the BW during calibration is wide enough */
		ret = __ad9361_update_rf_bandwidth(phy, txnco_freq * 8, txnco_freq * 8);
		if (ret < 0)
			return ret;
	}

	if (phy->pdata->rx1rx2_phase_inversion_en ||
		(phy->pdata->port_ctrl.pp_conf[1] & INVERT_RX2)) {

		ad9361_spi_writef(spi, REG_PARALLEL_PORT_CONF_2, INVERT_RX2, 0);

		reg_inv_bits = ad9361_spi_read(spi, REG_INVERT_BITS);

		ad9361_spi_write(spi, REG_INVERT_BITS,
					INVERT_RX1_RF_DC_CGOUT_WORD |
					INVERT_RX2_RF_DC_CGOUT_WORD);
	}

	ad9361_spi_write(spi, REG_QUAD_CAL_NCO_FREQ_PHASE_OFFSET,
		RX_NCO_FREQ(rxnco_word) | RX_NCO_PHASE_OFFSET(__rx_phase));
	ad9361_spi_writef(spi, REG_KEXP_2, TX_NCO_FREQ(~0), txnco_word);

	ad9361_spi_write(spi, REG_QUAD_CAL_CTRL,
		SETTLE_MAIN_ENABLE | DC_OFFSET_ENABLE |
		GAIN_ENABLE | PHASE_ENABLE | M_DECIM(3));
	ad9361_spi_write(spi, REG_QUAD_CAL_COUNT, 0xFF);
	ad9361_spi_write(spi, REG_KEXP_1, KEXP_TX(1) | KEXP_TX_COMP(3) |
		KEXP_DC_I(3) | KEXP_DC_Q(3));
	ad9361_spi_write(spi, REG_MAG_FTEST_THRESH, 0x01);
	ad9361_spi_write(spi, REG_MAG_FTEST_THRESH_2, 0x01);

	if (phy->pdata->split_gt) {
		tab = &split_gain_table[phy->current_table][0];
		index_max = SIZE_SPLIT_TABLE;
		lpf_tia_mask = 0x20;
	}
	else {
		tab = &full_gain_table[phy->current_table][0];
		index_max = SIZE_FULL_TABLE;
		lpf_tia_mask = 0x3F;
	}

	for (i = 0; i < index_max; i++)
	if ((tab[i][1] & lpf_tia_mask) == 0x20) {
		ad9361_spi_write(spi, REG_TX_QUAD_FULL_LMT_GAIN, i);
		break;
	}

	if (i >= index_max)
		dev_err(dev, "failed to find suitable LPF TIA value in gain table");

	ad9361_spi_write(spi, REG_QUAD_SETTLE_COUNT, 0xF0);
	ad9361_spi_write(spi, REG_TX_QUAD_LPF_GAIN, 0x00);

	ret = ad9361_run_calibration(phy, TX_QUAD_CAL);

	val = ad9361_spi_readf(spi, REG_QUAD_CAL_STATUS_TX1,
			       TX1_LO_CONV | TX1_SSB_CONV);
	dev_dbg(dev, "LO leakage: %d Quadrature Calibration: %d : rx_phase %d\n",
		!!(val & TX1_LO_CONV), !!(val & TX1_SSB_CONV), __rx_phase);

	/* Calibration failed -> loop through all 32 phase offsets */
	if (val != (TX1_LO_CONV | TX1_SSB_CONV))
		ret = ad9361_tx_quad_phase_search(phy, rxnco_word);

	if (phy->pdata->rx1rx2_phase_inversion_en ||
		(phy->pdata->port_ctrl.pp_conf[1] & INVERT_RX2)) {
		ad9361_spi_writef(spi, REG_PARALLEL_PORT_CONF_2, INVERT_RX2, 1);
		ad9361_spi_write(spi, REG_INVERT_BITS, reg_inv_bits);
	}

	if (phy->pdata->rx1rx2_phase_inversion_en ||
		(phy->pdata->port_ctrl.pp_conf[1] & INVERT_RX2)) {
		ad9361_spi_writef(spi, REG_PARALLEL_PORT_CONF_2, INVERT_RX2, 1);
		ad9361_spi_write(spi, REG_INVERT_BITS, reg_inv_bits);
	}

	if (txnco_freq > (int64_t)(bw_rx / 4) || txnco_freq > (int64_t)(bw_tx / 4)) {
		__ad9361_update_rf_bandwidth(phy,
			phy->current_rx_bw_Hz,
			phy->current_tx_bw_Hz);
	}

	return ret;
}

/**
 * Setup RX tracking calibrations.
 * @param phy The AD9361 state structure.
 * @param bbdc_track Set true, will enable the BBDC tracking.
 * @param rfdc_track Set true, will enable the RFDC tracking.
 * @param rxquad_track Set true, will enable the RXQUAD tracking.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_tracking_control(struct ad9361_rf_phy *phy, bool bbdc_track,
	bool rfdc_track, bool rxquad_track)
{
	struct spi_device *spi = phy->spi;
	uint32_t qtrack = 0;

	dev_dbg(&spi->dev, "%s : bbdc_track=%d, rfdc_track=%d, rxquad_track=%d",
		__func__, bbdc_track, rfdc_track, rxquad_track);

	ad9361_spi_write(spi, REG_CALIBRATION_CONFIG_2,
		CALIBRATION_CONFIG2_DFLT | K_EXP_PHASE(0x15));
	ad9361_spi_write(spi, REG_CALIBRATION_CONFIG_3,
		PREVENT_POS_LOOP_GAIN | K_EXP_AMPLITUDE(0x15));

	ad9361_spi_write(spi, REG_DC_OFFSET_CONFIG2,
		USE_WAIT_COUNTER_FOR_RF_DC_INIT_CAL |
		DC_OFFSET_UPDATE(phy->pdata->dc_offset_update_events) |
		(bbdc_track ? ENABLE_BB_DC_OFFSET_TRACKING : 0) |
		(rfdc_track ? ENABLE_RF_OFFSET_TRACKING : 0));

	ad9361_spi_writef(spi, REG_RX_QUAD_GAIN2,
			 CORRECTION_WORD_DECIMATION_M(~0),
			 phy->pdata->qec_tracking_slow_mode_en ? 4 : 0);

	if (rxquad_track)
		qtrack = ENABLE_TRACKING_MODE_CH1 |
		(phy->pdata->rx2tx2 ? ENABLE_TRACKING_MODE_CH2 : 0);

	ad9361_spi_write(spi, REG_CALIBRATION_CONFIG_1,
		ENABLE_PHASE_CORR | ENABLE_GAIN_CORR |
		FREE_RUN_MODE | ENABLE_CORR_WORD_DECIMATION |
		qtrack);

	return 0;
}

/**
 * Enable/disable the VCO cal.
 * @param phy The AD9361 state structure.
 * @param rx Set true for rx.
 * @param enable Set true to enable.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_trx_vco_cal_control(struct ad9361_rf_phy *phy,
	bool tx, bool enable)
{
	dev_dbg(&phy->spi->dev, "%s : state %d",
		__func__, enable);

	return ad9361_spi_writef(phy->spi,
		tx ? REG_TX_PFD_CONFIG : REG_RX_PFD_CONFIG,
		BYPASS_LD_SYNTH, !enable);
}

/**
 * Enable/disable the ext. LO.
 * @param phy The AD9361 state structure.
 * @param rx Set true for rx.
 * @param enable Set true to enable.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_trx_ext_lo_control(struct ad9361_rf_phy *phy,
	bool tx, bool enable)
{
	int32_t val = enable ? ~0 : 0;

	dev_dbg(&phy->spi->dev, "%s : state %d",
		__func__, enable);

	if (tx) {
		ad9361_spi_writef(phy->spi, REG_ENSM_CONFIG_2,
			POWER_DOWN_TX_SYNTH, enable);

		ad9361_spi_writef(phy->spi, REG_RFPLL_DIVIDERS,
			TX_VCO_DIVIDER(~0), 0x7);

		ad9361_spi_write(phy->spi, REG_TX_SYNTH_POWER_DOWN_OVERRIDE,
			enable ? TX_SYNTH_VCO_ALC_POWER_DOWN |
			TX_SYNTH_PTAT_POWER_DOWN |
			TX_SYNTH_VCO_POWER_DOWN : 0);

		ad9361_spi_writef(phy->spi, REG_ANALOG_POWER_DOWN_OVERRIDE,
			TX_EXT_VCO_BUFFER_POWER_DOWN, !enable);

		return ad9361_spi_write(phy->spi, REG_TX_LO_GEN_POWER_MODE,
			TX_LO_GEN_POWER_MODE(val));
	}
	else {
		ad9361_spi_writef(phy->spi, REG_ENSM_CONFIG_2,
			POWER_DOWN_RX_SYNTH, enable);

		ad9361_spi_writef(phy->spi, REG_RFPLL_DIVIDERS,
			RX_VCO_DIVIDER(~0), 0x7);

		ad9361_spi_write(phy->spi, REG_RX_SYNTH_POWER_DOWN_OVERRIDE,
			enable ? RX_SYNTH_VCO_ALC_POWER_DOWN |
			RX_SYNTH_PTAT_POWER_DOWN |
			RX_SYNTH_VCO_POWER_DOWN : 0);

		ad9361_spi_writef(phy->spi, REG_ANALOG_POWER_DOWN_OVERRIDE,
			RX_EXT_VCO_BUFFER_POWER_DOWN, !enable);

		return ad9361_spi_write(phy->spi, REG_RX_LO_GEN_POWER_MODE,
			RX_LO_GEN_POWER_MODE(val));
	}
}

/**
 * Setup the reference clock delay unit counter register.
 * @param phy The AD9361 state structure.
 * @param ref_clk_hz The reference clock frequency [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_set_ref_clk_cycles(struct ad9361_rf_phy *phy,
	uint32_t ref_clk_hz)
{
	dev_dbg(&phy->spi->dev, "%s : ref_clk_hz %"PRIu32,
		__func__, ref_clk_hz);

	return ad9361_spi_write(phy->spi, REG_REFERENCE_CLOCK_CYCLES,
		REFERENCE_CLOCK_CYCLES_PER_US((ref_clk_hz / 1000000UL) - 1));
}

/**
 * Setup the DCXO tune.
 * @param phy The AD9361 state structure.
 * @param coarse The DCXO tune coarse.
 * @param fine The DCXO tune fine.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_set_dcxo_tune(struct ad9361_rf_phy *phy,
	uint32_t coarse, uint32_t fine)
{
	dev_dbg(&phy->spi->dev, "%s : coarse %"PRIu32" fine %"PRIu32,
		__func__, coarse, fine);

	ad9361_spi_write(phy->spi, REG_DCXO_COARSE_TUNE,
		DCXO_TUNE_COARSE(coarse));
	ad9361_spi_write(phy->spi, REG_DCXO_FINE_TUNE_LOW,
		DCXO_TUNE_FINE_LOW(fine));
	return ad9361_spi_write(phy->spi, REG_DCXO_FINE_TUNE_HIGH,
		DCXO_TUNE_FINE_HIGH(fine));
}

/**
 * Setup TXMON.
 * @param phy The AD9361 state structure.
 * @param ctrl The TXMON settings.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_txmon_setup(struct ad9361_rf_phy *phy,
			       struct tx_monitor_control *ctrl)
{
	struct spi_device *spi = phy->spi;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	ad9361_spi_write(spi, REG_TPM_MODE_ENABLE,
			 (ctrl->one_shot_mode_en ? ONE_SHOT_MODE : 0) |
			 TX_MON_DURATION(ilog2(ctrl->tx_mon_duration / 16)));

	ad9361_spi_write(spi, REG_TX_MON_DELAY, ctrl->tx_mon_delay);

	ad9361_spi_write(spi, REG_TX_MON_1_CONFIG,
			 TX_MON_1_LO_CM(ctrl->tx1_mon_lo_cm) |
			 TX_MON_1_GAIN(ctrl->tx1_mon_front_end_gain));
	ad9361_spi_write(spi, REG_TX_MON_2_CONFIG,
			 TX_MON_2_LO_CM(ctrl->tx2_mon_lo_cm) |
			 TX_MON_2_GAIN(ctrl->tx2_mon_front_end_gain));

	ad9361_spi_write(spi, REG_TX_ATTEN_THRESH,
			 ctrl->low_high_gain_threshold_mdB / 250);

	ad9361_spi_write(spi, REG_TX_MON_HIGH_GAIN,
			 TX_MON_HIGH_GAIN(ctrl->high_gain_dB));

	ad9361_spi_write(spi, REG_TX_MON_LOW_GAIN,
			 (ctrl->tx_mon_track_en ? TX_MON_TRACK : 0) |
			 TX_MON_LOW_GAIN(ctrl->low_gain_dB));

	return 0;
}

/**
 * Enable TXMON.
 * @param phy The AD9361 state structure.
 * @param en_mask The enable mask.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_txmon_control(struct ad9361_rf_phy *phy,
				int32_t en_mask)
{
	dev_dbg(&phy->spi->dev, "%s: mask 0x%"PRIx32, __func__, en_mask);

#if 0
	if (!phy->pdata->fdd && en_mask) {
		ad9361_spi_writef(phy->spi, REG_ENSM_CONFIG_1,
				ENABLE_RX_DATA_PORT_FOR_CAL, 1);
		phy->txmon_tdd_en = true;
	} else {
		ad9361_spi_writef(phy->spi, REG_ENSM_CONFIG_1,
				ENABLE_RX_DATA_PORT_FOR_CAL, 0);
		phy->txmon_tdd_en = false;
	}
#endif

	ad9361_spi_writef(phy->spi, REG_ANALOG_POWER_DOWN_OVERRIDE,
			TX_MONITOR_POWER_DOWN(~0), ~en_mask);

	ad9361_spi_writef(phy->spi, REG_TPM_MODE_ENABLE,
			TX1_MON_ENABLE, !!(en_mask & TX_1));

	return ad9361_spi_writef(phy->spi, REG_TPM_MODE_ENABLE,
			TX2_MON_ENABLE, !!(en_mask & TX_2));
}

/**
* Setup the RF port.
* Note:
* val
* 0	(RX1A_N &  RX1A_P) and (RX2A_N & RX2A_P) enabled; balanced
* 1	(RX1B_N &  RX1B_P) and (RX2B_N & RX2B_P) enabled; balanced
* 2	(RX1C_N &  RX1C_P) and (RX2C_N & RX2C_P) enabled; balanced
*
* 3	RX1A_N and RX2A_N enabled; unbalanced
* 4	RX1A_P and RX2A_P enabled; unbalanced
* 5	RX1B_N and RX2B_N enabled; unbalanced
* 6	RX1B_P and RX2B_P enabled; unbalanced
* 7	RX1C_N and RX2C_N enabled; unbalanced
* 8	RX1C_P and RX2C_P enabled; unbalanced
* @param phy The AD9361 state structure.
* @param rx_inputs RX input option identifier
* @param txb TX output option identifier
* @return 0 in case of success, negative error code otherwise.
*/
static int32_t ad9361_rf_port_setup(struct ad9361_rf_phy *phy, bool is_out,
				    uint32_t rx_inputs, uint32_t txb)
{
	uint32_t val;

	if (rx_inputs > 11)
		return -EINVAL;

	if (!is_out) {
		if (rx_inputs > 8)
			return ad9361_txmon_control(phy, rx_inputs & (TX_1 | TX_2));
		else
			ad9361_txmon_control(phy, 0);
	}

	if (rx_inputs < 3)
		val = 3 << (rx_inputs * 2);
	else
		val = 1 << (rx_inputs - 3);

	if (txb)
		val |= TX_OUTPUT; /* Select TX1B, TX2B */

	dev_dbg(&phy->spi->dev, "%s : INPUT_SELECT 0x%"PRIx32,
		__func__, val);

	return ad9361_spi_write(phy->spi, REG_INPUT_SELECT, val);
}

/**
 * Setup the Parallel Port (Digital Data Interface).
 * @param phy The AD9361 state structure.
 * @param restore_c3 Set true, will restore the Parallel Port Configuration 3
 *                   register.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_pp_port_setup(struct ad9361_rf_phy *phy, bool restore_c3)
{
	struct spi_device *spi = phy->spi;
	struct ad9361_phy_platform_data *pd = phy->pdata;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	if (restore_c3) {
		return ad9361_spi_write(spi, REG_PARALLEL_PORT_CONF_3,
			pd->port_ctrl.pp_conf[2]);
	}

	ad9361_spi_write(spi, REG_PARALLEL_PORT_CONF_1, pd->port_ctrl.pp_conf[0]);
	ad9361_spi_write(spi, REG_PARALLEL_PORT_CONF_2, pd->port_ctrl.pp_conf[1]);
	ad9361_spi_write(spi, REG_PARALLEL_PORT_CONF_3, pd->port_ctrl.pp_conf[2]);
	ad9361_spi_write(spi, REG_RX_CLOCK_DATA_DELAY, pd->port_ctrl.rx_clk_data_delay);
	ad9361_spi_write(spi, REG_TX_CLOCK_DATA_DELAY, pd->port_ctrl.tx_clk_data_delay);

	ad9361_spi_write(spi, REG_LVDS_BIAS_CTRL, pd->port_ctrl.lvds_bias_ctrl);
	//	ad9361_spi_write(spi, REG_DIGITAL_IO_CTRL, pd->port_ctrl.digital_io_ctrl);
	ad9361_spi_write(spi, REG_LVDS_INVERT_CTRL1, pd->port_ctrl.lvds_invert[0]);
	ad9361_spi_write(spi, REG_LVDS_INVERT_CTRL2, pd->port_ctrl.lvds_invert[1]);

	if (pd->rx1rx2_phase_inversion_en ||
		(pd->port_ctrl.pp_conf[1] & INVERT_RX2)) {

		ad9361_spi_writef(spi, REG_PARALLEL_PORT_CONF_2, INVERT_RX2, 1);
		ad9361_spi_writef(spi, REG_INVERT_BITS,
				  INVERT_RX2_RF_DC_CGOUT_WORD, 0);
	}

	return 0;
}

/**
 * Setup the Gain Control Blocks (common function for MGC, AGC modes)
 * @param phy The AD9361 state structure.
 * @param ctrl The gain control settings.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_gc_setup(struct ad9361_rf_phy *phy, struct gain_control *ctrl)
{
	struct spi_device *spi = phy->spi;
	uint32_t reg, tmp1, tmp2;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	reg = DEC_PWR_FOR_GAIN_LOCK_EXIT | DEC_PWR_FOR_LOCK_LEVEL |
		DEC_PWR_FOR_LOW_PWR;

	if (ctrl->rx1_mode == RF_GAIN_HYBRID_AGC ||
		ctrl->rx2_mode == RF_GAIN_HYBRID_AGC)
		reg |= SLOW_ATTACK_HYBRID_MODE;

	reg |= RX1_GAIN_CTRL_SETUP(ctrl->rx1_mode) |
		RX2_GAIN_CTRL_SETUP(ctrl->rx2_mode);

	phy->agc_mode[0] = ctrl->rx1_mode;
	phy->agc_mode[1] = ctrl->rx2_mode;

	ad9361_spi_write(spi, REG_AGC_CONFIG_1, reg); // Gain Control Mode Select

	/* AGC_USE_FULL_GAIN_TABLE handled in ad9361_load_gt() */
	ad9361_spi_writef(spi, REG_AGC_CONFIG_2, MAN_GAIN_CTRL_RX1,
		ctrl->mgc_rx1_ctrl_inp_en);
	ad9361_spi_writef(spi, REG_AGC_CONFIG_2, MAN_GAIN_CTRL_RX2,
		ctrl->mgc_rx2_ctrl_inp_en);
	ad9361_spi_writef(spi, REG_AGC_CONFIG_2, DIG_GAIN_EN,
		ctrl->dig_gain_en);

	ctrl->adc_ovr_sample_size = clamp_t(uint8_t, ctrl->adc_ovr_sample_size, 1U, 8U);
	reg = ADC_OVERRANGE_SAMPLE_SIZE(ctrl->adc_ovr_sample_size - 1);

	if (phy->pdata->split_gt &&
		(ctrl->mgc_rx1_ctrl_inp_en || ctrl->mgc_rx2_ctrl_inp_en)) {
		switch (ctrl->mgc_split_table_ctrl_inp_gain_mode) {
		case 1:
			reg &= ~INCDEC_LMT_GAIN;
			break;
		case 2:
			reg |= INCDEC_LMT_GAIN;
			break;
		default:
		case 0:
			reg |= USE_AGC_FOR_LMTLPF_GAIN;
			break;
		}
	}

	ctrl->mgc_inc_gain_step = clamp_t(uint8_t, ctrl->mgc_inc_gain_step, 1U, 8U);
	reg |= MANUAL_INCR_STEP_SIZE(ctrl->mgc_inc_gain_step - 1);
	ad9361_spi_write(spi, REG_AGC_CONFIG_3, reg); // Incr Step Size, ADC Overrange Size

	if (phy->pdata->split_gt) {
		reg = SIZE_SPLIT_TABLE - 1;
	}
	else {
		reg = SIZE_FULL_TABLE - 1;
	}
	ad9361_spi_write(spi, REG_MAX_LMT_FULL_GAIN, reg); // Max Full/LMT Gain Table Index
	ad9361_spi_write(spi, REG_RX1_MANUAL_LMT_FULL_GAIN, reg); // Rx1 Full/LMT Gain Index
	ad9361_spi_write(spi, REG_RX2_MANUAL_LMT_FULL_GAIN, reg); // Rx2 Full/LMT Gain Index

	ctrl->mgc_dec_gain_step = clamp_t(uint8_t, ctrl->mgc_dec_gain_step, 1U, 8U);
	reg = MANUAL_CTRL_IN_DECR_GAIN_STP_SIZE(ctrl->mgc_dec_gain_step);
	ad9361_spi_write(spi, REG_PEAK_WAIT_TIME, reg); // Decr Step Size, Peak Overload Time

	if (ctrl->dig_gain_en)
		ad9361_spi_write(spi, REG_DIGITAL_GAIN,
		MAXIMUM_DIGITAL_GAIN(ctrl->max_dig_gain) |
		DIG_GAIN_STP_SIZE(ctrl->dig_gain_step_size));

	if (ctrl->adc_large_overload_thresh >= ctrl->adc_small_overload_thresh) {
		ad9361_spi_write(spi, REG_ADC_SMALL_OVERLOAD_THRESH,
			ctrl->adc_small_overload_thresh); // ADC Small Overload Threshold
		ad9361_spi_write(spi, REG_ADC_LARGE_OVERLOAD_THRESH,
			ctrl->adc_large_overload_thresh); // ADC Large Overload Threshold
	}
	else {
		ad9361_spi_write(spi, REG_ADC_SMALL_OVERLOAD_THRESH,
			ctrl->adc_large_overload_thresh); // ADC Small Overload Threshold
		ad9361_spi_write(spi, REG_ADC_LARGE_OVERLOAD_THRESH,
			ctrl->adc_small_overload_thresh); // ADC Large Overload Threshold
	}

	reg = (ctrl->lmt_overload_high_thresh / 16) - 1;
	reg = clamp(reg, 0U, 63U);
	ad9361_spi_write(spi, REG_LARGE_LMT_OVERLOAD_THRESH, reg);
	reg = (ctrl->lmt_overload_low_thresh / 16) - 1;
	reg = clamp(reg, 0U, 63U);
	ad9361_spi_writef(spi, REG_SMALL_LMT_OVERLOAD_THRESH,
		SMALL_LMT_OVERLOAD_THRESH(~0), reg);

	if (phy->pdata->split_gt) {
		/* REVIST */
		ad9361_spi_write(spi, REG_RX1_MANUAL_LPF_GAIN, 0x58); // Rx1 LPF Gain Index
		ad9361_spi_write(spi, REG_RX2_MANUAL_LPF_GAIN, 0x18); // Rx2 LPF Gain Index
		ad9361_spi_write(spi, REG_FAST_INITIAL_LMT_GAIN_LIMIT, 0x27); // Initial LMT Gain Limit
	}

	ad9361_spi_write(spi, REG_RX1_MANUAL_DIGITALFORCED_GAIN, 0x00); // Rx1 Digital Gain Index
	ad9361_spi_write(spi, REG_RX2_MANUAL_DIGITALFORCED_GAIN, 0x00); // Rx2 Digital Gain Index

	reg = clamp_t(uint8_t, ctrl->low_power_thresh, 0U, 64U) * 2;
	ad9361_spi_write(spi, REG_FAST_LOW_POWER_THRESH, reg); // Low Power Threshold
	ad9361_spi_write(spi, REG_TX_SYMBOL_ATTEN_CONFIG, 0x00); // Tx Symbol Gain Control

	ad9361_spi_writef(spi, REG_DEC_POWER_MEASURE_DURATION_0,
		USE_HB1_OUT_FOR_DEC_PWR_MEAS, 1); // Power Measurement Duration

	ad9361_spi_writef(spi, REG_DEC_POWER_MEASURE_DURATION_0,
		ENABLE_DEC_PWR_MEAS, 1); // Power Measurement Duration

	if (ctrl->rx1_mode == RF_GAIN_FASTATTACK_AGC ||
		ctrl->rx2_mode == RF_GAIN_FASTATTACK_AGC)
		reg = ilog2(ctrl->f_agc_dec_pow_measuremnt_duration / 16);
	else
		reg = ilog2(ctrl->dec_pow_measuremnt_duration / 16);

	ad9361_spi_writef(spi, REG_DEC_POWER_MEASURE_DURATION_0,
		DEC_POWER_MEASUREMENT_DURATION(~0), reg); // Power Measurement Duration

	/* AGC */

	tmp1 = reg = clamp_t(uint8_t, ctrl->agc_inner_thresh_high, 0U, 127U);
	ad9361_spi_writef(spi, REG_AGC_LOCK_LEVEL,
		AGC_LOCK_LEVEL_FAST_AGC_INNER_HIGH_THRESH_SLOW(~0),
		reg);

	tmp2 = reg = clamp_t(uint8_t, ctrl->agc_inner_thresh_low, 0U, 127U);
	reg |= (ctrl->adc_lmt_small_overload_prevent_gain_inc ?
	PREVENT_GAIN_INC : 0);
	ad9361_spi_write(spi, REG_AGC_INNER_LOW_THRESH, reg);

	reg = AGC_OUTER_HIGH_THRESH(tmp1 - ctrl->agc_outer_thresh_high) |
		AGC_OUTER_LOW_THRESH(ctrl->agc_outer_thresh_low - tmp2);
	ad9361_spi_write(spi, REG_OUTER_POWER_THRESHS, reg);

	reg = AGC_OUTER_HIGH_THRESH_EXED_STP_SIZE(ctrl->agc_outer_thresh_high_dec_steps) |
		AGC_OUTER_LOW_THRESH_EXED_STP_SIZE(ctrl->agc_outer_thresh_low_inc_steps);
	ad9361_spi_write(spi, REG_GAIN_STP_2, reg);

	reg = ((ctrl->immed_gain_change_if_large_adc_overload) ?
	IMMED_GAIN_CHANGE_IF_LG_ADC_OVERLOAD : 0) |
										   ((ctrl->immed_gain_change_if_large_lmt_overload) ?
									   IMMED_GAIN_CHANGE_IF_LG_LMT_OVERLOAD : 0) |
																			  AGC_INNER_HIGH_THRESH_EXED_STP_SIZE(ctrl->agc_inner_thresh_high_dec_steps) |
																			  AGC_INNER_LOW_THRESH_EXED_STP_SIZE(ctrl->agc_inner_thresh_low_inc_steps);
	ad9361_spi_write(spi, REG_GAIN_STP1, reg);

	reg = LARGE_ADC_OVERLOAD_EXED_COUNTER(ctrl->adc_large_overload_exceed_counter) |
		SMALL_ADC_OVERLOAD_EXED_COUNTER(ctrl->adc_small_overload_exceed_counter);
	ad9361_spi_write(spi, REG_ADC_OVERLOAD_COUNTERS, reg);

	ad9361_spi_writef(spi, REG_GAIN_STP_CONFIG_2, LARGE_LPF_GAIN_STEP(~0),
		LARGE_LPF_GAIN_STEP(ctrl->adc_large_overload_inc_steps));

	reg = LARGE_LMT_OVERLOAD_EXED_COUNTER(ctrl->lmt_overload_large_exceed_counter) |
		SMALL_LMT_OVERLOAD_EXED_COUNTER(ctrl->lmt_overload_small_exceed_counter);
	ad9361_spi_write(spi, REG_LMT_OVERLOAD_COUNTERS, reg);

	ad9361_spi_writef(spi, REG_GAIN_STP_CONFIG1,
		DEC_STP_SIZE_FOR_LARGE_LMT_OVERLOAD(~0),
		ctrl->lmt_overload_large_inc_steps);

	reg = DIG_SATURATION_EXED_COUNTER(ctrl->dig_saturation_exceed_counter) |
		(ctrl->sync_for_gain_counter_en ?
	ENABLE_SYNC_FOR_GAIN_COUNTER : 0);
	ad9361_spi_write(spi, REG_DIGITAL_SAT_COUNTER, reg);

	/*
	* Fast AGC
	*/

	/* Fast AGC - Low Power */
	ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
		ENABLE_INCR_GAIN,
		ctrl->f_agc_allow_agc_gain_increase);

	ad9361_spi_write(spi, REG_FAST_INCREMENT_TIME,
		ctrl->f_agc_lp_thresh_increment_time);

	reg = ctrl->f_agc_lp_thresh_increment_steps - 1;
	reg = clamp_t(uint32_t, reg, 0U, 7U);
	ad9361_spi_writef(spi, REG_FAST_ENERGY_DETECT_COUNT,
		INCREMENT_GAIN_STP_LPFLMT(~0), reg);

	/* Fast AGC - Lock Level */
	/* Dual use see also agc_inner_thresh_high */
	ad9361_spi_writef(spi, REG_FAST_CONFIG_2_SETTLING_DELAY,
		ENABLE_LMT_GAIN_INC_FOR_LOCK_LEVEL,
		ctrl->f_agc_lock_level_lmt_gain_increase_en);

	reg = ctrl->f_agc_lock_level_gain_increase_upper_limit;
	reg = clamp_t(uint32_t, reg, 0U, 63U);
	ad9361_spi_writef(spi, REG_FAST_AGCLL_UPPER_LIMIT,
		AGCLL_MAX_INCREASE(~0), reg);

	/* Fast AGC - Peak Detectors and Final Settling */
	reg = ctrl->f_agc_lpf_final_settling_steps;
	reg = clamp_t(uint32_t, reg, 0U, 3U);
	ad9361_spi_writef(spi, REG_FAST_ENERGY_LOST_THRESH,
		POST_LOCK_LEVEL_STP_SIZE_FOR_LPF_TABLE_FULL_TABLE(~0),
		reg);

	reg = ctrl->f_agc_lmt_final_settling_steps;
	reg = clamp_t(uint32_t, reg, 0U, 3U);
	ad9361_spi_writef(spi, REG_FAST_STRONGER_SIGNAL_THRESH,
		POST_LOCK_LEVEL_STP_FOR_LMT_TABLE(~0), reg);

	reg = ctrl->f_agc_final_overrange_count;
	reg = clamp_t(uint32_t, reg, 0U, 7U);
	ad9361_spi_writef(spi, REG_FAST_FINAL_OVER_RANGE_AND_OPT_GAIN,
		FINAL_OVER_RANGE_COUNT(~0), reg);

	/* Fast AGC - Final Power Test */
	ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
		ENABLE_GAIN_INC_AFTER_GAIN_LOCK,
		ctrl->f_agc_gain_increase_after_gain_lock_en);

	/* Fast AGC - Unlocking the Gain */
	/* 0 = MAX Gain, 1 = Optimized Gain, 2 = Set Gain */

	reg = ctrl->f_agc_gain_index_type_after_exit_rx_mode;
	ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
		GOTO_SET_GAIN_IF_EXIT_RX_STATE, reg == SET_GAIN);
	ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
		GOTO_OPTIMIZED_GAIN_IF_EXIT_RX_STATE,
		reg == OPTIMIZED_GAIN);

	ad9361_spi_writef(spi, REG_FAST_CONFIG_2_SETTLING_DELAY,
		USE_LAST_LOCK_LEVEL_FOR_SET_GAIN,
		ctrl->f_agc_use_last_lock_level_for_set_gain_en);

	reg = ctrl->f_agc_optimized_gain_offset;
	reg = clamp_t(uint32_t, reg, 0U, 15U);
	ad9361_spi_writef(spi, REG_FAST_FINAL_OVER_RANGE_AND_OPT_GAIN,
		OPTIMIZE_GAIN_OFFSET(~0), reg);

	tmp1 = !ctrl->f_agc_rst_gla_stronger_sig_thresh_exceeded_en ||
		!ctrl->f_agc_rst_gla_engergy_lost_sig_thresh_exceeded_en ||
		!ctrl->f_agc_rst_gla_large_adc_overload_en ||
		!ctrl->f_agc_rst_gla_large_lmt_overload_en ||
		ctrl->f_agc_rst_gla_en_agc_pulled_high_en;

	ad9361_spi_writef(spi, REG_AGC_CONFIG_2,
		AGC_GAIN_UNLOCK_CTRL, tmp1);

	reg = !ctrl->f_agc_rst_gla_stronger_sig_thresh_exceeded_en;
	ad9361_spi_writef(spi, REG_FAST_STRONG_SIGNAL_FREEZE,
		DONT_UNLOCK_GAIN_IF_STRONGER_SIGNAL, reg);

	reg = ctrl->f_agc_rst_gla_stronger_sig_thresh_above_ll;
	reg = clamp_t(uint32_t, reg, 0U, 63U);
	ad9361_spi_writef(spi, REG_FAST_STRONGER_SIGNAL_THRESH,
		STRONGER_SIGNAL_THRESH(~0), reg);

	reg = ctrl->f_agc_rst_gla_engergy_lost_goto_optim_gain_en;
	ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
		GOTO_OPT_GAIN_IF_ENERGY_LOST_OR_EN_AGC_HIGH, reg);

	reg = !ctrl->f_agc_rst_gla_engergy_lost_sig_thresh_exceeded_en;
	ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
		DONT_UNLOCK_GAIN_IF_ENERGY_LOST, reg);

	reg = ctrl->f_agc_energy_lost_stronger_sig_gain_lock_exit_cnt;
	reg = clamp_t(uint32_t, reg, 0U, 63U);
	ad9361_spi_writef(spi, REG_FAST_GAIN_LOCK_EXIT_COUNT,
		GAIN_LOCK_EXIT_COUNT(~0), reg);

	reg = !ctrl->f_agc_rst_gla_large_adc_overload_en ||
		!ctrl->f_agc_rst_gla_large_lmt_overload_en;
	ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
		DONT_UNLOCK_GAIN_IF_LG_ADC_OR_LMT_OVRG, reg);

	reg = !ctrl->f_agc_rst_gla_large_adc_overload_en;
	ad9361_spi_writef(spi, REG_FAST_LOW_POWER_THRESH,
		DONT_UNLOCK_GAIN_IF_ADC_OVRG, reg);

	/* 0 = Max Gain, 1 = Set Gain, 2 = Optimized Gain, 3 = No Gain Change */

	if (ctrl->f_agc_rst_gla_en_agc_pulled_high_en) {
		switch (ctrl->f_agc_rst_gla_if_en_agc_pulled_high_mode) {
		case MAX_GAIN:
			ad9361_spi_writef(spi, REG_FAST_CONFIG_2_SETTLING_DELAY,
				GOTO_MAX_GAIN_OR_OPT_GAIN_IF_EN_AGC_HIGH, 1);

			ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
				GOTO_SET_GAIN_IF_EN_AGC_HIGH, 0);

			ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
				GOTO_OPT_GAIN_IF_ENERGY_LOST_OR_EN_AGC_HIGH, 0);
			break;
		case SET_GAIN:
			ad9361_spi_writef(spi, REG_FAST_CONFIG_2_SETTLING_DELAY,
				GOTO_MAX_GAIN_OR_OPT_GAIN_IF_EN_AGC_HIGH, 0);

			ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
				GOTO_SET_GAIN_IF_EN_AGC_HIGH, 1);
			break;
		case OPTIMIZED_GAIN:
			ad9361_spi_writef(spi, REG_FAST_CONFIG_2_SETTLING_DELAY,
				GOTO_MAX_GAIN_OR_OPT_GAIN_IF_EN_AGC_HIGH, 1);

			ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
				GOTO_SET_GAIN_IF_EN_AGC_HIGH, 0);

			ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
				GOTO_OPT_GAIN_IF_ENERGY_LOST_OR_EN_AGC_HIGH, 1);
			break;
		case NO_GAIN_CHANGE:
			ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
				GOTO_SET_GAIN_IF_EN_AGC_HIGH, 0);
			ad9361_spi_writef(spi, REG_FAST_CONFIG_2_SETTLING_DELAY,
				GOTO_MAX_GAIN_OR_OPT_GAIN_IF_EN_AGC_HIGH, 0);
			break;
		}
	}
	else {
		ad9361_spi_writef(spi, REG_FAST_CONFIG_1,
			GOTO_SET_GAIN_IF_EN_AGC_HIGH, 0);
		ad9361_spi_writef(spi, REG_FAST_CONFIG_2_SETTLING_DELAY,
			GOTO_MAX_GAIN_OR_OPT_GAIN_IF_EN_AGC_HIGH, 0);
	}

	reg = ilog2(ctrl->f_agc_power_measurement_duration_in_state5 / 16);
	reg = clamp_t(uint32_t, reg, 0U, 15U);
	ad9361_spi_writef(spi, REG_RX1_MANUAL_LPF_GAIN,
		POWER_MEAS_IN_STATE_5(~0), reg);
	ad9361_spi_writef(spi, REG_RX1_MANUAL_LMT_FULL_GAIN,
		POWER_MEAS_IN_STATE_5_MSB, reg >> 3);

	return ad9361_gc_update(phy);
}

/**
 * Set the Aux DAC.
 * @param phy The AD9361 state structure.
 * @param dac The DAC.
 * @param val_mV The value.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_auxdac_set(struct ad9361_rf_phy *phy, int32_t dac,
	int32_t val_mV)
{
	struct spi_device *spi = phy->spi;
	uint32_t val, tmp;

	dev_dbg(&phy->spi->dev, "%s DAC%"PRId32" = %"PRId32" mV", __func__, dac, val_mV);

	/* Disable DAC if val == 0, Ignored in ENSM Auto Mode */
	ad9361_spi_writef(spi, REG_AUXDAC_ENABLE_CTRL,
		AUXDAC_MANUAL_BAR(dac), val_mV ? 0 : 1);

	if (val_mV < 306)
		val_mV = 306;

	if (val_mV < 1888) {
		val = ((val_mV - 306) * 1000) / 1404; /* Vref = 1V, Step = 2 */
		tmp = AUXDAC_1_VREF(0);
	}
	else {
		val = ((val_mV - 1761) * 1000) / 1836; /* Vref = 2.5V, Step = 2 */
		tmp = AUXDAC_1_VREF(3);
	}

	val = clamp_t(uint32_t, val, 0, 1023);

	switch (dac) {
	case 1:
		ad9361_spi_write(spi, REG_AUXDAC_1_WORD, val >> 2);
		ad9361_spi_write(spi, REG_AUXDAC_1_CONFIG, AUXDAC_1_WORD_LSB(val) | tmp);
		phy->auxdac1_value = val_mV;
		break;
	case 2:
		ad9361_spi_write(spi, REG_AUXDAC_2_WORD, val >> 2);
		ad9361_spi_write(spi, REG_AUXDAC_2_CONFIG, AUXDAC_2_WORD_LSB(val) | tmp);
		phy->auxdac2_value = val_mV;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * Get the Aux DAC value.
 * @param phy The AD9361 state structure.
 * @param dac The DAC.
 * @return The value in case of success, negative error code otherwise.
 */
int32_t ad9361_auxdac_get(struct ad9361_rf_phy *phy, int32_t dac)
{

	switch (dac) {
	case 1:
		return phy->auxdac1_value;
	case 2:
		return phy->auxdac2_value;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * Setup the AuxDAC.
 * @param phy The AD9361 state structure.
 * @param ctrl Pointer to auxdac_control structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_auxdac_setup(struct ad9361_rf_phy *phy,
struct auxdac_control *ctrl)
{
	struct spi_device *spi = phy->spi;
	uint8_t tmp;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	ad9361_auxdac_set(phy, 1, ctrl->dac1_default_value);
	ad9361_auxdac_set(phy, 2, ctrl->dac2_default_value);

	tmp = ~(AUXDAC_AUTO_TX_BAR(ctrl->dac2_in_tx_en << 1 | ctrl->dac1_in_tx_en) |
		AUXDAC_AUTO_RX_BAR(ctrl->dac2_in_rx_en << 1 | ctrl->dac1_in_rx_en) |
		AUXDAC_INIT_BAR(ctrl->dac2_in_alert_en << 1 | ctrl->dac1_in_alert_en));

	ad9361_spi_writef(spi, REG_AUXDAC_ENABLE_CTRL,
		AUXDAC_AUTO_TX_BAR(~0) |
		AUXDAC_AUTO_RX_BAR(~0) |
		AUXDAC_INIT_BAR(~0),
		tmp); /* Auto Control */

	ad9361_spi_writef(spi, REG_EXTERNAL_LNA_CTRL,
		AUXDAC_MANUAL_SELECT, ctrl->auxdac_manual_mode_en);
	ad9361_spi_write(spi, REG_AUXDAC1_RX_DELAY, ctrl->dac1_rx_delay_us);
	ad9361_spi_write(spi, REG_AUXDAC1_TX_DELAY, ctrl->dac1_tx_delay_us);
	ad9361_spi_write(spi, REG_AUXDAC2_RX_DELAY, ctrl->dac2_rx_delay_us);
	ad9361_spi_write(spi, REG_AUXDAC2_TX_DELAY, ctrl->dac2_tx_delay_us);

	return 0;
}

/**
 * Setup the AuxADC.
 * @param phy The AD9361 state structure.
 * @param ctrl The AuxADC settings.
 * @param bbpll_freq The BBPLL frequency [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_auxadc_setup(struct ad9361_rf_phy *phy,
struct auxadc_control *ctrl,
	uint32_t bbpll_freq)
{
	struct spi_device *spi = phy->spi;
	uint32_t val;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	val = DIV_ROUND_CLOSEST(ctrl->temp_time_inteval_ms *
		(bbpll_freq / 1000UL), (1 << 29));

	ad9361_spi_write(spi, REG_TEMP_OFFSET, ctrl->offset);
	ad9361_spi_write(spi, REG_START_TEMP_READING, 0x00);
	ad9361_spi_write(spi, REG_TEMP_SENSE2,
		MEASUREMENT_TIME_INTERVAL(val) |
		(ctrl->periodic_temp_measuremnt ?
	TEMP_SENSE_PERIODIC_ENABLE : 0));
	ad9361_spi_write(spi, REG_TEMP_SENSOR_CONFIG,
		TEMP_SENSOR_DECIMATION(
		ilog2(ctrl->temp_sensor_decimation) - 8));
	ad9361_spi_write(spi, REG_AUXADC_CLOCK_DIVIDER,
		bbpll_freq / ctrl->auxadc_clock_rate);
	ad9361_spi_write(spi, REG_AUXADC_CONFIG,
		AUX_ADC_DECIMATION(
		ilog2(ctrl->auxadc_decimation) - 8));

	return 0;
}

/**
 * Get the measured temperature of the device.
 * @param phy The AD9361 state structure.
 * @return The measured temperature of the device.
 */
int32_t ad9361_get_temp(struct ad9361_rf_phy *phy)
{
	uint32_t val;

	ad9361_spi_writef(phy->spi, REG_AUXADC_CONFIG, AUXADC_POWER_DOWN, 1);
	val = ad9361_spi_read(phy->spi, REG_TEMPERATURE);
	ad9361_spi_writef(phy->spi, REG_AUXADC_CONFIG, AUXADC_POWER_DOWN, 0);

	return DIV_ROUND_CLOSEST(val * 1000000, 1140);
}

/**
 * Get the Aux ADC value.
 * @param phy The AD9361 state structure.
 * @return The value in case of success, negative error code otherwise.
 */
int32_t ad9361_get_auxadc(struct ad9361_rf_phy *phy)
{
	uint8_t buf[2];

	ad9361_spi_writef(phy->spi, REG_AUXADC_CONFIG, AUXADC_POWER_DOWN, 1);
	ad9361_spi_readm(phy->spi, REG_AUXADC_LSB, buf, 2);
	ad9361_spi_writef(phy->spi, REG_AUXADC_CONFIG, AUXADC_POWER_DOWN, 0);

	return (buf[1] << 4) | AUXADC_WORD_LSB(buf[0]);
}

/**
 * Setup the Control Output pins.
 * @param phy The AD9361 state structure.
 * @param ctrl The Control Output pins settings.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_ctrl_outs_setup(struct ad9361_rf_phy *phy,
struct ctrl_outs_control *ctrl)
{
	struct spi_device *spi = phy->spi;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	ad9361_spi_write(spi, REG_CTRL_OUTPUT_POINTER, ctrl->index); // Ctrl Out index
	return ad9361_spi_write(spi, REG_CTRL_OUTPUT_ENABLE, ctrl->en_mask); // Ctrl Out [7:0] output enable
}

/**
 * Setup the GPO pins.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_gpo_setup(struct ad9361_rf_phy *phy)
{
	struct spi_device *spi = phy->spi;
	/* FIXME later */

	dev_dbg(&phy->spi->dev, "%s", __func__);

	ad9361_spi_write(spi, 0x020, 0x00); // GPO Auto Enable Setup in RX and TX
	ad9361_spi_write(spi, 0x027, 0x03); // GPO Manual and GPO auto value in ALERT
	ad9361_spi_write(spi, 0x028, 0x00); // GPO_0 RX Delay
	ad9361_spi_write(spi, 0x029, 0x00); // GPO_1 RX Delay
	ad9361_spi_write(spi, 0x02a, 0x00); // GPO_2 RX Delay
	ad9361_spi_write(spi, 0x02b, 0x00); // GPO_3 RX Delay
	ad9361_spi_write(spi, 0x02c, 0x00); // GPO_0 TX Delay
	ad9361_spi_write(spi, 0x02d, 0x00); // GPO_1 TX Delay
	ad9361_spi_write(spi, 0x02e, 0x00); // GPO_2 TX Delay
	ad9361_spi_write(spi, 0x02f, 0x00); // GPO_3 TX Delay

	return 0;
}

/**
 * Setup the RSSI.
 * @param phy The AD9361 state structure.
 * @param ctrl The RSSI settings.
 * @param is_update True if update
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_rssi_setup(struct ad9361_rf_phy *phy,
struct rssi_control *ctrl,
	bool is_update)
{
	struct spi_device *spi = phy->spi;
	uint32_t total_weight, weight[4], total_dur = 0, temp;
	uint8_t dur_buf[4] = { 0 };
	int32_t val, ret, i, j = 0;
	uint32_t rssi_delay;
	uint32_t rssi_wait;
	uint32_t rssi_duration;
	uint32_t rate;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	if (ctrl->rssi_unit_is_rx_samples) {
		if (is_update)
			return 0; /* no update required */

		rssi_delay = ctrl->rssi_delay;
		rssi_wait = ctrl->rssi_wait;
		rssi_duration = ctrl->rssi_duration;
	}
	else {
		/* update sample based on RX rate */
		rate = DIV_ROUND_CLOSEST(
			clk_get_rate(phy, phy->ref_clk_scale[RX_SAMPL_CLK]), 1000);
		/* units are in us */
		rssi_delay = DIV_ROUND_CLOSEST(ctrl->rssi_delay * rate, 1000);
		rssi_wait = DIV_ROUND_CLOSEST(ctrl->rssi_wait * rate, 1000);
		rssi_duration = DIV_ROUND_CLOSEST(
			ctrl->rssi_duration * rate, 1000);
	}

	if (ctrl->restart_mode == EN_AGC_PIN_IS_PULLED_HIGH)
		rssi_delay = 0;

	rssi_delay = clamp(rssi_delay / 8, 0U, 255U);
	rssi_wait = clamp(rssi_wait / 4, 0U, 255U);

	do {
		for (i = 14; rssi_duration > 0 && i >= 0; i--) {
			val = 1 << i;
			if ((int64_t)rssi_duration >= val) {
				dur_buf[j++] = i;
				total_dur += val;
				rssi_duration -= val;
				break;
			}
		}

	} while (j < 4 && rssi_duration > 0);

	for (i = 0, total_weight = 0; i < 4; i++)
		total_weight += weight[i] =
		DIV_ROUND_CLOSEST(RSSI_MAX_WEIGHT *
		(1 << dur_buf[i]), total_dur);

	/* total of all weights must be 0xFF */
	val = total_weight - 0xFF;
	weight[j - 1] -= val;

	ad9361_spi_write(spi, REG_MEASURE_DURATION_01,
		(dur_buf[1] << 4) | dur_buf[0]); // RSSI Measurement Duration 0, 1
	ad9361_spi_write(spi, REG_MEASURE_DURATION_23,
		(dur_buf[3] << 4) | dur_buf[2]); // RSSI Measurement Duration 2, 3
	ad9361_spi_write(spi, REG_RSSI_WEIGHT_0, weight[0]); // RSSI Weighted Multiplier 0
	ad9361_spi_write(spi, REG_RSSI_WEIGHT_1, weight[1]); // RSSI Weighted Multiplier 1
	ad9361_spi_write(spi, REG_RSSI_WEIGHT_2, weight[2]); // RSSI Weighted Multiplier 2
	ad9361_spi_write(spi, REG_RSSI_WEIGHT_3, weight[3]); // RSSI Weighted Multiplier 3
	ad9361_spi_write(spi, REG_RSSI_DELAY, rssi_delay); // RSSI Delay
	ad9361_spi_write(spi, REG_RSSI_WAIT_TIME, rssi_wait); // RSSI Wait

	temp = RSSI_MODE_SELECT(ctrl->restart_mode);
	if (ctrl->restart_mode == SPI_WRITE_TO_REGISTER)
		temp |= START_RSSI_MEAS;

	ret = ad9361_spi_write(spi, REG_RSSI_CONFIG, temp); // RSSI Mode Select

	if (ret < 0)
		dev_err(&phy->spi->dev, "Unable to write rssi config");

	return 0;
}

/**
 * This function needs to be called whenever BBPLL changes.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_bb_clk_change_handler(struct ad9361_rf_phy *phy)
{
	int32_t ret;

	ret = ad9361_gc_update(phy);
	ret |= ad9361_rssi_setup(phy, &phy->pdata->rssi_ctrl, true);
	ret |= ad9361_auxadc_setup(phy, &phy->pdata->auxadc_ctrl,
		clk_get_rate(phy, phy->ref_clk_scale[BBPLL_CLK]));

	return ret;
}

/**
 * Set the desired Enable State Machine (ENSM) state.
 * @param phy The AD9361 state structure.
 * @param ensm_state The ENSM state [ENSM_STATE_SLEEP_WAIT, ENSM_STATE_ALERT,
 *                   ENSM_STATE_TX, ENSM_STATE_TX_FLUSH, ENSM_STATE_RX,
 *                   ENSM_STATE_RX_FLUSH, ENSM_STATE_FDD, ENSM_STATE_FDD_FLUSH].
 * @param pinctrl Set true, will enable the ENSM pin control.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_ensm_set_state(struct ad9361_rf_phy *phy, uint8_t ensm_state,
	bool pinctrl)
{
	struct spi_device *spi = phy->spi;
	int32_t rc = 0;
	uint32_t val;
	uint32_t tmp;

	// 	if (phy->curr_ensm_state == ensm_state) {
	// 		dev_dbg(dev, "Nothing to do, device is already in %d state",
	// 			ensm_state);
	// 		goto out;
	// 	}

	dev_dbg(dev, "Device is in %x state, moving to %x", phy->curr_ensm_state,
		ensm_state);


	if (phy->curr_ensm_state == ENSM_STATE_SLEEP) {
		ad9361_spi_write(spi, REG_CLOCK_ENABLE,
			DIGITAL_POWER_UP | CLOCK_ENABLE_DFLT | BBPLL_ENABLE |
			(phy->pdata->use_extclk ? XO_BYPASS : 0)); /* Enable Clocks */
		udelay(20);
		ad9361_spi_write(spi, REG_ENSM_CONFIG_1, TO_ALERT | FORCE_ALERT_STATE);
		ad9361_trx_vco_cal_control(phy, false, true); /* Enable VCO Cal */
		ad9361_trx_vco_cal_control(phy, true, true);
	}

	val = (phy->pdata->ensm_pin_pulse_mode ? 0 : LEVEL_MODE) |
		(pinctrl ? ENABLE_ENSM_PIN_CTRL : 0) |
		(phy->txmon_tdd_en ? ENABLE_RX_DATA_PORT_FOR_CAL : 0) |
		TO_ALERT;

	switch (ensm_state) {
	case ENSM_STATE_TX:
		val |= FORCE_TX_ON;
		if (phy->pdata->fdd)
			rc = -EINVAL;
		else if (phy->curr_ensm_state != ENSM_STATE_ALERT)
			rc = -EINVAL;
		break;
	case ENSM_STATE_RX:
		val |= FORCE_RX_ON;
		if (phy->pdata->fdd)
			rc = -EINVAL;
		else if (phy->curr_ensm_state != ENSM_STATE_ALERT)
			rc = -EINVAL;
		break;
	case ENSM_STATE_FDD:
		val |= (FORCE_TX_ON | FORCE_RX_ON);
		if (!phy->pdata->fdd)
			rc = -EINVAL;
		break;
	case ENSM_STATE_ALERT:
		val &= ~(FORCE_TX_ON | FORCE_RX_ON);
		val |= TO_ALERT | FORCE_ALERT_STATE;
		break;
	case ENSM_STATE_SLEEP_WAIT:
		break;
	case ENSM_STATE_SLEEP:
		ad9361_trx_vco_cal_control(phy, false, false); /* Disable VCO Cal */
		ad9361_trx_vco_cal_control(phy, true, false);
		ad9361_spi_write(spi, REG_ENSM_CONFIG_1, 0); /* Clear To Alert */
		ad9361_spi_write(spi, REG_ENSM_CONFIG_1,
			phy->pdata->fdd ? FORCE_TX_ON : FORCE_RX_ON);
		/* Delay Flush Time 384 ADC clock cycles */
		udelay(384000000UL / clk_get_rate(phy, phy->ref_clk_scale[ADC_CLK]));
		ad9361_spi_write(spi, REG_ENSM_CONFIG_1, 0); /* Move to Wait*/
		udelay(1); /* Wait for ENSM settle */
		ad9361_spi_write(spi, REG_CLOCK_ENABLE, 0); /* Turn off all clocks */
		phy->curr_ensm_state = ensm_state;
		return 0;

	default:
		dev_err(dev, "No handling for forcing %d ensm state",
			ensm_state);
		goto out;
	}

	if (rc) {
		dev_err(dev, "Invalid ENSM state transition in %s mode",
			phy->pdata->fdd ? "FDD" : "TDD");
		goto out;
	}

	rc = ad9361_spi_write(spi, REG_ENSM_CONFIG_1, val);
	if (rc)
		dev_err(dev, "Failed to restore state");

	if ((val & FORCE_RX_ON) &&
		(phy->agc_mode[0] == RF_GAIN_MGC ||
		 phy->agc_mode[1] == RF_GAIN_MGC)) {
		tmp = ad9361_spi_read(spi, REG_SMALL_LMT_OVERLOAD_THRESH);
		ad9361_spi_write(spi, REG_SMALL_LMT_OVERLOAD_THRESH,
			(tmp & SMALL_LMT_OVERLOAD_THRESH(~0)) |
			(phy->agc_mode[0] == RF_GAIN_MGC ? FORCE_PD_RESET_RX1 : 0) |
			(phy->agc_mode[1] == RF_GAIN_MGC ? FORCE_PD_RESET_RX2 : 0));
		ad9361_spi_write(spi, REG_SMALL_LMT_OVERLOAD_THRESH,
				 tmp & SMALL_LMT_OVERLOAD_THRESH(~0));
	}

	phy->curr_ensm_state = ensm_state;

out:
	return rc;

}

/**
 * Check if at least one of the clock rates is equal to the DATA_CLK (lvds) rate.
 * @param phy The AD9361 state structure.
 * @param rx_path_clks RX path rates buffer.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_validate_trx_clock_chain(struct ad9361_rf_phy *phy,
		uint32_t *rx_path_clks)
{
	int i;
	uint32_t data_clk;

	data_clk = (phy->pdata->rx2tx2 ? 4 : 2) * rx_path_clks[RX_SAMPL_FREQ];

	for (i = ADC_FREQ; i < RX_SAMPL_CLK; i++) {
		if (abs(rx_path_clks[i] - data_clk) < 4)
			return 0;
	}

	dev_err(&phy->spi->dev, "%s: Failed - at least one of the clock rates"
		"must be equal to the DATA_CLK (lvds) rate", __func__);

	return -EINVAL;
}

/**
 * Set the RX and TX path rates.
 * @param phy The AD9361 state structure.
 * @param rx_path_clks RX path rates buffer.
 * @param tx_path_clks TX path rates buffer.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_set_trx_clock_chain(struct ad9361_rf_phy *phy,
	uint32_t *rx_path_clks,
	uint32_t *tx_path_clks)
{
	int32_t ret, i, j, n;

	dev_dbg(&phy->spi->dev, "%s", __func__);

	if (!rx_path_clks || !tx_path_clks)
		return -EINVAL;

	dev_dbg(&phy->spi->dev, "%s: %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32,
		__func__, rx_path_clks[BBPLL_FREQ], rx_path_clks[ADC_FREQ],
		rx_path_clks[R2_FREQ], rx_path_clks[R1_FREQ],
		rx_path_clks[CLKRF_FREQ], rx_path_clks[RX_SAMPL_FREQ]);

	dev_dbg(&phy->spi->dev, "%s: %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32,
		__func__, tx_path_clks[BBPLL_FREQ], tx_path_clks[ADC_FREQ],
		tx_path_clks[R2_FREQ], tx_path_clks[R1_FREQ],
		tx_path_clks[CLKRF_FREQ], tx_path_clks[RX_SAMPL_FREQ]);

	ret = ad9361_validate_trx_clock_chain(phy, rx_path_clks);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(phy, phy->ref_clk_scale[BBPLL_CLK], rx_path_clks[BBPLL_FREQ]);
	if (ret < 0)
		return ret;

	for (i = ADC_CLK, j = DAC_CLK, n = ADC_FREQ;
		i <= RX_SAMPL_CLK; i++, j++, n++) {
		ret = clk_set_rate(phy, phy->ref_clk_scale[i], rx_path_clks[n]);
		if (ret < 0) {
			dev_err(dev, "Failed to set BB ref clock rate (%"PRId32")",
				ret);
			return ret;
		}
		ret = clk_set_rate(phy, phy->ref_clk_scale[j], tx_path_clks[n]);
		if (ret < 0) {
			dev_err(dev, "Failed to set BB ref clock rate (%"PRId32")",
				ret);
			return ret;
		}
	}
	return ad9361_bb_clk_change_handler(phy);
}

/**
 * Get the RX and TX path rates.
 * @param phy The AD9361 state structure.
 * @param rx_path_clks RX path rates buffer.
 * @param tx_path_clks TX path rates buffer.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_get_trx_clock_chain(struct ad9361_rf_phy *phy, uint32_t *rx_path_clks,
	uint32_t *tx_path_clks)
{
	int32_t i, j, n;
	uint32_t bbpll_freq;

	if (!rx_path_clks && !tx_path_clks)
		return -EINVAL;

	bbpll_freq = clk_get_rate(phy, phy->ref_clk_scale[BBPLL_CLK]);

	if (rx_path_clks)
		rx_path_clks[BBPLL_FREQ] = bbpll_freq;

	if (tx_path_clks)
		tx_path_clks[BBPLL_FREQ] = bbpll_freq;

	for (i = ADC_CLK, j = DAC_CLK, n = ADC_FREQ;
		i <= RX_SAMPL_CLK; i++, j++, n++) {
		if (rx_path_clks)
			rx_path_clks[n] = clk_get_rate(phy, phy->ref_clk_scale[i]);
		if (tx_path_clks)
			tx_path_clks[n] = clk_get_rate(phy, phy->ref_clk_scale[j]);
	}

	return 0;
}

/**
 * Calculate the RX and TX path rates to obtain the desired sample rate.
 * @param phy The AD9361 state structure.
 * @param tx_sample_rate The desired sample rate.
 * @param rate_gov The rate governor option.
 * @param rx_path_clks RX path rates buffer.
 * @param tx_path_clks TX path rates buffer.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_calculate_rf_clock_chain(struct ad9361_rf_phy *phy,
	uint32_t tx_sample_rate,
	uint32_t rate_gov,
	uint32_t *rx_path_clks,
	uint32_t *tx_path_clks)
{
	uint32_t clktf, clkrf, adc_rate = 0, dac_rate = 0;
	uint64_t bbpll_rate;
	int32_t i, index_rx = -1, index_tx = -1, tmp;
	uint32_t div, tx_intdec, rx_intdec;
	const int8_t clk_dividers[][4] = {
		{ 12, 3, 2, 2 },
		{ 8, 2, 2, 2 },
		{ 6, 3, 1, 2 },
		{ 4, 2, 2, 1 },
		{ 3, 3, 1, 1 },
		{ 2, 2, 1, 1 },
		{ 1, 1, 1, 1 },
	};

	if (phy->bypass_rx_fir)
		rx_intdec = 1;
	else
		rx_intdec = phy->rx_fir_dec;

	if (phy->bypass_tx_fir)
		tx_intdec = 1;
	else
		tx_intdec = phy->tx_fir_int;

	dev_dbg(&phy->spi->dev, "%s: requested rate %"PRIu32" TXFIR int %"PRIu32" RXFIR dec %"PRIu32" mode %s",
		__func__, tx_sample_rate, tx_intdec, rx_intdec,
		rate_gov ? "Nominal" : "Highest OSR");

	if (tx_sample_rate > (phy->pdata->rx2tx2 ? 61440000UL : 122880000UL))
		return -EINVAL;

	clktf = tx_sample_rate * tx_intdec;
	clkrf = tx_sample_rate * rx_intdec * (phy->rx_eq_2tx ? 2 : 1);

	for (i = rate_gov; i < 7; i++) {
		adc_rate = clkrf * clk_dividers[i][0];
		dac_rate = clktf * clk_dividers[i][0];

		if ((adc_rate <= MAX_ADC_CLK) && (adc_rate >= MIN_ADC_CLK)) {


			if (dac_rate > adc_rate)
				tmp = (dac_rate / adc_rate) * -1;
			else
				tmp = adc_rate / dac_rate;

			if (adc_rate <= MAX_DAC_CLK) {
				index_rx = i;
				index_tx = i - ((tmp == 1) ? 0 : tmp);
				dac_rate = adc_rate; /* ADC_CLK */
				break;
			}
			else {
				dac_rate = adc_rate / 2;  /* ADC_CLK/2 */
				index_rx = i;

				if (i == 4 && tmp >= 0)
					index_tx = 7; /* STOP: 3/2 != 1 */
				else
					index_tx = i + ((i == 5 && tmp >= 0) ? 1 : 2) -
					((tmp == 1) ? 0 : tmp);

				break;
			}
		}
	}

	if ((index_tx < 0 || index_tx > 6 || index_rx < 0 || index_rx > 6) && rate_gov < 7) {
		return ad9361_calculate_rf_clock_chain(phy, tx_sample_rate,
			++rate_gov, rx_path_clks, tx_path_clks);
	}
	else if ((index_tx < 0 || index_tx > 6 || index_rx < 0 || index_rx > 6)) {
		dev_err(&phy->spi->dev, "%s: Failed to find suitable dividers: %s",
			__func__, (adc_rate < MIN_ADC_CLK) ? "ADC clock below limit" : "BBPLL rate above limit");

		return -EINVAL;
	}

	/* Calculate target BBPLL rate */
	div = MAX_BBPLL_DIV;

	do {
		bbpll_rate = (uint64_t)adc_rate * div;
		div >>= 1;

	} while ((bbpll_rate > MAX_BBPLL_FREQ) && (div >= MIN_BBPLL_DIV));

	rx_path_clks[BBPLL_FREQ] = bbpll_rate;
	rx_path_clks[ADC_FREQ] = adc_rate;
	rx_path_clks[R2_FREQ] = rx_path_clks[ADC_FREQ] / clk_dividers[index_rx][1];
	rx_path_clks[R1_FREQ] = rx_path_clks[R2_FREQ] / clk_dividers[index_rx][2];
	rx_path_clks[CLKRF_FREQ] = rx_path_clks[R1_FREQ] / clk_dividers[index_rx][3];
	rx_path_clks[RX_SAMPL_FREQ] = rx_path_clks[CLKRF_FREQ] / rx_intdec;

	tx_path_clks[BBPLL_FREQ] = bbpll_rate;
	tx_path_clks[DAC_FREQ] = dac_rate;
	tx_path_clks[T2_FREQ] = tx_path_clks[DAC_FREQ] / clk_dividers[index_tx][1];
	tx_path_clks[T1_FREQ] = tx_path_clks[T2_FREQ] / clk_dividers[index_tx][2];
	tx_path_clks[CLKTF_FREQ] = tx_path_clks[T1_FREQ] / clk_dividers[index_tx][3];
	tx_path_clks[TX_SAMPL_FREQ] = tx_path_clks[CLKTF_FREQ] / tx_intdec;

	return 0;
}

/**
 * Set the desired sample rate.
 * @param phy The AD9361 state structure.
 * @param freq The desired sample rate.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_set_trx_clock_chain_freq(struct ad9361_rf_phy *phy,
	uint32_t freq)
{
	uint32_t rx[6], tx[6];
	int32_t ret;

	ret = ad9361_calculate_rf_clock_chain(phy, freq,
		phy->rate_governor, rx, tx);
	if (ret < 0)
		return ret;
	return ad9361_set_trx_clock_chain(phy, rx, tx);
}

/**
 * Internal ENSM mode options helper function.
 * @param phy The AD9361 state structure.
 * @param fdd
 * @param pinctrl
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_set_ensm_mode(struct ad9361_rf_phy *phy, bool fdd, bool pinctrl)
{
	struct ad9361_phy_platform_data *pd = phy->pdata;
	int32_t ret;
	uint32_t val = 0;

	ad9361_spi_write(phy->spi, REG_ENSM_MODE, fdd ? FDD_MODE : 0);

	if (pd->use_ext_rx_lo)
		val |= POWER_DOWN_RX_SYNTH;

	if (pd->use_ext_tx_lo)
		val |= POWER_DOWN_TX_SYNTH;

	if (fdd)
		ret = ad9361_spi_write(phy->spi, REG_ENSM_CONFIG_2,
				val | DUAL_SYNTH_MODE |
				(pinctrl ? (pd->fdd_independent_mode ?
					FDD_EXTERNAL_CTRL_ENABLE : 0) : 0));
	else
		ret = ad9361_spi_write(phy->spi, REG_ENSM_CONFIG_2, val |
		(pd->tdd_use_dual_synth ? DUAL_SYNTH_MODE : 0) |
		(pd->tdd_use_dual_synth ? 0 :
		(pinctrl ? SYNTH_ENABLE_PIN_CTRL_MODE : TXNRX_SPI_CTRL)));

	return ret;
}

/**
 * Fastlock read value.
 * @param spi
 * @param tx
 * @param profile
 * @param word
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_fastlock_readval(struct spi_device *spi, bool tx,
	uint32_t profile, uint32_t word)
{
	uint32_t offs = 0;

	if (tx)
		offs = REG_TX_FAST_LOCK_SETUP - REG_RX_FAST_LOCK_SETUP;

	ad9361_spi_write(spi, REG_RX_FAST_LOCK_PROGRAM_ADDR + offs,
		RX_FAST_LOCK_PROFILE_ADDR(profile) |
		RX_FAST_LOCK_PROFILE_WORD(word));

	return ad9361_spi_read(spi, REG_RX_FAST_LOCK_PROGRAM_READ + offs);
}

/**
 * Fastlock write value.
 * @param spi
 * @param tx
 * @param profile
 * @param word
 * @param val
 * @param last
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_fastlock_writeval(struct spi_device *spi, bool tx,
	uint32_t profile, uint32_t word, uint8_t val, bool last)
{
	uint32_t offs = 0;
	int32_t ret;

	if (tx)
		offs = REG_TX_FAST_LOCK_SETUP - REG_RX_FAST_LOCK_SETUP;

	ret = ad9361_spi_write(spi, REG_RX_FAST_LOCK_PROGRAM_ADDR + offs,
		RX_FAST_LOCK_PROFILE_ADDR(profile) |
		RX_FAST_LOCK_PROFILE_WORD(word));
	ret |= ad9361_spi_write(spi, REG_RX_FAST_LOCK_PROGRAM_DATA + offs, val);
	ret |= ad9361_spi_write(spi, REG_RX_FAST_LOCK_PROGRAM_CTRL + offs,
		RX_FAST_LOCK_PROGRAM_WRITE |
		RX_FAST_LOCK_PROGRAM_CLOCK_ENABLE);

	if (last) /* Stop Clocks */
		ret |= ad9361_spi_write(spi,
		REG_RX_FAST_LOCK_PROGRAM_CTRL + offs, 0);

	return ret;
}

/**
 * Fastlock load values.
 * @param phy The AD9361 state structure.
 * @param tx
 * @param profile
 * @param values
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_fastlock_load(struct ad9361_rf_phy *phy, bool tx,
	uint32_t profile, uint8_t *values)
{
	int32_t i, ret = 0;

	dev_dbg(&phy->spi->dev, "%s: %s Profile %"PRIu32":",
		__func__, tx ? "TX" : "RX", profile);

	for (i = 0; i < RX_FAST_LOCK_CONFIG_WORD_NUM; i++)
		ret |= ad9361_fastlock_writeval(phy->spi, tx, profile,
		i, values[i], i == 0xF);

	phy->fastlock.entry[tx][profile].flags = FASTLOOK_INIT;
	phy->fastlock.entry[tx][profile].alc_orig = values[15];
	phy->fastlock.entry[tx][profile].alc_written = values[15];

	return ret;
}

/**
 * Fastlock store.
 * @param phy The AD9361 state structure.
 * @param tx
 * @param profile
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_fastlock_store(struct ad9361_rf_phy *phy, bool tx, uint32_t profile)
{
	struct spi_device *spi = phy->spi;
	uint8_t val[16];
	uint32_t offs = 0, x, y;

	dev_dbg(&phy->spi->dev, "%s: %s Profile %"PRIu32":",
		__func__, tx ? "TX" : "RX", profile);

	if (tx)
		offs = REG_TX_FAST_LOCK_SETUP - REG_RX_FAST_LOCK_SETUP;

	val[0] = ad9361_spi_read(spi, REG_RX_INTEGER_BYTE_0 + offs);
	val[1] = ad9361_spi_read(spi, REG_RX_INTEGER_BYTE_1 + offs);
	val[2] = ad9361_spi_read(spi, REG_RX_FRACT_BYTE_0 + offs);
	val[3] = ad9361_spi_read(spi, REG_RX_FRACT_BYTE_1 + offs);
	val[4] = ad9361_spi_read(spi, REG_RX_FRACT_BYTE_2 + offs);

	x = ad9361_spi_readf(spi, REG_RX_VCO_BIAS_1 + offs, VCO_BIAS_REF(~0));
	y = ad9361_spi_readf(spi, REG_RX_ALC_VARACTOR + offs, VCO_VARACTOR(~0));
	val[5] = (x << 4) | y;

	x = ad9361_spi_readf(spi, REG_RX_VCO_BIAS_1 + offs, VCO_BIAS_TCF(~0));
	y = ad9361_spi_readf(spi, REG_RX_CP_CURRENT + offs, CHARGE_PUMP_CURRENT(~0));
	/* Wide BW option: N = 1
	* Set init and steady state values to the same - let user space handle it
	*/
	val[6] = (x << 3) | y;
	val[7] = y;

	x = ad9361_spi_readf(spi, REG_RX_LOOP_FILTER_3 + offs, LOOP_FILTER_R3(~0));
	val[8] = (x << 4) | x;

	x = ad9361_spi_readf(spi, REG_RX_LOOP_FILTER_2 + offs, LOOP_FILTER_C3(~0));
	val[9] = (x << 4) | x;

	x = ad9361_spi_readf(spi, REG_RX_LOOP_FILTER_1 + offs, LOOP_FILTER_C1(~0));
	y = ad9361_spi_readf(spi, REG_RX_LOOP_FILTER_1 + offs, LOOP_FILTER_C2(~0));
	val[10] = (x << 4) | y;

	x = ad9361_spi_readf(spi, REG_RX_LOOP_FILTER_2 + offs, LOOP_FILTER_R1(~0));
	val[11] = (x << 4) | x;

	x = ad9361_spi_readf(spi, REG_RX_VCO_VARACTOR_CTRL_0 + offs,
		VCO_VARACTOR_REFERENCE_TCF(~0));
	y = ad9361_spi_readf(spi, REG_RFPLL_DIVIDERS,
		tx ? TX_VCO_DIVIDER(~0) : RX_VCO_DIVIDER(~0));
	val[12] = (x << 4) | y;

	x = ad9361_spi_readf(spi, REG_RX_FORCE_VCO_TUNE_1 + offs, VCO_CAL_OFFSET(~0));
	y = ad9361_spi_readf(spi, REG_RX_VCO_VARACTOR_CTRL_1 + offs, VCO_VARACTOR_REFERENCE(~0));
	val[13] = (x << 4) | y;

	val[14] = ad9361_spi_read(spi, REG_RX_FORCE_VCO_TUNE_0 + offs);

	x = ad9361_spi_readf(spi, REG_RX_FORCE_ALC + offs, FORCE_ALC_WORD(~0));
	y = ad9361_spi_readf(spi, REG_RX_FORCE_VCO_TUNE_1 + offs, FORCE_VCO_TUNE);
	val[15] = (x << 1) | y;

	return ad9361_fastlock_load(phy, tx, profile, val);
}

/**
 * Fastlock prepare.
 * @param phy The AD9361 state structure.
 * @param tx
 * @param profile
 * @param prepare
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_fastlock_prepare(struct ad9361_rf_phy *phy, bool tx,
	uint32_t profile, bool prepare)
{
	uint32_t offs, ready_mask;
	bool is_prepared;

	dev_dbg(&phy->spi->dev, "%s: %s Profile %"PRIu32": %s",
		__func__, tx ? "TX" : "RX", profile,
		prepare ? "Prepare" : "Un-Prepare");

	if (tx) {
		offs = REG_TX_FAST_LOCK_SETUP - REG_RX_FAST_LOCK_SETUP;
		ready_mask = TX_SYNTH_READY_MASK;
	}
	else {
		offs = 0;
		ready_mask = RX_SYNTH_READY_MASK;
	}

	is_prepared = !!phy->fastlock.current_profile[tx];

	if (prepare && !is_prepared) {
		ad9361_spi_write(phy->spi,
			REG_RX_FAST_LOCK_SETUP_INIT_DELAY + offs,
			(tx ? phy->pdata->tx_fastlock_delay_ns :
			phy->pdata->rx_fastlock_delay_ns) / 250);
		ad9361_spi_write(phy->spi, REG_RX_FAST_LOCK_SETUP + offs,
			RX_FAST_LOCK_PROFILE(profile) |
			RX_FAST_LOCK_MODE_ENABLE);
		ad9361_spi_write(phy->spi, REG_RX_FAST_LOCK_PROGRAM_CTRL + offs,
			0);

		ad9361_spi_writef(phy->spi, REG_ENSM_CONFIG_2, ready_mask, 1);
		ad9361_trx_vco_cal_control(phy, tx, false);
	}
	else if (!prepare && is_prepared) {
		ad9361_spi_write(phy->spi, REG_RX_FAST_LOCK_SETUP + offs, 0);

		/* Workaround: Exiting Fastlock Mode */
		ad9361_spi_writef(phy->spi, REG_RX_FORCE_ALC + offs, FORCE_ALC_ENABLE, 1);
		ad9361_spi_writef(phy->spi, REG_RX_FORCE_VCO_TUNE_1 + offs, FORCE_VCO_TUNE, 1);
		ad9361_spi_writef(phy->spi, REG_RX_FORCE_ALC + offs, FORCE_ALC_ENABLE, 0);
		ad9361_spi_writef(phy->spi, REG_RX_FORCE_VCO_TUNE_1 + offs, FORCE_VCO_TUNE, 0);

		ad9361_trx_vco_cal_control(phy, tx, true);
		ad9361_spi_writef(phy->spi, REG_ENSM_CONFIG_2, ready_mask, 0);

		phy->fastlock.current_profile[tx] = 0;
	}

	return 0;
}

/**
 * Fastlock recall.
 * @param phy The AD9361 state structure.
 * @param tx
 * @param profile
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_fastlock_recall(struct ad9361_rf_phy *phy, bool tx, uint32_t profile)
{
	uint32_t offs = 0;
	uint8_t curr, _new, orig, current_profile;

	dev_dbg(&phy->spi->dev, "%s: %s Profile %"PRIu32":",
		__func__, tx ? "TX" : "RX", profile);

	if (tx)
		offs = REG_TX_FAST_LOCK_SETUP - REG_RX_FAST_LOCK_SETUP;

	if (phy->fastlock.entry[tx][profile].flags != FASTLOOK_INIT)
		return -EINVAL;

	/* Workaround: Lock problem with same ALC word */

	current_profile = phy->fastlock.current_profile[tx];
	_new = phy->fastlock.entry[tx][profile].alc_written;

	if (current_profile == 0)
		curr = ad9361_spi_readf(phy->spi, REG_RX_FORCE_ALC + offs,
		FORCE_ALC_WORD(~0)) << 1;
	else
		curr = phy->fastlock.entry[tx][current_profile - 1].alc_written;

	if ((curr >> 1) == (_new >> 1)) {
		orig = phy->fastlock.entry[tx][profile].alc_orig;

		if ((orig >> 1) == (_new >> 1))
			phy->fastlock.entry[tx][profile].alc_written += 2;
		else
			phy->fastlock.entry[tx][profile].alc_written = orig;

		ad9361_fastlock_writeval(phy->spi, tx, profile, 0xF,
			phy->fastlock.entry[tx][profile].alc_written, true);
	}

	ad9361_fastlock_prepare(phy, tx, profile, true);
	phy->fastlock.current_profile[tx] = profile + 1;

	return ad9361_spi_write(phy->spi, REG_RX_FAST_LOCK_SETUP + offs,
		RX_FAST_LOCK_PROFILE(profile) |
		(phy->pdata->trx_fastlock_pinctrl_en[tx] ?
	RX_FAST_LOCK_PROFILE_PIN_SELECT : 0) |
									  RX_FAST_LOCK_MODE_ENABLE);
}

/**
 * Fastlock save.
 * @param phy The AD9361 state structure.
 * @param tx
 * @param profile
 * @param values
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_fastlock_save(struct ad9361_rf_phy *phy, bool tx,
	uint32_t profile, uint8_t *values)
{
	int32_t i;

	dev_dbg(&phy->spi->dev, "%s: %s Profile %"PRIu32":",
		__func__, tx ? "TX" : "RX", profile);

	for (i = 0; i < RX_FAST_LOCK_CONFIG_WORD_NUM; i++)
		values[i] = ad9361_fastlock_readval(phy->spi, tx, profile, i);

	return 0;
}

/**
 * Multi Chip Sync (MCS) config.
 * @param phy The AD9361 state structure.
 * @param step MCS step.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_mcs(struct ad9361_rf_phy *phy, int32_t step)
{
	int32_t mcs_mask = MCS_BBPLL_ENABLE | MCS_DIGITAL_CLK_ENABLE | MCS_BB_ENABLE;

	dev_dbg(&phy->spi->dev, "%s: MCS step %"PRId32, __func__, step);

	switch (step) {
	case 1:
		ad9361_spi_writef(phy->spi, REG_MULTICHIP_SYNC_AND_TX_MON_CTRL,
			mcs_mask, MCS_BB_ENABLE | MCS_BBPLL_ENABLE);
		ad9361_spi_writef(phy->spi, REG_CP_BLEED_CURRENT,
			MCS_REFCLK_SCALE_EN, 1);
		break;
	case 2:
		if(!gpio_is_valid(phy->pdata->gpio_sync))
			break;
		/*
		 * NOTE: This is not a regular GPIO -
		 * HDL ensures Multi-chip Synchronization SYNC_IN Pulse Timing
		 * relative to rising and falling edge of REF_CLK
		 */
		gpio_set_value(phy->pdata->gpio_sync, 1);
		gpio_set_value(phy->pdata->gpio_sync, 0);
		break;
	case 3:
		ad9361_spi_writef(phy->spi, REG_MULTICHIP_SYNC_AND_TX_MON_CTRL,
			mcs_mask, MCS_BB_ENABLE | MCS_DIGITAL_CLK_ENABLE);
		break;
	case 4:
		if(!gpio_is_valid(phy->pdata->gpio_sync))
			break;
		gpio_set_value(phy->pdata->gpio_sync, 1);
		gpio_set_value(phy->pdata->gpio_sync, 0);
		break;
	case 0:
	case 5:
		ad9361_spi_writef(phy->spi, REG_MULTICHIP_SYNC_AND_TX_MON_CTRL,
			mcs_mask, 0);
		break;
	}

	return 0;
}

/**
 * Clear state.
 * @param phy The AD9361 state structure.
 * @return None.
 */
void ad9361_clear_state(struct ad9361_rf_phy *phy)
{
	phy->current_table = RXGAIN_TBLS_END;
	phy->bypass_tx_fir = true;
	phy->bypass_rx_fir = true;
	phy->rate_governor = 1;
	phy->rfdc_track_en = true;
	phy->bbdc_track_en = true;
	phy->quad_track_en = true;
	phy->prev_ensm_state = 0;
	phy->curr_ensm_state = 0;
	phy->auto_cal_en = false;
	phy->last_tx_quad_cal_freq = 0;
	phy->flags = 0;
	phy->current_rx_bw_Hz = 0;
	phy->current_tx_bw_Hz = 0;
	phy->rxbbf_div = 0;
	phy->tx_fir_int = 0;
	phy->tx_fir_ntaps = 0;
	phy->rx_fir_dec = 0;
	phy->rx_fir_ntaps = 0;
	phy->ensm_pin_ctl_en = false;
	phy->txmon_tdd_en = 0;
	memset(&phy->fastlock, 0, sizeof(phy->fastlock));
}

/**
 * Determine the reference frequency value.
 * @param refin_Hz Maximum allowed frequency.
 * @param max Reference in frequency value.
 * @return Reference frequency value.
 */
static uint32_t ad9361_ref_div_sel(uint32_t refin_Hz, uint32_t max)
{
	if (refin_Hz <= (max / 2))
		return 2 * refin_Hz;
	else if (refin_Hz <= max)
		return refin_Hz;
	else if (refin_Hz <= (max * 2))
		return refin_Hz / 2;
	else if (refin_Hz <= (max * 4))
		return refin_Hz / 4;
	else
		return 0;
}

/**
 * Setup the AD9361 device.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_setup(struct ad9361_rf_phy *phy)
{
	uint32_t refin_Hz, ref_freq, bbpll_freq;
	struct spi_device *spi = phy->spi;
	struct ad9361_phy_platform_data *pd = phy->pdata;
	int32_t ret;
	uint32_t real_rx_bandwidth = pd->rf_rx_bandwidth_Hz / 2;
	uint32_t real_tx_bandwidth = pd->rf_tx_bandwidth_Hz / 2;

	dev_dbg(dev, "%s", __func__);

	if (pd->fdd) {
		pd->tdd_skip_vco_cal = false;
	} else { /* TDD Mode */
		if (pd->tdd_use_dual_synth || pd->tdd_skip_vco_cal)
			pd->tdd_use_fdd_tables = true;
	}

	if (pd->port_ctrl.pp_conf[2] & FDD_RX_RATE_2TX_RATE)
		phy->rx_eq_2tx = true;

	ad9361_spi_write(spi, REG_CTRL, CTRL_ENABLE);
	ad9361_spi_write(spi, REG_BANDGAP_CONFIG0, MASTER_BIAS_TRIM(0x0E)); /* Enable Master Bias */
	ad9361_spi_write(spi, REG_BANDGAP_CONFIG1, BANDGAP_TEMP_TRIM(0x0E)); /* Set Bandgap Trim */

	ad9361_set_dcxo_tune(phy, pd->dcxo_coarse, pd->dcxo_fine);

	refin_Hz = phy->clk_refin->rate;

	ref_freq = ad9361_ref_div_sel(refin_Hz, MAX_BBPLL_FREF);
	if (!ref_freq)
		return -EINVAL;

	ad9361_spi_writef(spi, REG_REF_DIVIDE_CONFIG_1, RX_REF_RESET_BAR, 1);
	ad9361_spi_writef(spi, REG_REF_DIVIDE_CONFIG_2, TX_REF_RESET_BAR, 1);
	ad9361_spi_writef(spi, REG_REF_DIVIDE_CONFIG_2,
		TX_REF_DOUBLER_FB_DELAY(~0), 3); /* FB DELAY */
	ad9361_spi_writef(spi, REG_REF_DIVIDE_CONFIG_2,
		RX_REF_DOUBLER_FB_DELAY(~0), 3); /* FB DELAY */

	ad9361_spi_write(spi, REG_CLOCK_ENABLE,
		DIGITAL_POWER_UP | CLOCK_ENABLE_DFLT | BBPLL_ENABLE |
		(pd->use_extclk ? XO_BYPASS : 0)); /* Enable Clocks */

	ret = clk_set_rate(phy, phy->ref_clk_scale[BB_REFCLK], ref_freq);
	if (ret < 0) {
		dev_err(dev, "Failed to set BB ref clock rate (%"PRId32")",
			ret);
		return ret;
	}

	ret = ad9361_set_trx_clock_chain(phy, pd->rx_path_clks,
		pd->tx_path_clks);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(phy->clks[BB_REFCLK]);
	if (ret < 0) {
		dev_err(dev, "Failed to enable BB ref clock rate (%"PRId32")",
			ret);
		return ret;
	}

	ad9361_en_dis_tx(phy, 1, TX_ENABLE);
	ad9361_en_dis_rx(phy, 1, RX_ENABLE);

	ad9361_en_dis_tx(phy, 2, pd->rx2tx2);
	ad9361_en_dis_rx(phy, 2, pd->rx2tx2);

	ret = ad9361_rf_port_setup(phy, true, pd->rf_rx_input_sel,
				   pd->rf_tx_output_sel);
	if (ret < 0)
		return ret;

	ret = ad9361_pp_port_setup(phy, false);
	if (ret < 0)
		return ret;

	ret = ad9361_auxdac_setup(phy, &pd->auxdac_ctrl);
	if (ret < 0)
		return ret;

	bbpll_freq = clk_get_rate(phy, phy->ref_clk_scale[BBPLL_CLK]);
	ret = ad9361_auxadc_setup(phy, &pd->auxadc_ctrl, bbpll_freq);
	if (ret < 0)
		return ret;

	ret = ad9361_ctrl_outs_setup(phy, &pd->ctrl_outs_ctrl);
	if (ret < 0)
		return ret;

	ret = ad9361_gpo_setup(phy);
	if (ret < 0)
		return ret;

	ret = ad9361_set_ref_clk_cycles(phy, refin_Hz);
	if (ret < 0)
		return ret;

	ret = ad9361_setup_ext_lna(phy, &pd->elna_ctrl);
	if (ret < 0)
		return ret;

	/*
	 * This allows forcing a lower F_REF window
	 * (worse phase noise, better fractional spurs)
	 */
	pd->trx_synth_max_fref = clamp_t(uint32_t, pd->trx_synth_max_fref,
					 MIN_SYNTH_FREF, MAX_SYNTH_FREF);

	ref_freq = ad9361_ref_div_sel(refin_Hz, pd->trx_synth_max_fref);
	if (!ref_freq)
		return -EINVAL;

	ret = clk_set_rate(phy, phy->ref_clk_scale[RX_REFCLK], ref_freq);
	if (ret < 0) {
		dev_err(dev, "Failed to set RX Synth ref clock rate (%"PRId32")", ret);
		return ret;
	}

	ret = clk_set_rate(phy, phy->ref_clk_scale[TX_REFCLK], ref_freq);
	if (ret < 0) {
		dev_err(dev, "Failed to set TX Synth ref clock rate (%"PRId32")", ret);
		return ret;
	}

	ret = ad9361_txrx_synth_cp_calib(phy, ref_freq, false); /* RXCP */
	if (ret < 0)
		return ret;

	ret = ad9361_txrx_synth_cp_calib(phy, ref_freq, true); /* TXCP */
	if (ret < 0)
		return ret;

	ret = clk_set_rate(phy, phy->ref_clk_scale[RX_RFPLL], ad9361_to_clk(pd->rx_synth_freq));
	if (ret < 0) {
		dev_err(dev, "Failed to set RX Synth rate (%"PRId32")",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(phy->clks[RX_REFCLK]);
	if (ret < 0) {
		dev_err(dev, "Failed to enable RX Synth ref clock (%"PRId32")", ret);
		return ret;
	}

	ret = clk_prepare_enable(phy->clks[RX_RFPLL]);
	if (ret < 0)
		return ret;

	/* REVISIT : add EXT LO clock */
	if (pd->use_ext_rx_lo)
		ad9361_trx_ext_lo_control(phy, false, pd->use_ext_rx_lo);

	ret = clk_set_rate(phy, phy->ref_clk_scale[TX_RFPLL], ad9361_to_clk(pd->tx_synth_freq));
	if (ret < 0) {
		dev_err(dev, "Failed to set TX Synth rate (%"PRId32")",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(phy->clks[TX_REFCLK]);
	if (ret < 0) {
		dev_err(dev, "Failed to enable TX Synth ref clock (%"PRId32")", ret);
		return ret;
	}

	ret = clk_prepare_enable(phy->clks[TX_RFPLL]);
	if (ret < 0)
		return ret;

	/* REVISIT : add EXT LO clock */
	if (pd->use_ext_tx_lo)
		ad9361_trx_ext_lo_control(phy, true, pd->use_ext_tx_lo);

	ret = ad9361_load_mixer_gm_subtable(phy);
	if (ret < 0)
		return ret;

	ret = ad9361_gc_setup(phy, &pd->gain_ctrl);
	if (ret < 0)
		return ret;

	ret = ad9361_rx_bb_analog_filter_calib(phy,
		real_rx_bandwidth,
		bbpll_freq);
	if (ret < 0)
		return ret;

	ret = ad9361_tx_bb_analog_filter_calib(phy,
		real_tx_bandwidth,
		bbpll_freq);
	if (ret < 0)
		return ret;

	ret = ad9361_rx_tia_calib(phy, real_rx_bandwidth);
	if (ret < 0)
		return ret;

	ret = ad9361_tx_bb_second_filter_calib(phy, real_tx_bandwidth);
	if (ret < 0)
		return ret;

	ret = ad9361_rx_adc_setup(phy,
		bbpll_freq,
		clk_get_rate(phy, phy->ref_clk_scale[ADC_CLK]));
	if (ret < 0)
		return ret;

	ret = ad9361_bb_dc_offset_calib(phy);
	if (ret < 0)
		return ret;

	ret = ad9361_rf_dc_offset_calib(phy,
		ad9361_from_clk(clk_get_rate(phy, phy->ref_clk_scale[RX_RFPLL])));
	if (ret < 0)
		return ret;

	ret = ad9361_tx_quad_calib(phy, real_rx_bandwidth, real_tx_bandwidth, -1);
	if (ret < 0)
		return ret;

	ret = ad9361_tracking_control(phy, phy->bbdc_track_en,
		phy->rfdc_track_en, phy->quad_track_en);
	if (ret < 0)
		return ret;

	if (!pd->fdd)
		ad9361_run_calibration(phy, TXMON_CAL);

	ad9361_pp_port_setup(phy, true);

	ret = ad9361_set_ensm_mode(phy, pd->fdd, pd->ensm_pin_ctrl);
	if (ret < 0)
		return ret;

	ad9361_spi_writef(phy->spi, REG_TX_ATTEN_OFFSET,
		MASK_CLR_ATTEN_UPDATE, 0);

	ret = ad9361_set_tx_atten(phy, pd->tx_atten, true, true, true);
	if (ret < 0)
		return ret;

	ret = ad9361_rssi_setup(phy, &pd->rssi_ctrl, false);
	if (ret < 0)
		return ret;

	ret = ad9361_clkout_control(phy, pd->ad9361_clkout_mode);
	if (ret < 0)
		return ret;


	ret = ad9361_txmon_setup(phy, &pd->txmon_ctrl);
	if (ret < 0)
		return ret;

	phy->curr_ensm_state = ad9361_spi_readf(spi, REG_STATE, ENSM_STATE(~0));
	ad9361_ensm_set_state(phy, pd->fdd ? ENSM_STATE_FDD : ENSM_STATE_RX,
		pd->ensm_pin_ctrl);

	phy->current_rx_bw_Hz = pd->rf_rx_bandwidth_Hz;
	phy->current_tx_bw_Hz = pd->rf_tx_bandwidth_Hz;
	phy->auto_cal_en = true;
	phy->cal_threshold_freq = 100000000ULL; /* 100 MHz */

	return 0;

}

/**
 * Perform the selected calibration
 * @param phy The AD9361 state structure.
 * @param cal The selected calibration.
 * @param arg The argument of the calibration.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_do_calib_run(struct ad9361_rf_phy *phy, uint32_t cal, int32_t arg)
{
	int32_t ret;

	ret = ad9361_tracking_control(phy, false, false, false);
	if (ret < 0)
		return ret;

	ad9361_ensm_force_state(phy, ENSM_STATE_ALERT);

	switch (cal) {
	case TX_QUAD_CAL:
		ret = ad9361_tx_quad_calib(phy, phy->current_rx_bw_Hz / 2,
					   phy->current_tx_bw_Hz / 2, arg);
		break;
	case RFDC_CAL:
		ret = ad9361_rf_dc_offset_calib(phy,
			ad9361_from_clk(clk_get_rate(phy, phy->ref_clk_scale[RX_RFPLL])));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	ret = ad9361_tracking_control(phy, phy->bbdc_track_en,
		phy->rfdc_track_en, phy->quad_track_en);
	ad9361_ensm_restore_prev_state(phy);

	return ret;
}

/**
 * Set the RF bandwidth.
 * @param phy The AD9361 state structure.
 * @param rf_rx_bw The desired RX bandwidth [Hz].
 * @param rf_tx_bw The desired TX bandwidth [Hz].
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_update_rf_bandwidth(struct ad9361_rf_phy *phy,
	uint32_t rf_rx_bw, uint32_t rf_tx_bw)
{
	int32_t ret;

	ret = ad9361_tracking_control(phy, false, false, false);
	if (ret < 0)
		return ret;

	ad9361_ensm_force_state(phy, ENSM_STATE_ALERT);

	ret = __ad9361_update_rf_bandwidth(phy, rf_rx_bw, rf_tx_bw);
	if (ret < 0)
		return ret;

	phy->current_rx_bw_Hz = rf_rx_bw;
	phy->current_tx_bw_Hz = rf_tx_bw;

	ret = ad9361_tx_quad_calib(phy, rf_rx_bw / 2, rf_tx_bw / 2, -1);
	if (ret < 0)
		return ret;

	ret = ad9361_tracking_control(phy, phy->bbdc_track_en,
		phy->rfdc_track_en, phy->quad_track_en);
	if (ret < 0)
		return ret;

	ad9361_ensm_restore_prev_state(phy);

	return 0;
}

/**
 * Verify the FIR filter coefficients.
 * @param phy The AD9361 state structure.
 * @param dest Destination identifier (RX1,2 / TX1,2).
 * @param ntaps Number of filter Taps.
 * @param coef Pointer to filter coefficients.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_verify_fir_filter_coef(struct ad9361_rf_phy *phy,
		enum fir_dest dest,
		uint32_t ntaps, short *coef)
{
	struct spi_device *spi = phy->spi;
	uint32_t val, offs = 0, gain = 0, conf, sel, cnt;
	int32_t ret = 0;

	dev_dbg(&phy->spi->dev, "%s: TAPS %"PRIu32", dest %d",
		__func__, ntaps, dest);

	if (dest & FIR_IS_RX) {
		gain = ad9361_spi_read(spi, REG_RX_FILTER_GAIN);
		offs = REG_RX_FILTER_COEF_ADDR - REG_TX_FILTER_COEF_ADDR;
		ad9361_spi_write(spi, REG_RX_FILTER_GAIN, 0);
	}

	conf = ad9361_spi_read(spi, REG_TX_FILTER_CONF + offs);

	if ((dest & 3) == 3) {
		sel = 1;
		cnt = 2;
	} else {
		sel = (dest & 3);
		cnt = 1;
	}

	for (; cnt > 0; cnt--, sel++) {

		ad9361_spi_write(spi, REG_TX_FILTER_CONF + offs,
				 FIR_NUM_TAPS(ntaps / 16 - 1) |
				 FIR_SELECT(sel) | FIR_START_CLK);
		for (val = 0; val < ntaps; val++) {
			short tmp;
			ad9361_spi_write(spi, REG_TX_FILTER_COEF_ADDR + offs, val);

			tmp = (ad9361_spi_read(spi, REG_TX_FILTER_COEF_READ_DATA_1 + offs) & 0xFF) |
			(ad9361_spi_read(spi, REG_TX_FILTER_COEF_READ_DATA_2 + offs) << 8);

			if (tmp != coef[val]) {
				dev_err(&phy->spi->dev,"%s%"PRIu32" read verify failed TAP%"PRIu32" %d =! %d \n",
					(dest & FIR_IS_RX) ? "RX" : "TX", sel,
					val, tmp, coef[val]);
				ret = -EIO;
			}
		}
	}

	if (dest & FIR_IS_RX) {
		ad9361_spi_write(spi, REG_RX_FILTER_GAIN, gain);
	}

	ad9361_spi_write(spi, REG_TX_FILTER_CONF + offs, conf);

	return ret;
}

/**
 * Load the FIR filter coefficients.
 * @param phy The AD9361 state structure.
 * @param dest Destination identifier (RX1,2 / TX1,2).
 * @param gain_dB Gain option.
 * @param ntaps Number of filter Taps.
 * @param coef Pointer to filter coefficients.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_load_fir_filter_coef(struct ad9361_rf_phy *phy,
	enum fir_dest dest, int32_t gain_dB,
	uint32_t ntaps, int16_t *coef)
{
	struct spi_device *spi = phy->spi;
	uint32_t val, offs = 0, fir_conf = 0, fir_enable = 0;

	dev_dbg(&phy->spi->dev, "%s: TAPS %"PRIu32", gain %"PRId32", dest %d",
		__func__, ntaps, gain_dB, dest);

	if (coef == NULL || !ntaps || ntaps > 128 || ntaps % 16) {
		dev_err(&phy->spi->dev,
			"%s: Invalid parameters: TAPS %"PRIu32", gain %"PRId32", dest 0x%X",
			__func__, ntaps, gain_dB, dest);

		return -EINVAL;
	}

	if (dest & FIR_IS_RX) {
		val = 3 - (gain_dB + 12) / 6;
		ad9361_spi_write(spi, REG_RX_FILTER_GAIN, val & 0x3);
		offs = REG_RX_FILTER_COEF_ADDR - REG_TX_FILTER_COEF_ADDR;
		phy->rx_fir_ntaps = ntaps;
		fir_enable = ad9361_spi_readf(phy->spi,
			REG_RX_ENABLE_FILTER_CTRL, RX_FIR_ENABLE_DECIMATION(~0));
		ad9361_spi_writef(phy->spi, REG_RX_ENABLE_FILTER_CTRL,
			RX_FIR_ENABLE_DECIMATION(~0),
			(phy->rx_fir_dec == 4) ? 3 : phy->rx_fir_dec);
	}
	else {
		if (gain_dB == -6)
			fir_conf = TX_FIR_GAIN_6DB;
		phy->tx_fir_ntaps = ntaps;
		fir_enable = ad9361_spi_readf(phy->spi,
			REG_TX_ENABLE_FILTER_CTRL, TX_FIR_ENABLE_INTERPOLATION(~0));
		ad9361_spi_writef(phy->spi, REG_TX_ENABLE_FILTER_CTRL,
			TX_FIR_ENABLE_INTERPOLATION(~0),
			(phy->tx_fir_int == 4) ? 3 : phy->tx_fir_int);
	}

	val = ntaps / 16 - 1;

	fir_conf |= FIR_NUM_TAPS(val) | FIR_SELECT(dest) | FIR_START_CLK;

	ad9361_spi_write(spi, REG_TX_FILTER_CONF + offs, fir_conf);

	for (val = 0; val < ntaps; val++) {
		ad9361_spi_write(spi, REG_TX_FILTER_COEF_ADDR + offs, val);
		ad9361_spi_write(spi, REG_TX_FILTER_COEF_WRITE_DATA_1 + offs,
			coef[val] & 0xFF);
		ad9361_spi_write(spi, REG_TX_FILTER_COEF_WRITE_DATA_2 + offs,
			coef[val] >> 8);
		ad9361_spi_write(spi, REG_TX_FILTER_CONF + offs,
			fir_conf | FIR_WRITE);
		ad9361_spi_write(spi, REG_TX_FILTER_COEF_READ_DATA_2 + offs, 0);
		ad9361_spi_write(spi, REG_TX_FILTER_COEF_READ_DATA_2 + offs, 0);
	}

	ad9361_spi_write(spi, REG_TX_FILTER_CONF + offs, fir_conf);
	fir_conf &= ~FIR_START_CLK;
	ad9361_spi_write(spi, REG_TX_FILTER_CONF + offs, fir_conf);

	if (dest & FIR_IS_RX)
		ad9361_spi_writef(phy->spi, REG_RX_ENABLE_FILTER_CTRL,
			RX_FIR_ENABLE_DECIMATION(~0), fir_enable);
	else
		ad9361_spi_writef(phy->spi, REG_TX_ENABLE_FILTER_CTRL,
			TX_FIR_ENABLE_INTERPOLATION(~0), fir_enable);

	return ad9361_verify_fir_filter_coef(phy, dest, ntaps, coef);
}

/**
 * Parse the FIR filter file/buffer.
 * @param phy The AD9361 state structure.
 * @param data Pointer to buffer.
 * @param size Buffer size.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_parse_fir(struct ad9361_rf_phy *phy,
	char *data, uint32_t size)
{
	char *line;
	int32_t i = 0, ret, txc, rxc;
	int32_t tx = -1, tx_gain, tx_int;
	int32_t rx = -1, rx_gain, rx_dec;
	int32_t rtx = -1, rrx = -1;
	int16_t coef_tx[128];
	int16_t coef_rx[128];
	char *ptr = data;

	phy->filt_rx_bw_Hz = 0;
	phy->filt_tx_bw_Hz = 0;
	phy->filt_valid = false;

	while ((line = strsep(&ptr, "\n"))) {
		if (line >= data + size) {
			break;
		}

		if (line[0] == '#')
			continue;

		if (tx < 0) {
#ifdef WIN32
			ret = sscanf_s(line, "TX %d GAIN %d INT %d",
				&tx, &tx_gain, &tx_int);
#else
			ret = sscanf(line, "TX %"PRId32" GAIN %"PRId32" INT %"PRId32,
				&tx, &tx_gain, &tx_int);
#endif
			if (ret == 3)
				continue;
			else
				tx = -1;
		}
		if (rx < 0) {
#ifdef WIN32
			ret = sscanf_s(line, "RX %d GAIN %d DEC %d",
				&rx, &rx_gain, &rx_dec);
#else
			ret = sscanf(line, "RX %"PRId32" GAIN %"PRId32" DEC %"PRId32,
				&rx, &rx_gain, &rx_dec);
#endif
			if (ret == 3)
				continue;
			else
				tx = -1;
		}

		if (rtx < 0) {
#ifdef WIN32
			ret = sscanf(line, "RTX %lu %lu %lu %lu %lu %lu",
#else
			ret = sscanf(line, "RTX %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32,
#endif
				     &phy->filt_tx_path_clks[0],
				     &phy->filt_tx_path_clks[1],
				     &phy->filt_tx_path_clks[2],
				     &phy->filt_tx_path_clks[3],
				     &phy->filt_tx_path_clks[4],
				     &phy->filt_tx_path_clks[5]);
			if (ret == 6) {
				rtx = 0;
				continue;
			} else {
				rtx = -1;
			}
		}

		if (rrx < 0) {
#ifdef WIN32
			ret = sscanf(line, "RRX %lu %lu %lu %lu %lu %lu",
#else
			ret = sscanf(line, "RRX %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32,
#endif
				     &phy->filt_rx_path_clks[0],
				     &phy->filt_rx_path_clks[1],
				     &phy->filt_rx_path_clks[2],
				     &phy->filt_rx_path_clks[3],
				     &phy->filt_rx_path_clks[4],
				     &phy->filt_rx_path_clks[5]);
			if (ret == 6) {
				rrx = 0;
				continue;
			} else {
				rrx = -1;
			}
		}

		if (!phy->filt_rx_bw_Hz) {
#ifdef WIN32
			ret = sscanf(line, "BWRX %d", &phy->filt_rx_bw_Hz);
#else
			ret = sscanf(line, "BWRX %"PRId32, &phy->filt_rx_bw_Hz);
#endif
			if (ret == 1)
				continue;
			else
				phy->filt_rx_bw_Hz = 0;
		}

		if (!phy->filt_tx_bw_Hz) {
#ifdef WIN32
			ret = sscanf(line, "BWTX %d", &phy->filt_tx_bw_Hz);
#else
			ret = sscanf(line, "BWTX %"PRId32, &phy->filt_tx_bw_Hz);
#endif
			if (ret == 1)
				continue;
			else
				phy->filt_tx_bw_Hz = 0;
		}

#ifdef WIN32
		ret = sscanf_s(line, "%d,%d", &txc, &rxc);
#else
		ret = sscanf(line, "%"PRId32",%"PRId32, &txc, &rxc);
#endif
		if (ret == 1) {
			coef_tx[i] = coef_rx[i] = (int16_t)txc;
			i++;
			continue;
		}
		else if (ret == 2) {
			coef_tx[i] = (int16_t)txc;
			coef_rx[i] = (int16_t)rxc;
			i++;
			continue;
		}
	}

	switch (tx) {
	case FIR_TX1:
	case FIR_TX2:
	case FIR_TX1_TX2:
		phy->tx_fir_int = tx_int;
		ret = ad9361_load_fir_filter_coef(phy, (enum fir_dest)tx, tx_gain, i, coef_tx);
		break;
	default:
		ret = -EINVAL;
	}

	switch (rx | FIR_IS_RX) {
	case FIR_RX1:
	case FIR_RX2:
	case FIR_RX1_RX2:
		phy->rx_fir_dec = rx_dec;
		ret = ad9361_load_fir_filter_coef(phy, (enum fir_dest)(rx | FIR_IS_RX),
			rx_gain, i, coef_rx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret < 0)
		return ret;

	if (!(rrx | rtx))
		phy->filt_valid = true;

	return size;
}

/**
 * Validate FIR filter configuration - on pass enable.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_validate_enable_fir(struct ad9361_rf_phy *phy)
{
	int32_t ret;
	uint32_t rx[6], tx[6];
	uint32_t max, min, valid;

	dev_dbg(dev, "%s: TX FIR EN=%d/TAPS%d/INT%d, RX FIR EN=%d/TAPS%d/DEC%d",
		__func__, !phy->bypass_tx_fir, phy->tx_fir_ntaps, phy->tx_fir_int,
		!phy->bypass_rx_fir, phy->rx_fir_ntaps, phy->rx_fir_dec);

	if (!phy->bypass_tx_fir) {
		if (!(phy->tx_fir_int == 1 || phy->tx_fir_int == 2 ||
			phy->tx_fir_int == 4)) {
			dev_err(dev,
				"%s: Invalid: Interpolation %d in filter config",
				__func__, phy->tx_fir_int);
			return -EINVAL;
		}


		if (phy->tx_fir_int == 1 && phy->tx_fir_ntaps > 64) {
			dev_err(dev,
				"%s: Invalid: TAPS > 64 and Interpolation = 1",
				__func__);
			return -EINVAL;
		}
	}

	if (!phy->bypass_rx_fir) {
		if (!(phy->rx_fir_dec == 1 || phy->rx_fir_dec == 2 ||
			phy->rx_fir_dec == 4)) {
			dev_err(dev,
				"%s: Invalid: Decimation %d in filter config",
				__func__, phy->rx_fir_dec);

			return -EINVAL;
		}
	}

	if (!phy->filt_valid || phy->bypass_rx_fir || phy->bypass_tx_fir) {
		ret = ad9361_calculate_rf_clock_chain(phy,
			clk_get_rate(phy, phy->ref_clk_scale[TX_SAMPL_CLK]),
			phy->rate_governor, rx, tx);
		if (ret < 0) {
			min = DIV_ROUND_UP(MIN_ADC_CLK,
					phy->rate_governor ? 8 : 12);
			dev_err(dev,
				"%s: Calculating filter rates failed %"PRId32
				" using min frequency",__func__, ret);
			if (clk_get_rate(phy, phy->ref_clk_scale[TX_SAMPL_CLK]) <= min)
				ret = ad9361_calculate_rf_clock_chain(phy, min,
					phy->rate_governor, rx, tx);
			if (ret < 0) {
				return ret;
			}
		}
		valid = false;
	} else {
		memcpy(rx, phy->filt_rx_path_clks, sizeof(rx));
		memcpy(tx, phy->filt_tx_path_clks, sizeof(tx));
		valid = true;

	}

#ifdef _DEBUG
	dev_dbg(&phy->spi->dev, "%s:RX %lu %lu %lu %lu %lu %lu",
		__func__, rx[BBPLL_FREQ], rx[ADC_FREQ],
		rx[R2_FREQ], rx[R1_FREQ],
		rx[CLKRF_FREQ], rx[RX_SAMPL_FREQ]);

	dev_dbg(&phy->spi->dev, "%s:TX %lu %lu %lu %lu %lu %lu",
		__func__, tx[BBPLL_FREQ], tx[ADC_FREQ],
		tx[R2_FREQ], tx[R1_FREQ],
		tx[CLKRF_FREQ], tx[RX_SAMPL_FREQ]);
#endif

	if (!phy->bypass_tx_fir) {
		max = (tx[DAC_FREQ] / tx[TX_SAMPL_FREQ]) * 16;
		if (phy->tx_fir_ntaps > max) {
			dev_err(dev,
				"%s: Invalid: ratio ADC/2 / TX_SAMPL * 16 > TAPS"
				"(max %"PRIu32", adc %"PRIu32", tx %"PRIu32")",
				__func__, max, rx[ADC_FREQ], tx[TX_SAMPL_FREQ]);
			return -EINVAL;
		}
	}

	if (!phy->bypass_rx_fir) {
		max = ((rx[ADC_FREQ] / ((rx[ADC_FREQ] == rx[R2_FREQ]) ? 1 : 2)) /
				rx[RX_SAMPL_FREQ]) * 16;
		if (phy->rx_fir_ntaps > max) {
			dev_err(dev,
				"%s: Invalid: ratio ADC/2 / RX_SAMPL * 16 > TAPS (max %"PRIu32")",
				__func__, max);
			return -EINVAL;
		}
	}

	ret = ad9361_set_trx_clock_chain(phy, rx, tx);
	if (ret < 0)
		return ret;

	/*
	* Workaround for clock framework since clocks don't change we
	* manually need to enable the filter
	*/

	if (phy->rx_fir_dec == 1 || phy->bypass_rx_fir) {
		ad9361_spi_writef(phy->spi, REG_RX_ENABLE_FILTER_CTRL,
			RX_FIR_ENABLE_DECIMATION(~0), !phy->bypass_rx_fir);
	}

	if (phy->tx_fir_int == 1 || phy->bypass_tx_fir) {
		ad9361_spi_writef(phy->spi, REG_TX_ENABLE_FILTER_CTRL,
			TX_FIR_ENABLE_INTERPOLATION(~0), !phy->bypass_tx_fir);
	}

	return ad9361_update_rf_bandwidth(phy,
		valid ? phy->filt_rx_bw_Hz : phy->current_rx_bw_Hz,
		valid ? phy->filt_tx_bw_Hz : phy->current_tx_bw_Hz);
}

/*
* AD9361 Clocks
*/

/**
* Set the multiplier and the divider for the selected refclk_scale structure.
* @param priv The selected refclk_scale structure.
* @param mul The multiplier value.
* @param div The divider value.
* @return 0 in case of success, negative error code otherwise.
*/
static inline int32_t ad9361_set_muldiv(struct refclk_scale *priv, uint32_t mul, uint32_t div)
{
	priv->mult = mul;
	priv->div = div;
	return 0;
}

/**
 * Get the clk scaler for the selected refclk_scale structure.
 * @param priv The selected refclk_scale structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_get_clk_scaler(struct refclk_scale *clk_priv)
{
	struct spi_device *spi = clk_priv->spi;
	uint32_t tmp, tmp1;

	switch (clk_priv->source) {
	case BB_REFCLK:
		tmp = ad9361_spi_read(spi, REG_CLOCK_CTRL);
		tmp &= 0x3;
		break;
	case RX_REFCLK:
		tmp = ad9361_spi_readf(spi, REG_REF_DIVIDE_CONFIG_1,
			RX_REF_DIVIDER_MSB);
		tmp1 = ad9361_spi_readf(spi, REG_REF_DIVIDE_CONFIG_2,
			RX_REF_DIVIDER_LSB);
		tmp = (tmp << 1) | tmp1;
		break;
	case TX_REFCLK:
		tmp = ad9361_spi_readf(spi, REG_REF_DIVIDE_CONFIG_2,
			TX_REF_DIVIDER(~0));
		break;
	case ADC_CLK:
		tmp = ad9361_spi_read(spi, REG_BBPLL);
		return ad9361_set_muldiv(clk_priv, 1, 1 << (tmp & 0x7));
	case R2_CLK:
		tmp = ad9361_spi_readf(spi, REG_RX_ENABLE_FILTER_CTRL,
			DEC3_ENABLE_DECIMATION(~0));
		return ad9361_set_muldiv(clk_priv, 1, tmp + 1);
	case R1_CLK:
		tmp = ad9361_spi_readf(spi, REG_RX_ENABLE_FILTER_CTRL, RHB2_EN);
		return ad9361_set_muldiv(clk_priv, 1, tmp + 1);
	case CLKRF_CLK:
		tmp = ad9361_spi_readf(spi, REG_RX_ENABLE_FILTER_CTRL, RHB1_EN);
		return ad9361_set_muldiv(clk_priv, 1, tmp + 1);
	case RX_SAMPL_CLK:
		tmp = ad9361_spi_readf(spi, REG_RX_ENABLE_FILTER_CTRL,
			RX_FIR_ENABLE_DECIMATION(~0));

		if (!tmp)
			tmp = 1; /* bypass filter */
		else
			tmp = (1 << (tmp - 1));

		return ad9361_set_muldiv(clk_priv, 1, tmp);
	case DAC_CLK:
		tmp = ad9361_spi_readf(spi, REG_BBPLL, BIT(3));
		return ad9361_set_muldiv(clk_priv, 1, tmp + 1);
	case T2_CLK:
		tmp = ad9361_spi_readf(spi, REG_TX_ENABLE_FILTER_CTRL,
			THB3_ENABLE_INTERP(~0));
		return ad9361_set_muldiv(clk_priv, 1, tmp + 1);
	case T1_CLK:
		tmp = ad9361_spi_readf(spi, REG_TX_ENABLE_FILTER_CTRL, THB2_EN);
		return ad9361_set_muldiv(clk_priv, 1, tmp + 1);
	case CLKTF_CLK:
		tmp = ad9361_spi_readf(spi, REG_TX_ENABLE_FILTER_CTRL, THB1_EN);
		return ad9361_set_muldiv(clk_priv, 1, tmp + 1);
	case TX_SAMPL_CLK:
		tmp = ad9361_spi_readf(spi, REG_TX_ENABLE_FILTER_CTRL,
			TX_FIR_ENABLE_INTERPOLATION(~0));

		if (!tmp)
			tmp = 1; /* bypass filter */
		else
			tmp = (1 << (tmp - 1));

		return ad9361_set_muldiv(clk_priv, 1, tmp);
	default:
		return -EINVAL;
	}

	/* REFCLK Scaler */
	switch (tmp) {
	case 0:
		ad9361_set_muldiv(clk_priv, 1, 1);
		break;
	case 1:
		ad9361_set_muldiv(clk_priv, 1, 2);
		break;
	case 2:
		ad9361_set_muldiv(clk_priv, 1, 4);
		break;
	case 3:
		ad9361_set_muldiv(clk_priv, 2, 1);
		break;
	default:
		return -EINVAL;

	}

	return 0;
}

/**
 * Calculate the REFCLK Scaler for the selected refclk_scale structure.
 * Note: REFCLK Scaler values - 00: x1; 01: x½; 10: x¼; 11: x2.
 * @param clk_priv The selected refclk_scale structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_to_refclk_scaler(struct refclk_scale *clk_priv)
{
	/* REFCLK Scaler */
	switch (((clk_priv->mult & 0xF) << 4) | (clk_priv->div & 0xF)) {
	case 0x11:
		return 0;
	case 0x12:
		return 1;
	case 0x14:
		return 2;
	case 0x21:
		return 3;
	default:
		return -EINVAL;
	}
};

/**
 * Set clk scaler for the selected refclk_scale structure.
 * @param clk_priv The selected refclk_scale structure.
 * @param set Set true, the reference clock frequency will be scaled before
 *            it enters the BBPLL.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_set_clk_scaler(struct refclk_scale *clk_priv, bool set)
{
	struct spi_device *spi = clk_priv->spi;
	uint32_t tmp;
	int32_t ret;

	switch (clk_priv->source) {
	case BB_REFCLK:
		ret = ad9361_to_refclk_scaler(clk_priv);
		if (ret < 0)
			return ret;
		if (set)
			return ad9361_spi_writef(spi, REG_CLOCK_CTRL,
						REF_FREQ_SCALER(~0), ret);
		break;

	case RX_REFCLK:
		ret = ad9361_to_refclk_scaler(clk_priv);
		if (ret < 0)
			return ret;
		if (set) {
			tmp = ret;
			ret = ad9361_spi_writef(spi, REG_REF_DIVIDE_CONFIG_1,
				RX_REF_DIVIDER_MSB, tmp >> 1);
			ret |= ad9361_spi_writef(spi, REG_REF_DIVIDE_CONFIG_2,
				RX_REF_DIVIDER_LSB, tmp & 1);
			return ret;
		}
		break;
	case TX_REFCLK:
		ret = ad9361_to_refclk_scaler(clk_priv);
		if (ret < 0)
			return ret;
		if (set)
			return ad9361_spi_writef(spi, REG_REF_DIVIDE_CONFIG_2,
			TX_REF_DIVIDER(~0), ret);
		break;
	case ADC_CLK:
		tmp = ilog2((uint8_t)clk_priv->div);
		if (clk_priv->mult != 1 || tmp > 6 || tmp < 1)
			return -EINVAL;

		if (set)
			return ad9361_spi_writef(spi, REG_BBPLL, 0x7, tmp);
		break;
	case R2_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 3 || clk_priv->div < 1)
			return -EINVAL;
		if (set)
			return ad9361_spi_writef(spi, REG_RX_ENABLE_FILTER_CTRL,
			DEC3_ENABLE_DECIMATION(~0),
			clk_priv->div - 1);
		break;
	case R1_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 2 || clk_priv->div < 1)
			return -EINVAL;
		if (set)
			return ad9361_spi_writef(spi, REG_RX_ENABLE_FILTER_CTRL,
			RHB2_EN, clk_priv->div - 1);
		break;
	case CLKRF_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 2 || clk_priv->div < 1)
			return -EINVAL;
		if (set)
			return ad9361_spi_writef(spi, REG_RX_ENABLE_FILTER_CTRL,
			RHB1_EN, clk_priv->div - 1);
		break;
	case RX_SAMPL_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 4 ||
			clk_priv->div < 1 || clk_priv->div == 3)
			return -EINVAL;

		if (clk_priv->phy->bypass_rx_fir)
			tmp = 0;
		else
			tmp = ilog2(clk_priv->div) + 1;

		if (set)
			return ad9361_spi_writef(spi, REG_RX_ENABLE_FILTER_CTRL,
			RX_FIR_ENABLE_DECIMATION(~0), tmp);
		break;
	case DAC_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 2 || clk_priv->div < 1)
			return -EINVAL;
		if (set)
			return ad9361_spi_writef(spi, REG_BBPLL,
			BIT(3), clk_priv->div - 1);
		break;
	case T2_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 3 || clk_priv->div < 1)
			return -EINVAL;
		if (set)
			return ad9361_spi_writef(spi, REG_TX_ENABLE_FILTER_CTRL,
			THB3_ENABLE_INTERP(~0),
			clk_priv->div - 1);
		break;
	case T1_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 2 || clk_priv->div < 1)
			return -EINVAL;
		if (set)
			return ad9361_spi_writef(spi, REG_TX_ENABLE_FILTER_CTRL,
			THB2_EN, clk_priv->div - 1);
		break;
	case CLKTF_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 2 || clk_priv->div < 1)
			return -EINVAL;
		if (set)
			return ad9361_spi_writef(spi, REG_TX_ENABLE_FILTER_CTRL,
			THB1_EN, clk_priv->div - 1);
		break;
	case TX_SAMPL_CLK:
		if (clk_priv->mult != 1 || clk_priv->div > 4 ||
			clk_priv->div < 1 || clk_priv->div == 3)
			return -EINVAL;

		if (clk_priv->phy->bypass_tx_fir)
			tmp = 0;
		else
			tmp = ilog2(clk_priv->div) + 1;

		if (set)
			return ad9361_spi_writef(spi, REG_TX_ENABLE_FILTER_CTRL,
			TX_FIR_ENABLE_INTERPOLATION(~0), tmp);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * Recalculate the clock rate.
 * @param refclk_scale The refclk_scale structure.
 * @param parent_rate The parent clock rate.
 * @return The clock rate.
 */
uint32_t ad9361_clk_factor_recalc_rate(struct refclk_scale *clk_priv,
	uint32_t parent_rate)
{
	uint64_t rate;

	ad9361_get_clk_scaler(clk_priv);
	rate = (parent_rate * clk_priv->mult) / clk_priv->div;

	return (uint32_t)rate;
}

/**
 * Calculate the closest possible clock rate that can be set.
 * @param refclk_scale The refclk_scale structure.
 * @param rate The clock rate.
 * @param parent_rate The parent clock rate.
 * @return The closest possible clock rate that can be set.
 */
int32_t ad9361_clk_factor_round_rate(struct refclk_scale *clk_priv, uint32_t rate,
	uint32_t *prate)
{
	int32_t ret;

	if (rate >= *prate) {
		clk_priv->mult = DIV_ROUND_CLOSEST(rate, *prate);
		clk_priv->div = 1;

	}
	else {
		clk_priv->div = DIV_ROUND_CLOSEST(*prate, rate);
		clk_priv->mult = 1;
		if (!clk_priv->div) {
			dev_err(&clk_priv->spi->dev, "%s: divide by zero",
				__func__);
			clk_priv->div = 1;
		}
	}

	ret = ad9361_set_clk_scaler(clk_priv, false);
	if (ret < 0)
		return ret;

	return (*prate / clk_priv->div) * clk_priv->mult;
}

/**
 * Set the clock rate.
 * @param refclk_scale The refclk_scale structure.
 * @param rate The clock rate.
 * @param parent_rate The parent clock rate.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_clk_factor_set_rate(struct refclk_scale *clk_priv, uint32_t rate,
	uint32_t parent_rate)
{
	dev_dbg(&clk_priv->spi->dev, "%s: Rate %"PRIu32" Hz Parent Rate %"PRIu32" Hz",
		__func__, rate, parent_rate);

	if (rate >= parent_rate) {
		clk_priv->mult = DIV_ROUND_CLOSEST(rate, parent_rate);
		clk_priv->div = 1;
	}
	else {
		clk_priv->div = DIV_ROUND_CLOSEST(parent_rate, rate);
		clk_priv->mult = 1;
		if (!clk_priv->div) {
			dev_err(&clk_priv->spi->dev, "%s: divide by zero",
				__func__);
			clk_priv->div = 1;
		}
	}

	return ad9361_set_clk_scaler(clk_priv, true);
}

/*
 * BBPLL
 */
/**
 * Recalculate the clock rate.
 * @param refclk_scale The refclk_scale structure.
 * @param parent_rate The parent clock rate.
 * @return The clock rate.
 */
uint32_t ad9361_bbpll_recalc_rate(struct refclk_scale *clk_priv,
	uint32_t parent_rate)
{
	uint64_t rate;
	uint32_t fract, integer;
	uint8_t buf[4];

	ad9361_spi_readm(clk_priv->spi, REG_INTEGER_BB_FREQ_WORD, &buf[0],
		REG_INTEGER_BB_FREQ_WORD - REG_FRACT_BB_FREQ_WORD_1 + 1);

	fract = (buf[3] << 16) | (buf[2] << 8) | buf[1];
	integer = buf[0];

	rate = ((uint64_t)parent_rate * fract);
	do_div(&rate, BBPLL_MODULUS);
	rate += (uint64_t)parent_rate * integer;

	return (uint32_t)rate;
}

/**
 * Calculate the closest possible clock rate that can be set.
 * @param refclk_scale The refclk_scale structure.
 * @param rate The clock rate.
 * @param parent_rate The parent clock rate.
 * @return The closest possible clock rate that can be set.
 */
int32_t ad9361_bbpll_round_rate(struct refclk_scale *clk_priv, uint32_t rate,
	uint32_t *prate)
{
	uint64_t tmp;
	uint32_t fract, integer;
	uint64_t temp;

	if (clk_priv) {
		// Unused variable - fix compiler warning
	}

	if (rate > MAX_BBPLL_FREQ)
		return MAX_BBPLL_FREQ;

	if (rate < MIN_BBPLL_FREQ)
		return MIN_BBPLL_FREQ;

	temp = rate;
	tmp = do_div(&temp, *prate);
	rate = temp;
	tmp = tmp * BBPLL_MODULUS + (*prate >> 1);
	do_div(&tmp, *prate);

	integer = rate;
	fract = tmp;

	tmp = *prate * (uint64_t)fract;
	do_div(&tmp, BBPLL_MODULUS);
	tmp += *prate * integer;

	return tmp;
}

/**
 * Set the clock rate.
 * @param refclk_scale The refclk_scale structure.
 * @param rate The clock rate.
 * @param parent_rate The parent clock rate.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_bbpll_set_rate(struct refclk_scale *clk_priv, uint32_t rate,
	uint32_t parent_rate)
{
	struct spi_device *spi = clk_priv->spi;
	uint64_t tmp;
	uint32_t fract, integer;
	int32_t icp_val;
	uint8_t lf_defaults[3] = { 0x35, 0x5B, 0xE8 };
	uint64_t temp;

	dev_dbg(&spi->dev, "%s: Rate %"PRIu32" Hz Parent Rate %"PRIu32" Hz",
		__func__, rate, parent_rate);

	/*
	* Setup Loop Filter and CP Current
	* Scale is 150uA @ (1280MHz BBPLL, 40MHz REFCLK)
	*/
	tmp = (rate >> 7) * 150ULL;
	do_div(&tmp, (parent_rate >> 7) * 32UL + (tmp >> 1));

	/* 25uA/LSB, Offset 25uA */
	icp_val = DIV_ROUND_CLOSEST((uint32_t)tmp, 25U) - 1;

	icp_val = clamp(icp_val, 1, 64);

	ad9361_spi_write(spi, REG_CP_CURRENT, icp_val);
	ad9361_spi_writem(spi, REG_LOOP_FILTER_3, lf_defaults,
		ARRAY_SIZE(lf_defaults));

	/* Allow calibration to occur and set cal count to 1024 for max accuracy */
	ad9361_spi_write(spi, REG_VCO_CTRL,
		FREQ_CAL_ENABLE | FREQ_CAL_COUNT_LENGTH(3));
	/* Set calibration clock to REFCLK/4 for more accuracy */
	ad9361_spi_write(spi, REG_SDM_CTRL, 0x10);

	/* Calculate and set BBPLL frequency word */
	temp = rate;
	tmp = do_div(&temp, parent_rate);
	rate = temp;
	tmp = tmp *(uint64_t)BBPLL_MODULUS + (parent_rate >> 1);
	do_div(&tmp, parent_rate);

	integer = rate;
	fract = tmp;

	ad9361_spi_write(spi, REG_INTEGER_BB_FREQ_WORD, integer);
	ad9361_spi_write(spi, REG_FRACT_BB_FREQ_WORD_3, fract);
	ad9361_spi_write(spi, REG_FRACT_BB_FREQ_WORD_2, fract >> 8);
	ad9361_spi_write(spi, REG_FRACT_BB_FREQ_WORD_1, fract >> 16);

	ad9361_spi_write(spi, REG_SDM_CTRL_1, INIT_BB_FO_CAL | BBPLL_RESET_BAR); /* Start BBPLL Calibration */
	ad9361_spi_write(spi, REG_SDM_CTRL_1, BBPLL_RESET_BAR); /* Clear BBPLL start calibration bit */

	ad9361_spi_write(spi, REG_VCO_PROGRAM_1, 0x86); /* Increase BBPLL KV and phase margin */
	ad9361_spi_write(spi, REG_VCO_PROGRAM_2, 0x01); /* Increase BBPLL KV and phase margin */
	ad9361_spi_write(spi, REG_VCO_PROGRAM_2, 0x05); /* Increase BBPLL KV and phase margin */

	return ad9361_check_cal_done(clk_priv->phy, REG_CH_1_OVERFLOW,
		BBPLL_LOCK, 1);
}

/*
 * RFPLL
 */

/**
 * Calculate the RFPLL frequency.
 * @param parent_rate The parent clock rate.
 * @param integer The integer value.
 * @param fract The fractional value.
 * @param vco_div The VCO divider.
 * @return The RFPLL frequency.
 */
static uint64_t ad9361_calc_rfpll_freq(uint64_t parent_rate,
	uint64_t integer,
	uint64_t fract, uint32_t vco_div)
{
	uint64_t rate;

	rate = parent_rate * fract;
	do_div(&rate, RFPLL_MODULUS);
	rate += parent_rate * integer;

	return rate >> (vco_div + 1);
}

/**
 * Calculate the RFPLL dividers.
 * @param freq The RFPLL frequency.
 * @param parent_rate The parent clock rate.
 * @param integer The integer value.
 * @param fract The fractional value.
 * @param vco_div The VCO divider.
 * @param vco_freq The VCO frequency.
 * @return The RFPLL frequency.
 */
static int32_t ad9361_calc_rfpll_divder(uint64_t freq,
	uint64_t parent_rate, uint32_t *integer,
	uint32_t *fract, int32_t *vco_div, uint64_t *vco_freq)
{
	uint64_t tmp;
	int32_t div;

	if (freq > MAX_CARRIER_FREQ_HZ || freq < MIN_CARRIER_FREQ_HZ)
		return -EINVAL;

	div = -1;

	while (freq <= MIN_VCO_FREQ_HZ) {
		freq <<= 1;
		div++;
	}

	*vco_div = div;
	*vco_freq = freq;
	tmp = do_div(&freq, parent_rate);
	tmp = tmp * RFPLL_MODULUS + (parent_rate >> 1);
	do_div(&tmp, parent_rate);
	*integer = freq;
	*fract = tmp;

	return 0;
}

/**
 * Recalculate the clock rate.
 * @param refclk_scale The refclk_scale structure.
 * @param parent_rate The parent clock rate.
 * @return The clock rate.
 */
uint32_t ad9361_rfpll_recalc_rate(struct refclk_scale *clk_priv,
	uint32_t parent_rate)
{
	struct ad9361_rf_phy *phy = clk_priv->phy;
	uint32_t fract, integer;
	uint8_t buf[5];
	uint32_t reg, div_mask, vco_div, profile;

	dev_dbg(&clk_priv->spi->dev, "%s: Parent Rate %"PRIu32" Hz",
		__func__, parent_rate);

	switch (clk_priv->source) {
	case RX_RFPLL:
		reg = REG_RX_FRACT_BYTE_2;
		div_mask = RX_VCO_DIVIDER(~0);
		profile = phy->fastlock.current_profile[0];
		break;
	case TX_RFPLL:
		reg = REG_TX_FRACT_BYTE_2;
		div_mask = TX_VCO_DIVIDER(~0);
		profile = phy->fastlock.current_profile[1];
		break;
	default:
		return -EINVAL;
	}

	if (profile) {
		bool tx = clk_priv->source == TX_RFPLL;
		profile = profile - 1;

		buf[0] = ad9361_fastlock_readval(phy->spi, tx, profile, 4);
		buf[1] = ad9361_fastlock_readval(phy->spi, tx, profile, 3);
		buf[2] = ad9361_fastlock_readval(phy->spi, tx, profile, 2);
		buf[3] = ad9361_fastlock_readval(phy->spi, tx, profile, 1);
		buf[4] = ad9361_fastlock_readval(phy->spi, tx, profile, 0);
		vco_div = ad9361_fastlock_readval(phy->spi, tx, profile, 12) & 0xF;

	}
	else {
		ad9361_spi_readm(clk_priv->spi, reg, &buf[0], ARRAY_SIZE(buf));
		vco_div = ad9361_spi_readf(clk_priv->spi, REG_RFPLL_DIVIDERS, div_mask);
	}

	fract = (buf[0] << 16) | (buf[1] << 8) | buf[2];
	integer = buf[3] << 8 | buf[4];

	return ad9361_to_clk(ad9361_calc_rfpll_freq(parent_rate, integer,
		fract, vco_div));
}

/**
 * Calculate the closest possible clock rate that can be set.
 * @param refclk_scale The refclk_scale structure.
 * @param rate The clock rate.
 * @param parent_rate The parent clock rate.
 * @return The closest possible clock rate that can be set.
 */
int32_t ad9361_rfpll_round_rate(struct refclk_scale *clk_priv, uint32_t rate,
	uint32_t *prate)
{
	if (clk_priv) {
		// Unused variable - fix compiler warning
	}
	if (prate) {
		// Unused variable - fix compiler warning
	}

	if (ad9361_from_clk(rate) > MAX_CARRIER_FREQ_HZ ||
		ad9361_from_clk(rate) < MIN_CARRIER_FREQ_HZ)
		return -EINVAL;

	return rate;
}

/**
 * Set the clock rate.
 * @param refclk_scale The refclk_scale structure.
 * @param rate The clock rate.
 * @param parent_rate The parent clock rate.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_rfpll_set_rate(struct refclk_scale *clk_priv, uint32_t rate,
	uint32_t parent_rate)
{
	struct ad9361_rf_phy *phy = clk_priv->phy;
	uint64_t vco;
	uint8_t buf[5];
	uint32_t reg, div_mask, lock_reg, fract, integer;
	int32_t vco_div, ret;

	dev_dbg(&clk_priv->spi->dev, "%s: Rate %"PRIu32" Hz Parent Rate %"PRIu32" Hz",
		__func__, rate, parent_rate);

	ad9361_fastlock_prepare(phy, clk_priv->source == TX_RFPLL, 0, false);

	ret = ad9361_calc_rfpll_divder(ad9361_from_clk(rate), parent_rate,
		&integer, &fract, &vco_div, &vco);
	if (ret < 0)
		return ret;

	switch (clk_priv->source) {
	case RX_RFPLL:
		reg = REG_RX_FRACT_BYTE_2;
		lock_reg = REG_RX_CP_OVERRANGE_VCO_LOCK;
		div_mask = RX_VCO_DIVIDER(~0);
		break;
	case TX_RFPLL:
		reg = REG_TX_FRACT_BYTE_2;
		lock_reg = REG_TX_CP_OVERRANGE_VCO_LOCK;
		div_mask = TX_VCO_DIVIDER(~0);
		break;
	default:
		return -EINVAL;

	}

	/* Option to skip VCO cal in TDD mode when moving from TX/RX to Alert */
	if (phy->pdata->tdd_skip_vco_cal)
		ad9361_trx_vco_cal_control(phy, clk_priv->source == TX_RFPLL,
					   true);

	ad9361_rfpll_vco_init(phy, div_mask == TX_VCO_DIVIDER(~0),
		vco, parent_rate);

	buf[0] = fract >> 16;
	buf[1] = fract >> 8;
	buf[2] = fract & 0xFF;
	buf[3] = integer >> 8;
	buf[4] = integer & 0xFF;

	ad9361_spi_writem(clk_priv->spi, reg, buf, 5);
	ad9361_spi_writef(clk_priv->spi, REG_RFPLL_DIVIDERS, div_mask, vco_div);

	/* Load Gain Table */
	if (clk_priv->source == RX_RFPLL) {
		ret = ad9361_load_gt(phy, ad9361_from_clk(rate), GT_RX1 + GT_RX2);
		if (ret < 0)
			return ret;
	}

	/* For RX LO we typically have the tracking option enabled
	* so for now do nothing here.
	*/
	if (phy->auto_cal_en && (clk_priv->source == TX_RFPLL))
		if (abs((int64_t)(phy->last_tx_quad_cal_freq - ad9361_from_clk(rate))) >
			(int64_t)phy->cal_threshold_freq) {
			ret = ad9361_do_calib_run(phy, TX_QUAD_CAL, -1);
			if (ret < 0)
				dev_err(&phy->spi->dev,
				"%s: TX QUAD cal failed", __func__);
			phy->last_tx_quad_cal_freq = ad9361_from_clk(rate);
		}

	ret = ad9361_check_cal_done(phy, lock_reg, VCO_LOCK, 1);

	if (phy->pdata->tdd_skip_vco_cal)
		ad9361_trx_vco_cal_control(phy, clk_priv->source == TX_RFPLL,
		false);

	return ret;
}

/**
 * Register and initialize a new clock.
 * @param phy The AD9361 state structure.
 * @param name The name of the new clock.
 * @param parent_name The name of the parent clock.
 * @param flags The flags.
 * @param source The source of the new clock.
 * @param parent_source The source of the parent clock.
 * @return A struct clk for the new clock or a negative error code.
 */
static struct clk *ad9361_clk_register(struct ad9361_rf_phy *phy, const char *name,
	const char *parent_name, uint32_t flags,
	uint32_t source, uint32_t parent_source)
{
	struct refclk_scale *clk_priv;
	struct clk *clk;

	if (name) {
		// Unused variable - fix compiler warning
	}
	if (parent_name) {
		// Unused variable - fix compiler warning
	}
	if (flags) {
		// Unused variable - fix compiler warning
	}

	clk_priv = (struct refclk_scale *)malloc(sizeof(*clk_priv));
	if (!clk_priv) {
		pr_err("ad9361_clk_register: could not allocate fixed factor clk");
		return (struct clk *)ERR_PTR(-ENOMEM);
	}

	/* struct refclk_scale assignments */
	clk_priv->source = (enum ad9361_clocks)source;
	clk_priv->parent_source = (enum ad9361_clocks)parent_source;
	clk_priv->spi = phy->spi;
	clk_priv->phy = phy;

	phy->ref_clk_scale[source] = clk_priv;

	clk = (struct clk *)malloc(sizeof(*clk));
	if (!clk) {
		free(clk_priv);
		return (struct clk *)ERR_PTR(-ENOMEM);
	}

	switch (source) {
	case TX_REFCLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clk_refin->rate);
		break;
	case RX_REFCLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clk_refin->rate);
		break;
	case BB_REFCLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clk_refin->rate);
		break;
	case BBPLL_CLK:
		clk->rate = ad9361_bbpll_recalc_rate(clk_priv, phy->clks[BB_REFCLK]->rate);
		break;
	case ADC_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[BBPLL_CLK]->rate);
		break;
	case R2_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[ADC_CLK]->rate);
		break;
	case R1_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[R2_CLK]->rate);
		break;
	case CLKRF_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[R1_CLK]->rate);
		break;
	case RX_SAMPL_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[CLKRF_CLK]->rate);
		break;
	case DAC_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[ADC_CLK]->rate);
		break;
	case T2_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[DAC_CLK]->rate);
		break;
	case T1_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[T2_CLK]->rate);
		break;
	case CLKTF_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[T1_CLK]->rate);
		break;
	case TX_SAMPL_CLK:
		clk->rate = ad9361_clk_factor_recalc_rate(clk_priv, phy->clks[CLKTF_CLK]->rate);
		break;
	case RX_RFPLL:
		clk->rate = ad9361_rfpll_recalc_rate(clk_priv, phy->clks[RX_REFCLK]->rate);
		break;
	case TX_RFPLL:
		clk->rate = ad9361_rfpll_recalc_rate(clk_priv, phy->clks[TX_REFCLK]->rate);
		break;
	}

	return clk;
}

/**
 * Register and initialize all the system clocks.
 * @param phy The AD9361 state structure.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t register_clocks(struct ad9361_rf_phy *phy)
{
	uint32_t flags = CLK_GET_RATE_NOCACHE;

	phy->clk_data.clks = (struct clk **)malloc(sizeof(*phy->clk_data.clks) *
		NUM_AD9361_CLKS);
	if (!phy->clk_data.clks) {
		dev_err(&phy->spi->dev, "could not allocate memory");
		return -ENOMEM;
	}

	phy->clk_data.clk_num = NUM_AD9361_CLKS;

	/* Scaled Reference Clocks */
	phy->clks[TX_REFCLK] = ad9361_clk_register(phy,
		"tx_refclk", "ad9361_ext_refclk",
		flags | CLK_IGNORE_UNUSED,
		TX_REFCLK, EXT_REF_CLK);

	phy->clks[RX_REFCLK] = ad9361_clk_register(phy,
		"rx_refclk", "ad9361_ext_refclk",
		flags | CLK_IGNORE_UNUSED,
		RX_REFCLK, EXT_REF_CLK);

	phy->clks[BB_REFCLK] = ad9361_clk_register(phy,
		"bb_refclk", "ad9361_ext_refclk",
		flags | CLK_IGNORE_UNUSED,
		BB_REFCLK, EXT_REF_CLK);

	/* Base Band PLL Clock */
	phy->clks[BBPLL_CLK] = ad9361_clk_register(phy,
		"bbpll_clk", "bb_refclk",
		flags | CLK_IGNORE_UNUSED,
		BBPLL_CLK, BB_REFCLK);

	phy->clks[ADC_CLK] = ad9361_clk_register(phy,
		"adc_clk", "bbpll_clk",
		flags | CLK_IGNORE_UNUSED,
		ADC_CLK, BBPLL_CLK);

	phy->clks[R2_CLK] = ad9361_clk_register(phy,
		"r2_clk", "adc_clk",
		flags | CLK_IGNORE_UNUSED,
		R2_CLK, ADC_CLK);

	phy->clks[R1_CLK] = ad9361_clk_register(phy,
		"r1_clk", "r2_clk",
		flags | CLK_IGNORE_UNUSED,
		R1_CLK, R2_CLK);

	phy->clks[CLKRF_CLK] = ad9361_clk_register(phy,
		"clkrf_clk", "r1_clk",
		flags | CLK_IGNORE_UNUSED,
		CLKRF_CLK, R1_CLK);

	phy->clks[RX_SAMPL_CLK] = ad9361_clk_register(phy,
		"rx_sampl_clk", "clkrf_clk",
		flags | CLK_IGNORE_UNUSED,
		RX_SAMPL_CLK, CLKRF_CLK);


	phy->clks[DAC_CLK] = ad9361_clk_register(phy,
		"dac_clk", "adc_clk",
		flags | CLK_IGNORE_UNUSED,
		DAC_CLK, ADC_CLK);

	phy->clks[T2_CLK] = ad9361_clk_register(phy,
		"t2_clk", "dac_clk",
		flags | CLK_IGNORE_UNUSED,
		T2_CLK, DAC_CLK);

	phy->clks[T1_CLK] = ad9361_clk_register(phy,
		"t1_clk", "t2_clk",
		flags | CLK_IGNORE_UNUSED,
		T1_CLK, T2_CLK);

	phy->clks[CLKTF_CLK] = ad9361_clk_register(phy,
		"clktf_clk", "t1_clk",
		flags | CLK_IGNORE_UNUSED,
		CLKTF_CLK, T1_CLK);

	phy->clks[TX_SAMPL_CLK] = ad9361_clk_register(phy,
		"tx_sampl_clk", "clktf_clk",
		flags | CLK_IGNORE_UNUSED,
		TX_SAMPL_CLK, CLKTF_CLK);

	phy->clks[RX_RFPLL] = ad9361_clk_register(phy,
		"rx_rfpll", "rx_refclk",
		flags | CLK_IGNORE_UNUSED,
		RX_RFPLL, RX_REFCLK);

	phy->clks[TX_RFPLL] = ad9361_clk_register(phy,
		"tx_rfpll", "tx_refclk",
		flags | CLK_IGNORE_UNUSED,
		TX_RFPLL, TX_REFCLK);

	return 0;
}

/**
 * Digital tune.
 * @param phy The AD9361 state structure.
 * @param max_freq Maximum frequency.
 * @return 0 in case of success, negative error code otherwise.
 */
static int32_t ad9361_dig_tune(struct ad9361_rf_phy *phy, uint32_t max_freq)
{
	struct axiadc_converter *conv = phy->adc_conv;
	struct axiadc_state *st = phy->adc_state;
	int32_t ret, i, j, k, chan, t, num_chan, err = 0;
	uint32_t s0, s1, c0, c1, tmp, saved = 0;
	uint8_t field[2][16];

	uint32_t hdl_dac_version = axiadc_read(st, 0x4000);

	if (phy->pdata->dig_interface_tune_skipmode == 2) {
	/* skip completely and use defaults */
		ad9361_spi_write(phy->spi, REG_RX_CLOCK_DATA_DELAY,
				phy->pdata->port_ctrl.rx_clk_data_delay);

		ad9361_spi_write(phy->spi, REG_TX_CLOCK_DATA_DELAY,
				phy->pdata->port_ctrl.tx_clk_data_delay);

		return 0;
	}

	if (!phy->pdata->fdd) {
		ad9361_set_ensm_mode(phy, true, false);
		ad9361_ensm_force_state(phy, ENSM_STATE_FDD);
	}

	num_chan = (conv->chip_info->num_channels > 4) ? 4 : conv->chip_info->num_channels;

	ad9361_bist_prbs(phy, BIST_INJ_RX);

	for (t = 0; t < 2; t++) {
		memset(field, 0, 32);
		for (k = 0; k < 2; k++) {
			if (max_freq)
				ad9361_set_trx_clock_chain_freq(phy, k ? max_freq : 10000000UL);
			for (i = 0; i < 2; i++) {
				for (j = 0; j < 16; j++) {
					ad9361_spi_write(phy->spi,
						REG_RX_CLOCK_DATA_DELAY + t,
						RX_DATA_DELAY(i == 0 ? j : 0) |
						DATA_CLK_DELAY(i ? j : 0));
					for (chan = 0; chan < num_chan; chan++)
						axiadc_write(st, ADI_REG_CHAN_STATUS(chan),
						ADI_PN_ERR | ADI_PN_OOS);
					mdelay(4);

					if ((t == 1) || (axiadc_read(st, ADI_REG_STATUS) & ADI_STATUS)) {
						for (chan = 0, ret = 0; chan < num_chan; chan++) {
							ret |= axiadc_read(st, ADI_REG_CHAN_STATUS(chan));
						}
					}
					else {
						ret = 1;
					}

					field[i][j] |= ret;
				}
			}
		}
#ifdef _DEBUG
		printk("SAMPL CLK: %"PRIu32"\n", clk_get_rate(phy, phy->ref_clk_scale[RX_SAMPL_CLK]));
		printk("  ");
		for (i = 0; i < 16; i++)
			printk("%"PRIx32":", i);
		printk("\n");

		for (i = 0; i < 2; i++) {
			printk("%"PRIx32":", i);
			for (j = 0; j < 16; j++) {
				printk("%c ", (field[i][j] ? '#' : 'o'));
			}
			printk("\n");
		}
		printk("\n");
#endif
		c0 = ad9361_find_opt(&field[0][0], 16, &s0);
		c1 = ad9361_find_opt(&field[1][0], 16, &s1);

		if (!c0 && !c1) {
			dev_err(&phy->spi->dev, "%s: Tuning %s FAILED!", __func__,
				t ? "TX" : "RX");
			err |= -EIO;
		}

		if (c1 > c0)
			ad9361_spi_write(phy->spi, REG_RX_CLOCK_DATA_DELAY + t,
			DATA_CLK_DELAY(s1 + c1 / 2) |
			RX_DATA_DELAY(0));
		else
			ad9361_spi_write(phy->spi, REG_RX_CLOCK_DATA_DELAY + t,
			DATA_CLK_DELAY(0) |
			RX_DATA_DELAY(s0 + c0 / 2));

		if (t == 0) {
			/* Now do the loopback and tune the digital out */
			ad9361_bist_prbs(phy, BIST_DISABLE);

			if (phy->pdata->dig_interface_tune_skipmode == 1) {
			/* skip TX */

				phy->pdata->port_ctrl.rx_clk_data_delay =
					ad9361_spi_read(phy->spi, REG_RX_CLOCK_DATA_DELAY);

				if (!phy->pdata->fdd) {
					ad9361_set_ensm_mode(phy, phy->pdata->fdd,
							     phy->pdata->ensm_pin_ctrl);
					ad9361_ensm_restore_prev_state(phy);
				}
				return 0;
			}

			ad9361_bist_loopback(phy, 1);

			for (chan = 0; chan < num_chan; chan++) {
				axiadc_write(st, ADI_REG_CHAN_CNTRL(chan),
					ADI_FORMAT_SIGNEXT | ADI_FORMAT_ENABLE |
					ADI_ENABLE | ADI_IQCOR_ENB);
				axiadc_set_pnsel(st, chan, ADC_PN_CUSTOM);
				if (PCORE_VERSION_MAJOR(hdl_dac_version) > 7)
				{
					axiadc_write(st, 0x4418 + (chan) * 0x40, 9);
					axiadc_write(st, 0x4044, 0x1);
				}
				else
					axiadc_write(st, 0x4414 + (chan) * 0x40, 1);

			}
			if (PCORE_VERSION_MAJOR(hdl_dac_version) < 8) {
				saved = tmp = axiadc_read(st, 0x4048);
				tmp &= ~0xF;
				tmp |= 1;
				axiadc_write(st, 0x4048, tmp);

			}
		} else {
			ad9361_bist_loopback(phy, 0);

			if (PCORE_VERSION_MAJOR(hdl_dac_version) < 8)
				axiadc_write(st, 0x4048, saved);

			for (chan = 0; chan < num_chan; chan++) {
				axiadc_write(st, ADI_REG_CHAN_CNTRL(chan),
					ADI_FORMAT_SIGNEXT | ADI_FORMAT_ENABLE |
					ADI_ENABLE | ADI_IQCOR_ENB);
				axiadc_set_pnsel(st, chan, ADC_PN9);
				if (PCORE_VERSION_MAJOR(hdl_dac_version) > 7)
				{
					axiadc_write(st, 0x4418 + (chan) * 0x40, 0);
					axiadc_write(st, 0x4044, 0x1);
				}
				else
					axiadc_write(st, 0x4414 + (chan) * 0x40, 0);

			}

			if (err == -EIO) {
				ad9361_spi_write(phy->spi, REG_RX_CLOCK_DATA_DELAY,
						phy->pdata->port_ctrl.rx_clk_data_delay);

				ad9361_spi_write(phy->spi, REG_TX_CLOCK_DATA_DELAY,
						phy->pdata->port_ctrl.tx_clk_data_delay);
				err = 0;
			} else {
				phy->pdata->port_ctrl.rx_clk_data_delay =
					ad9361_spi_read(phy->spi, REG_RX_CLOCK_DATA_DELAY);
				phy->pdata->port_ctrl.tx_clk_data_delay =
					ad9361_spi_read(phy->spi, REG_TX_CLOCK_DATA_DELAY);
			}

			if (!phy->pdata->fdd) {
				ad9361_set_ensm_mode(phy, phy->pdata->fdd, phy->pdata->ensm_pin_ctrl);
				ad9361_ensm_restore_prev_state(phy);
			}

			return err;
		}
	}

	return -EINVAL;
}

/**
* Setup the AD9361 device.
* @param phy The AD9361 state structure.
* @return 0 in case of success, negative error code otherwise.
*/
int32_t ad9361_post_setup(struct ad9361_rf_phy *phy)
{
	struct axiadc_converter *conv = phy->adc_conv;
	struct axiadc_state *st = phy->adc_state;
	int32_t rx2tx2 = phy->pdata->rx2tx2;
	int32_t tmp, num_chan;
	int32_t i, ret;

	num_chan = (conv->chip_info->num_channels > 4) ? 4 : conv->chip_info->num_channels;

	axiadc_write(st, ADI_REG_CNTRL, rx2tx2 ? 0 : ADI_R1_MODE);
	tmp = axiadc_read(st, 0x4048);

	if (!rx2tx2) {
		axiadc_write(st, 0x4048, tmp | BIT(5)); /* R1_MODE */
		axiadc_write(st, 0x404c, 1); /* RATE */
	}
	else {
		tmp &= ~BIT(5);
		axiadc_write(st, 0x4048, tmp);
		axiadc_write(st, 0x404c, 3); /* RATE */
	}

	for (i = 0; i < num_chan; i++) {
		axiadc_write(st, ADI_REG_CHAN_CNTRL_1(i),
			ADI_DCFILT_OFFSET(0));
		axiadc_write(st, ADI_REG_CHAN_CNTRL_2(i),
			(i & 1) ? 0x00004000 : 0x40000000);
		axiadc_write(st, ADI_REG_CHAN_CNTRL(i),
			ADI_FORMAT_SIGNEXT | ADI_FORMAT_ENABLE |
			ADI_ENABLE | ADI_IQCOR_ENB);
	}

	ret = ad9361_dig_tune(phy, ((conv->chip_info->num_channels > 4) ||
		axiadc_read(st, 0x0004)) ? 0 : 61440000);
	if (ret < 0)
		return ret;

	return ad9361_set_trx_clock_chain(phy,
		phy->pdata->rx_path_clks,
		phy->pdata->tx_path_clks);
}
