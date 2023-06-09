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
"""Markdown summary formatter."""

from pathlib import Path
from typing import List, TextIO, TypeVar

from lib.summary_formatters.formatter import SummaryFormatter
import lib.items as items


class MarkdownFormatter(SummaryFormatter):
    """Markdown summary formatter."""

    def __init__(self):
        self.__writer = None
        self.__summary_path = None

    @classmethod
    def is_gdrive_compatible(cls: TypeVar("SummaryFormatter")) -> bool:
        return False

    def create_writer(self, summary_path: Path) -> TextIO:
        self.__summary_path = summary_path
        self.__writer = open(summary_path, "w")
        return self.__writer

    # --------------------------------------------------------------------------

    def on_errors_available(self, title: str) -> type(None):
        self.write_heading(items.Heading(title, 2))

    def on_device_error(self, device_id: str, summary: str) -> type(None):
        self.write_heading(items.Heading(device_id, 3))
        self.write_text(items.Text(summary))
        self.write_separator()

    # --------------------------------------------------------------------------

    def write_separator(self):
        self.__writer.write(f'---\n\n')

    def write_heading(self, heading: items.Heading):
        level = ''.join(['#' for i in range(heading.level)])
        self.__writer.write(f'{level} {heading.text}\n\n')

    def write_text(self, text: items.Text):
        self.__writer.write(f'{text.text}\n\n')

    def write_image(self, image: items.Image):
        relative_path = image.path.relative_to(self.__summary_path.parent)
        self.__writer.write(
            f'<img src="{relative_path}" width="90%" alt="{image.alt}" />\n\n')

    def write_table(self, table: items.Table):

        def row_to_str(cells: List[str]):
            return '| ' + ' | '.join(cell for cell in cells) + ' |\n'

        text = row_to_str(table.header)
        text += row_to_str(['---' for cell in table.header])
        for row in table.rows:
            text += row_to_str(row)
        self.__writer.write(f'{text}\n')
