#!/bin/bash
glslc -fshader-stage=vertex   vertex.glsl    -o vertex.spv   
glslc -fshader-stage=fragment fragment.glsl  -o fragment.spv 
