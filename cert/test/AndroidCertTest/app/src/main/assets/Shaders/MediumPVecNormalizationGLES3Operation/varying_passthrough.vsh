#version 300 es

/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

precision mediump float;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inBaseColor;
layout (location = 2) in vec3 inColorOffset;

uniform float uOffsetScale;

out vec3 vSummedColor;

void main()
{
    vec3 v = inBaseColor;
    v += inColorOffset * uOffsetScale;
    vSummedColor = v;
    gl_Position = vec4(inPos.x, inPos.y, inPos.z, 1.0);
}

