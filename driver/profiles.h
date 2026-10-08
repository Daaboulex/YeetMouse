#ifndef PROFILES_H
#define PROFILES_H

#include "profile_table.h"

int profiles_init(void);

void profiles_exit(void);

const struct profile_table *profiles_current(void);

#endif
