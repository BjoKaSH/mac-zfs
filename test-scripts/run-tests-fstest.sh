#! /bin/bash


tests_func_init_done=0

# load various helper functions
if [ -z "${test_scripts_dir:-}" ] ; then
    test_scripts_dir=."$(dirname "$0")"
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
extra_args_help_short=" [ --fstest_suite path-to-fstest-script ] "
extra_args_help_long="   --fstest_suite path  path to script that executes fstest in the current directory"
extra_args="--fstest_suite:fstest_suite"

# defaults for above arguments
fstest_suite=run-fstest.sh

# initialize test system and parse arguments
tests_std_setup

# start test sequence

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

echo "Running fstest suite ..."
((curtest++))
mkdir ${pool1path}/fstest
pushd ${pool1path}/fstest
run_ret 0 "" run-fstest.sh
res=$?
popd
echo -n "Completed fstest suite"
print_count_ok_fail ${res}


run_ret 0 "Destroying pool p1" destroy_pool p1
#run_ret 0 "Unloading kern module " sudo kextunload org.maczfs.bjokash.zfs

#
# Done

# End
