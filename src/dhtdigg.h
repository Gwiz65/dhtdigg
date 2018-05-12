/****************************************************************************
 * dhtdigg.h                                                                *
 * Copyright (C) 2018 Gwiz <gwiz2009@gmail.com>                             *
 *                                                                          *
 * dhtdigg is free software: you can redistribute it and/or modify it       *
 * under the terms of the GNU General Public License as published by the    *
 * Free Software Foundation, either version 3 of the License, or            *
 * (at your option) any later version.                                      *
 *                                                                          *
 * dhtdigg is distributed in the hope that it will be useful, but           *
 * WITHOUT ANY WARRANTY; without even the implied warranty of               *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                     *
 * See the GNU General Public License for more details.                     *
 *                                                                          *
 * You should have received a copy of the GNU General Public License along  *
 * with this program.  If not, see <http://www.gnu.org/licenses/>.          *
 *                                                                          *
 ****************************************************************************/

#include <config.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <dirent.h>
#include <ctype.h>
#include <sqlite3.h> 

#include "dht.h"
#include "crypt3.h"

/****************************
 *        Macros            *
 ****************************/
#define CRYPT_HAPPY(c) ((c % 0x60) + 0x20)

/****************************
 *        Constants         *
 ****************************/
static unsigned char zeros[20] = {0};

/****************************
 *       Structures         *
 ****************************/
struct stat st = {0};
struct peerlist {
	unsigned char info_hash[20];
	struct in_addr addr;
	unsigned short prt;
};
struct bootstrap_storage {
	int numofIPv4s;
	int numofIPv6s;
	struct sockaddr_in IPv4bootnodes[6];
	struct sockaddr_in6 IPv6bootnodes[6];
};

/****************************
 *    Function Declares     *
 ****************************/
gint DhtThread(void);


static void callback(void *closure,
              int event,
              unsigned char *info_hash,
              void *data, size_t data_len);

extern void *memmem (const void *__haystack, size_t __haystacklen,
		     const void *__needle, size_t __needlelen);
