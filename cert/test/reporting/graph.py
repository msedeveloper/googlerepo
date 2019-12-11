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

import argparse
import os
import glob

from pathlib import Path
from typing import List

import lib.graphing

# DPI to save figure graphics; higher looks better but is slower
FIGURE_DPI = 300


def display_interactive_report(report_file: Path):
    for suite in lib.graphing.load_suites(report_file):
        if suite.handler:
            suite.handler.plot(True)


def summary_document_name(name, html):
    return f"summary_{name}" + (".html" if html else ".md")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dpi",
                        help="Render figures at DPI",
                        default=FIGURE_DPI,
                        type=int)

    parser.add_argument(
        "--interactive",
        action="store_true",
        default=False,
        help=
        "Show interactive pyplot window; only enabled for single --report input"
    )

    parser.add_argument("--html",
                        action="store_true",
                        default=False,
                        help="If true, render document as html, not markdown")

    parser.add_argument(
        "path",
        type=str,
        nargs=1,
        metavar="Path to report file or folder",
        help="Path to document or folder of documents to render")
    args = parser.parse_args()
    html = args.html
    interactive = args.interactive
    dpi = args.dpi
    path = Path(args.path[0])

    if not path.exists():
        print(f"File {str(path)} does not found; bailing.")
        exit(1)

    elif path.is_dir():
        # this is a batch
        report_files = [Path(f) for f in glob.glob(str(path) + '/*.json')]

        report_summary_file_name = summary_document_name(str(path.stem), html)

        report_summary_file = path.joinpath(report_summary_file_name)

        lib.graphing.render_report_document(report_files,
                                            report_summary_file,
                                            dpi=args.dpi)
    elif path.suffix == ".json":
        if interactive:
            display_interactive_report(path)
        else:
            report_summary_file_name = summary_document_name(
                str(path.stem), html)

            report_summary_file = path.parent.joinpath(report_summary_file_name)

            lib.graphing.render_report_document([path],
                                                report_summary_file,
                                                dpi=args.dpi)


if __name__ == "__main__":
    main()