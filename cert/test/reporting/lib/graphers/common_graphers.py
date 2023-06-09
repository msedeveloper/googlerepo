#
# Copyright 2020 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""Common graphic functions."""

from typing import List

from matplotlib.collections import PatchCollection
from matplotlib.patches import Circle
import matplotlib.pyplot as plt


def graph_functional_test_result(result_index: int, results: List[str]):
    """Creates a graph with a circle of a specific color depending on the
    result index. Red if 0, Yellow if 1 and Green if 2.
    The graph completes with a result description, applying the index to the
    results list.

    Note: Make sure the graph doesn't have a title, as it would appear too large
    and cover the rest of the graph.
    """
    fig, axes = plt.subplots()
    fig.set_figheight(1)
    fig.set_figwidth(1)

    xs = [0.16, 0.16, 0.16]
    ys = [0.84, 0.50, 0.16]
    radius = 0.15

    border_color = '#620202'
    colors = ['#ff0000', '#ffff00', '#009c05']

    make_circle = \
        lambda circle_index: \
            Circle((xs[circle_index], ys[circle_index]),
                   radius,
                   ec=border_color,
                   fc=colors[circle_index] if result_index == circle_index \
                       else '#000000cc')

    collection = PatchCollection(
        [make_circle(0), make_circle(1),
         make_circle(2)], True)
    axes.add_collection(collection)
    axes.annotate(results[result_index], (0.35, ys[result_index] - 0.03))

    axes.set_axis_off()
