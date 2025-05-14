#!/bin/bash

arguments=$1

echo "========== Starting fedora42 test =========="
cd fedora42 && sh test.sh ${arguments} && cd ../

echo "========== Starting fedora42-downgrade-okular test =========="
cd fedora42-downgrade-okular && sh test.sh ${arguments} && cd ../

echo "========== Starting fedora41-downgrade-okular test =========="
cd fedora41-downgrade-okular && sh test.sh ${arguments} && cd ../
