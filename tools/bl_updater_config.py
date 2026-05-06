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

from genscratchpad import Scratchpad, InFile, SignatureTLVFile


# Required value for the bootloader updater configuration
BL_UPDATER_CONFIG_TAG = 0x0E1EA6C0  # "BLUPD2"

# Human-readable version of BL_UPDATER_CONFIG_TAG
BL_UPDATER_CONFIG_VERSION = "BLUPD2"

# Bytes from this address up in the bootloader HEX file are silently discarded
#
# NOTE: The Wirepas bootloader HEX file has static data for Lock Bits (LB)
#       at 0xFE04000 on some SiLabs architectures. The Generic Bootloader
#       Updater Tool does not need to touch those bits, so it is safe to just
#       discard them. Additionally, other architectures have the Flash start
#       address at 0x08000000, and any code there must be preserved.
BL_TOP_ADDRESS = 0x0FE00000  # Start of Flash information block


def keys_to_data(key_type, keys):
    """Convert authentication and encryption keys to binary data"""

    keys = list(keys.values())  # Discard key names
    num_keys = len(keys)
    key_data = bytearray()

    # Key data for unset keys, all bytes 0x00
    if key_type == KeyDesc.KEY_TYPE_OMAC1_AES128CTR:
        empty_auth_key = b"\x00" * SIZE_OMAC1_KEY_IN_BYTES
    elif KeyDesc.KEY_TYPE_SHA256_ECDSA_P256_AES128CTR:
        empty_auth_key = b"\x00" * (SIZE_ECDSA_PUBLIC_KEY_IN_BYTES - 1)
    else:
        raise ValueError(f"unsupported key type: {KeyDesc.type_to_string(key_type)}")
    empty_encrypt_key = b"\x00" * SIZE_AES128_KEY_IN_BYTES

    # Add keys to config
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

        key_data += auth_key
        key_data += encrypt_key

    return key_data


def cmd_bl_to_data(args):
    """Command to convert a new bootloader Intel HEX file to a C data array"""

    # Parse configuration file
    config = BootloaderConfig.from_ini_files([args.config_file])

    try:
        bl_area = config.get_bootloader_area()
        bl_area_addr = bl_area.address
        bl_area_size = bl_area.length
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.config_file}") from None

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
    write_c_data_array(
        args.output_path, bootloader_data, "new_bootloader_data", ".romrodata"
    )


def cmd_config_to_data(args):
    """Command to compile a bootloader updater command list and to convert it to a C data array"""

    # Parse configuration file
    config = BootloaderConfig.from_ini_files([args.config_file])

    try:
        bl_area = config.get_bootloader_area()
        stack_area = config.get_wirepas_stack_area()

        # Set authentication and encryption key type, so that memory
        # addresses and sizes for bootloader settings can be calculated
        key_type = config.get_key_type()
        bl_area.set_key_type(key_type)

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
        raise ValueError(f"invalid configuration file: {args.config_file}") from None

    # Parse old configuration file
    old_config = BootloaderConfig.from_ini_files([args.old_config_file])

    try:
        old_bl_area = old_config.get_bootloader_area()

        old_bl_start = old_bl_area.address
        old_bl_max_num_bytes = old_bl_area.length
        old_bl_end = old_bl_start + old_bl_max_num_bytes
    except (AttributeError, IndexError, ValueError):
        raise ValueError(
            f"invalid configuration file: {args.old_config_file}"
        ) from None

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
        len(config.keys),
    )

    # Add keys to config
    bl_updater_config += keys_to_data(key_type, config.keys)

    class CommandType(Enum):
        CMD_END = 0  # No more commands
        CMD_MATCH_APP_AREA_ID = 1  # Permissible app area ID
        CMD_DELETE_AREAS = (
            2  # Delete all existing memory areas (except the bootlader and stack areas)
        )
        CMD_ADD_AREA = 3  # Add a new memory area

    commands = []

    # Match any app area ID if none given
    if len(args.match_app_area_id) > 0:
        app_area_ids = args.match_app_area_id
    else:
        app_area_ids = [0x00000000]  # Area ID 0x00000000 matches any app area ID

    # Add permissible area IDs to command list
    for area_id in app_area_ids:
        commands.append(
            struct.pack(
                "<LLLLL", CommandType.CMD_MATCH_APP_AREA_ID.value, area_id, 0, 0, 0
            )
        )

    if args.reset_areas:
        # Delete all existing memory areas (no parameters)
        commands.append(
            struct.pack("<LLLLL", CommandType.CMD_DELETE_AREAS.value, 0, 0, 0, 0)
        )

        # Add new memory areas to the command list (parameters are bl_memory_area_t)
        for area in config.areas.values():
            commands.append(
                struct.pack(
                    "<LLLLL",
                    CommandType.CMD_ADD_AREA.value,
                    area.address,
                    area.length,
                    area.id,
                    area.flags | (area.type << 2),
                )
            )

    # TODO: Parse command list file and add commands to config

    # Add CMD_END at the end of command list
    commands.append(struct.pack("<LLLLL", CommandType.CMD_END.value, 0, 0, 0, 0))

    # Add command list to config
    bl_updater_config += struct.pack("<L", len(commands))
    for cmd in commands:
        bl_updater_config += cmd

    # Write data to a C data array, incorporate config version in name
    write_c_data_array(
        args.output_path,
        bl_updater_config,
        f"bl_updater_config_{BL_UPDATER_CONFIG_VERSION.lower()}",
        ".romrodata",
    )


def cmd_keys_to_data(args):
    """Command to convert bootloader keys to C data arrays"""

    # Parse configuration file
    config = BootloaderConfig.from_ini_files([args.config_file])

    try:
        new_key = config.keys[args.new_key_name]
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.config_file}") from None

    # Parse old configuration file
    old_config = BootloaderConfig.from_ini_files([args.old_config_file])

    try:
        old_key = old_config.keys[args.old_key_name]
    except (AttributeError, IndexError, ValueError):
        raise ValueError(
            f"invalid configuration file: {args.old_config_file}"
        ) from None

    # Write encryption key to C data array
    write_c_data_array(
        args.output_path,
        old_key.encryption,
        "encrypt_key_for_old_bl",
    )
    write_c_data_array(
        args.output_path,
        new_key.encryption,
        "encrypt_key_for_new_bl",
    )


def cmd_scratchpads_to_data(args):
    """Command to convert two scratchpads to C data arrays"""

    # Parse configuration file
    config = BootloaderConfig.from_ini_files([args.config_file])

    try:
        new_key = config.keys[args.new_key_name]
        new_platform = config.platform
    except (AttributeError, IndexError, ValueError):
        raise ValueError(f"invalid configuration file: {args.config_file}") from None

    # Parse old configuration file
    old_config = BootloaderConfig.from_ini_files([args.old_config_file])

    try:
        old_key = old_config.keys[args.old_key_name]
        old_platform = config.platform
    except (AttributeError, IndexError, ValueError):
        raise ValueError(
            f"invalid configuration file: {args.old_config_file}"
        ) from None

    # Read scratchpad for old bootloader
    with open(args.scratchpad_for_old_bl, "rb") as f:
        scratchpad_for_old_bl = f.read()

    # Read scratchpad for new bootloader
    with open(args.scratchpad_for_new_bl, "rb") as f:
        scratchpad_for_new_bl = f.read()

    # Split both scratchpads to a prefix part and common decrypted file data
    try:
        prefix_for_old_bl, files_for_old_bl = split_scratchpad(
            scratchpad_for_old_bl, old_key, old_platform
        )
    except ValueError:
        raise ValueError(f"invalid scratchpad file {args.scratchpad_for_old_bl}")

    try:
        prefix_for_new_bl, files_for_new_bl = split_scratchpad(
            scratchpad_for_new_bl, new_key, new_platform
        )
    except ValueError:
        raise ValueError(f"invalid scratchpad file {args.scratchpad_for_new_bl}")

    file_headers = bytearray()
    file_data = bytearray()

    # Collect file headers and decrypted file data
    try:
        # Compare number of files
        if len(files_for_old_bl) != len(files_for_new_bl):
            # Different number of files
            raise ValueError()

        for n in range(len(files_for_old_bl)):
            # Compare file headers
            if files_for_old_bl[n].header != files_for_new_bl[n].header:
                # File headers differ
                raise ValueError()

            # Collect file header
            file_headers += files_for_old_bl[n].header

            # Compare decrypted file data
            if files_for_old_bl[n].data != files_for_new_bl[n].data:
                # Decryption error
                raise ValueError()

            # Collect decrypted file data
            file_data += files_for_old_bl[n].data
    except ValueError:
        raise ValueError(
            f"files {args.scratchpad_for_old_bl} and {args.scratchpad_for_new_bl} differ"
        )

    # Write prefix data, including the file header, to a C data array
    write_c_data_array(
        args.output_path,
        prefix_for_old_bl.data,
        "scratchpad_prefix_for_old_bl",
        ".romrodata",
    )
    write_c_data_array(
        args.output_path,
        prefix_for_new_bl.data,
        "scratchpad_prefix_for_new_bl",
        ".romrodata",
    )

    # Write file headers to a C data array
    write_c_data_array(
        args.output_path, file_headers, "scratchpad_file_headers", ".romrodata"
    )

    # Write decrypted file data to a C data array
    write_c_data_array(
        args.output_path, file_data, "scratchpad_file_data", ".romrodata"
    )

    # Write scratchpad prefix counter advance to
    # C header file (dummy data is ignored when linking)
    write_c_data_array(
        args.output_path,
        bytearray(prefix_for_old_bl.ctr_advance_num_bytes),  # Dummy data
        "ctr_advance_for_old_bl",
    )
    write_c_data_array(
        args.output_path,
        bytearray(prefix_for_new_bl.ctr_advance_num_bytes),  # Dummy data
        "ctr_advance_for_new_bl",
    )


def write_c_data_array(output_path, data, name, section_name=None):
    """Write binary data as a C const uint8_t array, with optional section name"""

    attributes = ["aligned(4)"]  # Always align everything to four bytes

    if section_name is not None:
        attributes.append(f'section ("{section_name}")')

    if len(attributes) > 0:
        attributes = f"__attribute__ (({", ".join(attributes)}))"
    else:
        attributes = ""

    name_upper = name.upper()

    # Write data array to file
    with open(os.path.join(output_path, name + ".c"), "w") as f:
        f.write(f'#include "{name}.h"\n')
        f.write(f"const size_t {name}_num_bytes = {name_upper}_NUM_BYTES;\n")
        f.write(f"const uint8_t {name}[{name_upper}_NUM_BYTES] {attributes} = {{")
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


def split_scratchpad(scratchpad_data, key, platform):
    """Split and decrypt a scratchpad"""

    class Prefix:
        def __init__(self, data, num_bytes, ctr_advance_num_bytes):
            self.data = data[:num_bytes]
            self.num_bytes = num_bytes
            self.ctr_advance_num_bytes = ctr_advance_num_bytes

    class File:
        def __init__(self, header, data):
            self.header = header
            self.data = data

    scratchpad_tag_size = len(Scratchpad.SCRATCHPAD_V1_TAG)
    crc_start_pos = scratchpad_tag_size + Scratchpad.SCRATCHPAD_HEADER_SIZE
    file_start_pos = (
        crc_start_pos + Scratchpad.CMAC_SIZE + Scratchpad.SECURE_HEADER_SIZE
    )
    scratchpad_min_size = file_start_pos + Scratchpad.FILE_HEADER_SIZE + 16

    # Verify scratchpad tag
    if len(scratchpad_data) < scratchpad_min_size or not scratchpad_data.startswith(
        Scratchpad.SCRATCHPAD_V1_TAG
    ):
        raise ValueError("invalid scratchpad")

    # Parse scratchpad header
    length, crc, seq, pad, type_, status = struct.unpack(
        "<LHBBLL",
        scratchpad_data[scratchpad_tag_size:crc_start_pos],
    )

    # Verify length
    if len(scratchpad_data) != length + crc_start_pos:
        raise ValueError("scratchpad length mismatch")

    # Read CMAC tag
    cmac_tag = scratchpad_data[crc_start_pos : crc_start_pos + Scratchpad.CMAC_SIZE]

    # Read secure header and Initial Counter Block (ICB)
    secure_header = scratchpad_data[
        crc_start_pos + Scratchpad.CMAC_SIZE : file_start_pos
    ]

    # Create a Scratchpad object using the secure header
    scratchpad = Scratchpad(key, platform, secure_header)

    # Verify CRC
    if crc != scratchpad.crc16_ccitt(scratchpad_data[crc_start_pos:]):
        raise ValueError("scratchpad crc mismatch")

    # Size of scratchpad prefix (i.e., tag, header, Initial Counter Block (ICB))
    prefix_num_bytes = file_start_pos

    # Number of bytes in the scratchpad prefix that advance the counter
    prefix_ctr_advance_num_bytes = 0

    # Read an decrypt files
    pos = file_start_pos
    files = []
    while pos < len(scratchpad_data):
        # Parse file header
        file_header = scratchpad_data[pos : pos + Scratchpad.FILE_HEADER_SIZE]
        file_id, file_length, file_version, file_pad = struct.unpack(
            "<LLLL", file_header
        )

        # Verify file length
        if file_length % InFile.BLOCK_LENGTH != 0:
            raise ValueError("invalid file length")

        file_data = scratchpad_data[
            pos
            + Scratchpad.FILE_HEADER_SIZE : pos
            + Scratchpad.FILE_HEADER_SIZE
            + file_length
        ]

        # Check if it is the signature file
        if file_id == SignatureTLVFile.AREA_ID:
            # Yes, include it as part of the prefix
            prefix_num_bytes = pos + Scratchpad.FILE_HEADER_SIZE + file_length

            # Just update the counter
            scratchpad.cipher.decrypt(bytes(file_data))

            # Update prefix encrypted number of bytes
            prefix_ctr_advance_num_bytes += len(file_data)
        else:
            # Decrypt file data and update counter
            dec_data = scratchpad.cipher.decrypt(bytes(file_data))
            file_data = bytearray(dec_data)

            files.append(File(file_header, file_data))

        # Get next file
        pos += Scratchpad.FILE_HEADER_SIZE + file_length

    # Return the prefix and decrypted files
    return (
        Prefix(scratchpad_data, prefix_num_bytes, prefix_ctr_advance_num_bytes),
        files,
    )


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
        "--config_file",
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
        "--config_file",
        "-c",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file with area definitions",
    )

    parser_config_to_data.add_argument(
        "--old_config_file",
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

    parser_config_to_data.add_argument(
        "--reset_areas",
        "-r",
        action="store_true",
        help="reset areas in the bootloader to those in the configuration file (default: keep areas)",
    )

    parser_keys_to_data = subparsers.add_parser(
        "keys_to_data",
        help="convert scratchpad encryption keys to C data",
    )

    parser_keys_to_data.add_argument(
        "output_path",
        type=arg_is_dir,
        metavar="OUTPUT_PATH",
        help="output path for generated C source files",
    )

    parser_keys_to_data.add_argument(
        "--old_config_file",
        "-d",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file of old bootloader, with keys",
    )

    parser_keys_to_data.add_argument(
        "--config_file",
        "-c",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file of new bootloader, with keys",
    )

    parser_keys_to_data.add_argument(
        "--old_key_name",
        "-l",
        type=str,
        metavar="KEY_NAME",
        default="default",
        help='key name to use for old bootloader (default: "default")',
    )

    parser_keys_to_data.add_argument(
        "--new_key_name",
        "-k",
        type=str,
        metavar="KEY_NAME",
        default="default",
        help='key name to use for new bootloader (default: "default")',
    )

    parser_scratchpads_to_data = subparsers.add_parser(
        "scratchpads_to_data",
        help="convert two scratchpads to C data",
    )

    parser_scratchpads_to_data.add_argument(
        "output_path",
        type=arg_is_dir,
        metavar="OUTPUT_PATH",
        help="output path for generated C source files",
    )

    parser_scratchpads_to_data.add_argument(
        "scratchpad_for_old_bl",
        type=validate_suffix(".otap"),
        metavar="SCRATCHPAD_FOR_OLD_BL",
        help="input scratchpad file for the old bootloader",
    )

    parser_scratchpads_to_data.add_argument(
        "scratchpad_for_new_bl",
        type=validate_suffix(".otap"),
        metavar="SCRATCHPAD_FOR_NEW_BL",
        help="input scratchpad file for the new bootloader",
    )

    parser_scratchpads_to_data.add_argument(
        "--old_config_file",
        "-d",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file of old bootloader, with keys",
    )

    parser_scratchpads_to_data.add_argument(
        "--config_file",
        "-c",
        type=validate_suffix(".ini"),
        metavar="CONFIG_FILE",
        required=True,
        help="configuration file of new bootloader, with keys",
    )

    parser_scratchpads_to_data.add_argument(
        "--old_key_name",
        "-l",
        type=str,
        metavar="KEY_NAME",
        default="default",
        help='key name to use for old bootloader (default: "default")',
    )

    parser_scratchpads_to_data.add_argument(
        "--new_key_name",
        "-k",
        type=str,
        metavar="KEY_NAME",
        default="default",
        help='key name to use for new bootloader (default: "default")',
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
        if args.command == "bl_to_data":
            cmd_bl_to_data(args)
        elif args.command == "config_to_data":
            cmd_config_to_data(args)
        elif args.command == "keys_to_data":
            cmd_keys_to_data(args)
        elif args.command == "scratchpads_to_data":
            cmd_scratchpads_to_data(args)
        else:
            raise ValueError()  # Not possible
    except (ValueError, IOError, OSError) as exc:
        sys.stdout.write("%s: %s\n" % (pgmname, exc))
        return 1


# Run main
if __name__ == "__main__":
    sys.exit(main())
