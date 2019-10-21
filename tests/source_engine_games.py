"""SourceEngineGame object and supported Source Engine games."""
import glob
import os
from typing import Optional, List


class SourceEngineGame(object):
    def __init__(self, game_tag: str, game_dir_env_var: str, exe_filename: str,
                 game_short_code: str, test_data_dir: str,
                 expected_demo_names: List[str], rel_plugin_path: str):
        resolved_game_dir: str = os.path.abspath(
            os.getenv(game_dir_env_var, ""))
        self.__game_tag: str = game_tag
        self.__game_dir: str = os.path.join(resolved_game_dir, game_short_code)
        self.__exe_path: str = os.path.join(resolved_game_dir, exe_filename)
        self.__game_short_code: str = game_short_code
        self.__test_data_dir: str = test_data_dir
        self.__expected_demo_names: List[str] = expected_demo_names
        self.__plugin_path: str = os.path.join(self.__game_dir,
                                               rel_plugin_path)

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
            '-allowdebug', '-sw', '-w', '1280', '-h', '720', '-conclearlog',
            '-condebug'
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

    @property
    def plugin_path(self) -> str:
        return self.__plugin_path

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


# To test a new game for support, add it to this list then exec run_tests.bat
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
                     ],
                     rel_plugin_path=".\\speedrun_demorecord.dll"),
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
                     ],
                     rel_plugin_path=".\\speedrun_demorecord.dll"),
    #This actually doesn't work because wait prevents stuff from executing :(
    # SourceEngineGame(game_tag='hl2_2006',
    #                  game_dir_env_var='GAME_2006_HL2_DIR',
    #                  exe_filename='hl2.exe',
    #                  game_short_code='hl2',
    #                  test_data_dir=os.path.join(os.path.dirname(__file__),
    #                                             'reproduction', 'hl2_2006'),
    #                  expected_demo_names=[
    #                      "d1_canals_06.dem", "d1_canals_06_1.dem",
    #                      "d1_canals_06_2.dem", "d1_canals_07.dem",
    #                      "d1_canals_06_3.dem", "d1_canals_06_4.dem",
    #                      "d1_canals_08.dem", "d2_coast_01.dem"
    #                  ],
    #                  rel_plugin_path=".\\..\\bin\\speedrun_demorecord.dll"),
    SourceEngineGame(game_tag='portal_2013',
                     game_dir_env_var='GAME_2013_PORTAL_DIR',
                     exe_filename='hl2.exe',
                     game_short_code='portal',
                     test_data_dir=os.path.join(os.path.dirname(__file__),
                                                'reproduction', 'portal_2013'),
                     expected_demo_names=[
                         "testchmb_a_07.dem", "testchmb_a_07_1.dem",
                         "testchmb_a_07_2.dem", "testchmb_a_08.dem",
                         "testchmb_a_08_1.dem", "testchmb_a_09.dem",
                         "escape_01.dem"
                     ],
                     rel_plugin_path=".\\speedrun_demorecord.dll"),
    SourceEngineGame(game_tag='portal_2007',
                     game_dir_env_var='GAME_2007_PORTAL_DIR',
                     exe_filename='hl2.exe',
                     game_short_code='portal',
                     test_data_dir=os.path.join(os.path.dirname(__file__),
                                                'reproduction', 'portal_2007'),
                     expected_demo_names=[
                         "testchmb_a_07.dem", "testchmb_a_07_1.dem",
                         "testchmb_a_07_2.dem", "testchmb_a_08.dem",
                         "testchmb_a_08_1.dem", "testchmb_a_09.dem",
                         "escape_01.dem"
                     ],
                     rel_plugin_path=".\\speedrun_demorecord.dll"),
    SourceEngineGame(game_tag='ep2_2007',
                     game_dir_env_var='GAME_2007_EP2_DIR',
                     exe_filename='hl2.exe',
                     game_short_code='ep2',
                     test_data_dir=os.path.join(os.path.dirname(__file__),
                                                'reproduction', 'ep2_2007'),
                     expected_demo_names=[
                         "ep2_outland_06a.dem", "ep2_outland_06a_1.dem",
                         "ep2_outland_06a_2.dem", "ep2_outland_07.dem",
                         "ep2_outland_07_1.dem", "ep2_outland_08.dem",
                         "ep2_outland_12a.dem"
                     ],
                     rel_plugin_path=".\\speedrun_demorecord.dll"),
]
