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
"""Base class for all summary formatters.
"""

from abc import ABC, abstractclassmethod, abstractmethod
from pathlib import Path
from typing import ContextManager, List, TypeVar
import lib.items


class SummaryFormatter(ABC):
    """Abstract class that encapsulates the common events that occur during a
    summary generation.
    Subclasses define, before each event, how to get format.
    """

    @abstractclassmethod
    def is_gdrive_compatible(cls: TypeVar("SummaryFormatter")) -> bool:
        """True if the format is accepted by Google Drive.
        """
        raise NotImplementedError(
            "SummaryFormatter subclass must implement is_gdrive_compatible()")

    @abstractmethod
    def create_writer(self, summary_path: Path) -> ContextManager:
        """Given a summary path, returns a writer instance for the formatter.
        Its type is left to each SummaryFormatter subtype.

        Args:
            summary_path: the report summary Path.

        Returns: a Python ContextManager to take care of the summary writing.
        """
        raise NotImplementedError(
            "SummaryFormatter subclass must implement create_writer()")

    # --------------------------------------------------------------------------

    def on_summary(self, title: str, date: str,
                   items: List[lib.items.Item]) -> type(None):
        """Writes a formatted summary with title, date, and format items

        Args:
            title: the title to appear as an H1.
            date: the date string to be printed under a title
            items: a list of format items.
        """
        self.write_heading(lib.items.Heading(title, 1))
        self.write_text(lib.items.Text(date))
        self.write_items(items)
        if items and not isinstance(items[-1], lib.items.Separator):
            self.write_separator()

    @abstractmethod
    def on_errors_available(self, title: str) -> type(None):
        """Writes a title for a section on failed tests.

        Args:
            title: Failed tests header.
        """
        raise NotImplementedError(
            "SummaryFormatter subclass must implement on_errors_available()")

    @abstractmethod
    def on_device_error(self, device_id: str, summary: str) -> type(None):
        """Writes formatted error results for a given device.

        Args:
            device_id: the device manufacturer, model and API level.
            summary: a string containing some description about the failure.
        """
        raise NotImplementedError(
            "SummaryFormatter subclass must implement on_device_error()")

    def on_finish(self) -> type(None):
        """(Optional) Writes formatted closing marks to the summary."""

    # --------------------------------------------------------------------------

    @abstractmethod
    def write_separator(self):
        """Writes a separator, which visually separates sections of a document,
        such as by a horizontal line or page break. (The exact implementation is
        dependent on the Formatter).
        """
        raise NotImplementedError(
            "SummaryFormatter subclass must implement write_separator()")

    @abstractmethod
    def write_heading(self, heading: lib.items.Heading):
        """Writes a formatted Heading."""
        raise NotImplementedError(
            "SummaryFormatter subclass must implement write_heading()")

    @abstractmethod
    def write_text(self, text: lib.items.Text):
        """Writes a formatted Text item."""
        raise NotImplementedError(
            "SummaryFormatter subclass must implement write_text()")

    @abstractmethod
    def write_image(self, image: lib.items.Image):
        """Writes a formatted Image."""
        raise NotImplementedError(
            "SummaryFormatter subclass must implement write_image()")

    @abstractmethod
    def write_table(self, table: lib.items.Table):
        """Writes a formatted Table."""
        raise NotImplementedError(
            "SummaryFormatter subclass must implement write_table()")

    def write_items(self, items: List[lib.items.Item]):
        """Writes all items in order."""
        for item in items:
            if isinstance(item, lib.items.Separator):
                self.write_separator()
            elif isinstance(item, lib.items.Text):
                self.write_text(item)
            elif isinstance(item, lib.items.Heading):
                self.write_heading(item)
            elif isinstance(item, lib.items.Image):
                self.write_image(item)
            elif isinstance(item, lib.items.Table):
                self.write_table(item)
