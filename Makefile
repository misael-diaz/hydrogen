#!/usr/bin/make
#
# Hydrogen
#
# source: Makefile
# author: @misael-diaz
#
# Copyright (c) 2026 Misael Diaz-Maldonado
#
# This file is released under the GNU General Public License version 2 only
# as published by the Free Software Foundation.
#

include make-inc

all: $(SRV_BIN) $(MODULES)

$(SRV_BIN): $(SRV_OBJ)
	$(CC) $(CCOPT) $(SRV_OBJ) -o $(SRV_BIN) $(LNOPT) $(LIBS)

$(SRV_OBJ): $(SRV_CXX)
	$(CC) $(CCOPT) -c $(SRV_CXX) -o $(SRV_OBJ)

# normally you would build the modules separately from the server build
$(FAVICON_SO): $(FAVICON_CXX)
	@mkdir -p modules
	$(CC) $(CCOPT) -fPIC -shared $(FAVICON_CXX) -o $(FAVICON_SO)

$(HERO_SO): $(HERO_CXX)
	@mkdir -p modules
	$(CC) $(CCOPT) -fPIC -shared $(HERO_CXX) -o $(HERO_SO)

$(ROOT_SO): $(ROOT_CXX)
	@mkdir -p modules
	$(CC) $(CCOPT) -fPIC -shared $(ROOT_CXX) -o $(ROOT_SO)

clean:
	rm -f *.o *.obj modules/*.so *.bin
