#!/bin/bash
set -e # Exit on error
export ANDROID_HOME=`pwd`/../prebuilts/sdk
export ANDROID_NDK_HOME=`pwd`/../prebuilts/ndk/r20
./gradlew gamesdkZip

# Build samples
cp -Rf samples/sdk_licenses ../prebuilts/sdk/licenses
pushd samples/bouncyball
./gradlew build
popd
# Commented out because of NDK warning
#pushd samples/cube
#./gradlew build
#popd
pushd samples/tuningfork/tftestapp
./gradlew build
popd

dist_dir=$DIST_DIR
if [[ -z dist_dir ]]
    then
        export dist_dir=`pwd`/../
fi

if [ $1 == "samples" ]
    then
        mkdir $dist_dir/samples
        cp samples/bouncyball/app/build/outputs/apk/debug/app-debug.apk \
            $dist_dir/samples/bouncyball.apk
        cp samples/tuningfork/tftestapp/app/build/outputs/apk/debug/app-debug.apk \
            $dist_dir/samples/tuningfork.apk
fi
