#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xf6628fc9, "module_layout" },
	{ 0x3ec8886f, "param_ops_int" },
	{ 0xf6fd5309, "device_destroy" },
	{ 0x7c61340c, "__release_region" },
	{ 0xf20dabd8, "free_irq" },
	{ 0x5722473f, "class_destroy" },
	{ 0x6c075966, "device_create" },
	{ 0xdc5e7ab3, "__class_create" },
	{ 0xdaf688a7, "pv_cpu_ops" },
	{ 0x2072ee9b, "request_threaded_irq" },
	{ 0xcc18bdb6, "remove_proc_entry" },
	{ 0xf0ea9b7d, "kmem_cache_destroy" },
	{ 0x1fedf0f4, "__request_region" },
	{ 0xff7559e4, "ioport_resource" },
	{ 0x4640a095, "create_proc_entry" },
	{ 0xd0efbd1f, "kmem_cache_create" },
	{ 0xd6a968fe, "cdev_del" },
	{ 0x2b140f5e, "cdev_add" },
	{ 0x88dc55cf, "cdev_init" },
	{ 0x7485e15e, "unregister_chrdev_region" },
	{ 0xe116ba9b, "cdev_alloc" },
	{ 0x29537c9e, "alloc_chrdev_region" },
	{ 0xd8e484f0, "register_chrdev_region" },
	{ 0xf161980c, "kmem_cache_free" },
	{ 0xb1486780, "__free_pages" },
	{ 0x6729d3df, "__get_user_4" },
	{ 0x4f8b5ddb, "_copy_to_user" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0x50720c5f, "snprintf" },
	{ 0x27e1a049, "printk" },
	{ 0xb4390f9a, "mcount" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "905C25C44212A023338F868");
