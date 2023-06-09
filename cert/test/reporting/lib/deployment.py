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
"""Functions for deploying locally and to FTL, and handing the test data on
for subsequent processing

Meant to be called from ./run.py
"""

import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess
import time
from typing import Tuple, Dict, List

from lib.build import APP_ID, build_apk
from lib.common import create_output_dir, run_command, \
    NonZeroSubprocessExitCode, get_attached_devices, get_indexable_utc
from lib.configuration import Configuration
from lib.devicefarm import run_on_farm_and_collect_reports, DeploymentTarget
import lib.graphing
from lib.outer import OuterOperation
from lib.recipe import Recipe
from lib.report import extract_and_export, normalize_report_name, \
    merge_systrace
from lib.systrace import LocalSystrace, SystraceParseError
import lib.tasks_runner

# ------------------------------------------------------------------------------


def unpack(s):
    """Convenience string join function"""
    return " ".join([str(i) for i in s])


def get_systrace_config(recipe: Recipe) -> (bool, List[str], List[str]):
    """Extracts, from the recipe, systrace related arguments.
    These are:

    * enabled: a boolean telling whether systrace is enabled at all.
    * keywords: a list of string keywords to report.
    * categories: a list of string categories to append to the command.
    """
    enabled = recipe.lookup("systrace.enabled")
    keywords = recipe.lookup("systrace.keywords")
    categories = recipe.lookup("systrace.categories")
    categories = [] if not categories else categories.split(" ")
    return enabled, keywords, categories


def load_tasks(tasks_dict: List[Dict], task_ctors: Dict
              ) -> Tuple[List[lib.tasks.Task], lib.tasks.Environment]:
    """Load tasks and create default task environment (internal, called by
    get_preflight_tasks and get_postflight_tasks)
    Args:
        tasks_dict: The dict from a recipe corresponding to pre or postflight
        task_ctors: Dict mapping task action to task constructor
    """
    env = lib.tasks.Environment()

    # set the workspace dir to the parent folder (folder containing run.py)
    env.workspace_dir = Path(__file__).parents[1]

    return lib.tasks_runner.load(tasks_dict, task_ctors), env


def get_preflight_tasks(recipe: Recipe
                       ) -> Tuple[List[lib.tasks.Task], lib.tasks.Environment]:
    """Load preflight tasks from the local deployment block of the recipe,
    and generate preflight environment
    """
    preflight = recipe.lookup("deployment.local.preflight", fallback=[])
    return load_tasks(preflight, lib.tasks_runner.PREFLIGHT_TASKS)


def get_postflight_tasks(recipe: Recipe
                        ) -> Tuple[List[lib.tasks.Task], lib.tasks.Environment]:
    """Load postflight tasks from the local deployment block of the recipe,
    and generate preflight environment
    """
    postflight = recipe.lookup("deployment.local.postflight", fallback=[])
    return load_tasks(postflight, lib.tasks_runner.POSTFLIGHT_TASKS)


def uninstall_apk_from_device(device_id: str) -> type(None):
    """Uninstalls the APK from the specified device
    """
    try:
        run_command(f"adb -s {device_id} uninstall {APP_ID}")
    except NonZeroSubprocessExitCode as ex:
        # uninstall is expected to fail if the APK is not currently installed
        print(
            f"uninstall_apk_from_device[{device_id}] - unable to uninstall"\
                f": {repr(ex)}"
        )


def push_apk_to_device(apk: Path, device_id: str) -> type(None):
    """Gets the app installed in the device.

    Args:
        apk: path to the packaged app.
        device_id: the device serial identifier.
    """
    # install the APK
    run_command(f"adb -s {device_id} install -r {str(apk)}")


def block_for_systrace(device_id: str, out_dir: Path,
                       categories: List[str]) -> LocalSystrace:
    """Starts systrace and blocks until finished.

    Args:
        device_id: local device serial number.
        out_dir: output directory where to place the HTML capture.
        categories: systrace categories to capture.

    Returns:
        A LocalSystrace instance.
    """
    # Start systrace - this blocks until systrace is ready to run
    return LocalSystrace(device_id=device_id,
                         dst_file=out_dir.joinpath("tracey.html"),
                         categories=categories)


def poll_app_active(device_id: str, app_id: str):
    #pylint: disable=line-too-long
    """Check if the given app is running and frontmost on the activity stack

    Args:
        device_id: The USB device to query
        app_id: The id of the app in question

    Returns:
        True if the app is running and at top of activity stack
    """

    cmdline = [
        "adb", "-s", device_id, "shell", "dumpsys", "activity", "activities"
    ]

    # * TaskRecord{e5f38d5d0 #12 I=com.sec.android.app.launcher/com.android.launcher3.Launcher U=0 StackId=0 sz=1}
    # if the app id in that line is ours, we are running and frontmost. If
    # we're not in that line we probably called finish() and are done

    output: str = subprocess.check_output(
        cmdline, stderr=subprocess.PIPE).decode('utf-8')

    for line in output.splitlines():
        line = line.strip()
        if line.find("* TaskRecord") == 0:
            if line.find(app_id) >= 0:
                return True
            return False

    return False


def start_test_and_wait_for_completion(local_device_id: str,
                                       display_wait_message: bool
                                      ) -> type(None):
    """Launches the test on the device. Doesn't return control until execution
    has finished.

    Args:
        local_device_id: local device serial identifier.
        display_wait_message: if true, display a "Waiting..." message to stdout
    """
    # launch the activity in GAME_LOOP mode
    command = (f"adb -s {local_device_id} shell am start"
               f" -n \"{APP_ID}/.MainActivity\""
               f" -a \"com.google.intent.action.TEST_LOOP\"")
    run_command(command)

    # wait for the GAME_LOOP to complete
    tick = 0
    while poll_app_active(local_device_id, APP_ID):
        time.sleep(0.25)
        if display_wait_message:
            count = tick % 4
            ellipsis = "." * count
            ellipsis += " " * (4 - count)
            print(f"Waiting on app to finish tests{ellipsis}", end='\r')
            tick += 1

    if display_wait_message:
        print("\n")

    time.sleep(2)


def process_ftl_reports(
        out_dir: Path,
        report_files: List[Path],
        systrace_files: List[Path],
        systrace_keywords: List[str],
) -> List[Path]:
    #pylint: disable=broad-except
    """Given report files (and optional systrace files) copy the files into the
    designated output dir and normalize file names to convention, merging
    systrace data if available

    Args:
        out_dir: directory where gCloud test results got downloaded.
        report_files: list of report file names (one per device).
        systrace_files: list of associated systrace collections. May be empty
                        if systrace_enabled is False.
        systrace_keywords: systrace keywords to filter.

    Returns:
        A list paths to report files (per device) with input data consolidated
        and transformed.
    """
    out_files = []

    report_pairs = zip(report_files, systrace_files) if systrace_files \
            else [(json_file, None) for json_file in report_files]

    for report_file, systrace_file in report_pairs:
        # copy from the ftl dest folder into tmp
        dst_report_file = out_dir.joinpath(report_file.name)
        shutil.copy(report_file, dst_report_file)

        try:
            # normalize the file name
            report_file = normalize_report_name(dst_report_file)

            if systrace_file:
                # copy the trace.html file to basename_trace.html
                dst_trace_file = out_dir.joinpath(
                    f"{report_file.stem}_trace.html")
                shutil.copy(systrace_file, dst_trace_file)

                # merge the systrace data
                report_file = merge_systrace(report_file, dst_trace_file,
                                             systrace_keywords)

            out_files.append(report_file)

        except SystraceParseError as ex:
            print(f"Unable to parse systrace data, error: {ex}")

        except Exception as ex:
            print(f"Unable to process file {report_file}. Error: {ex}")

    return out_files


# ------------------------------------------------------------------------------


def run_local_deployment(recipe: Recipe, apk: Path, out_dir: Path):
    """Execute a local deployment (to device(s) attached over USB)
    Args:
        recipe: The recipe dict describing the deployment
        apk: Path to the APK to deploy and test
        out_dir: Path to a location to save result JSON reports
    """

    if not recipe.lookup("deployment.local.all_attached_devices", False):
        devices = recipe.lookup("deployment.local.device_ids", None)
        devices = list(set(devices) & set(get_attached_devices()))
    else:
        devices = get_attached_devices()

    systrace_enabled, systrace_keywords, systrace_categories = \
        get_systrace_config(recipe)

    # at present, preflight_tasks are only available for local deployment
    preflight_tasks, preflight_env = get_preflight_tasks(recipe)
    postflight_tasks, postflight_env = get_postflight_tasks(recipe)

    print("Will run local deployment on the following ADB device IDs: " +
          unpack(devices))

    print(f"\tSystrace: {systrace_enabled}")
    if systrace_enabled:
        print(f"\tKeywords: {unpack(systrace_keywords)}\n\n")
    else:
        print('\n')

    configuration = Configuration(
        recipe.lookup('build.configuration')) if not apk else None

    # run install and preflight in serial
    for device_id in devices:
        if apk:
            print(f"[INFO] Installing APK on device {device_id}")
            uninstall_apk_from_device(device_id)
            push_apk_to_device(apk, device_id)

        # run preflight tasks
        if preflight_tasks:
            print(f"[INFO] Running preflight tasks for device {device_id}")
            lib.tasks_runner.run(preflight_tasks, device_id, preflight_env)

    # run the test in parallel if possible
    # TODO(shamyl@google.com) for now, don't run in parallel if systrace enabled
    can_run_in_parallel = len(devices) > 1 and not systrace_enabled
    if can_run_in_parallel:
        print(f"[INFO] Waiting for test to run on {len(devices)} devices...")

        with concurrent.futures.ThreadPoolExecutor() as executor:
            for device_id in devices:
                if apk:
                    executor.submit(run_apk_and_collect_results, device_id,
                                    out_dir, systrace_enabled,
                                    systrace_keywords, systrace_categories,
                                    False)
                else:
                    executor.submit(run_outer_operation, device_id,
                                    configuration, out_dir)

        print(f"[INFO] All devices finished")

    else:
        for device_id in devices:
            if apk:
                run_apk_and_collect_results(device_id, out_dir,
                                            systrace_enabled, systrace_keywords,
                                            systrace_categories, True)
            else:
                run_outer_operation(device_id, configuration, out_dir)

    # run the postflight tasks in serial
    if postflight_tasks:
        for device_id in devices:
            # run postflight tasks
            print(f"[INFO] Running postflight tasks for device {device_id}")
            lib.tasks_runner.run(postflight_tasks, device_id, postflight_env)


def run_apk_and_collect_results(device_id: str, out_dir: Path,
                                systrace_enabled: bool,
                                systrace_keywords: List[str],
                                systrace_categories: List[str],
                                display_wait_message: bool):
    '''execute apk and collect result report'''
    # if enabled, start systrace and wait for it

    systracer = block_for_systrace(
        device_id, out_dir,
        systrace_categories) if systrace_enabled else None

    # run test!
    start_test_and_wait_for_completion(device_id, display_wait_message)

    # extract report
    extract_and_export(
        device_id,
        dst_dir=out_dir,
        systrace_file=systracer.finish() if systracer else None,
        systrace_keywords=systrace_keywords)


def run_outer_operation(device_id: str, configuration: Configuration,
                        out_dir: Path):
    """Instantiates an outer operation and runs it."""
    operation = OuterOperation.load_from_configuration(configuration)
    operation.run(device_id, out_dir)
    normalize_report_name(operation.report)


# ------------------------------------------------------------------------------


def run_ftl_deployment(recipe: Recipe, target_devices: DeploymentTarget,
                       apk: Path, out_dir: Path):
    """Execute a remote deployment (to devices on Firebase test lab (FTL))
    Args:
        recipe: The recipe dict describing the deployment
        target_devices: The suite of devices to target on FTL (may be overridden
        by devices specified in the recipe)
        apk: Path to the APK to deploy and test
        out_dir: Path to a location to save result JSON reports
    """
    if recipe.lookup('build.type') == 'none':
        raise RuntimeError('Outer operations not supported on FTL.')

    # first step is to save out an args.yaml file to pass on to gsutil API
    args_dict = recipe.lookup("deployment.ftl.args", fallback=None)
    for test in args_dict:
        args_dict[test]["app"] = str(apk)

    tests = recipe.lookup("deployment.ftl.args", fallback=None)
    fallback_test = list(tests.keys())[0]
    active_test = recipe.lookup("deployment.ftl.test", fallback=fallback_test)
    flags_dict = recipe.lookup("deployment.ftl.flags", {})

    devices = recipe.lookup("deployment.ftl.devices", fallback=[])
    excluding = recipe.lookup("deployment.ftl.excluding", fallback=[])

    systrace_enabled, systrace_keywords, _ = \
        get_systrace_config(recipe)

    video_enabled = recipe.lookup("deployment.ftl.record_video", fallback=False)

    report_files, systrace_files = run_on_farm_and_collect_reports(
        args_dict, flags_dict, active_test, systrace_enabled, video_enabled,
        devices, excluding, target_devices, out_dir)

    report_files = process_ftl_reports(out_dir, report_files, systrace_files,
                                       systrace_keywords)


# ------------------------------------------------------------------------------


def run_recipe(recipe: Recipe, args: Dict, out_dir: Path = None) -> Path:
    '''Executes the deployment specified in the recipe
    Args:
        recipe_path: path to recipe yaml file
        out_dir: if specified, reports will be saved to this folder; otherwise
            a default will be created via lib.graphing.setup_report_dir()

    Returns: The directory where result data is stored
    '''

    # ensure the out/ dir exists for storing reports/systraces/etc
    # use the name of the provided configuration, or if none, the yaml file
    # to specialize the output dir
    custom_config = recipe.lookup("build.configuration", fallback="")

    if not out_dir:
        if custom_config:
            prefix = Path(custom_config).stem
        else:
            prefix = Path(recipe.get_recipe_path()).stem

        out_dir = create_output_dir(prefix)

    # append a timestamped subfolder
    timestamped_folder = get_indexable_utc()
    out_dir = out_dir / timestamped_folder
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    # build the APK
    apk_path = build_apk(
        clean=recipe.lookup("build.clean", fallback=False),
        build_type=recipe.lookup("build.type", fallback="assembleDebug"),
        custom_configuration=Path(custom_config) if custom_config else None)

    if args.get("local"):
        run_local_deployment(recipe, apk_path, out_dir)
    if args.get("ftl"):
        run_ftl_deployment(recipe, args["ftl-deployment-target"], apk_path,
                           out_dir)

    return out_dir
