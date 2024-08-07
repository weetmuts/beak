#
# Copyright (C) 2017-2024 Fredrik Öhrström
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Expecting a spec.gmk to be included already!

# To build with a lot of build output, type:
# make VERBOSE=

# Expects release or debug as a target

ifeq (release,$(findstring release,$(MAKECMDGOALS)))
TYPE:=release
STRIP_COMMAND:=$(STRIP)
endif

ifeq (debug,$(findstring debug,$(MAKECMDGOALS)))
TYPE:=debug
STRIP_COMMAND:=true
endif

ifeq (asan,$(findstring asan,$(MAKECMDGOALS)))
TYPE:=asan
STRIP_COMMAND:=true
endif

ifeq ($(TYPE),)
    $(error You must specify "make release" or "make debug" or "make asan")
endif

$(shell mkdir -p $(OUTPUT_ROOT)/$(TYPE))

VERBOSE?=@

ifeq ($(PLATFORM),WINAPI)
STRIP_COMMAND:=true
endif

MEDIA_SOURCES:=beak_importmedia.cc beak_cameramedia.cc beak_servemedia.cc beak_indexmedia.cc media.cc
MEDIA_SOURCES:=$(addprefix $(SRC_ROOT)/src/,$(MEDIA_SOURCES))
NO_MEDIA_SOURCES:=no_beak_importmedia.cc no_beak_cameramedia.cc no_beak_servemedia.cc no_beak_indexmedia.cc no_media.cc
NO_MEDIA_SOURCES:=$(addprefix $(SRC_ROOT)/src/,$(NO_MEDIA_SOURCES))

WINAPI_SOURCES:=$(filter-out %posix.cc, $(wildcard $(SRC_ROOT)/src/*.cc))
ifeq ($(ENABLE_FUSE),yes)
WINAPI_SOURCES:=$(filter-out %no_fuse.cc,$(WINAPI_SOURCES))
endif

WINAPI_SOURCES:=$(filter-out %media.cc,$(WINAPI_SOURCES))

WINAPI_OBJS:=\
    $(patsubst %.cc,%.o,$(subst $(SRC_ROOT)/src,$(OUTPUT_ROOT)/$(TYPE),$(WINAPI_SOURCES)))

WINAPI_MEDIA_OBJS:=\
    $(patsubst %.cc,%.o,$(subst $(SRC_ROOT)/src,$(OUTPUT_ROOT)/$(TYPE),$(MEDIA_SOURCES)))

WINAPI_NO_MEDIA_OBJS:=\
    $(patsubst %.cc,%.o,$(subst $(SRC_ROOT)/src,$(OUTPUT_ROOT)/$(TYPE),$(NO_MEDIA_SOURCES)))

WINAPI_BEAK_OBJS:=\
    $(filter-out %testinternals.o,$(WINAPI_OBJS))

WINAPI_LIBS := \
$(OUTPUT_ROOT)/$(TYPE)/libgcc_s_seh-1.dll \
$(OUTPUT_ROOT)/$(TYPE)/libstdc++-6.dll \
$(OUTPUT_ROOT)/$(TYPE)/libwinpthread-1.dll


WINAPI_TESTINTERNALS_OBJS:=\
    $(filter-out %main.o,$(WINAPI_OBJS))

POSIX_SOURCES:=$(filter-out %winapi.cc,$(wildcard $(SRC_ROOT)/src/*.cc))

ifeq ($(ENABLE_FUSE),yes)
POSIX_SOURCES:=$(filter-out %no_fuse.cc,$(POSIX_SOURCES))
endif

POSIX_SOURCES:=$(filter-out %media.cc,$(POSIX_SOURCES))

POSIX_OBJS:=\
    $(patsubst %.cc,%.o,$(subst $(SRC_ROOT)/src,$(OUTPUT_ROOT)/$(TYPE),$(POSIX_SOURCES)))

POSIX_MEDIA_OBJS:=\
    $(patsubst %.cc,%.o,$(subst $(SRC_ROOT)/src,$(OUTPUT_ROOT)/$(TYPE),$(MEDIA_SOURCES)))

POSIX_NO_MEDIA_OBJS:=\
    $(patsubst %.cc,%.o,$(subst $(SRC_ROOT)/src,$(OUTPUT_ROOT)/$(TYPE),$(NO_MEDIA_SOURCES)))

POSIX_BEAK_OBJS:=\
    $(filter-out %testinternals.o,$(POSIX_OBJS))

POSIX_TESTINTERNALS_OBJS:=\
    $(filter-out %main.o,$(POSIX_OBJS))

POSIX_LIBS :=

EXTRA_LIBS := $($(PLATFORM)_LIBS)

$(OUTPUT_ROOT)/generated_autocomplete.h: $(SRC_ROOT)/scripts/autocompletion_for_beak.sh
	echo Generating autocomplete include.
	cat $< | sed 's/\\/\\\\/g' | sed 's/\"/\\\"/g' | sed 's/^\(.*\)$$/"\1\\n"/g'> $@

$(OUTPUT_ROOT)/generated_filetypes.h: $(SRC_ROOT)/scripts/filetypes.inc
	echo Generating filetypes include.
	echo '#define LIST_OF_SUFFIXES \\' > $@
	$(SRC_ROOT)/scripts/generate_filetypes.sh $(SRC_ROOT)/scripts/filetypes.inc >> $@
	echo  >> $@

BEAK_OBJS:=$($(PLATFORM)_BEAK_OBJS)
BEAK_MEDIA_OBJS:=$($(PLATFORM)_MEDIA_OBJS)
BEAK_NO_MEDIA_OBJS:=$($(PLATFORM)_NO_MEDIA_OBJS)
TESTINTERNALS_OBJS:=$($(PLATFORM)_TESTINTERNALS_OBJS)

$(OUTPUT_ROOT)/$(TYPE)/beak_genautocomp.o: $(OUTPUT_ROOT)/generated_autocomplete.h
$(OUTPUT_ROOT)/$(TYPE)/fileinfo.o: $(OUTPUT_ROOT)/generated_filetypes.h

$(OUTPUT_ROOT)/$(TYPE)/%.o: $(SRC_ROOT)/src/%.cc
	@echo Compiling $(TYPE) $(CONF_MNEMONIC) $$(basename $<)
	$(VERBOSE)$(CXX) -I$(OUTPUT_ROOT) -I$(BUILD_ROOT) $(CXXFLAGS_$(TYPE)) $(CXXFLAGS) -MMD $< -c -o $@
	$(VERBOSE)$(CXX) -E  -I$(OUTPUT_ROOT) -I$(BUILD_ROOT) $(CXXFLAGS_$(TYPE)) $(CXXFLAGS) -MMD $< -c > $@.source

$(OUTPUT_ROOT)/$(TYPE)/beak-media: $(BEAK_OBJS) $(BEAK_MEDIA_OBJS)
	@echo Linking $(TYPE) $(CONF_MNEMONIC) $@
	$(VERBOSE)$(CXX) -o $@ $(LDFLAGS_$(TYPE)) $(LDFLAGS) $(BEAK_OBJS) $(BEAK_MEDIA_OBJS) \
                      $(LDFLAGSBEGIN_$(TYPE)) $(OPENSSL_LIBS) $(ZLIB_LIBS) $(FUSE_LIBS) $(LIBRSYNC_LIBS) $(GPHOTO2_LIBS) $(LDFLAGSEND_$(TYPE)) $(MEDIA_LIBS) -lpthread
	$(VERBOSE)$(STRIP_COMMAND) $@
	@echo Done linking $(TYPE) $(CONF_MNEMONIC) $@

$(OUTPUT_ROOT)/$(TYPE)/beak: $(BEAK_OBJS) $(BEAK_NO_MEDIA_OBJS)
	@echo Linking $(TYPE) $(CONF_MNEMONIC) $@
	$(VERBOSE)$(CXX) -o $@ $(LDFLAGS_$(TYPE)) $(LDFLAGS) $(BEAK_OBJS) $(BEAK_NO_MEDIA_OBJS) \
                      $(LDFLAGSBEGIN_$(TYPE)) $(OPENSSL_LIBS) $(ZLIB_LIBS) $(FUSE_LIBS) $(LIBRSYNC_LIBS) $(GPHOTO2_LIBS) $(LDFLAGSEND_$(TYPE)) -lpthread
	$(VERBOSE)$(STRIP_COMMAND) $@
	@echo Done linking $(TYPE) $(CONF_MNEMONIC) $@

$(OUTPUT_ROOT)/$(TYPE)/testinternals: $(TESTINTERNALS_OBJS) $(BEAK_NO_MEDIA_OBJS)
	@echo Linking $(TYPE) $(CONF_MNEMONIC) $@
	$(VERBOSE)$(CXX) -o $@ $(LDFLAGS_$(TYPE)) $(LDFLAGS) $(TESTINTERNALS_OBJS) $(BEAK_NO_MEDIA_OBJS) \
                      $(LDFLAGSBEGIN_$(TYPE)) $(OPENSSL_LIBS) $(ZLIB_LIBS) $(FUSE_LIBS) $(LIBRSYNC_LIBS) $(GPHOTO2_LIBS) $(MEDIA_LIBS) $(LDFLAGSEND_$(TYPE)) -lpthread
	$(VERBOSE)$(STRIP_COMMAND) $@
	@echo Done linking $(TYPE) $(CONF_MNEMONIC) $@

$(OUTPUT_ROOT)/$(TYPE)/libgcc_s_seh-1.dll: /usr/lib/gcc/x86_64-w64-mingw32/10-win32/libgcc_s_seh-1.dll
	cp /usr/lib/gcc/x86_64-w64-mingw32/10-win32/libgcc_s_seh-1.dll $@

$(OUTPUT_ROOT)/$(TYPE)/libstdc++-6.dll: /usr/lib/gcc/x86_64-w64-mingw32/10-win32/libstdc++-6.dll
	cp /usr/lib/gcc/x86_64-w64-mingw32/10-win32/libstdc++-6.dll $@

$(OUTPUT_ROOT)/$(TYPE)/libwinpthread-1.dll: /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll
	cp $< $@

BINARIES:=$(OUTPUT_ROOT)/$(TYPE)/beak $(OUTPUT_ROOT)/$(TYPE)/testinternals

ifeq ($(ENABLE_MEDIA),yes)
BINARIES:=$(BINARIES) $(OUTPUT_ROOT)/$(TYPE)/beak-media
endif

ifeq ($(CLEAN),clean)
# Clean!
release debug:
	rm -f $(OUTPUT_ROOT)/$(TYPE)/*
	rm -f $(OUTPUT_ROOT)/$(TYPE)/generated_autocomplete.h
	rm -f $(OUTPUT_ROOT)/$(TYPE)/generated_filetypes.h
else
# Build!
release debug asan: $(BINARIES) $(EXTRA_LIBS)
ifeq ($(PLATFORM),winapi)
	cp $(OUTPUT_ROOT)/$(TYPE)/beak $(OUTPUT_ROOT)/$(TYPE)/beak.exe
        cp $(OUTPUT_ROOT)/$(TYPE)/testinternals $(OUTPUT_ROOT)/$(TYPE)/testinternals.exe
endif
endif

clean_cc:
	find . -name "*.gcov" -delete
	find . -name "*.gcda" -delete

# This generates annotated source files ending in .gcov
# inside the build_debug where non-executed source lines are marked #####
gcov:
	@if [ "$(TYPE)" != "debug" ]; then echo "You have to run \"make debug gcov\""; exit 1; fi
	$(GCOV) -o build_debug $(PROG_OBJS) $(DRIVER_OBJS)
	mv *.gcov build_debug

# --no-external
lcov:
	@if [ "$(TYPE)" != "debug" ]; then echo "You have to run \"make debug lcov\""; exit 1; fi
	lcov --directory . -c --output-file $(OUTPUT_ROOT)/$(TYPE)/lcov.info
	(cd $(OUTPUT_ROOT)/$(TYPE); genhtml lcov.info)
	xdg-open $(OUTPUT_ROOT)/$(TYPE)/src/index.html

.PHONY: release debug

# Include dependency information generated by gcc in a previous compile.
include $(wildcard $(patsubst %.o,%.d,$(POSIX_OBJS)))
