#!/usr/bin/env python3
# -*- coding: utf-8 -*-

##
# firmware_selector.py - A tool to select the correct Wirepas firmware
#

import sys
import os
import struct
import argparse
import textwrap
import shutil

from os import listdir
from os.path import isfile, join, exists
from shutil import copyfile

import hextool
import zipfile
import hashlib

# Python 2 and Python 3 support
try:
    # Python 3
    import configparser

    config_parser_tweaks = {
        "comment_prefixes": ("#", ";"),
        "inline_comment_prefixes": (";",),
    }
except ImportError:
    # Python 2
    import ConfigParser as configparser

    config_parser_tweaks = {}


class FirmwareConfig(object):
    """Firmware configuration

    An object to store a firmware configuration

    conf_path       String containing the path to firmware config
    general         Dictionary containing the general config
    mem_layout      Dictionary containing the memory layout
    libraries       Dictionary containing the list of libraries

    """

    def __init__(self, general, mem_layout=None, libraries=None, path=None):
        self.general = general
        self.mem_layout = mem_layout
        self.libraries = libraries
        self.conf_path = path

    @classmethod
    def from_file(cls, file):
        cp = configparser.RawConfigParser(
            **config_parser_tweaks
        )  # Most basic parser only
        cp.read(file)

        try:
            general_conf = dict(cp.items("general"))
        except Exception:
            return None

        try:
            mem_layout = dict(cp.items("memory"))
        except Exception:
            mem_layout = None

        try:
            libs = dict(cp.items("libraries"))
        except Exception:
            libs = None

        return cls(
            general=general_conf, mem_layout=mem_layout, libraries=libs, path=file
        )

    def __str__(self):
        return "Firmware config is: {}".format(self.__dict__)

    def is_version_greater_or_equal(self, min_version):
        # Both versions should have same format
        # List of digit separated by points
        min_ver_digits = min_version.split(".")
        cur_ver_digits = self.general["version"].split(".")

        if min_ver_digits.__len__() != cur_ver_digits.__len__():
            return False

        i = 0
        while i < min_ver_digits.__len__():
            if int(min_ver_digits[i]) > int(cur_ver_digits[i]):
                print(
                    "Version for this binary is too old: {} < {}".format(
                        self.general["version"], min_version
                    )
                )
                return False
            i += 1

        return True

    def is_compatible(self, target_config):
        # Function to check if config is compatible with provided config
        for key in target_config:
            # In case keys are string, just compare them in case insensitive
            try:
                if key == "version":
                    # Version is the minimal version to test
                    if not self.is_version_greater_or_equal(target_config[key]):
                        return False
                elif key == "mcu_sub":
                    possible_values = [target_config["mcu_sub"].lower()]
                    if "mcu_mem_var" in target_config:
                        possible_values.append(
                            target_config["mcu_sub"].lower()
                            + target_config["mcu_mem_var"].lower()
                        )

                    if self.general["mcu_sub"].lower() not in possible_values:
                        return False
                elif key == "mcu_mem_var":
                    # Handle in previous case
                    continue
                elif key == "modem_fw":
                    continue
                else:
                    # Other keys must match
                    if self.general[key].lower() != target_config[key].lower():
                        return False
            except KeyError:
                if key == "mcu_sub":
                    # if sub_mcu is not defined it is that the mcu is same for all
                    continue
                return False

        # This Firmware has all the requirements
        return True

    def get_unspecified_keys(self, target_config):
        # Function to get all the keys that are not in specified target_config
        unspecified_keys = {}
        for key in self.general:
            if not key in target_config:
                unspecified_keys[key] = self.general[key]

        return unspecified_keys

    def has_lib(self, lib_name, lib_version=None):
        try:
            if self.libraries is None:
                return False
            version = self.libraries[lib_name]
            if lib_version is not None and int(version) >= int(lib_version):
                return False
        except KeyError:
            # Lib is not present
            return False

        return True


def create_stack_binary(filename, stack_area_addr):
    """Create a stack binary file and add padding if necessary"""

    memory = hextool.Memory(gap_fill_byte=0xFF)
    hextool.load_intel_hex(memory, filename=filename)

    # Get stack start address
    stack_min_addr = memory.min_address

    # Determine the need for padding
    padding_needed = stack_min_addr - stack_area_addr

    if padding_needed == 0:
        # Stack already at the correct address, do nothing
        pass
    elif padding_needed < 0:
        # Stack binary starts at an address that
        # is below the stack area start address
        raise ValueError("{}: start address is too low".format(filename))
    elif padding_needed > 0:
        # Stack binary starts at an address that is larger than
        # the stack area start address, padding required

        # Jump trampoline
        trampoline = bytearray(
            [
                # fmt: off
                0x03, 0xB4,     # push  {r0, r1}
                0x01, 0x48,     # ldr   r0, [pc, #4]    ; (8 <entrypoint+0x8>)
                0x01, 0x90,     # str   r0, [sp, #4]
                0x01, 0xBD,     # pop   {r0, pc}

                0x00, 0x00, 0x00, 0x00, # Jump address

                0x00, 0x00, 0x00, 0x00, # Align to 16 bytes

                0xFF, 0xFF, 0xFF, 0xFF, # Bootloader info header, 20 bytes
                0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF,

                0xFF, 0xFF, 0xFF, 0xFF, # 12 bytes for future use, align to 16 bytes
                0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF,
                # fmt: on
            ]
        )

        if padding_needed < len(trampoline):
            # Not enough space for the jump trampoline and bootloader info header
            raise ValueError("{}: not enough space to add padding".format(filename))

        # Store an address for the jump trampoline to jump to
        # LSB must be 1 as we are jumping to Thumb code
        trampoline[8:12] = struct.pack("<L", stack_min_addr | 0x1)

        # Add jump trampoline to stack binary, with implicit 0xff padding
        memory.cursor = stack_area_addr
        memory += trampoline

    return memory


def dir_path(string):
    if os.path.isdir(string):
        return string
    else:
        raise NotADirectoryError("{} is not a valid directory".format(string))


def firmware_type(string):
    if string == "wp_bootloader" or string == "wp_stack":
        return string
    else:
        raise ValueError("{} is not a valid firmware type".format(string))


def str2bool(string):
    if string.lower() in ("yes", "true", "t", "y", "1"):
        return True
    elif string.lower() in ("no", "false", "f", "n", "0", ""):
        # '' means False
        return False
    else:
        raise argparse.ArgumentTypeError("Boolean value expected.")


stars_line = """*******************************************************************************"""

warning_unprotected_bootloader_msg = """* WARNING:
 *     The generated image is UNPROTECTED and must not be used
 *     in the field"""

error_no_firmware_found = """* ERROR:
 *     No firmware found fulfilling your requirements.
 *     Please contact the Wirepas support team with the following message
 *     to fix the issue:"""

error_no_firmware_found_india865 = """* ERROR:
 *     No firmware found fulfilling your requirements.
 *     radio_config=india865 is defined. india865 radio configuration
 *     is obsolete and has been replaced with
 *
 *          -india865nw (PHY non-whitening)
 *          -india865w  (PHY whitening)
 *
 *     which are incompatible with each other.
 *"""

error_no_firmware_found_india865_xg13 = """*
 *     EFR32xG13 supports only india865w (PHY whitening).
 *     Please refer to the release specific 'Silabs EFR32 Hardware Reference Manual'
 *     for instructions on choosing the proper radio configuration.
 *     Link to the document can be found from 'references' section
 *     of the release notes."""

error_no_firmware_found_india865_xg23 = """*
 *     EFR32xG23 supports both.
 *     Please refer to the release specific 'Silabs EFR32 Hardware Reference Manual'
 *     for instructions on choosing the proper radio configuration.
 *     Link to the document can be found from 'references' section
 *     of the release notes."""

error_multiple_firmwares_found = """* ERROR:
 *     Multiple firmware found fulfilling your requirements.
 *     In order to avoid ambiguity, please add requirements to your app config.mk
 *     Here is the list of potential differentiators:"""


def print_red(skk):
    print("\033[91m {}\033[00m".format(skk))


def print_unprotected_bootloader():
    print_red(stars_line)
    print_red(warning_unprotected_bootloader_msg)
    print_red(stars_line)


def print_no_firmware_found(target_config):
    print_red(stars_line)
    try:
        if target_config["radio_config"] == "india865":
            print_red(error_no_firmware_found_india865)
            if target_config["mcu_sub"] == "xg13":
                print_red(error_no_firmware_found_india865_xg13)
            if target_config["mcu_sub"] == "xg23":
                print_red(error_no_firmware_found_india865_xg23)
        else:
            print_red(error_no_firmware_found)
            print_red("*        {}".format(target_config))
    except:
        print_red(error_no_firmware_found)
        print_red("*        {}".format(target_config))
    print_red(stars_line)


def print_multiple_firmware_found(target_config, firmwares):
    print_red(stars_line)
    print_red(error_multiple_firmwares_found)
    for fw in firmwares:
        # Print all keys that are not in target_config
        print_red(
            "*        {}: {}".format(
                fw.conf_path, fw.get_unspecified_keys(target_config)
            )
        )
    print_red(stars_line)


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
            "A tool to select correct Wirepas firmware binary", help_width
        ),
    )

    parser.add_argument(
        "--firmware_path",
        "-i",
        help="Colon separated list of folders where the different Wirepas firmware are stored",
        default="./image",
    )
    parser.add_argument(
        "--extract_path",
        "-x",
        help="Path where to extract firmware binaries from archives",
        default=None,
    )
    parser.add_argument(
        "--archive_path",
        "-ar",
        help="Colon separated list of folders for firmware binary archives",
        default=None,
    )
    parser.add_argument(
        "--output_name", help="Name for the firmware binaries", type=str
    )
    parser.add_argument(
        "--output_path", help="Path to copy the firmware binaries", type=dir_path
    )

    parser.add_argument(
        "--firmware_type",
        "-ft",
        type=firmware_type,
        help="Type of firmware to get (wp_bootloader or wp_stack)",
        required=True,
    )
    parser.add_argument("--version", "-v", help="Target minimum version", default=None)
    parser.add_argument("--mcu", "-m", help="Target mcu", required=True)
    parser.add_argument("--mcu_sub", "-ms", help="Target sub_mcu")
    parser.add_argument("--mcu_mem_var", "-mmv", help="Target mcu memory variant")
    parser.add_argument(
        "--radio", "-r", help="Target radio (if relevant)", default=None
    )
    parser.add_argument(
        "--radio_config", "-rc", help="Target radio_config (if relevant)", default=None
    )
    parser.add_argument(
        "--channel_map", "-b", help="Target band (if relevant)", default=None
    )
    parser.add_argument(
        "--mac_profile", "-p", help="Target profile (if relevant)", default=None
    )
    parser.add_argument(
        "--mac_profileid", "-pid", help="Target profile id (if relevant)", default=None
    )
    parser.add_argument(
        "--unlocked",
        "-u",
        help="Unlocked version (only valid if type is wp_bootloader)",
        type=str2bool,
        default=False,
    )
    parser.add_argument(
        "--mode", "-mo", help="Special mode for the binary", default=None
    )
    parser.add_argument(
        "--modem_fw", "-mfw", help="Modem firmware image file name", default=None
    )
    parser.add_argument(
        "--key_type",
        "-kt",
        help="Supported key types in bootloader "
        "(only valid if type is wp_bootloader)",
        default=None,
    )
    parser.add_argument(
        "--stack_area_addr",
        "-sa",
        help="Start address of the stack area, for stack binary padding "
        "(only valid if type is wp_stack)",
        type=lambda x: int(x, 0),
        default=0,
    )

    return parser


def get_target_config(args):

    target_config = {}

    # Mandatory fields
    target_config["type"] = args.firmware_type
    target_config["mcu"] = args.mcu

    # Mandatory fields for "wp_bootloader"
    if args.firmware_type == "wp_bootloader":
        if args.key_type == "OMAC1_AES128CTR":
            auth_type = "cmac"
        elif args.key_type == "ES256_AES128CTR":
            auth_type = "es256"
        else:
            raise RuntimeError("Invalid or missing key_type: {}".format(args.key_type))
        target_config["auth_type"] = auth_type
    else:
        if args.key_type not in (None, ""):
            raise RuntimeError(
                "Argument key_type is for " "--firmware_type=wp_bootloader only"
            )

    # Optional fields
    if args.mcu_sub not in (None, ""):
        target_config["mcu_sub"] = args.mcu_sub
    if args.mcu_mem_var not in (None, ""):
        target_config["mcu_mem_var"] = args.mcu_mem_var
    if args.version not in (None, ""):
        target_config["version"] = args.version
    if args.radio not in (None, ""):
        target_config["radio"] = args.radio
    if args.radio_config not in (None, ""):
        target_config["radio_config"] = args.radio_config
    if args.channel_map not in (None, ""):
        target_config["radio_channel_map"] = args.channel_map
    if args.mac_profile not in (None, ""):
        target_config["mac_profile"] = args.mac_profile
    if args.mac_profileid not in (None, ""):
        target_config["mac_profileid"] = args.mac_profileid
    if args.mode not in (None, ""):
        target_config["mode"] = args.mode
    if args.modem_fw not in (None, ""):
        target_config["modem_fw"] = args.modem_fw

    if args.firmware_type == "wp_bootloader":
        # Set it as a string instead of bool to ease the following check
        target_config["unlocked"] = str(args.unlocked)

    return target_config


def get_path_config(args):

    path_config = {}

    path_config["firmware_path"] = args.firmware_path
    path_config["output_path"] = args.output_path
    path_config["output_name"] = args.output_name
    path_config["archive_path"] = args.archive_path
    path_config["extract_path"] = args.extract_path

    return path_config


def extract_stack_zip(fullpath, mcu_full, extract_path):

    with zipfile.ZipFile(fullpath, "r") as archive:

        for archived_file in archive.infolist():

            #
            # We only extract files needed by the MCU
            #
            if mcu_full in archived_file.filename:

                archive.extract(archived_file, extract_path)


def extract_stack_archives(target_config, path_config):

    if not path_config["archive_path"]:

        if not path_config["extract_path"]:

            return

        else:

            raise RuntimeError("extract_path given but no archive_path")

    if not path_config["extract_path"]:

        raise RuntimeError("archive_path given but no extract_path")

    # Full string of MCU to match correct files in archives
    mcu_full = target_config["mcu"] + target_config["mcu_sub"]

    for directory in path_config["archive_path"].split(":"):

        for file in os.listdir(directory):

            fullpath = directory + "/" + file

            if file.endswith(".zip"):

                extract_stack_zip(fullpath, mcu_full, path_config["extract_path"])


def get_firmware_config_files(firmware_paths):

    conf_files = []

    for path in firmware_paths.split(":"):

        for root, dirs, files in os.walk(path):

            for file in files:

                fullpath = os.path.join(root, file)

                if isfile(fullpath) and file.endswith(".conf"):

                    conf_files.append(fullpath)

    return conf_files


def get_firmware_configs(firmware_paths, target_config):

    firmware_configs = []

    for conf_path in get_firmware_config_files(firmware_paths):

        config = FirmwareConfig.from_file(conf_path)

        if config is None:

            print("{} has wrong format".format(conf_path))
            continue

        if not config.is_compatible(target_config):
            continue

        firmware_configs.append(config)

    return firmware_configs


def select_firmware(path_config, target_config):

    # Get all firmware configurations matching the target_config
    firmware_configs = get_firmware_configs(path_config["firmware_path"], target_config)

    # Only one found, so no need for selection process
    if len(firmware_configs) == 1:
        return firmware_configs[0]

    # Found more than one, select most recent version
    elif len(firmware_configs) > 1:
        print_multiple_firmware_found(target_config, firmware_configs)
        raise RuntimeError("Too many firmware found")

    # No configs found, error
    print_no_firmware_found(target_config)
    raise RuntimeError("No firmware found")


def get_firmware_suffix(firmware_config):

    # Select right deliverable format
    if firmware_config.general["type"] == "wp_stack":
        return ".hex"
    else:
        return ".a"


def get_binary_path(firmware_config):

    firmware_suffix = get_firmware_suffix(firmware_config)

    binary_path = os.path.splitext(firmware_config.conf_path)[0] + firmware_suffix

    if not os.path.isfile(binary_path):

        print(
            "Cannot find the associated binary to conf file {}".format(
                firmware_config.conf_path
            )
        )
        raise ValueError

    return binary_path


def output_stack_binary(firmware_config, path_config, stack_area_addr):

    binary_path = get_binary_path(firmware_config)

    firmware_suffix = get_firmware_suffix(firmware_config)

    # Create a stack firmware binary with padding, as needed
    memory = create_stack_binary(binary_path, stack_area_addr)

    out_file = path_config["output_name"] + firmware_suffix
    out_path = os.path.join(path_config["output_path"], out_file)

    # Write stack firmware binary to output folder
    hextool.save_intel_hex(memory, filename=out_path)

    del memory


def output_bootloader_binary(firmware_config, path_config):

    binary_path = get_binary_path(firmware_config)

    firmware_suffix = get_firmware_suffix(firmware_config)

    out_file = path_config["output_name"] + firmware_suffix
    out_path = os.path.join(path_config["output_path"], out_file)

    # Copy firmware binary to output folder as is
    copyfile(binary_path, out_path)


def verify_modem_archive(archive_fullpath, modem_md5):

    with open(archive_fullpath, "rb") as archive_file:

        archive_content = archive_file.read()
        archive_md5 = hashlib.md5(archive_content).hexdigest()

        if archive_md5 != modem_md5:

            print(
                "MD5 mismatch for {}. Expected: {}, got: {}".format(
                    archive_fullpath, modem_md5, archive_md5
                )
            )

            return False

        return True


def extract_modem_archive(path_config, firmware_config):

    modem_fw_archive = firmware_config.general["modem_release_zip"]
    modem_md5 = firmware_config.general["modem_firmware_md5"]

    print("Look for {} in {}".format(modem_fw_archive, path_config["archive_path"]))

    for directory in path_config["archive_path"].split(":"):

        for file in os.listdir(directory):

            if file == modem_fw_archive:

                archive_fullpath = join(directory, file)

                print("Found modem fw archive {}".format(archive_fullpath))

                if verify_modem_archive(archive_fullpath, modem_md5) == False:

                    continue

                with zipfile.ZipFile(archive_fullpath, "r") as archive:

                    archive.extractall(path_config["extract_path"])

                    return

    raise ValueError("No valid modem archive found for {}".format(modem_fw_archive))


def output_modem_binary(firmware_config, target_config, path_config):

    if (
        "modem_fw" not in target_config
        or target_config["modem_fw"] is None
        or len(target_config["modem_fw"]) == 0
        or target_config["radio"] == "none"
    ):
        return

    modem_fw = target_config["modem_fw"]

    if "modem_release_zip" in firmware_config.general:
        extract_modem_archive(path_config, firmware_config)

    binary_path = os.path.split(firmware_config.conf_path)[0]
    modem_fw_file = os.path.join(binary_path, modem_fw)
    if exists(modem_fw_file):

        target_path = os.path.join(path_config["output_path"], modem_fw)

        copyfile(modem_fw_file, target_path)

    else:

        print("Unable to find modem firmware {}".format(modem_fw))

        print("Available modem firmwares:")

        if "modem_dfu_full_image" in firmware_config.general:

            print("- {}".format(firmware_config.general["modem_dfu_full_image"]))

        if "modem_dfu_delta_images" in firmware_config.general:

            delta_images = firmware_config.general["modem_dfu_delta_images"].strip(
                " []"
            )

            for image in delta_images.split(","):

                print("- {}".format(image.strip()))

        raise ValueError("Error: Modem firmware not found")


def output_firmware(firmware_config, target_config, path_config, stack_area_addr):

    # No output path, no write
    if path_config["output_path"] in (None, ""):
        return

    conf_out_file = path_config["output_name"] + ".conf"
    conf_out_path = os.path.join(path_config["output_path"], conf_out_file)

    # Copy conf file to output folder
    copyfile(firmware_config.conf_path, conf_out_path)

    if firmware_config.general["type"] == "wp_stack":

        output_stack_binary(firmware_config, path_config, stack_area_addr)

        output_modem_binary(firmware_config, target_config, path_config)

    else:

        output_bootloader_binary(firmware_config, path_config)


def main():
    """Main program"""

    # Determine program name, for error messages.
    pgmname = os.path.split(sys.argv[0])[-1]

    try:
        # Parse command line arguments
        args = create_argument_parser(pgmname).parse_args()

        # Do some sanity check on parameters and impossible combinations
        if args.unlocked and args.firmware_type == "wp_stack":
            raise ValueError("wp_stack cannot be unlocked only apply to wp_bootloader")

        #
        # Create a target config dictionary based on parameters
        #
        # Used to find a correct firmware
        #
        target_config = get_target_config(args)

        #
        # Create a config of input and output paths to be able to find
        # correct files and put them to proper places
        #
        path_config = get_path_config(args)

        # Firmware might be in archives, so handle them first
        extract_stack_archives(target_config, path_config)

        # Select firmware by given parameters
        firmware_config = select_firmware(path_config, target_config)

        print(
            "Firmware config for {}: {}".format(
                firmware_config.general["type"], firmware_config.conf_path
            )
        )

        # Write firmware to physical location
        output_firmware(
            firmware_config, target_config, path_config, args.stack_area_addr
        )

        # If the bootloader is unlocked, print a message about it
        if args.unlocked:
            print_unprotected_bootloader()

        # Check Libraries configurations
        # Once firmware is selected, Lib version could be checked to
        # be sure app doesn't need higher versions

    except (ValueError, IOError, OSError, RuntimeError) as exc:
        sys.stdout.write("%s: %s\n" % (pgmname, exc))
        return 1

    return 0


# Run main.
if __name__ == "__main__":
    sys.exit(main())
