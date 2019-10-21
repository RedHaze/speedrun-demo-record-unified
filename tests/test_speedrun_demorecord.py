"""Integration tests for speedrun_demorecord.

Basically just want to make sure that demos dont crash on playback.

Tests:
* Ensure demos don't crash on playback

Future Tests:
* Ensure teleportation doesn't occur when spamming save commands during load
  when speedrun is active
* See if demos have timing similar to engine auto-record through map
  transition.
* speedrun_democrecord_bookmarks.txt verification
* Long paths testing & handling
* Unicode paths & handling
"""

import glob
import os
import re
import shutil
import subprocess
import warnings
from typing import Dict, Iterable, List, Optional, cast

import pytest

from demo_utils import construct_vdm, get_demo_tick_count
from source_engine_games import SUPPORTED_GAMES, SourceEngineGame

RE_EXPECTED_DATETIME_DIR = re.compile(
    r"[0-9]{4}\.[0-9]{2}\.[0-9]{2}-[0-9]{2}\.[0-9]{2}\.[0-9]{2}")
RE_LOADED_PLUGINS = re.compile(
    r"Loaded plugins:\n-+\n((?:(?:[0-9]+:[ \t]+\"[^\"]+\")\n?)*)-+",
    re.MULTILINE)
RE_QUOTE_GROUP = re.compile(r"[0-9]:+[\t\s]+\"([^\"]+)\"")
SPEEDRUN_DEMORECORD_DEMO_FOLDER: str = "./integration_tests/"
SPEEDRUN_DEMORECORD_DESCRIPTION: str = "Speedrun Demo Record, Maxx"
SPEEDRUN_DEMORECORD_PLUGIN_NAME: str = "speedrun_demorecord"
SPEEDRUN_DEMORECORD_VERSION: str = "Version:0.0.6.1"


def get_console_output(source_game: SourceEngineGame):
    # Open the console log
    con_log_path: str = os.path.join(source_game.game_dir, 'console.log')
    with open(con_log_path, 'r', encoding='utf-8') as fd:
        con_log_contents: str = fd.read()
    return con_log_contents


@pytest.fixture
def setup_teardown(request) -> Iterable[SourceEngineGame]:
    source_game: SourceEngineGame = request.param

    # Fail test if game not found
    if not os.path.isfile(source_game.exe_path):
        pytest.fail(
            f"game exe not found for \"{source_game.game_tag}\", expected it to be at \"{source_game.exe_path}\"."
        )

    # Fail test if speedrun_demorecord is not deployed
    if not os.path.isfile(source_game.plugin_path):
        pytest.fail(
            f"speedrun_demorecord not deployed to \"{source_game.game_dir}\", be sure to build via solution first!"
        )

    # Attempt to setup according to layout.ini
    files_list: List[str] = source_game.test_data_file_list
    if not files_list:
        pytest.fail(
            f"failed to parse test data layout from \"{source_game.test_data_dir}\"."
        )

    # Need playback cfg
    if not source_game.test_data_playback_cfg_path:
        pytest.fail(
            f"no playback cfg found in test data layout from \"{source_game.test_data_dir}\"."
        )

    # Ensure speedrun_demorecord won't auto-load first
    # Warn user if more than one plugin is loaded
    proc_args: List[str] = source_game.launch_args
    proc_args.extend(
        ["-textmode", "+exec", "autoexec", "+plugin_print", "+exit"])
    subprocess.run(proc_args, check=True)

    con_log_contents: str = get_console_output(source_game)

    plugin_print_result: List[str] = RE_LOADED_PLUGINS.findall(
        con_log_contents)

    # There should only be one plugin list associated with one call to plugin_print
    assert len(plugin_print_result) == 1

    loaded_plugins: List[str] = list(
        filter(None, plugin_print_result[0].strip().split('\n')))
    loaded_plugins_parsed: List[str] = [
        RE_QUOTE_GROUP.fullmatch(x).group(1) for x in loaded_plugins
    ]

    if len(loaded_plugins_parsed) != 0:
        if SPEEDRUN_DEMORECORD_DESCRIPTION in loaded_plugins_parsed:
            pytest.fail(
                f"speedrun_demorecord is being automatically loaded for \"{source_game.game_tag}\", please remove the VDF file so the older version does not load."
            )
        else:
            warnings.warn(
                UserWarning(
                    f"other plugins are being automatically loaded which may cause integration tests to fail: {loaded_plugins}"
                ))

    # Copy files
    for file_path in cast(List[str], files_list):

        # Test file path
        glob_path: str = os.path.join(source_game.test_data_dir, file_path)

        # Target directory
        target_dir: str = os.path.join(source_game.game_dir,
                                       os.path.dirname(file_path))

        # Glob it
        for target_file in glob.iglob(glob_path):
            os.makedirs(target_dir, exist_ok=True)
            shutil.copy2(target_file, target_dir)

    yield source_game

    # Attempt test file cleanup
    for file_path in cast(List[str], files_list):

        # Target glob cleanup
        glob_path = os.path.join(source_game.game_dir, file_path)

        # Glob it
        for target_file in glob.iglob(glob_path):
            os.remove(target_file)

            # See if directory is empty for removal
            if not os.listdir(os.path.dirname(target_file)):
                os.rmdir(os.path.dirname(target_file))


@pytest.mark.parametrize("setup_teardown", SUPPORTED_GAMES, indirect=True)
def test_demo_playback_doesnt_crash(setup_teardown) -> None:

    # Prep game by copying in test data
    source_game: SourceEngineGame = setup_teardown

    # Ensure SPEEDRUN_DEMORECORD_DEMO_FOLDER does not already exist in the
    # game's directory. God forbid someone actually records demos to this dir.
    game_srdf: str = os.path.abspath(
        os.path.join(source_game.game_dir, SPEEDRUN_DEMORECORD_DEMO_FOLDER))
    # Attempt demo cleanup after test is done
    # Don't clean it up on failure so we can repro the demo crash
    if os.path.isdir(game_srdf):
        shutil.rmtree(game_srdf)

    # if os.path.isdir(game_srdf):
    #     pytest.fail(
    #         f"the folder \"{game_srdf}\" already exists, please review the contents and remove before running integration tests."
    #     )

    # Load the plugin
    # Set speedrun_dir to something like ./integration_test
    # Execute commands and speedrun_start accordingly
    proc_args: List[str] = source_game.launch_args
    proc_args.extend([
        "-nomouse", "+plugin_load", SPEEDRUN_DEMORECORD_PLUGIN_NAME,
        "+speedrun_dir", SPEEDRUN_DEMORECORD_DEMO_FOLDER, "+exec",
        source_game.test_data_playback_cfg_path
    ])

    # Launch with speedrun_start and also begin playback
    # Playback should have:
    # - some movement in the current map
    # - A single tick save/load
    # - load a save next to a level transition and walk into it
    # - some movement in the next map
    # Block until game EXE closes
    subprocess.run(proc_args,
                   stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT,
                   check=True)

    con_log_contents: List[str] = get_console_output(source_game).split()

    # Ensure the expected version exists
    assert SPEEDRUN_DEMORECORD_VERSION in con_log_contents

    # Ensure the speedrun_demorecord folder was created
    assert os.path.isdir(game_srdf) is True

    game_srdf_contents: List[str] = os.listdir(game_srdf)
    game_srdf_folder: str = ""
    for item in game_srdf_contents:
        if os.path.isdir(os.path.join(game_srdf, item)):
            # Ensure there is only one folder in the SRDF
            assert not game_srdf_folder
            game_srdf_folder = item

    # Ensure the folder has the correct date/time format
    assert RE_EXPECTED_DATETIME_DIR.fullmatch(game_srdf_folder) is not None

    # Ensure all the files in this folder are .dem files and also have the
    # expected names. Ensure expected number of demos as well.
    game_srdf_folder_abspath: str = os.path.join(game_srdf, game_srdf_folder)
    sorted_file_list: List[str] = sorted([
        os.path.join(game_srdf_folder_abspath, x)
        for x in os.listdir(game_srdf_folder_abspath)
    ],
                                         key=lambda x: os.path.getctime(x))

    # Assert that the number of expected demo files matches. Required so we
    # match file names in next test.
    assert len(sorted_file_list) == len(source_game.expected_demo_names)

    # Create VDM files for sequential playback
    for idx, demo_abspath in enumerate(sorted_file_list):
        # Ensure expected demo name matches
        assert os.path.basename(
            demo_abspath) == source_game.expected_demo_names[idx]

        # Next demo in sequence
        next_demo_rel_path: Optional[str] = None
        if idx + 1 < len(sorted_file_list):
            # Rel path to next demo from game_dir root
            next_demo_rel_path = os.path.relpath(sorted_file_list[idx + 1],
                                                 source_game.game_dir)

        # This is also kinda validating the header of the demo file
        # Construct VDM files to play demos in sequence
        construct_vdm(demo_abspath, next_demo_rel_path)

    # TODO: Don't load speedrun_demorecord plugin for playback to detect if any speedrun_demorecord
    # commands were executed during playback. Parameterize that? Want to make sure with plugin loaded
    # can still playback demos...
    # Construct new arguments.
    # Launch game again and play all recorded demos at high speed
    # Demos will playback in the order they were discovered above.
    # Play just the first demo in the sequence since others will be chained
    # using VDM files.
    first_dem_rel_path: str = os.path.relpath(sorted_file_list[0],
                                              source_game.game_dir)
    proc_args = source_game.launch_args
    proc_args.extend([
        "+plugin_load", SPEEDRUN_DEMORECORD_PLUGIN_NAME, "+speedrun_dir",
        SPEEDRUN_DEMORECORD_DEMO_FOLDER, "+playdemo", first_dem_rel_path
    ])

    # Run the game, if a non-zero exit code is returned, this test should fail
    subprocess.run(proc_args,
                   stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT,
                   check=True)

    # Attempt demo cleanup after test is done
    # Don't clean it up on failure so we can repro the demo crash
    if os.path.isdir(game_srdf):
        shutil.rmtree(game_srdf)
