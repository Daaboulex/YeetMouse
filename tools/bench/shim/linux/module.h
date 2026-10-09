#ifndef BENCH_SHIM_LINUX_MODULE_H
#define BENCH_SHIM_LINUX_MODULE_H

#define MODULE_AUTHOR(author)
#define MODULE_DESCRIPTION(description)
#define MODULE_LICENSE(license)
#define MODULE_VERSION(version)
#define MODULE_PARM_DESC(name, description)
#define module_param_named(name, variable, type, permissions)
#define module_param_string(name, variable, length, permissions)
#define module_param(name, type, permissions)
#define EXPORT_SYMBOL(symbol)
#define module_init(function)
#define module_exit(function)

#endif
