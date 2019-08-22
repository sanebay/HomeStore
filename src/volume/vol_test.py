#!/usr/bin/env python
## @file vol_test.py
#  Regression Tests linked to PR merge

import subprocess
import os
import sys
import getopt
import sys
sys.stdout.flush()
from time import sleep

opts,args = getopt.getopt(sys.argv[1:], 'td:', ['test_suits=', 'dirpath=']) 
test_suits = ""
dirpath = "./"

# slack details
slackcmd = ("./slackpost "
            "https://hooks.slack.com/services/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 "
            "sds-homestore "
            "regression-bot \""
           )

def slackpost(msg):
    cmd = slackcmd + msg + "\""
    subprocess.call(cmd, shell=True)

for opt,arg in opts:
    if opt in ('-t', '--test_suits'):
        test_suits = arg
        print(("testing suits (%s)")%(arg))
    if opt in ('-d', '--dirpath'):
        dirpath = arg
        print(("dir path (%s)")%(arg))


def recovery():
    subprocess.call(dirpath + "test_volume \
    --gtest_filter=IOTest.init_io_test --run_time=30 --enable_crash_handler=0 --remove_file=0", shell=True)
    
    status = subprocess.check_call(dirpath + "test_volume \
    --gtest_filter=IOTest.recovery_io_test --verify_hdr=0 --verify_data=0 --run_time=30 --enable_crash_handler=1", shell=True)
    if status == True:
        print("recovery failed")
        sys.exit(1)

## @test    normal
#  @brief   Normal IO test
def normal():
    print("normal test started")
    status = subprocess.check_call(dirpath + "test_volume \
            --run_time=12000 --max_num_writes=5000000 --gtest_filter=IOTest.init_io_test --remove_file=0", shell=True)
    print("normal test completed")
    if status == True:
        print("normal test failed")
        sys.exit(1)

## @test    load
#  @brief   Test using load generator
def load():
    print("load test started")
    status = subprocess.check_call(dirpath + "test_load \
            --num_io=100000000000 --num_keys=1000000 --run_time=21600 --gtest_filter=Map* ", shell=True)
    print("load test completed")
    if status == True:
        print("load test failed")
        sys.exit(1)

## @test    recovery_nightly
#  @brief   Nightly recovery test
def recovery_nightly():
    print("recovery test started")
    i = 1
    while i < 10:
        status = subprocess.call(dirpath + "test_volume \
        --gtest_filter=IOTest.recovery_io_test --run_time=300 --enable_crash_handler=0 --verify_only=true", shell=True)
        if status == True:
            print("recovery test failed")
            sys.exit(1)
        subprocess.call(dirpath + "test_volume \
        --gtest_filter=IOTest.recovery_io_test --run_time=300 --enable_crash_handler=0 --verify_data=0 --verify_hdr=0 \
        --abort=true", shell=True)
        s = "recovery test iteration" + repr(i) + "passed" 
        print(s)
        i += 1
    
    status = subprocess.check_call(dirpath + "test_volume --gtest_filter=IOTest.recovery_io_test \
            --run_time=300 --remove_file=0", shell=True)
    if status == True:
        print("recovery test failed")
        sys.exit(1)
    print("recovery test completed")

## @test    one_disk_replace
#  @brief   One disk is replaced during boot time
def one_disk_replace():
    print("one disk replace test started");
    status = subprocess.check_call(dirpath + "test_volume --gtest_filter=IOTest.one_disk_replace_test \
            --run_time=300 --remove_file=0 --verify_hdr=0 --verify_data=0", shell=True)
    if status == True:
        print("recovery test with one disk replace failed")
        sys.exit(1)
    print("recovery test with one disk replace passed")

## @test    one_disk_replace_abort
#  @brief   Homestore crashed during recovery with one disk replace
def one_disk_replace_abort():
    print("recovery abort with one disk replace started")
    subprocess.call(dirpath + "test_volume --gtest_filter=IOTest.one_disk_replace_abort_test \
          --run_time=300 --remove_file=0 --verify_hdr=0 --verify_data=0 --enable_crash_handler=0", shell=True)
    status = subprocess.check_call(dirpath + "test_volume --gtest_filter=IOTest.recovery_io_test \
          --run_time=300 --remove_file=0 --verify_hdr=0 --verify_data=0 --expected_vol_state=2", shell=True)
    if status == True:
        print("recovery abort with one disk replace failed")
        sys.exit(1)
    print("recovery abort with one disk replace passed")

## @test    both_disk_replace
#  @brief   Both disks are replaced during boot time
def both_disk_replace():
    print("Both disk replace started")
    status = subprocess.check_call(dirpath + "test_volume \
                    --gtest_filter=IOTest.two_disk_replace_test --run_time=300", shell=True)
    if status == True:
        print("Both disk replace failed")
        sys.exit(1)
    status = subprocess.check_call(dirpath + "test_volume \
            --run_time=300 --max_num_writes=5000000 --gtest_filter=IOTest.init_io_test --remove_file=0", shell=True)
    print("Both disk replace passed")

## @test    one_disk_fail
#  @brief   One Disk failure during boot time
def one_disk_fail():
    print("one disk fail test started")
    status = subprocess.check_call(dirpath + "test_volume \
                    --gtest_filter=IOTest.one_disk_fail_test --run_time=300", shell=True)
    if status == True:
        print("Both disk replace failed")
        sys.exit(1)
    print("one disk fail test passed")

## @test    vol_offline_test
#  @brief   Move volume to offline when IOs are going on
def vol_offline_test():
    print("vol offline test started")
    status = subprocess.check_call(dirpath + "test_volume \
                --gtest_filter=IOTest.vol_offline_test --run_time=300", shell=True)
    if status == True:
        print("vol offline test failed")
        sys.exit(1)
    print("vol offline test passed")
    
    print("vol offline test recovery started")
    status = subprocess.check_call(dirpath + "test_volume \
                --gtest_filter=IOTest.recovery_io_test --run_time=300 --expected_vol_state=1", shell=True)
    if status == True:
        print("vol offline test recovery failed")
        sys.exit(1)
    print("vol offline test recovery passed")

## @test    vol_io_fail_test
#  @brief   Set IO error and verify all volumes come online
#           after reboot and data is verified.
def vol_io_fail_test():
    print("vol io fail test started")
    
    status = subprocess.check_call(dirpath + "test_volume \
                --gtest_filter=IOTest.vol_io_fail_test --run_time=300 --remove_file=0", shell=True)
    if status == True:
        print("vol io fail test failed")
        sys.exit(1)
    print("vol io test test passed")
    
    print("vol io fail test recovery started")
    status = subprocess.check_call(dirpath + "test_volume \
                --gtest_filter=IOTest.recovery_io_test --run_time=300 --verify_data=0", shell=True)
    if status == True:
        print("vol io fail recevery test failed")
        sys.exit(1)
    print("vol io fail test recovery passed")

##  @test   vol_create_del_test
#   @brief  Create and Delete Volume
def vol_create_del_test():
    print("create del vol test started")
    status = subprocess.check_call(dirpath + "test_volume \
                             --gtest_filter=IOTest.normal_vol_create_del_test --max_vols=10000", shell=True)
    if status == True:
         print("create del vol test failed")
         sys.exit(1)
    print("create del vol test passed")

def nightly():

    load()
    sleep(5)

    normal()
    sleep(5)

    recovery_nightly()
    sleep(5)

    one_disk_replace()
    sleep(5)

    one_disk_replace_abort()
    sleep(5)

    both_disk_replace()
    sleep(5)

    one_disk_fail()
    sleep(5)

    vol_offline_test()
    sleep(5)

    vol_io_fail_test()
    sleep(5)

    vol_create_del_test()
    sleep(5)
    print("nightly test passed")

if test_suits == "normal":
    normal()
    
if test_suits == "recovery":
    recovery()
    
if test_suits == "mapping":
    mapping()

if test_suits == "one_disk_replace":
    one_disk_replace()

if test_suits == "one_disk_replace_abort":
    one_disk_replace_abort()

if test_suits == "both_disk_replace":
    both_disk_replace()

if test_suits == "one_disk_fail":
    one_disk_fail()

if test_suits == "vol_offline_test":
    vol_offline_test()

if test_suits == "vol_io_fail_test":
    vol_io_fail_test()

if test_suits == "vol_create_del_test":
    vol_create_del_test()

if test_suits == "nightly":
    nightly()

if test_suits == "recovery_nightly":
    recovery_nightly()

if test_suits == "load":
    load()
