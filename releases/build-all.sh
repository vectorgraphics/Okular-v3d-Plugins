#!/bin/bash

echo "========== Building version 25.04 =========="
cd 25.04 && sh build.sh && cd ../

echo "========== Building version 24.12 =========="
cd 24.12 && sh build.sh && cd ../

echo "========== Building version 24.08 =========="
cd 24.08 && sh build.sh && cd ../
