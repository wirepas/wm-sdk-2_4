#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import os
import zlib
import struct
import argparse
import textwrap
import hextool

from enum import Enum

from bootloader_config import (
    BootloaderConfig,
    KeyDesc,
    MAX_NUM_KEYS,
    SIZE_OMAC1_KEY_IN_BYTES,
    SIZE_ECDSA_PUBLIC_KEY_IN_BYTES,
    SIZE_AES128_KEY_IN_BYTES,
)

# Required value for the bootloader updater configuration
BL_UPDATER_CONFIG_TAG = 0x0E1EA6BF  # "BLUPD1"

# Bytes from this address up in the bootloader HEX file are silently discarded
#
# NOTE: The Wirepas bootloader HEX file has static data for Lock Bits (LB)
#       at 0xFE04000 on some SiLabs architectures. The Generic Bootloader
#       Updater Tool does not need to touch those bits, so it is safe to just
#       discard them. Additionally, other architectures have the Flash start
#       address at 0x08000000, and any code there must be preserved.
BL_TOP_ADDRESS = 0x0FE00000  # Start of Flash information block


def cmd_combine(args):
    """Command to combine Intel HEX files of Wirepas Mesh stack and the bootloader updater"""

    # Parse old configuration file
    old_config = BootloaderConfig.from_ini_files([args.oldconfigfile])

    try:
        erase_block_size = old_config.get_flash().eraseblock
        stack_area_addr = old_config.get_wirepas_stack_area().address
        stack_area_size = old_config.get_wirepas_stack_area().length
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.oldconfigfile}") from None

    # Read stack hex file to a Memory() object
    stack_data = hextool.Memory(gap_fill_byte=0xFF)
    hextool.load_intel_hex(stack_data, filename=args.stack_hex)
    if stack_data.min_address != stack_area_addr:
        raise ValueError(
            f"wrong stack start address 0x{stack_data.min_address:08x}, should be 0x{stack_area_addr:08x}"
        )
    if stack_data.max_address > stack_area_addr + stack_area_size:
        raise ValueError(
            f"stack is too large for area by {stack_data.max_address - (stack_area_addr + stack_area_size)} bytes"
        )

    # Read bootloader updater hex file to a Memory() object
    bl_updater_data = hextool.Memory(gap_fill_byte=0xFF)
    hextool.load_intel_hex(bl_updater_data, filename=args.bl_updater_hex)
    bl_updater_data_ranges = list(bl_updater_data.memory_ranges())
    if (
        bl_updater_data.min_address < stack_area_addr
        or bl_updater_data.max_address > stack_area_addr + stack_area_size
    ):
        raise ValueError("bootloader updater not within stack area")
    elif stack_data.max_address > bl_updater_data_ranges[1].start:
        raise ValueError(
            f"bootloader updater overlaps stack by {stack_data.max_address - bl_updater_data_ranges[1].start} bytes"
        )

    # Create a new, empty Memory() object for the output
    # NOTE: Any attempts to add overlapping data will raise ValueError
    output = hextool.Memory(overlap_ok=False, gap_fill_byte=0xFF)

    # Add bootloader updater and stack to the output
    #
    # NOTE: bl_updater_data has an entry point at the start of the stack area.
    #       The bootloader updater code, command list, new bootloader and
    #       backup of the stack code overwritten by the entry point are located
    #       at the end of the stack area.
    try:
        # First, add stack code to output
        output += stack_data

        # Next, delete data from stack that overlaps the entry point
        entrypoint_range = bl_updater_data_ranges[0]
        del output[entrypoint_range.start : entrypoint_range.end]

        # Finally, add bootloader updater to output
        output += bl_updater_data

        if output.max_address > stack_area_addr + stack_area_size:
            raise ValueError
    except (ValueError, StopIteration):
        raise ValueError("no space for bootloader updater in stack area") from None

    # Write output file
    hextool.save_intel_hex(output, filename=args.output_hex)


def cmd_bl_to_data(args):
    """Command to convert a new bootloader Intel HEX file to a C data array"""

    # Parse configuration file
    config = BootloaderConfig.from_ini_files([args.configfile])

    try:
        bl_area = config.get_bootloader_area()
        bl_area_addr = bl_area.address
        bl_area_size = bl_area.length
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.configfile}") from None

    # Read new bootloader hex file and convert to bytearray()
    bootloader_data = hextool.Memory(gap_fill_byte=0xFF)
    hextool.load_intel_hex(bootloader_data, filename=args.bootloader_hex)
    if bl_area_addr is not None and bootloader_data.min_address != bl_area_addr:
        raise ValueError(
            f"wrong bootloader start address 0x{bootloader_data.min_address:08x}, should be 0x{bl_area_addr:08x}"
        )
    del bootloader_data[BL_TOP_ADDRESS:]  # Discard possible hardware config bits
    bootloader_data = bootloader_data[
        bootloader_data.min_address : bootloader_data.max_address
    ]
    if bl_area_size is not None and len(bootloader_data) > bl_area_size:
        raise ValueError(
            f"bootloader is too large for area by {len(bootloader_data) - bl_area_size} bytes"
        )

    # Compress bootloader data
    bootloader_data = zlib.compress(bootloader_data, 9)

    # Remove zlib header and Adler-32 checksum, as the
    # bootloader updater will not use those
    bootloader_data = bootloader_data[2:-4]

    # Write data to a C data array
    write_c_data_array(args.output_path, bootloader_data, "new_bootloader_data")


def cmd_config_to_data(args):
    """Command to compile a bootloader updater command list and to convert it to a C data array"""

    # Parse configuration file
    config = BootloaderConfig.from_ini_files([args.configfile])

    try:
        bl_area = config.get_bootloader_area()
        stack_area = config.get_wirepas_stack_area()

        # Set authentication and encryption key type, so that memory
        # addresses and sizes for bootloader settings can be calculated
        key_type = config.get_key_type()
        bl_area.set_key_type(key_type)

        num_keys = len(config.keys)

        bl_area_id = bl_area.id
        bl_area_start = bl_area.address
        bl_area_num_bytes = bl_area.length
        bl_end = bl_area_start + bl_area_num_bytes

        stack_area_id = stack_area.id
        stack_area_start = stack_area.address
        stack_area_num_bytes = stack_area.length
        stack_area_end = stack_area_start + stack_area_num_bytes

        set_start = bl_area.get_settings_start_address()
        set_end = bl_area.get_settings_end_address()
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.configfile}") from None

    # Parse old configuration file
    old_config = BootloaderConfig.from_ini_files([args.oldconfigfile])

    try:
        old_bl_area = old_config.get_bootloader_area()

        old_bl_start = old_bl_area.address
        old_bl_max_num_bytes = old_bl_area.length
        old_bl_end = old_bl_start + old_bl_max_num_bytes
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.oldconfigfile}") from None

    if bl_area_num_bytes < old_bl_max_num_bytes:
        raise ValueError(
            f"new bootloader cannot be smaller than the old bootloader: {bl_area_num_bytes} < {old_bl_max_num_bytes} bytes"
        )

    bl_updater_config = bytearray()

    # Bootloader updater configuration:
    #
    #   typedef struct
    #   {
    #       uint32_t tag;         /// Sanity check, must be \ref BL_UPDATER_CONFIG_TAG
    #       uint32_t bl_area_id;  /// Bootloader area ID
    #       uint32_t
    #           bl_area_start;  /// Bootloader area start address (may be != 0x00000000)
    #       uint32_t             bl_area_num_bytes;  /// Bootloader area size, in bytes
    #       uint32_t             stack_area_id;      /// Stack area ID
    #       uint32_t             stack_area_start;   /// Stack area start address
    #       uint32_t             stack_area_num_bytes;  /// Stack area size, in bytes
    #       uint32_t             set_start;             /// Start address of settings
    #       uint32_t             set_end;               /// End address of settings
    #       uint32_t             num_keys;              /// Number of configured keys
    #       bl_key_slot_t        keys[BL_UPDATER_NUM_KEY_SLOTS];  /// New keys
    #       uint32_t             num_commands;  /// Number of commands in command list
    #       bl_updater_command_t command_list[];
    #   } bl_updater_config_t;

    # Build a bl_updater_config_t for the bootloader updater
    bl_updater_config += struct.pack(
        "<LLLLLLLLLL",
        BL_UPDATER_CONFIG_TAG,
        bl_area_id,
        bl_area_start,
        bl_area_num_bytes,
        stack_area_id,
        stack_area_start,
        stack_area_num_bytes,
        set_start,
        set_end,
        num_keys,
    )

    # Key data for unset keys, all bytes 0x00
    if key_type == KeyDesc.KEY_TYPE_OMAC1_AES128CTR:
        empty_auth_key = b"\x00" * SIZE_OMAC1_KEY_IN_BYTES
    elif KeyDesc.KEY_TYPE_SHA256_ECDSA_P256_AES128CTR:
        empty_auth_key = b"\x00" * (SIZE_ECDSA_PUBLIC_KEY_IN_BYTES - 1)
    else:
        raise ValueError(f"unsupported key type: {KeyDesc.type_to_string(key_type)}")
    empty_encrypt_key = b"\x00" * SIZE_AES128_KEY_IN_BYTES

    # Add keys to config
    keys = list(config.keys.values())
    for key_num in range(MAX_NUM_KEYS):
        # Keys for CMAC / OMAC1 authentication:
        #
        #   typedef struct
        #   {
        #       /** CMAC / OMAC1 authentication key */
        #       uint8_t auth_key[16];
        #       /** AES-128 CTR encryption key */
        #       uint8_t encrypt_key[16];
        #   } bl_key_slot_t;
        #
        #
        # Keys for ECDSA P-256 authentication:
        #
        #   typedef struct
        #   {
        #       /** ECDSA P-256 authentication public key */
        #       uint8_t auth_key[64];
        #       /** AES-128 CTR encryption key */
        #       uint8_t encrypt_key[16];
        #   } bl_key_slot_t;

        if key_num < num_keys:
            auth_key = keys[key_num].authentication
            encrypt_key = keys[key_num].encryption

            if key_type == KeyDesc.KEY_TYPE_SHA256_ECDSA_P256_AES128CTR:
                # Get ECDSA P-256 public key
                auth_key = KeyDesc.get_public_key(auth_key)

                if len(auth_key) != 65 or auth_key[0] != 0x04:
                    raise ValueError("not a valid uncompressed public key")

                # Discard first byte, which indicates
                # whether the key is compressed or not
                auth_key = auth_key[1:]
        else:
            auth_key = empty_auth_key
            encrypt_key = empty_encrypt_key

        bl_updater_config += auth_key
        bl_updater_config += encrypt_key

    class CommandType(Enum):
        CMD_END = 0  # No more commands
        CMD_MATCH_APP_AREA_ID = 1  # Permissible app area ID

    commands = []

    # Match any app area ID if none given
    if len(args.match_app_area_id) > 0:
        app_area_ids = args.match_app_area_id
    else:
        app_area_ids = [0x00000000]  # Area ID 0x00000000 matches any app area ID

    # Add permissible area IDs to command list
    for area_id in app_area_ids:
        commands.append(
            struct.pack("<LLLL", CommandType.CMD_MATCH_APP_AREA_ID.value, area_id, 0, 0)
        )

    # TODO: Parse command list file and add commands to config

    # Add CMD_END at the end of command list
    commands.append(struct.pack("<LLLL", CommandType.CMD_END.value, 0, 0, 0))

    # Add command list to config
    bl_updater_config += struct.pack("<L", len(commands))
    for cmd in commands:
        bl_updater_config += cmd

    # Write data to a C data array
    write_c_data_array(args.output_path, bl_updater_config, "bl_updater_config")


def cmd_stack_to_data(args):
    """Command to back up overwritten part of stack and to convert it to a C data array"""

    # Parse configuration file
    config = BootloaderConfig.from_ini_files([args.configfile])

    try:
        erase_block_size = config.get_flash().eraseblock
        stack_area_addr = config.get_wirepas_stack_area().address
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.configfile}") from None

    # Parse old configuration file
    old_config = BootloaderConfig.from_ini_files([args.oldconfigfile])

    try:
        old_stack_area_addr = old_config.get_wirepas_stack_area().address
        old_stack_area_size = old_config.get_wirepas_stack_area().length
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.oldconfigfile}") from None

    # Read stack hex file to a Memory() object
    stack_data = hextool.Memory(gap_fill_byte=0xFF)
    hextool.load_intel_hex(stack_data, filename=args.stack_hex)
    if stack_data.min_address != old_stack_area_addr:
        raise ValueError(
            f"wrong stack start address 0x{stack_data.min_address:08x}, should be 0x{old_stack_area_addr:08x}"
        )
    if stack_data.max_address > old_stack_area_addr + old_stack_area_size:
        raise ValueError(
            f"stack is too large for area by {stack_data.max_address - (old_stack_area_addr + old_stack_area_size)} bytes"
        )

    # Read bootloader updater entry point hex file
    entrypoint_data = hextool.Memory(gap_fill_byte=0xFF)
    hextool.load_intel_hex(entrypoint_data, filename=args.entrypoint_hex)
    if (
        entrypoint_data.min_address < stack_data.min_address
        or entrypoint_data.max_address > stack_data.max_address
    ):
        raise ValueError("entry point not within stack area")
    if entrypoint_data.max_address - entrypoint_data.min_address > erase_block_size:
        raise ValueError(
            f"entry point is too large for area by {entrypoint_data.max_address - entrypoint_data.min_address - erase_block_size} bytes"
        )

    # Get range of addresses that are overwritten by the bootloader updater entry point
    stack_backup = stack_data[entrypoint_data.min_address : entrypoint_data.max_address]

    # Write data to a C data array
    write_c_data_array(args.output_path, stack_backup, "stack_backup")


def write_c_data_array(output_path, data, name, section_name=None):
    """Write binary data as a C const uint8_t array, with optional section name"""

    if section_name is not None:
        section_attr = f'__attribute__ ((section ("{section_name}"))) '
    else:
        section_attr = ""

    name_upper = name.upper()

    # Write data array to file
    with open(os.path.join(output_path, name + ".c"), "w") as f:
        f.write(f'#include "{name}.h"\n')
        f.write(f"const size_t {name}_num_bytes = {name_upper}_NUM_BYTES;\n")
        f.write(f"const uint8_t {name}[{name_upper}_NUM_BYTES] {section_attr}= {{")
        comma = ""
        for offset in range(0, len(data), 8):
            f.write(f"{comma}\n    ")
            row = data[offset : offset + 8]
            f.write(", ".join([f"0x{b:02x}" for b in row]))
            comma = ","
        f.write("\n};\n")

    # Write header file
    with open(os.path.join(output_path, name + ".h"), "w") as f:
        f.write(f"#ifndef {name_upper}_H\n")
        f.write(f"#define {name_upper}_H\n")
        f.write("#include <stdlib.h>\n")
        f.write("#include <stdint.h>\n")
        f.write(f"#define {name_upper}_NUM_BYTES {len(data)}\n")
        f.write(f"extern const size_t {name}_num_bytes;\n")
        f.write(f"extern const uint8_t {name}[{name_upper}_NUM_BYTES];\n")
        f.write("#endif\n")


def round_up_to_block_size(length, block_size):
    """Round a value to the next multiple of block size"""
    return (length + block_size - 1) // block_size * block_size


def arg_bytes_amount(string_amount):
    """Parse a byte amount string, possibly including a suffix"""

    suffixes = [
        ("k", 1024),
        ("kb", 1024),
        ("kib", 1024),
        ("m", 1024 * 1024),
        ("mb", 1024 * 1024),
        ("mib", 1024 * 1024),
    ]

    value = string_amount.lower()

    # Match suffix
    for s in suffixes:
        if value.endswith(s[0]):
            value = value[: -len(s[0])]
            break
    else:
        # No suffix found
        s = ("", 1)

    try:
        # Convert to int and apply suffix weight
        return int(value, 0) * s[1]
    except ValueError:
        # Invalid input string
        raise argparse.ArgumentTypeError("invalid value %s" % string_amount) from None


def arg_int_base_0(string_value):
    """Parser for integers in decimal or hexadecimal"""

    try:
        return int(string_value, 0)
    except ValueError:
        # Invalid input string
        raise argparse.ArgumentTypeError("invalid value %s" % string_value) from None


def arg_int_base_0_list(string_value):
    """Parser for a comma or whitespace separated list of integers in decimal or hexadecimal"""

    string_value = string_value.strip()

    values = []
    for s in string_value.replace(",", " ").split():
        try:
            values.append(arg_int_base_0(s))
        except ValueError:
            # Invalid input string
            raise argparse.ArgumentTypeError(
                "invalid value %s" % string_value
            ) from None

    return values


def arg_is_dir(string_value):
    """Make sure a given path points to a directory"""

    if not os.path.isdir(string_value):
        # Not a directory path
        raise argparse.ArgumentTypeError("%s is not a directory" % string_value)

    return string_value


def validate_suffix(suffix):
    """Create a function to validate a filename suffix"""

    def suffix_checker(string_file):
        f"""Validate a filename, which must end in '{suffix}'"""
        if not string_file.endswith(suffix):
            raise argparse.ArgumentTypeError(
                f"invalid suffix in '{string_file}', must be '{suffix}'"
            )
        return string_file

    return suffix_checker


def create_argument_parser(pgmname):
    """Create a parser for parsing the command line"""

    # Determine help text width
    try:
        help_width = int(os.environ["COLUMNS"])
    except (KeyError, ValueError):
        help_width = 80
    help_width -= 2

    parser = argparse.ArgumentParser(
        prog=pgmname,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=textwrap.fill(
            "A tool to embed a bootloader updater in a Wirepas Mesh stack firmware",
            help_width,
        ),
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    parser_combine = subparsers.add_parser(
        "combine",
        help="combine Intel HEX files of Wirepas Mesh stack and the bootloader updater",
    )

    parser_combine.add_argument(
        "output_hex",
        type=validate_suffix(".hex"),
        metavar="OUTPUT_HEX_FILE",
        help="output Intel HEX file",
    )

    parser_combine.add_argument(
        "stack_hex",
        type=validate_suffix(".hex"),
        metavar="HEX_FILE",
        help="input Intel HEX file for the Wirepas Mesh stack",
    )

    parser_combine.add_argument(
        "bl_updater_hex",
        type=validate_suffix(".hex"),
        metavar="HEX_FILE",
        help="input Intel HEX file for the bootloader updater",
    )

    parser_combine.add_argument(
        "--oldconfigfile",
        "-d",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file of old bootloader, with area definitions",
    )

    parser_bl_to_data = subparsers.add_parser(
        "bl_to_data", help="compress a new bootloader and convert it to C data"
    )

    parser_bl_to_data.add_argument(
        "output_path",
        type=arg_is_dir,
        metavar="OUTPUT_PATH",
        help="output path for generated C source files",
    )

    parser_bl_to_data.add_argument(
        "bootloader_hex",
        type=validate_suffix(".hex"),
        metavar="HEX_FILE",
        help="input Intel HEX file for the new bootloader",
    )

    parser_bl_to_data.add_argument(
        "--configfile",
        "-c",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file with area definitions",
    )

    parser_config_to_data = subparsers.add_parser(
        "config_to_data",
        help="build bootloader updater configuration and convert it to C data",
    )

    parser_config_to_data.add_argument(
        "output_path",
        type=arg_is_dir,
        metavar="OUTPUT_PATH",
        help="output path for generated C source files",
    )

    parser_config_to_data.add_argument(
        "cmd_list",
        type=validate_suffix(".conf"),
        metavar="CMD_LIST_FILE",
        help="command list file for bootloader updater (if not found, defaults used)",
    )

    parser_config_to_data.add_argument(
        "--configfile",
        "-c",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file with area definitions",
    )

    parser_config_to_data.add_argument(
        "--oldconfigfile",
        "-d",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file of old bootloader, with area definitions",
    )

    parser_config_to_data.add_argument(
        "--match_app_area_id",
        "-a",
        type=arg_int_base_0_list,
        metavar="APP_AREA_IDS",
        default="",
        help="comma or whitespace separated list of permissible application area IDs, or blank to match any area ID",
    )

    parser_stack_to_data = subparsers.add_parser(
        "stack_to_data",
        help="back up part of stack and convert it to C data",
    )

    parser_stack_to_data.add_argument(
        "output_path",
        type=arg_is_dir,
        metavar="OUTPUT_PATH",
        help="output path for generated C source files",
    )

    parser_stack_to_data.add_argument(
        "stack_hex",
        type=validate_suffix(".hex"),
        metavar="STACK_HEX_FILE",
        help="input Intel HEX file for the Wirepas Mesh stack",
    )

    parser_stack_to_data.add_argument(
        "entrypoint_hex",
        type=validate_suffix(".hex"),
        metavar="EP_HEX_FILE",
        help="input Intel HEX file for the bootloader updater entry point",
    )

    parser_stack_to_data.add_argument(
        "--configfile",
        "-c",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file with area definitions",
    )

    parser_stack_to_data.add_argument(
        "--oldconfigfile",
        "-d",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file of old bootloader, with area definitions",
    )

    return parser


def main():
    """Main program"""

    # Determine program name, for error messages
    pgmname = os.path.split(sys.argv[0])[-1]

    try:
        # Parse command line arguments
        args = create_argument_parser(pgmname).parse_args()

        # Perform command
        if args.command == "combine":
            cmd_combine(args)
        elif args.command == "bl_to_data":
            cmd_bl_to_data(args)
        elif args.command == "config_to_data":
            cmd_config_to_data(args)
        elif args.command == "stack_to_data":
            cmd_stack_to_data(args)
        else:
            raise ValueError()  # Not possible
    except (ValueError, IOError, OSError) as exc:
        sys.stdout.write("%s: %s\n" % (pgmname, exc))
        return 1


# Run main
if __name__ == "__main__":
    sys.exit(main())
