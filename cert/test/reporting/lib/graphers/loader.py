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
"""Provides factory function load_suite_summarizers which will scan a set of
report files and vend the correct SuiteSummarizer implementation to process
it and generate summaries.
"""

from copy import deepcopy
from typing import List

from lib.graphers.__init__ import SUMMARIZERS
from lib.report import Report, Suite, load_report
from lib.graphers.suite_handler import SuiteSummarizer

# -----------------------------------------------------------------------------

def load_suite_summarizers(report_files: List[str]) -> List[SuiteSummarizer]:
    """
    Loads report files and determines the set of SuiteSummarizers that claim to
    be able to handle the report data. SuiteSummarizers are initialized with the
    report data, which they can then use to marshall their associated
    SuiteHandlers. A SuiteSummarizer can synthesize results across all of its
    SuiteHandlers, while each SuiteHandler considers a single report file (or
    "Suite") in isolation.

    Args:
        report_files: paths to report files.
    Returns:
        a list of initialized SuiteSummarizers.
    """
    # Load all reports
    reports: List[Suite] = []
    for report_file in report_files:
        build_data, data = load_report(report_file)
        reports.append(Report(build_data, data, report_file))

    # Figure out which summarizers to load, initializing them with copies of
    # the report data.
    summarizers: List[SuiteSummarizer] = []
    for summarizer_class in SUMMARIZERS:
        report_copies = []
        for report in reports:
            if summarizer_class.can_handle_report(report):
                report_copies.append(deepcopy(report))
        if report_copies:
            summarizer = summarizer_class(report_copies)
            summarizers.append(summarizer)

    return summarizers
