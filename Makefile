# Copyright (C) 2017 Fredrik Öhrström
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
# 
help:
	@echo "Usage: make (release|debug|clean|clean-all)"

BUILDDIR:=$(dir $(realpath $(wildcard build/*/spec.gmk)))

ifeq (,$(BUILDDIR))
    $(error Run configure first!)
endif

VERBOSE?=@

release:
	@echo Building release
	$(VERBOSE)$(MAKE) --no-print-directory -C $(BUILDDIR) release

debug:
	@echo Building debug
	$(VERBOSE)$(MAKE) --no-print-directory -C $(BUILDDIR) debug

test_release:
	@echo Running tests
	./test.sh $(BUILDDIR)/release/beak

test_debug:
	@echo Running tests
	./test.sh $(BUILDDIR)/debug/beak

clean:
	@echo Removing release and debug builds
	$(VERBOSE)rm -rf $(BUILDDIR)/release $(BUILDDIR)/debug

clean-all:
	@echo Removing configuration and artifacts
	$(VERBOSE)rm -rf $(BUILDDIR)

.PHONY: release debug test clean clean-all help
