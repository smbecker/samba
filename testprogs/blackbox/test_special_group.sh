#!/bin/sh

if [ $# -lt 1 ]; then
cat <<EOF
Usage: $0 PREFIX
EOF
exit 1;
fi

PREFIX_ABS="$1"
shift 1

failed=0

. `dirname $0`/subunit.sh
. `dirname $0`/common_test_fns.inc

OLD_RELEASE="release-4-5-0-pre1"
old_release_dir="$SRCDIR_ABS/source4/selftest/provisions/$OLD_RELEASE"

samba_tdbrestore="tdbrestore"
if [ -x "$BINDIR/tdbrestore" ]; then
    samba_tdbrestore="$BINDIR/tdbrestore"
fi

samba_undump="$SRCDIR_ABS/source4/selftest/provisions/undump.sh"

cleanup_output_directories()
{
    remove_directory $PREFIX_ABS/$OLD_RELEASE
}

undump_old() {
    $samba_undump $old_release_dir $PREFIX_ABS/$OLD_RELEASE $samba_tdbrestore
}

add_special_group() {
    $PYTHON $BINDIR/samba-tool group add 'protected users' --special -H tdb://$PREFIX_ABS/$OLD_RELEASE/private/sam.ldb
}

# double-check we cleaned up from the last test run
cleanup_output_directories

testit $OLD_RELEASE undump_old || failed=`expr $failed + 1`

testit "add_special_group" add_special_group || failed=`expr $failed + 1`

testit_expect_failure_grep "add_duplicate_special_group" "Failed to add group.*already exists" add_special_group || failed=`expr $failed + 1`

cleanup_output_directories

exit $failed
