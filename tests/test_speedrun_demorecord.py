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

import pytest
import os
import subprocess
import shutil
import re
import glob

from enum import IntEnum, auto
from typing import Optional, List, Dict, cast, Iterable

# ----- TODO REMOVE THIS WHEN DONE DEBUGGING ---------
os.environ[
    "GAME_2013_HL2_DIR"] = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Half-Life 2"
os.environ["GAME_2007_HL2_DIR"] = "C:\\SourceUnpack\\hl2_vanilla"
# ----- TODO REMOVE THIS WHEN DONE DEBUGGING ---------


class SourceEngineGame(object):
    def __init__(self, game_tag: str, game_dir_env_var: str, exe_filename: str,
                 game_short_code: str, test_data_dir: str,
                 expected_demo_names: List[str]):
        resolved_game_dir: str = os.path.abspath(
            os.getenv(game_dir_env_var, ""))
        self.__game_tag: str = game_tag
        self.__game_dir: str = os.path.join(resolved_game_dir, game_short_code)
        self.__exe_path: str = os.path.join(resolved_game_dir, exe_filename)
        self.__game_short_code: str = game_short_code
        self.__test_data_dir: str = test_data_dir
        self.__expected_demo_names: List[str] = expected_demo_names

        # Parse layout config
        self.__test_data_file_list: List[str] = []
        self.__test_data_playback_cfg: str = ""
        self.__parse_layout_config()

    @property
    def game_tag(self) -> str:
        return self.__game_tag

    @property
    def exe_path(self) -> str:
        return self.__exe_path

    @property
    def game_dir(self) -> str:
        return self.__game_dir

    @property
    def launch_args(self) -> List[str]:
        return [
            self.__exe_path, '-game', self.__game_short_code, '-novid',
            '-allowdebug', '-sw', '-w', '1280', '-h', '720'
        ]

    @property
    def expected_demo_names(self) -> List[str]:
        return self.__expected_demo_names

    @property
    def test_data_dir(self) -> str:
        return self.__test_data_dir

    @property
    def test_data_file_list(self) -> List[str]:
        return self.__test_data_file_list

    @property
    def test_data_playback_cfg_path(self) -> str:
        return os.path.relpath(self.__test_data_playback_cfg, 'cfg/')

    def __parse_layout_config(self) -> None:

        layout_path: str = os.path.join(self.__test_data_dir, 'layout.ini')
        if not os.path.isfile(layout_path):
            return

        file_paths: List[str] = []
        with open(layout_path, 'r', encoding='utf-8') as fd:
            for line in fd:

                # Ignore comments
                normalized_line: str = line.strip()
                if not normalized_line.startswith('#'):
                    k_v_pair: List[str] = normalized_line.split('=')
                    assert len(k_v_pair) == 2

                    # Ensure file actually exists if it isn't a glob path
                    rel_file_path: str = k_v_pair[1]

                    abs_file_path: str = os.path.join(self.__test_data_dir,
                                                      rel_file_path)
                    if not glob.has_magic(
                            abs_file_path) and not os.path.isfile(
                                abs_file_path):
                        raise Exception(
                            f"the file \"{abs_file_path}\" does not exist as defined in \"{layout_path}\"."
                        )

                    # Playback config
                    if k_v_pair[0] == 'playback':
                        assert not self.__test_data_playback_cfg
                        self.__test_data_playback_cfg = rel_file_path

                    file_paths.append(rel_file_path)
        self.__test_data_file_list = file_paths


SUPPORTED_GAMES: List[SourceEngineGame] = [
    SourceEngineGame(game_tag='hl2_2013',
                     game_dir_env_var='GAME_2013_HL2_DIR',
                     exe_filename='hl2.exe',
                     game_short_code='hl2',
                     test_data_dir=os.path.join(os.path.dirname(__file__),
                                                'reproduction', 'hl2_2013'),
                     expected_demo_names=[
                         "d1_canals_06.dem", "d1_canals_06_1.dem",
                         "d1_canals_06_2.dem", "d1_canals_07.dem",
                         "d1_canals_06_3.dem", "d1_canals_06_4.dem",
                         "d1_canals_08.dem", "d2_coast_01.dem"
                     ]),
    SourceEngineGame(game_tag='hl2_2007',
                     game_dir_env_var='GAME_2007_HL2_DIR',
                     exe_filename='hl2.exe',
                     game_short_code='hl2',
                     test_data_dir=os.path.join(os.path.dirname(__file__),
                                                'reproduction', 'hl2_2007'),
                     expected_demo_names=[
                         "d1_canals_06.dem", "d1_canals_06_1.dem",
                         "d1_canals_06_2.dem", "d1_canals_07.dem",
                         "d1_canals_06_3.dem", "d1_canals_06_4.dem",
                         "d1_canals_08.dem", "d2_coast_01.dem"
                     ])
]

RE_EXPECTED_DATETIME_DIR = re.compile(
    r"[0-9]{4}\.[0-9]{2}\.[0-9]{2}-[0-9]{2}\.[0-9]{2}\.[0-9]{2}")
SPEEDRUN_DEMORECORD_DEMO_FOLDER: str = "./integration_tests/"


class DemMsgType(IntEnum):
    Nop = 0
    Signon = auto()
    Packet = auto()
    SyncTick = auto()
    ConsoleCmd = auto()
    UserCmd = auto()
    DataTables = auto()
    Stop = auto()
    StringTables = auto()


DEM_MSG_DATA: List[DemMsgType] = [
    DemMsgType.Signon, DemMsgType.Packet, DemMsgType.ConsoleCmd,
    DemMsgType.UserCmd, DemMsgType.DataTables, DemMsgType.StringTables
]
DEM_MSG_OTHER: List[DemMsgType] = [DemMsgType.SyncTick, DemMsgType.Nop]
DEM_HEADER_SIZE = 0x430


def get_demo_tick_count(demo_path: str) -> Optional[int]:
    with open(demo_path, 'rb') as fd:
        # Grab firt eight bytes for header start confirmation
        demo_header_start_bytes: bytes = fd.read(8)

        # Assert header correct
        assert demo_header_start_bytes == b"HL2DEMO\x00"

        # Move past header
        fd.seek(DEM_HEADER_SIZE)

        tick: Optional[int] = None

        while True:
            dem_msg: int = int.from_bytes(fd.read(1),
                                          byteorder='little',
                                          signed=True)

            if dem_msg == DemMsgType.Stop:
                break

            tmp_tick = int.from_bytes(fd.read(4),
                                      byteorder='little',
                                      signed=True)
            if tmp_tick >= 0:
                # Ignore negative tick values
                tick = tmp_tick

            if dem_msg in DEM_MSG_DATA:
                if dem_msg == DemMsgType.Packet or dem_msg == DemMsgType.Signon:
                    fd.read(0x54)
                elif dem_msg == DemMsgType.UserCmd:
                    fd.read(0x4)

                data_len: int = int.from_bytes(fd.read(4), byteorder='little')
                fd.read(data_len)
            elif dem_msg in DEM_MSG_OTHER:
                continue
            else:
                raise Exception(f"Unknown message {dem_msg}!")

        # Return actual tick count
        return tick


def construct_vdm(demo_path: str, next_demo: Optional[str]):
    # next_demo should be a path relative to the game folder (?)
    demo_endtick: Optional[int] = get_demo_tick_count(demo_path)
    if not demo_endtick:
        raise Exception("Failed to parse end tick from demo \"{demo_path}\"")

    # Remove two ticks just to make sure the PlayCommand action works
    demo_endtick -= 2
    demo_file_path: str = os.path.splitext(demo_path)[0]
    demo_vdm_path: str = f"{demo_file_path}.vdm"

    cmd_at_end: str = "wait 200; "
    if next_demo is None:
        cmd_at_end += "exit"
    else:
        cmd_at_end += f"playdemo {next_demo}"

    with open(demo_vdm_path, 'w', encoding='utf-8') as fd:
        fd.write(f"""demoactions
{{
    "1"
    {{
        factory "PlayCommands"
        name "Execute command"
        starttick "{demo_endtick}"
        commands "{cmd_at_end}"
    }}
    "2"
    {{
        factory "ChangePlaybackRate"
        name "Play demo fast"
        starttick "0"
        playbackrate "10.000000"
    }}
}}""")


@pytest.fixture
def setup_teardown(request) -> Iterable[SourceEngineGame]:
    source_game: SourceEngineGame = request.param

    # Fail test if game not found
    if not os.path.isfile(source_game.exe_path):
        pytest.fail(
            f"game exe not found for \"{source_game.game_tag}\", expected it to be at \"{source_game.exe_path}\"."
        )

    # Fail test if speedrun_demorecord is not deployed
    if not os.path.isfile(
            os.path.join(source_game.game_dir, "speedrun_demorecord.dll")):
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

    # Prep game by copying in dependencies
    source_game: SourceEngineGame = setup_teardown

    # Ensure SPEEDRUN_DEMORECORD_DEMO_FOLDER does not already exist in the
    # game's directory. God forbid someone actually records demos to this dir.
    game_srdf: str = os.path.abspath(
        os.path.join(source_game.game_dir, SPEEDRUN_DEMORECORD_DEMO_FOLDER))
    if os.path.isdir(game_srdf):
        pytest.fail(
            f"the folder \"{game_srdf}\" already exists, please review the contents and remove before running integration tests."
        )

    # Load the plugin
    # Set speedrun_dir to something like ./integration_test
    # Execute commands and speedrun_start accordingly
    proc_args: List[str] = source_game.launch_args
    proc_args.extend([
        "+plugin_load", "speedrun_demorecord", "+speedrun_dir",
        SPEEDRUN_DEMORECORD_DEMO_FOLDER, "+exec",
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
        "+plugin_load", "speedrun_demorecord", "+speedrun_dir",
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
