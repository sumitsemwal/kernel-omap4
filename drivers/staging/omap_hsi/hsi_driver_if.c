/*
 * hsi_driver_if.c
 *
 * Implements HSI hardware driver interfaces for the upper layers.
 *
 * Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Author: Carlos Chinea <carlos.chinea@nokia.com>
 * Author: Sebastien JAN <s-jan@ti.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "hsi_driver.h"

#define NOT_SET		(-1)

/* Manage HSR divisor update
 * A special divisor value allows switching to auto-divisor mode in Rx
 * (but with error counters deactivated). This function implements the
 * the transitions to/from this mode.
 */
int hsi_set_rx_divisor(struct hsi_port *sport, u32 divisor)
{
	struct hsi_dev *hsi_ctrl = sport->hsi_controller;
	void __iomem *base = hsi_ctrl->base;
	int port = sport->port_number;
	struct platform_device *pdev = to_platform_device(hsi_ctrl->dev);

	if (divisor == NOT_SET)
		return 0;

	if (hsi_driver_device_is_hsi(pdev)) {
		if (divisor == HSI_HSR_DIVISOR_AUTO && sport->counters_on) {
			/* auto mode: deactivate counters + set divisor = 0 */
			sport->reg_counters = hsi_inl(base,
						      HSI_HSR_COUNTERS_REG
						      (port));
			sport->counters_on = 0;
			hsi_outl(0, base, HSI_HSR_COUNTERS_REG(port));
			hsi_outl(0, base, HSI_HSR_DIVISOR_REG(port));
			dev_dbg(hsi_ctrl->dev, "Switched to HSR auto mode\n");
		} else if (divisor != HSI_HSR_DIVISOR_AUTO) {
			/* Divisor set mode: use counters */
			if (!sport->counters_on) {
				/* Leave auto mode: restore counters */
				hsi_outl(sport->reg_counters, base,
					 HSI_HSR_COUNTERS_REG(port));
				sport->counters_on = 1;
				dev_dbg(hsi_ctrl->dev, "Left HSR auto mode. "
					"Counters=0x%lx\n",
					sport->reg_counters);
			}
			hsi_outl(divisor, base, HSI_HSR_DIVISOR_REG(port));
		}
	} else {
		if (divisor == HSI_HSR_DIVISOR_AUTO && sport->counters_on) {
			/* auto mode: deactivate timeout */
			sport->reg_counters = hsi_inl(base,
						      HSI_HSR_COUNTERS_REG
						      (port));
			sport->counters_on = 0;
			hsi_outl(0, base, HSI_HSR_COUNTERS_REG(port));
			dev_dbg(hsi_ctrl->dev, "Deactivated SSR timeout\n");
		} else if (divisor == HSI_SSR_DIVISOR_USE_TIMEOUT &&
			   !sport->counters_on) {
			/* Leave auto mode: restore timeout */
			hsi_outl(sport->reg_counters, base,
				 HSI_HSR_COUNTERS_REG(port));
			sport->counters_on = 1;
			dev_dbg(hsi_ctrl->dev, "Re-activated SSR timeout; "
				"timeout=0x%lx\n", sport->reg_counters);
		}
	}

	return 0;
}

int hsi_set_rx(struct hsi_port *sport, struct hsr_ctx *cfg)
{
	struct hsi_dev *hsi_ctrl = sport->hsi_controller;
	void __iomem *base = hsi_ctrl->base;
	int port = sport->port_number;
	struct platform_device *pdev = to_platform_device(hsi_ctrl->dev);

	if (((cfg->mode & HSI_MODE_VAL_MASK) != HSI_MODE_STREAM) &&
	    ((cfg->mode & HSI_MODE_VAL_MASK) != HSI_MODE_FRAME) &&
	    ((cfg->mode & HSI_MODE_VAL_MASK) != HSI_MODE_SLEEP) &&
	    (cfg->mode != NOT_SET))
		return -EINVAL;

	if (hsi_driver_device_is_hsi(pdev)) {
		if (((cfg->flow & HSI_FLOW_VAL_MASK) != HSI_FLOW_SYNCHRONIZED)
		    && ((cfg->flow & HSI_FLOW_VAL_MASK) != HSI_FLOW_PIPELINED)
		    && (cfg->flow != NOT_SET))
			return -EINVAL;
		/* HSI only supports payload size of 32bits */
		if ((cfg->frame_size != HSI_FRAMESIZE_MAX) &&
		    (cfg->frame_size != NOT_SET))
			return -EINVAL;
	} else {
		if (((cfg->flow & HSI_FLOW_VAL_MASK) != HSI_FLOW_SYNCHRONIZED)
		    && (cfg->flow != NOT_SET))
			return -EINVAL;
		/* HSI only supports payload size of 32bits */
		if ((cfg->frame_size != HSI_FRAMESIZE_MAX) &&
		    (cfg->frame_size != NOT_SET))
			return -EINVAL;
	}

	if ((cfg->channels == 0) ||
	    ((cfg->channels > sport->max_ch) && (cfg->channels != NOT_SET)))
		return -EINVAL;

	if (hsi_driver_device_is_hsi(pdev)) {
		if ((cfg->divisor > HSI_MAX_RX_DIVISOR) &&
		    (cfg->divisor != HSI_HSR_DIVISOR_AUTO) &&
		    (cfg->divisor != NOT_SET))
			return -EINVAL;
	}

	if ((cfg->mode != NOT_SET) && (cfg->flow != NOT_SET))
		hsi_outl(cfg->mode | ((cfg->flow & HSI_FLOW_VAL_MASK)
				      << HSI_FLOW_OFFSET), base,
			 HSI_HSR_MODE_REG(port));

	if (cfg->frame_size != NOT_SET)
		hsi_outl(cfg->frame_size, base, HSI_HSR_FRAMESIZE_REG(port));

	if (cfg->channels != NOT_SET) {
		if ((cfg->channels & (-cfg->channels)) ^ cfg->channels)
			return -EINVAL;
		else
			hsi_outl(cfg->channels, base,
				 HSI_HSR_CHANNELS_REG(port));
	}

	return hsi_set_rx_divisor(sport, cfg->divisor);
}

void hsi_get_rx(struct hsi_port *sport, struct hsr_ctx *cfg)
{
	struct hsi_dev *hsi_ctrl = sport->hsi_controller;
	void __iomem *base = hsi_ctrl->base;
	int port = sport->port_number;
	struct platform_device *pdev = to_platform_device(hsi_ctrl->dev);

	cfg->mode = hsi_inl(base, HSI_HSR_MODE_REG(port)) & HSI_MODE_VAL_MASK;
	cfg->flow = (hsi_inl(base, HSI_HSR_MODE_REG(port)) & HSI_FLOW_VAL_MASK)
	    >> HSI_FLOW_OFFSET;
	cfg->frame_size = hsi_inl(base, HSI_HSR_FRAMESIZE_REG(port));
	cfg->channels = hsi_inl(base, HSI_HSR_CHANNELS_REG(port));
	if (hsi_driver_device_is_hsi(pdev))
		cfg->divisor = hsi_inl(base, HSI_HSR_DIVISOR_REG(port));
}

int hsi_set_tx(struct hsi_port *sport, struct hst_ctx *cfg)
{
	struct hsi_dev *hsi_ctrl = sport->hsi_controller;
	void __iomem *base = hsi_ctrl->base;
	int port = sport->port_number;
	struct platform_device *pdev = to_platform_device(hsi_ctrl->dev);
	unsigned int max_divisor = hsi_driver_device_is_hsi(pdev) ?
	    HSI_MAX_TX_DIVISOR : HSI_SSI_MAX_TX_DIVISOR;

	if (((cfg->mode & HSI_MODE_VAL_MASK) != HSI_MODE_STREAM) &&
	    ((cfg->mode & HSI_MODE_VAL_MASK) != HSI_MODE_FRAME) &&
	    (cfg->mode != NOT_SET))
		return -EINVAL;

	if (hsi_driver_device_is_hsi(pdev)) {
		if (((cfg->flow & HSI_FLOW_VAL_MASK) != HSI_FLOW_SYNCHRONIZED)
		    && ((cfg->flow & HSI_FLOW_VAL_MASK) != HSI_FLOW_PIPELINED)
		    && (cfg->flow != NOT_SET))
			return -EINVAL;
		/* HSI only supports payload size of 32bits */
		if ((cfg->frame_size != HSI_FRAMESIZE_MAX) &&
		    (cfg->frame_size != NOT_SET))
			return -EINVAL;
	} else {
		if (((cfg->flow & HSI_FLOW_VAL_MASK) != HSI_FLOW_SYNCHRONIZED)
		    && (cfg->flow != NOT_SET))
			return -EINVAL;

		if ((cfg->frame_size > HSI_FRAMESIZE_MAX) &&
		    (cfg->frame_size != NOT_SET))
			return -EINVAL;
	}

	if ((cfg->channels == 0) ||
	    ((cfg->channels > sport->max_ch) && (cfg->channels != NOT_SET)))
		return -EINVAL;

	if ((cfg->divisor > max_divisor) && (cfg->divisor != NOT_SET))
		return -EINVAL;

	if ((cfg->arb_mode != HSI_ARBMODE_ROUNDROBIN) &&
	    (cfg->arb_mode != HSI_ARBMODE_PRIORITY) && (cfg->mode != NOT_SET))
		return -EINVAL;

	if ((cfg->mode != NOT_SET) && (cfg->flow != NOT_SET))
		hsi_outl(cfg->mode | ((cfg->flow & HSI_FLOW_VAL_MASK) <<
				      HSI_FLOW_OFFSET) |
			 HSI_HST_MODE_WAKE_CTRL_SW, base,
			 HSI_HST_MODE_REG(port));

	if (cfg->frame_size != NOT_SET)
		hsi_outl(cfg->frame_size, base, HSI_HST_FRAMESIZE_REG(port));

	if (cfg->channels != NOT_SET) {
		if ((cfg->channels & (-cfg->channels)) ^ cfg->channels)
			return -EINVAL;
		else
			hsi_outl(cfg->channels, base,
				 HSI_HST_CHANNELS_REG(port));
	}

	if (cfg->divisor != NOT_SET)
		hsi_outl(cfg->divisor, base, HSI_HST_DIVISOR_REG(port));

	if (cfg->arb_mode != NOT_SET)
		hsi_outl(cfg->arb_mode, base, HSI_HST_ARBMODE_REG(port));

	return 0;
}

void hsi_get_tx(struct hsi_port *sport, struct hst_ctx *cfg)
{
	struct hsi_dev *hsi_ctrl = sport->hsi_controller;
	void __iomem *base = hsi_ctrl->base;
	int port = sport->port_number;

	cfg->mode = hsi_inl(base, HSI_HST_MODE_REG(port)) & HSI_MODE_VAL_MASK;
	cfg->flow = (hsi_inl(base, HSI_HST_MODE_REG(port)) & HSI_FLOW_VAL_MASK)
	    >> HSI_FLOW_OFFSET;
	cfg->frame_size = hsi_inl(base, HSI_HST_FRAMESIZE_REG(port));
	cfg->channels = hsi_inl(base, HSI_HST_CHANNELS_REG(port));
	cfg->divisor = hsi_inl(base, HSI_HST_DIVISOR_REG(port));
	cfg->arb_mode = hsi_inl(base, HSI_HST_ARBMODE_REG(port));
}

/**
 * hsi_open - open a hsi device channel.
 * @dev - Reference to the hsi device channel to be openned.
 *
 * Returns 0 on success, -EINVAL on bad parameters, -EBUSY if is already opened.
 */
int hsi_open(struct hsi_device *dev)
{
	struct hsi_channel *ch;
	struct hsi_port *port;
	struct hsi_dev *hsi_ctrl;

	if (!dev || !dev->ch) {
		pr_err(LOG_NAME "Wrong HSI device %p\n", dev);
		return -EINVAL;
	}
	dev_dbg(dev->device.parent, "%s\n", __func__);

	ch = dev->ch;
	if (!ch->read_done || !ch->write_done) {
		dev_err(dev->device.parent,
			"Trying to open with no (read/write) callbacks "
			"registered\n");
		return -EINVAL;
	}
	port = ch->hsi_port;
	hsi_ctrl = port->hsi_controller;

	hsi_clocks_enable_channel(dev->device.parent, ch->channel_number,
				__func__);

	spin_lock_bh(&hsi_ctrl->lock);
	if (ch->flags & HSI_CH_OPEN) {
		dev_err(dev->device.parent,
			"Port %d Channel %d already OPENED\n",
			dev->n_p, dev->n_ch);
		spin_unlock_bh(&hsi_ctrl->lock);
		return -EBUSY;
	}

	ch->flags |= HSI_CH_OPEN;

	hsi_outl_or(HSI_CAWAKEDETECTED | HSI_ERROROCCURED | HSI_BREAKDETECTED,
		    hsi_ctrl->base, HSI_SYS_MPU_ENABLE_REG(port->port_number,
							   port->n_irq));
	/* NOTE: error and break are port events and do not need to be
	 * enabled for HSI extended enable register */

	spin_unlock_bh(&hsi_ctrl->lock);

	/* Cut the clocks and wait for ACWAKE change */
	hsi_clocks_disable_channel(dev->device.parent, ch->channel_number,
				__func__);

	return 0;
}
EXPORT_SYMBOL(hsi_open);

/**
 * hsi_write - write data into the hsi device channel
 * @dev - reference to the hsi device channel to write into.
 * @addr - pointer to a 32-bit word data to be written.
 * @size - number of 32-bit word to be written.
 *
 * Return 0 on sucess, a negative value on failure.
 * A success value only indicates that the request has been accepted.
 * Transfer is only completed when the write_done callback is called.
 *
 */
int hsi_write(struct hsi_device *dev, u32 * addr, unsigned int size)
{
	struct hsi_channel *ch;
	int err;

	if (unlikely(!dev || !dev->ch || !addr || (size <= 0))) {
		dev_err(dev->device.parent,
			"Wrong parameters hsi_device %p data %p count %d",
			dev, addr, size);
		return -EINVAL;
	}
	dev_dbg(dev->device.parent, "%s @%x, size %d u32\n", __func__,
		(u32) addr, size);

	if (unlikely(!(dev->ch->flags & HSI_CH_OPEN))) {
		dev_err(dev->device.parent, "HSI device NOT open\n");
		return -EINVAL;
	}

	ch = dev->ch;
	spin_lock_bh(&ch->hsi_port->hsi_controller->lock);

	if (ch->write_data.addr != NULL) {
		dev_err(dev->device.parent, "# Invalid request - Write "
				"operation pending port %d channel %d\n",
					ch->hsi_port->port_number,
					ch->channel_number);
		err = -EINVAL;
		goto abort;
	}

	ch->write_data.addr = addr;
	ch->write_data.size = size;
	ch->write_data.lch = -1;

	if (size == 1)
		err = hsi_driver_enable_write_interrupt(ch, addr);
	else
		err = hsi_driver_write_dma(ch, addr, size);

	if (unlikely(err < 0)) {
		ch->write_data.addr = NULL;
		ch->write_data.size = 0;
	}
abort:
	spin_unlock_bh(&ch->hsi_port->hsi_controller->lock);
	return err;
}
EXPORT_SYMBOL(hsi_write);

/**
 * hsi_read - read data from the hsi device channel
 * @dev - hsi device channel reference to read data from.
 * @addr - pointer to a 32-bit word data to store the data.
 * @size - number of 32-bit word to be stored.
 *
 * Return 0 on sucess, a negative value on failure.
 * A success value only indicates that the request has been accepted.
 * Data is only available in the buffer when the read_done callback is called.
 *
 */
int hsi_read(struct hsi_device *dev, u32 * addr, unsigned int size)
{
	struct hsi_channel *ch;
	int err;

	if (unlikely(!dev || !dev->ch || !addr || (size <= 0))) {
		dev_err(dev->device.parent, "Wrong parameters "
			"hsi_device %p data %p count %d", dev, addr, size);
		return -EINVAL;
	}
	dev_dbg(dev->device.parent, "%s @%x, size %d u32\n", __func__,
		(u32) addr, size);

	if (unlikely(!(dev->ch->flags & HSI_CH_OPEN))) {
		dev_err(dev->device.parent, "HSI device NOT open\n");
		return -EINVAL;
	}

	ch = dev->ch;
	spin_lock_bh(&ch->hsi_port->hsi_controller->lock);

	if (ch->read_data.addr != NULL) {
		dev_err(dev->device.parent, "# Invalid request - Read "
				"operation pending port %d channel %d\n",
					ch->hsi_port->port_number,
					ch->channel_number);
		err = -EINVAL;
		goto abort;
	}

	ch->read_data.addr = addr;
	ch->read_data.size = size;
	ch->read_data.lch = -1;

	if (size == 1)
		err = hsi_driver_enable_read_interrupt(ch, addr);
	else
		err = hsi_driver_read_dma(ch, addr, size);

	if (unlikely(err < 0)) {
		ch->read_data.addr = NULL;
		ch->read_data.size = 0;
	}
abort:
	spin_unlock_bh(&ch->hsi_port->hsi_controller->lock);

	return err;
}
EXPORT_SYMBOL(hsi_read);

void __hsi_write_cancel(struct hsi_channel *ch)
{
	if (ch->write_data.size == 1)
		hsi_driver_cancel_write_interrupt(ch);
	else if (ch->write_data.size > 1)
		hsi_driver_cancel_write_dma(ch);
}

/**
 * hsi_write_cancel - Cancel pending write request.
 * @dev - hsi device channel where to cancel the pending write.
 *
 * write_done() callback will not be called after sucess of this function.
 */
void hsi_write_cancel(struct hsi_device *dev)
{
	if (unlikely(!dev || !dev->ch)) {
		pr_err(LOG_NAME "Wrong HSI device %p\n", dev);
		return;
	}
	dev_dbg(dev->device.parent, "%s\n", __func__);

	if (unlikely(!(dev->ch->flags & HSI_CH_OPEN))) {
		dev_err(dev->device.parent, "HSI device NOT open\n");
		return;
	}

	hsi_clocks_enable_channel(dev->device.parent, dev->ch->channel_number,
				__func__);

	spin_lock_bh(&dev->ch->hsi_port->hsi_controller->lock);
	__hsi_write_cancel(dev->ch);
	spin_unlock_bh(&dev->ch->hsi_port->hsi_controller->lock);

	hsi_clocks_disable_channel(dev->device.parent, dev->ch->channel_number,
				__func__);
}
EXPORT_SYMBOL(hsi_write_cancel);

void __hsi_read_cancel(struct hsi_channel *ch)
{
	if (ch->read_data.size == 1)
		hsi_driver_cancel_read_interrupt(ch);
	else if (ch->read_data.size > 1)
		hsi_driver_cancel_read_dma(ch);
}

/**
 * hsi_read_cancel - Cancel pending read request.
 * @dev - hsi device channel where to cancel the pending read.
 *
 * read_done() callback will not be called after sucess of this function.
 */
void hsi_read_cancel(struct hsi_device *dev)
{
	if (unlikely(!dev || !dev->ch)) {
		pr_err(LOG_NAME "Wrong HSI device %p\n", dev);
		return;
	}
	dev_dbg(dev->device.parent, "%s\n", __func__);

	if (unlikely(!(dev->ch->flags & HSI_CH_OPEN))) {
		dev_err(dev->device.parent, "HSI device NOT open\n");
		return;
	}

	hsi_clocks_enable_channel(dev->device.parent, dev->ch->channel_number,
				__func__);

	spin_lock_bh(&dev->ch->hsi_port->hsi_controller->lock);
	__hsi_read_cancel(dev->ch);
	spin_unlock_bh(&dev->ch->hsi_port->hsi_controller->lock);

	hsi_clocks_disable_channel(dev->device.parent, dev->ch->channel_number,
				__func__);
}
EXPORT_SYMBOL(hsi_read_cancel);

/**
 * hsi_poll - HSI poll, enables data reception
 * @dev - hsi device channel reference to apply the I/O control
 *						(or port associated to it)
 *
 * Return 0 on sucess, a negative value on failure.
 *
 */
int hsi_poll(struct hsi_device *dev)
{
	struct hsi_channel *ch;
	struct hsi_dev *hsi_ctrl;
	int err;

	if (unlikely(!dev || !dev->ch))
		return -EINVAL;
	dev_dbg(dev->device.parent, "%s\n", __func__);

	if (unlikely(!(dev->ch->flags & HSI_CH_OPEN))) {
		dev_err(dev->device.parent, "HSI device NOT open\n");
		return -EINVAL;
	}

	ch = dev->ch;
	hsi_ctrl = ch->hsi_port->hsi_controller;

	/* Safety : enable the clocks */
	hsi_clocks_enable_channel(dev->device.parent, dev->ch->channel_number,
				__func__);

	spin_lock_bh(&hsi_ctrl->lock);
	ch->flags |= HSI_CH_RX_POLL;

	err = hsi_driver_enable_read_interrupt(ch, NULL);

	spin_unlock_bh(&hsi_ctrl->lock);

	/* Cut the clocks */
	hsi_clocks_disable_channel(dev->device.parent, dev->ch->channel_number,
				__func__);

	return err;
}
EXPORT_SYMBOL(hsi_poll);

/**
 * hsi_ioctl - HSI I/O control
 * @dev - hsi device channel reference to apply the I/O control
 *						(or port associated to it)
 * @command - HSI I/O control command
 * @arg - parameter associated to the control command. NULL, if no parameter.
 *
 * Return 0 on sucess, a negative value on failure.
 *
 */
int hsi_ioctl(struct hsi_device *dev, unsigned int command, void *arg)
{
	struct hsi_channel *ch;
	struct hsi_dev *hsi_ctrl;
	void __iomem *base;
	unsigned int port, channel;
	u32 wake;
	int err = 0;

	if (unlikely((!dev) ||
		     (!dev->ch) ||
		     (!dev->ch->hsi_port) ||
		     (!dev->ch->hsi_port->hsi_controller)) ||
	    (!(dev->ch->flags & HSI_CH_OPEN))) {
		pr_err(LOG_NAME "HSI IOCTL Invalid parameter\n");
		return -EINVAL;
	}

	ch = dev->ch;
	hsi_ctrl = ch->hsi_port->hsi_controller;
	port = ch->hsi_port->port_number;
	channel = ch->channel_number;
	base = hsi_ctrl->base;

	dev_dbg(dev->device.parent, "IOCTL: ch %d, command %d\n",
		channel, command);

	switch (command) {
	case HSI_IOCTL_ACWAKE_UP:
		/* Wake up request to Modem (typically OMAP initiated or */
		/* ACK from Modem following CAWAKE high) */
		/* Symetrical disable will be done in HSI_IOCTL_ACWAKE_DOWN */
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);

		spin_lock_bh(&hsi_ctrl->lock);
		ch->flags |= HSI_CH_ACWAKE;
		hsi_ctrl->acwake_status |= BIT(channel);

		/* We only claim once the wake line per channel */
		wake = hsi_inl(base, HSI_SYS_WAKE_REG(port));
		if (!(wake & HSI_WAKE(channel))) {
			hsi_outl(HSI_SET_WAKE(channel), base,
				 HSI_SYS_SET_WAKE_REG(port));
		}
		spin_unlock_bh(&hsi_ctrl->lock);

		goto out;
		break;
	case HSI_IOCTL_ACWAKE_DOWN:
		/* Low power request initiation (OMAP initiated, typically */
		/* following inactivity timeout) */
		/* ACPU HSI block shall still be capable of receiving */

		/* Safety clock enable: */
		/* Clocks should be on, but to be sure we enable them here. */
		/* Clocks will be disabled immediatly after register access */
		/* (or at least clock counter will be decremented) */
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);

		spin_lock_bh(&hsi_ctrl->lock);

		/* Read the CAWAKE status before changing the ACWAKE. */
		/* This is to prevent a race condition with CAWAKE interrupt */
		hsi_ctrl->cawake_status = hsi_get_cawake(ch->hsi_port);
		wake = hsi_inl(base, HSI_SYS_WAKE_REG(port));
		if (unlikely(hsi_ctrl->acwake_status !=
				(wake & HSI_WAKE_MASK))) {
			dev_warn(&dev->device, "ACWAKE shadow register mismatch"
				" acwake_status: 0x%x, HSI_SYS_WAKE_REG: 0x%x",
				hsi_ctrl->acwake_status, wake);
			hsi_ctrl->acwake_status = wake & HSI_WAKE_MASK;
		}
		/* SSI_TODO: add safety check for SSI also */

		ch->flags &= ~HSI_CH_ACWAKE;
		hsi_ctrl->acwake_status &= ~BIT(channel);

		/* Check if we expect some ACWAKE line transition ? */
		/* Note that we test the acwake_status value after this IOCTL */
		if (!hsi_ctrl->acwake_status) {
			/* Check if CAWAKE is already low */
			if (!hsi_ctrl->cawake_status) {
				spin_unlock_bh(&hsi_ctrl->lock);
				dev_dbg(hsi_ctrl->dev,
					"CAWAKE is already low at the time of "
					"ACWAKE down, cancelling hsi_work...");

				if (cancel_work_sync(&ch->hsi_port->hsi_work)) {
					dev_dbg(hsi_ctrl->dev,
					"cancel_work_sync with success, "
					"disabling clocks...\n");

					/* Disable clocks (symetrical to */
					/* enable clocks of ACWAKE UP) */
					/* Here clocks are not yet disabled */
					/* due to previous safety clock */
					/* enable mechanism */
					hsi_clocks_disable_channel(
							dev->device.parent,
							ch->channel_number,
							__func__);
				} else {
					dev_dbg(hsi_ctrl->dev,
					"cancel_work_sync failed: hsi_work was "
					"already running\n");

				}
				spin_lock_bh(&hsi_ctrl->lock);
			} else {
				dev_dbg(hsi_ctrl->dev,
					"CAWAKE is high at the time of ACWAKE "
					"down, waiting CAWAKE falling edge.\n");
			}
		} else {
			/* ACWAKE line should not be deasserted yet */
			spin_unlock_bh(&hsi_ctrl->lock);
			/* Disable clocks (symetrical to enable clocks of */
			/* ACWAKE UP) */
			/* Here clocks are not yet disabled due to Safety */
			/* clock enable mechanism */
			hsi_clocks_disable_channel(dev->device.parent,
						   ch->channel_number,
						   __func__);
			spin_lock_bh(&hsi_ctrl->lock);
		}

		/* Release the wake line per channel */
		if ((wake & HSI_WAKE(channel))) {
			hsi_outl(HSI_CLEAR_WAKE(channel), base,
				 HSI_SYS_CLEAR_WAKE_REG(port));
		}

		spin_unlock_bh(&hsi_ctrl->lock);

		/* End of safety clock enable. */
		/* This may lead  to a real HW clock cut down based on CAWAKE */
		/* status */
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);

		goto out;
		break;
	case HSI_IOCTL_SEND_BREAK:
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		hsi_outl(1, base, HSI_HST_BREAK_REG(port));
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		/*HSI_TODO : need to deactivate clock after BREAK frames sent*/
		/*Use interrupt ? (if TX BREAK INT exists)*/
		break;
	case HSI_IOCTL_GET_ACWAKE:
		if (arg == NULL) {
			err = -EINVAL;
			goto out;
		} else
			hsi_clocks_enable_channel(dev->device.parent,
						  ch->channel_number, __func__);
		*(u32 *) arg = hsi_inl(base, HSI_SYS_WAKE_REG(port));
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	case HSI_IOCTL_FLUSH_RX:
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		hsi_outl(0, base, HSI_HSR_RXSTATE_REG(port));
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	case HSI_IOCTL_FLUSH_TX:
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		hsi_outl(0, base, HSI_HST_TXSTATE_REG(port));
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	case HSI_IOCTL_GET_CAWAKE:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		if (dev->ch->hsi_port->cawake_gpio < 0) {
			err = -ENODEV;
			goto out;
		}
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		*(unsigned int *)arg = hsi_get_cawake(dev->ch->hsi_port);
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	case HSI_IOCTL_SET_RX:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		spin_lock_bh(&hsi_ctrl->lock);
		err = hsi_set_rx(dev->ch->hsi_port, (struct hsr_ctx *)arg);
		spin_unlock_bh(&hsi_ctrl->lock);
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	case HSI_IOCTL_GET_RX:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		spin_lock_bh(&hsi_ctrl->lock);
		hsi_get_rx(dev->ch->hsi_port, (struct hsr_ctx *)arg);
		spin_unlock_bh(&hsi_ctrl->lock);
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	case HSI_IOCTL_SET_TX:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		spin_lock_bh(&hsi_ctrl->lock);
		err = hsi_set_tx(dev->ch->hsi_port, (struct hst_ctx *)arg);
		spin_unlock_bh(&hsi_ctrl->lock);
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	case HSI_IOCTL_GET_TX:
		if (!arg) {
			err = -EINVAL;
			goto out;
		}
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		spin_lock_bh(&hsi_ctrl->lock);
		hsi_get_tx(dev->ch->hsi_port, (struct hst_ctx *)arg);
		spin_unlock_bh(&hsi_ctrl->lock);
		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	case HSI_IOCTL_SW_RESET:
		hsi_clocks_enable_channel(dev->device.parent,
					  ch->channel_number, __func__);
		dev_info(dev->device.parent, "SW Reset\n");
		spin_lock_bh(&hsi_ctrl->lock);
		err = hsi_softreset(hsi_ctrl);

		/* Reset HSI config to default */
		hsi_softreset_driver(hsi_ctrl);
		spin_unlock_bh(&hsi_ctrl->lock);

		hsi_clocks_disable_channel(dev->device.parent,
					   ch->channel_number, __func__);
		break;
	default:
		err = -ENOIOCTLCMD;
		break;
	}
out:

	return err;
}
EXPORT_SYMBOL(hsi_ioctl);

/**
 * hsi_close - close given hsi device channel
 * @dev - reference to hsi device channel.
 */
void hsi_close(struct hsi_device *dev)
{
	struct hsi_dev *hsi_ctrl;

	if (!dev || !dev->ch) {
		pr_err(LOG_NAME "Trying to close wrong HSI device %p\n", dev);
		return;
	}
	dev_dbg(dev->device.parent, "%s\n", __func__);

	hsi_ctrl = dev->ch->hsi_port->hsi_controller;

	hsi_clocks_enable_channel(dev->device.parent, dev->ch->channel_number,
				__func__);

	spin_lock_bh(&hsi_ctrl->lock);
	if (dev->ch->flags & HSI_CH_OPEN) {
		dev->ch->flags &= ~HSI_CH_OPEN;
		__hsi_write_cancel(dev->ch);
		__hsi_read_cancel(dev->ch);
	}
	spin_unlock_bh(&hsi_ctrl->lock);

	hsi_clocks_disable_channel(dev->device.parent, dev->ch->channel_number,
				__func__);
}
EXPORT_SYMBOL(hsi_close);

/**
 * hsi_set_read_cb - register read_done() callback.
 * @dev - reference to hsi device channel where the callback is associated to.
 * @read_cb - callback to signal read transfer completed.
 *		size is expressed in number of 32-bit words.
 *
 * NOTE: Write callback must be only set when channel is not open !
 */
void hsi_set_read_cb(struct hsi_device *dev,
		     void (*read_cb) (struct hsi_device *dev,
				      unsigned int size))
{
	dev_dbg(dev->device.parent, "%s\n", __func__);

	dev->ch->read_done = read_cb;
}
EXPORT_SYMBOL(hsi_set_read_cb);

/**
 * hsi_set_read_cb - register write_done() callback.
 * @dev - reference to hsi device channel where the callback is associated to.
 * @write_cb - callback to signal read transfer completed.
 *		size is expressed in number of 32-bit words.
 *
 * NOTE: Read callback must be only set when channel is not open !
 */
void hsi_set_write_cb(struct hsi_device *dev,
		      void (*write_cb) (struct hsi_device *dev,
					unsigned int size))
{
	dev_dbg(dev->device.parent, "%s\n", __func__);

	dev->ch->write_done = write_cb;
}
EXPORT_SYMBOL(hsi_set_write_cb);

/**
 * hsi_set_port_event_cb - register port_event callback.
 * @dev - reference to hsi device channel where the callback is associated to.
 * @port_event_cb - callback to signal events from the channel port.
 */
void hsi_set_port_event_cb(struct hsi_device *dev,
			   void (*port_event_cb) (struct hsi_device *dev,
						  unsigned int event,
						  void *arg))
{
	dev_dbg(dev->device.parent, "%s\n", __func__);

	write_lock_bh(&dev->ch->rw_lock);
	dev->ch->port_event = port_event_cb;
	write_unlock_bh(&dev->ch->rw_lock);
}
EXPORT_SYMBOL(hsi_set_port_event_cb);
