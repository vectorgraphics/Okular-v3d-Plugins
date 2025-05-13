#!/bin/bash

arguments=$1

echo "========== Starting fedora42 test =========="
cd fedora42 && sh test.sh ${arguments}
