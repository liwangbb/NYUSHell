#!/bin/bash

echo -e "\e[1;33mPreparing test cases...\e[m"
if ! sha1sum -c --quiet checksum.txt; then
  echo -e "\e[1;31mSome test cases are corrupted. Please make sure you have not modified any files provided.\e[m"
  exit 1
fi

NYUENC_GRADING=$PWD/nyuenc-grading
rm -rf $NYUENC_GRADING
mkdir $NYUENC_GRADING
if ! unzip -d $NYUENC_GRADING *.zip 2> /dev/null; then
  echo -e "\e[1;31mThere was an error extracting your source code. Please make sure your zip file is in the current directory.\e[m"
  exit 1
fi

echo -e "\e[1;33mCompiling nyuenc...\e[m"
if [ `hostname | cut -d. -f2` == cims ]; then
  module load gcc-12.2
else
  source scl_source enable gcc-toolset-12
fi
if ! make -C $NYUENC_GRADING; then
  echo -e "\e[1;31mThere was an error compiling nyuenc. Please make sure your source code and Makefile are in the root of your submission.\e[m"
  exit 1
fi
if [ ! -f $NYUENC_GRADING/nyuenc ]; then
  echo -e "\e[1;31mThere was an error compiling nyuenc. Please make sure your Makefile generates an executable file named nyuenc.\e[m"
  exit 1
fi

echo -e "\e[1;33mRunning nyuenc...\e[m"
killall -q -9 nyuenc 2> /dev/null
if [ `hostname | cut -d. -f2` == cims ]; then
  MYOUTPUTS=/tmp/nyuenc-$USER
  rm -rf $MYOUTPUTS myoutputs
  ln -sf $MYOUTPUTS myoutputs
else
  MYOUTPUTS=$PWD/myoutputs
  rm -rf $MYOUTPUTS
fi
mkdir $MYOUTPUTS
score=0

run_test() {
  echo -n "Case $1 (nyuenc ${@:2}): "

  timeout 60 $NYUENC_GRADING/nyuenc ${@:2} > myoutputs/$1.out
  status=$?

  if [ $status -eq 124 ]; then
    echo -e "\e[1;31mFAILED (time limit exceeded)\e[m"
  elif [ $status -ne 0 ]; then
    echo -e "\e[1;31mFAILED (crashed)\e[m"
  elif cmp -s -b refoutputs/$1.out myoutputs/$1.out; then
    echo -e "\e[1;32mPASSED\e[m"
    score=$(($score+1))
  else
    echo -e "\e[1;31mFAILED (wrong output)\e[m"
  fi
}

echo -e "\e[1;33mTesting example input...\e[m"
run_test 1 inputs/1.in
echo -e "\e[1;33mTesting Milestone 1 (single file)...\e[m"
run_test 2 inputs/2.in
run_test 3 inputs/3.in
run_test 4 inputs/4.in
echo -e "\e[1;33mTesting Milestone 1 (multiple files)...\e[m"
run_test 5 inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in
echo -e "\e[1;33mTesting Milestone 2...\e[m"
run_test 6 -j 3 inputs/4.in
case7="-j 3 inputs/5.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in inputs/1.in"
run_test 7 $case7

if [ $score -eq 7 ]; then
  echo "Your program spent $(/bin/time -f "%e" $NYUENC_GRADING/nyuenc $case7 2>&1 > /dev/null) seconds on Case 7."
  echo "Prof. Tang's program spent $(/bin/time -f "%e" `uname -m`/nyuenc $case7 2>&1 > /dev/null) seconds on Case 7."
fi

echo -e "\e[1;33mCleaning up...\e[m"
rm -rf $NYUENC_GRADING
