#
# Copyright 2019 The Android Open Source Project
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
'''Renders reports for DepthClearGLESOperation
'''

from typing import List

import matplotlib.pyplot as plt

from lib.graphers.suite_handler import SuiteHandler, SuiteSummarizer
from lib.report import Datum, SummaryContext
import lib.items as items


class DepthClearSummarizer(SuiteSummarizer):
    """Suite summarizer for Depth Clear OpenGL ES test."""
    @classmethod
    def default_handler(cls) -> SuiteHandler:
        return DepthClearSuiteHandler


class DepthClearSuiteHandler(SuiteHandler):
    '''Renders reports for DepthClearGLESOperation
    Each report says, in short:
        - what kind of context it was run on, whether that was the requested kind
        - for each depth clear value, did fragments pass/reject correctly
    We render a histogram of successes @ depth and errors @ depth for each report
    '''
    def __init__(self, suite):
        super().__init__(suite)

        self.my_data = self.suite.data_by_operation_id[
            "DepthClearGLES3Operation"]
        self.depth_bits = 0

        for datum in self.my_data:
            depth_bits = datum.get_custom_field_numeric("depth_bits")
            has_requested_ctx = datum.get_custom_field("has_requested_context")
            if depth_bits is not None and has_requested_ctx is not None:
                self.depth_bits = depth_bits
                break

    @classmethod
    def can_handle_datum(cls, datum: Datum):
        return "DepthClearGLES3Operation" in datum.operation_id

    def render(self, ctx: SummaryContext) -> List[items.Item]:
        errors_at_depth = []
        successes_at_depth = []

        for datum in self.my_data:
            depth = datum.get_custom_field_numeric("depth_clear_value")
            success = datum.get_custom_field("writes_passed_as_expected")

            if depth is not None and success is not None:
                if success:
                    successes_at_depth.append(depth)
                else:
                    errors_at_depth.append(depth)

        def graph():
            plt.subplot(1, 2, 1)
            plt.ylabel("Errors")
            plt.xlabel("Depth Clear Value")
            if errors_at_depth:
                plt.hist(errors_at_depth, color=(0.6, 0.3, 0.3))
            else:
                plt.xticks([])
                plt.yticks([])

            plt.subplot(1, 2, 2)
            plt.ylabel("Success")
            plt.xlabel("Depth Clear Value")
            if successes_at_depth:
                plt.hist(successes_at_depth, color=(0.3, 0.6, 0.3))
            else:
                plt.xticks([])
                plt.yticks([])
            plt.subplots_adjust(hspace=1)

        message = ""
        if not errors_at_depth:
            message = "No errors detected."
        else:
            message = f"{len(errors_at_depth)} errors out of"\
                f" {len(errors_at_depth) + len(successes_at_depth)} samples"

        if self.depth_bits == 32:
            message += "\nRan on requested 32-bit depth context"
        else:
            message += f"\nRan on {int(self.depth_bits)}-bits; requested 32"

        image_path = self.plot(ctx, graph)
        image = items.Image(image_path, self.device())
        return [image, items.Text(message)]
