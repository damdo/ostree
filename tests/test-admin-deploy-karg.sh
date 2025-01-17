#!/bin/bash
#
# Copyright (C) 2011 Colin Walters <walters@verbum.org>
#
# SPDX-License-Identifier: LGPL-2.0+
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library. If not, see <https://www.gnu.org/licenses/>.

set -euo pipefail

. $(dirname $0)/libtest.sh

# Exports OSTREE_SYSROOT so --sysroot not needed.
setup_os_repository "archive" "syslinux"

echo "1..3"

${CMD_PREFIX} ostree --repo=sysroot/ostree/repo pull-local --remote=testos testos-repo testos/buildmain/x86_64-runtime
rev=$(${CMD_PREFIX} ostree --repo=sysroot/ostree/repo rev-parse testos/buildmain/x86_64-runtime)
export rev
# This initial deployment gets kicked off with some kernel arguments 
${CMD_PREFIX} ostree admin deploy --karg=root=LABEL=MOO --karg=quiet --os=testos testos:testos/buildmain/x86_64-runtime
${CMD_PREFIX} ostree admin deploy --karg=FOO=BAR --os=testos testos:testos/buildmain/x86_64-runtime
${CMD_PREFIX} ostree admin deploy --karg=TESTARG=TESTVALUE --os=testos testos:testos/buildmain/x86_64-runtime
assert_file_has_content sysroot/boot/loader/entries/ostree-1-testos.conf 'options.*FOO=BAR'
assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf 'options.*FOO=BAR'
assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf 'options.*TESTARG=TESTVALUE'
${CMD_PREFIX} ostree admin deploy --karg=ANOTHERARG=ANOTHERVALUE --os=testos testos:testos/buildmain/x86_64-runtime
assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf 'options.*TESTARG=TESTVALUE'
assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf 'options.*ANOTHERARG=ANOTHERVALUE'

echo "ok deploy with --karg, but same config"

${CMD_PREFIX} ostree admin deploy --karg-proc-cmdline --os=testos testos:testos/buildmain/x86_64-runtime
for arg in $(cat /proc/cmdline); do
    case "$arg" in
	ostree=*) # Skip ostree arg that gets stripped out
	  ;;
	initrd=*|BOOT_IMAGE=*) # Skip options set by bootloader that gets filtered out
	   ;;
	*) assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf "options.*$arg"
	   ;;
    esac
done

echo "ok deploy --karg-proc-cmdline"

${CMD_PREFIX} ostree admin status
${CMD_PREFIX} ostree admin undeploy 0

${CMD_PREFIX} ostree admin deploy  --os=testos --karg-append=APPENDARG=VALAPPEND --karg-append=APPENDARG=2NDAPPEND testos:testos/buildmain/x86_64-runtime
assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf 'options.*FOO=BAR'
assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf 'options.*TESTARG=TESTVALUE'
assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf 'options.*APPENDARG=VALAPPEND .*APPENDARG=2NDAPPEND'

# Check correct ordering of different-valued args of the same key.
${CMD_PREFIX} ostree admin deploy  --os=testos --karg-append=FOO=TESTORDERED --karg-append=APPENDARG=3RDAPPEND testos:testos/buildmain/x86_64-runtime
assert_file_has_content sysroot/boot/loader/entries/ostree-2-testos.conf 'options.*APPENDARG=VALAPPEND .*APPENDARG=2NDAPPEND .*FOO=TESTORDERED .*APPENDARG=3RDAPPEND'

echo "ok deploy --karg-append"
