#ifndef CPUMINER_INCLUDE_H_
#define CPUMINER_INCLUDE_H_


#include <libsysutils/sysutils.h>
#include "cpuminer-config.h"
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#ifndef WIN32
#include <sys/resource.h>
#endif
#include <getopt.h>
#include <jansson.h>
#include <curl/curl.h>
#include "compat.h"
#include "miner.h"
#include "scrypt_spu_bin.h"

#include "type.h"
#include "work/work.h"
#include "ppu/ppu.h"

#endif // CPUMINER_INCLUDE_H_
