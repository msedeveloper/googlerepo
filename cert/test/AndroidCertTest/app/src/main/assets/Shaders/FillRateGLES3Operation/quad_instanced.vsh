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

layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec2 offset;
layout(location = 4) in vec4 scaleRot;

uniform mat4 uProjection;

out vec4 vColor;
out vec2 vTexCoord;

void main() {
    mat2 sr = mat2(scaleRot.xy, scaleRot.zw);
    gl_Position = uProjection * vec4(sr * pos + offset, 0.0, 1.0);
    vColor = color;
    vTexCoord = texCoord;
}
