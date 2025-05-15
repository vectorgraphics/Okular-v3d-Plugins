#!/bin/bash

echo "========== Starting fedora42 test =========="
cd fedora42 && sh test.sh $@ && cd ../

echo "========== Starting fedora42-downgrade-okular test =========="
cd fedora42-downgrade-okular && sh test.sh $@ && cd ../

echo "========== Starting fedora41-downgrade-okular test =========="
cd fedora41-downgrade-okular && sh test.sh $@ && cd ../

echo "========== Starting ubuntu24.10 test =========="
cd ubuntu24.10 && sh test.sh $@ && cd ../

echo "========== Starting fedora40-downgrade-okular test =========="
cd fedora40-downgrade-okular && sh test.sh $@ && cd ../
