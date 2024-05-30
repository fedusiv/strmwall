#!/bin/bash

COMPILE=0
VERBOSE=0
DEBUG=0

PROJ_DIR=$PWD

make_func()
{
    # verbose mode for make
    if [ ${VERBOSE} -eq 1 ];
    then
        make VERBOSE=1
    else
        make
    fi
}

cmake_func()
{
    if [ ${DEBUG} -eq 1 ];
    then
        echo "Debug build"
        cmake \
        --log-level=VERBOSE \
        -DDEBUG_FLAG=ON\
            ..
    else
        echo "Release build"
        cmake \
        --log-level=VERBOSE \
        -DCMAKE_BUILD_TYPE=Release \
            ..
    fi
}



while getopts cdv flag
do
    case ${flag} in
        c) COMPILE=1 ;;
        d) DEBUG=1 ;;
        v) VERBOSE=1 ;;
    esac
done

if [ ${COMPILE} -eq 1 ];
then
    echo "Running only compilation"
    cd build
    make_func
else
    echo "Rebulding.."
    rm -rf build/
    mkdir build && cd build
    cmake_func
    make_func
fi

cd ${PROJ_DIR}
cp -f build/strmw/app .
cp -f build/udp_test/udp_test udp_test/
