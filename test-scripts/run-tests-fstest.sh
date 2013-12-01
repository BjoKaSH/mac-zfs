#! /bin/bash

set -x -v
export LC_ALL=C

# load configuration, if present
#
# set defaults
poolbase=pool_$(date +%s)_$$
has_fstest=1
genrand_bin=./test-scripts/genrand
#
# check for local config file
conf=maczfs-tests.conf
if [ "${1:-}" == "-C" ] ; then
    conf="${2:-}"
    if [ ! -f "${conf}" ] ; then
        echo "Config file '${conf}' not readable."
        exit 1
    fi
elif [ "${1:-}" == "--help" ] ; then
    echo "$0 [ -C conf-file ] "
    exit 1
fi

if [ -f ${conf} ] ; then
    source ${conf}
    # make sure diskstore is set, otherwise bad things will happen
    if [ ! -d "${diskstore}" ] ; then
        echo "diskstore not set. Abort."
        exit 1
    fi
else
    diskstore=$(mktemp -d -t diskstore_)
fi

stop_on_fail=1

if [ ! -z "${tests_tmpdir}" ] ; then
    export TMPDIR=${tests_tmpdir}
else
    tests_tmpdir=$(mktemp -d -t tests_maczfs_)
    export TMPDIR=${tests_tmpdir}
fi

if [ -z "${tests_logdir}" ] ; then
    tests_logdir=$(mktemp -d -t test_logs_maczfs_)
fi

if [ ! -x "${genrand_bin}" ] ; then
    echo "Error: random number generator '' not found."
    echo "Did you compiled it? ('gcc -o genrand -O3 genrand.c' in the support folder.)"
fi

# initialize random datas generator
genrand_state=${tests_logdir}/randstate.txt
${genrand_bin} -s 13446 -S ${genrand_state}

tests_func_init_done=1

# load various helper functions
source ./test-scripts/tests-functions.sh

trap "interact '(err)'" EXIT

# Test sequence:
# - create single-disk pool in default config, using disk-based vdev "vd1"
#   - verify it auto mounts
#   - verify it can be ejected with diskutil
#   - verify it can be re-mounted
#   - verify it can be exported
#   - verify it can be reimported and auto-mounts

cleanup=0
failcnt=0
okcnt=0
subfailcnt=0
subokcnt=0
cursubtest=0
tottests=82
curtest=0

if [ ${has_fstest} -eq 1 ] ; then
    tottests=$(($tottest+1))
fi

run_abort 0 mkdir -pv ${diskstore}/dev

run_ret 0 "Create disk vd1" make_disk 5 vd1 8
attach_disk vd1
#run_ret 0 "Partition disk vd1" partion_disk vd1

run_ret 0 "Create zpool ${pool1} with vdev vd1 at ${vd1_disk}s2" make_pool p1 vd1:2
pool1=${pool_p1_fullname}
pool1path=${pool_p1_path}

run_check_regex 0 "Checking it auto-mounted" "${pool1}" mount

run_ret 0 "Unmount using diskutil" diskutil umount ${pool1path}
run_check_regex 0 "Verifying unmount" '-n' "${pool1}" mount

run_ret 0 "Remounting using zfs utility" zfs mount ${pool1}
run_check_regex 0 "Verifying mount" "${pool1}" mount

sleep 2
echo "Turning of indexing on test pool(s)"
mdutil -i off /Volumes/${pool1}

echo "Waiting 5 secs for Spotlight & Co to move on"
sleep 5


# - run fstest in subdir of pool

if [ ${has_fstest} -eq 1 ] ; then
    echo "Running fstest suite ..."
    ((curtest++))
    mkdir ${pool1path}/fstest
    pushd ${pool1path}/fstest
    run_ret 0 "" run-fstest.sh
    res=$?
    popd
    echo -n "Completed fstest suite"
    print_count_ok_fail ${res}
fi


run_ret 0 "Destroying pool p1" destroy_pool p1
#run_ret 0 "Unloading kern module " sudo kextunload org.maczfs.bjokash.zfs

#
# Done

trap - EXIT

# End
