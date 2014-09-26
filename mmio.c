/*
 * MMIO
 *
 * Copyright (C) 2014 Joe Balough <jbb5044@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/rwsem.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <net/sctp/command.h>
#include "mmio.h"

DECLARE_RWSEM(mmio_list_lock);
LIST_HEAD(mmio_list);

static struct class *mmio_class;


/**
 * mmio_get_value - Internal mechanism to get the value of a register
 * @parent The mmio_classdev bank containing the entry
 * @entry  The mmio_entry to get
 */
u32 mmio_get_value(struct mmio_classdev *parent, struct mmio_entry *entry)
{
	u32 reg, mask;
	if (! parent || !entry)
	{
		printk(KERN_ERR "%s: preventing null pointer deref. parent is 0x%p, entry is 0x%p\n", __FUNCTION__, parent, entry);
		return 0;
	}
	
	down_read(&parent->rwsem);
	switch(parent->size)
	{
		default:
		case 1:
			reg = __raw_readb(parent->base + parent->offset);
			break;
		case 2:
			reg = __raw_readw(parent->base + parent->offset);
			break;
		case 4:
			reg = __raw_readl(parent->base + parent->offset);
			break;
	}
	
	up_read(&parent->rwsem);
	
	reg &= entry->mask;
	
	mask = entry->mask;
	while (0 == (mask & 1))
	{
		mask >>= 1;
		reg >>= 1;
	}
	
	return reg;
}
EXPORT_SYMBOL_GPL(mmio_get_value);

/**
 * mmio_value_show - Sysfs interface to show the value of a register.
 */
static ssize_t mmio_value_show(struct device *dev, 
							   struct device_attribute *attr, char *buf)
{
	int i;
	struct mmio_classdev *mmio_cdev = dev_get_drvdata(dev);
	struct mmio_entry *entry = NULL;
	u32 value;
	
	for (i = 0; i < mmio_cdev->num_entries; i++)
	{
		if ( mmio_cdev->entries[i].name == attr->attr.name )
		{
			entry = &(mmio_cdev->entries[i]);
			break;
		}
	}
	if (entry == NULL)
		return -EINVAL;
	
	if (! (entry->flags & MMIO_ENTRY_READ) )
		return -EPERM;
	
	value = mmio_get_value(mmio_cdev, entry);
	
	return sprintf(buf, "%u\n", value);
}

/**
 * mmio_set_value - Internal mechanism to set value to register
 * @parent The mmio_classdev bank containing the entry
 * @entry  The mmio_entry to modify
 * @value  The value to set
 */
int mmio_set_value(struct mmio_classdev *parent, struct mmio_entry *entry, unsigned long value)
{
	u32 reg, mask;
	
	if (!parent || !entry)
		return -EINVAL;
	
	down_write(&parent->rwsem);
	switch(parent->size)
	{
		default:
		case 1:
			reg = __raw_readb(parent->base + parent->offset);
			break;
		case 2:
			reg = __raw_readw(parent->base + parent->offset);
			break;
		case 4:
			reg = __raw_readl(parent->base + parent->offset);
			break;
	}
	
	if (value)
	{
		mask = entry->mask;
		while (0 == (mask & 1))
		{
			mask >>= 1;
			value <<= 1;
		}
	}
	
	if ((value & entry->mask) != value)
	{
		up_write(&parent->rwsem);
		return -EOVERFLOW;
	}
	
	reg &= ~entry->mask;
	reg |= value;
	
	switch(parent->size)
	{
		default:
		case 1:
			__raw_writeb((u8) reg, parent->base + parent->offset);
			break;
		case 2:
			__raw_writew((u16) reg, parent->base + parent->offset);
			break;
		case 4:
			__raw_writel(reg, parent->base + parent->offset);
			break;
	}
	
	up_write(&parent->rwsem);
	return 0;
}
EXPORT_SYMBOL_GPL(mmio_set_value);

/**
 * mmio_value_store - Sysfs interface to store a value to a register.
 */
static ssize_t mmio_value_store(struct device *dev,
								struct device_attribute *attr, const char *buf, size_t size)
{
	int i, r;
	struct mmio_classdev *mmio_cdev = dev_get_drvdata(dev);
	struct mmio_entry *entry = NULL;
	ssize_t ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;
	
	for (i = 0; i < mmio_cdev->num_entries; i++)
	{
		if ( mmio_cdev->entries[i].name == attr->attr.name )
		{
			entry = &(mmio_cdev->entries[i]);
			break;
		}
	}
	if (entry == NULL)
		return -EINVAL;
	
	if (! (entry->flags & MMIO_ENTRY_WRITE) )
		return -EPERM;
	
	if (isspace(*after))
		count++;
	
	if (count == size) {
		ret = count;
		
		r = mmio_set_value(mmio_cdev, entry, state);
		if (r < 0) ret = r;
	}
	
	return ret;
}

/**
 * mmio_classdev_register - register a new object of the mmio_classdev class.
 * @parent: The device to register
 * @mmio_cdev: The mmio_classdev structure for this device
 */
int mmio_classdev_register(struct device *parent, struct mmio_classdev *mmio_cdev)
{
	int i, ret;
	if (!mmio_cdev->base || !mmio_cdev->entries || !mmio_cdev->name)
		return -EINVAL;
	if (mmio_cdev->size != 1 && mmio_cdev->size != 2 && mmio_cdev->size != 4)
		return -EINVAL;
	if (mmio_cdev->size == 2 && ((int) mmio_cdev->base + mmio_cdev->offset) & 0x01)
		return -EINVAL;
	if (mmio_cdev->size == 4 && ((int) mmio_cdev->base + mmio_cdev->offset) & 0x03)
		return -EINVAL;
	
	mmio_cdev->dev = device_create(mmio_class, parent, 0, mmio_cdev,
								   "%s", mmio_cdev->name);
	if (IS_ERR(mmio_cdev->dev))
		return PTR_ERR(mmio_cdev->dev);
	
	init_rwsem(&mmio_cdev->rwsem);
	for (i = 0; i < mmio_cdev->num_entries; i++)
	{
		if (!mmio_cdev->entries[i].mask)
		{
			printk(KERN_INFO "%s: Skipping entry %d (%s), mask is zero.\n", __FUNCTION__, i, mmio_cdev->entries[i].name);
			continue;
		}
		mmio_cdev->entries[i].attr.attr.name = mmio_cdev->entries[i].name;
		mmio_cdev->entries[i].attr.attr.mode = 0644;
		mmio_cdev->entries[i].attr.show = mmio_value_show;
		mmio_cdev->entries[i].attr.store = mmio_value_store;
		
		ret = device_create_file(mmio_cdev->dev, &(mmio_cdev->entries[i].attr));
		if (ret) {
			printk(KERN_ERR "%s: Failed to add sysfs file %s\n", __FUNCTION__, mmio_cdev->entries[i].name);
			dev_err(mmio_cdev->dev, "failed: sysfs file %s\n", mmio_cdev->entries[i].name);
			goto failed_unregister_dev_file;
		}
	}
	
	// add to the list of mmio devices
	down_write(&mmio_list_lock);
	list_add_tail(&mmio_cdev->node, &mmio_list);
	up_write(&mmio_list_lock);
	
	printk(KERN_INFO "Registered mmio device \"%s\" at 0x%p, offset 0x%x, size %d B.\n",
		   mmio_cdev->name, mmio_cdev->base, mmio_cdev->offset, mmio_cdev->size);
	
	return 0;
	
	failed_unregister_dev_file:
	for (i--; i >= 0; i--)
		device_remove_file(mmio_cdev->dev, &(mmio_cdev->entries[i].attr));
	
	return ret;
}
EXPORT_SYMBOL_GPL(mmio_classdev_register);

/**
 * mmio_classdev_unregister - unregisters a object of mmio_classdev class.
 * @mmio_cdev: the mmio device to unregister
 *
 * Unregisters a previously registered via led_classdev_register object.
 */
void mmio_classdev_unregister(struct mmio_classdev *mmio_cdev)
{
	int i;
	for (i = 0; i < mmio_cdev->num_entries; i++)
	{
		device_remove_file(mmio_cdev->dev, &(mmio_cdev->entries[i].attr));
	}
	
	device_unregister(mmio_cdev->dev);
	
	down_write(&mmio_list_lock);
	list_del(&mmio_cdev->node);
	up_write(&mmio_list_lock);
}
EXPORT_SYMBOL_GPL(mmio_classdev_unregister);

static int __init mmio_init(void)
{
	mmio_class = class_create(THIS_MODULE, "mmio");
	if (IS_ERR(mmio_class))
		return PTR_ERR(mmio_class);
	return 0;
}

static void __exit mmio_exit(void)
{
	class_destroy(mmio_class);
}

subsys_initcall(mmio_init);
module_exit(mmio_exit);

MODULE_AUTHOR("Joe Balough <jbb5044@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MMIO Class Interface");
