## Process this file with automake to produce Makefile.in
##
## /****************************************************************************
##  * dhtdigg.c                                                                *
##  * Copyright (C) 2018 Gwiz <gwiz2009@gmail.com>                             *
##  *                                                                          *
##  * dhtdigg is free software: you can redistribute it and/or modify it       *
##  * under the terms of the GNU General Public License as published by the    *
##  * Free Software Foundation, either version 3 of the License, or            *
##  * (at your option) any later version.                                      *
##  *                                                                          *
##  * dhtdigg is distributed in the hope that it will be useful, but           *
##  * WITHOUT ANY WARRANTY; without even the implied warranty of               *
##  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                     *
##  * See the GNU General Public License for more details.                     *
##  *                                                                          *
##  * You should have received a copy of the GNU General Public License along  *
##  * with this program.  If not, see <http://www.gnu.org/licenses/>.          *
##  *                                                                          *
##  ****************************************************************************/
## 
uidir = $(pkgdatadir)/ui
ui_DATA = dhtdigg.ui
AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
	$(DHTDIGG_CFLAGS)
AM_CFLAGS =\
	 -Wall\
	 -g
bin_PROGRAMS = dhtdigg
dhtdigg_SOURCES = \
	dhtdigg.c \
	dht.h \
	dht.c \
	dhtdigg.h \
	crypt3.h
dhtdigg_LDFLAGS = \
	-Wl,--export-dynamic
dhtdigg_LDADD = $(DHTDIGG_LIBS)
EXTRA_DIST = $(ui_DATA)
# Remove ui directory on uninstall
uninstall-local:
	-rm -r $(uidir)
	-rm -r $(pkgdatadir)

