"""Utilities for Source Engine demos."""
import os
from enum import IntEnum, auto
from typing import List, Optional


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
