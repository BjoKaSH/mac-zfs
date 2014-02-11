#! /bin/bash


tests_func_init_done=0

# load various helper functions
if [ -z "${test_scripts_dir:-}" ] ; then
    test_scripts_dir="$(dirname "$0")"
fi
test_scripts_lib=${test_scripts_dir}/tests-functions.sh
if [ ! -f "${test_scripts_lib}" ] ; then
    read -p  "test functions 'tests-functions.sh' not found.  Enter path: "  test_scripts_lib
    test_scripts_dir="$(dirname "${test_scripts_lib}")"
fi
if [ ! -f "${test_scripts_dir}/tests-functions.sh" ] ; then
    echo "Need 'tests-functions.sh' but not found at '${test_scripts_lib}'.  Abort."
    exit 1
fi
source ${test_scripts_dir}/tests-functions.sh

# define arguments specific to this set of tests
extra_args_help_short=" [ --fstest_suite path-to-fstest-repository ] "
extra_args_help_long="   --fstest_suite path  path to fstest repository which contains the 'fstest' binary"
extra_args="--fstest_suite:fstest_suite"

# defaults for above arguments
fstest_suite=run-fstest.sh

# initialize test system and parse arguments
stop_on_fail=1 # shell (0 continue, 2 just abort, 3 abort & clean up)
tests_std_setup "$@"

# start test sequence

# verify fstest can be found
if [ ! -x "${fstest_suite}/fstest" ] ; then
    echo "Could not find 'fstest'."
    echo "Specify path to fstest repository using '--fstest_suite'."
    echo "You can get it here: https://github.com/BjoKaSH/fstest.git"
    echo "Stopping now."
    tests_std_teardown -d
    exit 1
fi
if [ "${fstest_suite:0:1}" != "/" ] ; then
    # relative path, make absolute, because we are going to chdir later
    fstest_suite_dir="$(dirname "${fstest_suite}")"
    pushd "${fstest_suite_dir}"
    fstest_suite="$(pwd)"
    popd
fi

run_ret 0 "Create disk vd1" make_disk 5 vd1 8
attach_disk vd1
#run_ret 0 "Partition disk vd1" partion_disk vd1

run_ret 0 "Create zpool ${pool1} with vdev vd1 at ${vd1_disk}s2" make_pool p1 vd1:2
pool1=${pool_p1_fullname}
pool1path=${pool_p1_path}

echo "Will run some basic sanity checks ..."

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

run_ret 0 "Creating work directory 'fstest'"  mkdir -v ${pool1path}/fstest

# - run fstest in subdir of pool

echo "Running fstest suite ..."
((curtest++))
pushd ${pool1path}/fstest

fstest_log=${tests_logdir}/fstest-log-$(date +%Y-%m-%d-%H%M%S)
if ! mkdir ${fstest_log} ; then
    echo "Can't create log directory, giving up."
    tests_std_teardown
    exit 1
fi
echo "fstest requires root permission. will use sudo."
echo "Enter your administration password if prompted by sudo."

# Generate list of available tests
sudo prove --timer -v -D -r ${fstest_suite} >testlist.txt
# Execute tests one-by-one, syncing often to keep as much log data as possible in case of a panic.
while read n ; do
    t="${n#/*/tests/}" ;
    t2="${t/\//_}" ;
    sudo verbose=1 prove --timer -v  $n 2>&1 | tee ${fstest_log}/${t2};
    sync ;
    sleep 1 ;
done <testlist.txt
grep -e 'not ok' -e '^#' -r ${fstest_log} >fail.txt
res=$?
cp testlist.txt  ${fstest_log}/testlist.txt
cp fail.txt  ${fstest_log}/fail.txt

popd
echo -n "Completed fstest suite"
print_count_ok_fail ${res}


run_ret 0 "Destroying pool p1" destroy_pool p1
#run_ret 0 "Unloading kern module " sudo kextunload org.maczfs.bjokash.zfs

tests_std_teardown -k

#
# Done

# End
