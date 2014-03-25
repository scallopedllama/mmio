#ifndef __LINUX_MMIO_H_INCLUDED
#define __LINUX_MMIO_H_INCLUDED

#include <linux/rwsem.h>
#include <linux/device.h>

#define MMIO_ENTRY_RW        (MMIO_ENTRY_READ | MMIO_ENTRY_WRITE)
#define MMIO_ENTRY_READ      (1 << 0)
#define MMIO_ENTRY_WRITE     (1 << 1)

struct device;

struct mmio_classdev {
	 const char           *name;    // Name of folder to put in /sys/class/mmio
	 u8                   size;     // Size in bytes of this bank (1, 2 or 4)
	 struct mmio_entry    *entries; // Array of mmio entries
	 unsigned int         num_entries;
	 unsigned int         offset;   // Offset from base for this bank
	 void                 *base;    // io_remap'd base of mmio memory
	 
	 struct device        *dev;
	 struct list_head     node;     // MMIO Device list
	 struct rw_semaphore  rwsem;
};
 
struct mmio_entry {
	const char               *name;
	u32                      mask;       // Mask to apply and shift to get mmio value
	unsigned long            flags;      // Directionality and such. Defaults to just MMIO_ENTRY_RW
	
	struct device_attribute  attr;       // Populated automatically
};

extern int  mmio_classdev_register(struct device *parent, struct mmio_classdev *mmio_cdev);
extern void mmio_classdev_unregister(struct mmio_classdev *mmio_cdev);

extern int mmio_set_value(struct mmio_classdev *parent, struct mmio_entry *entry, unsigned long value);
extern u32 mmio_get_value(struct mmio_classdev *parent, struct mmio_entry *entry);

#endif
