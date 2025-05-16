#!/bin/bash

for d in */ ; do
    echo "========== Starting "$(basename ${d})}" test =========="
    cd $d && sh test.sh $@ && cd ../
done
