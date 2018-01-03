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

BUILDDIRS:=$(dir $(realpath $(wildcard build/*/spec.gmk)))

ifeq (,$(BUILDDIRS))
    $(error Run configure first!)
endif

VERBOSE?=@

release:
	@echo Building release for $(words $(BUILDDIRS)) host\(s\).
	@for x in $(BUILDDIRS); do echo; echo Bulding $$(basename $$x) ; $(MAKE) --no-print-directory -C $$x release ; done

debug:
	@echo Building debug for $(words $(BUILDDIRS)) host\(s\).
	@for x in $(BUILDDIRS); do echo; echo Bulding $$(basename $$x) ; $(MAKE) --no-print-directory -C $$x debug ; done

test_release:
	@echo Running tests
	./test.sh $(BUILDDIRS)/release/beak

test_debug:
	@echo Running tests
	./test.sh $(BUILDDIRS)/debug/beak

clean:
	@echo Removing release and debug builds
	@for x in $(BUILDDIRS); do echo; rm -rf $$x/release $$x/debug $$x/generated_autocomplete.h; done

clean-all:
	@echo Removing configuration and artifacts
	$(VERBOSE)rm -rf $(BUILDDIRS)

DESTDIR?=/usr/local
install:
	install -Dm 755 -s $(BUILDDIRS)/release/beak $(DESTDIR)/bin/beak
	install -Dm 644 beak.1 $(DESTDIR)/man/man1/beak.1

uninstall:
	rm -f $(DESTDIR)/bin/beak
	rm -f $(DESTDIR)/man/man1/beak.1

.PHONY: release debug test clean clean-all help
