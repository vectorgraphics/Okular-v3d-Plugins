#!/bin/bash

for d in */ ; do
    echo "========== Starting "${d}" test =========="
    cd $d && sh test.sh $@ && cd ../
done
