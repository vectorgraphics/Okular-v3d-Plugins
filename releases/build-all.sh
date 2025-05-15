#!/bin/bash

argument=$1

echo "========== Building version 25.04 =========="
cd 25.04 && sh build.sh ${argument} && cd ../

echo "========== Building version 24.12 =========="
cd 24.12 && sh build.sh ${argument} && cd ../

echo "========== Building version 24.08 =========="
cd 24.08 && sh build.sh ${argument} && cd ../

echo "========== Building version 24.05 =========="
cd 24.05 && sh build.sh ${argument} && cd ../

echo "========== Building version 24.02 =========="
cd 24.02 && sh build.sh ${argument} && cd ../
