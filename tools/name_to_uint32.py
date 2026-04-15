#!/usr/bin/env python3

# name_to_uint32.py - A tool to convert a six-character name to a 32-bit integer
#                     and back, using the DEC RADIX 50 character set with an IBM
#                     SQUOZE character encoding scheme
#
# See:
# https://en.wikipedia.org/wiki/DEC_Radix-50
# http://hackaday.com/2016/11/22/squoze-your-data/

import sys
import os
import math
import shutil
import textwrap
import argparse


class Squoze:
    chars = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789"  # From DEC Radix-50
    num_bits = 32
    weight_to_char = dict(enumerate(chars))
    char_to_weight = dict(zip(chars, range(len(chars))))
    max_length = int(math.log(2**num_bits - 1) / math.log(len(chars)))

    @classmethod
    def squoze(cls, name):
        """Pack a string to a 32-bit number using the DEC Radix-50 character set"""

        if len(name) > cls.max_length:
            raise ValueError(f"'{name}': string too long")

        # Only use upper case characters
        name = name.upper()

        if name[0] == cls.chars[0]:
            # First character cannot be the character of weight of 0
            raise ValueError(f"'{name}': ambiguous first character")

        value = 0

        # Convert characters to weights
        for c in name:
            value *= len(cls.chars)
            try:
                value += cls.char_to_weight[c]
            except KeyError:
                # No weight for character found
                raise ValueError(f"'{c}': invalid character")

        # Sanity check
        if value >= (2**cls.num_bits):
            raise ValueError(f"assert failed: {value} < {2**cls.num_bits}")

        return value

    @classmethod
    def unsquoze(cls, value):
        """Unpack a packed number using the DEC Radix-50 character set to a string"""

        if value >= (2**cls.num_bits):
            raise ValueError(f"{value}: invalid value")

        name = []

        # Convert weights to characters
        while value > 0:
            name.append(cls.weight_to_char[value % len(cls.chars)])
            value //= len(cls.chars)

        name.reverse()

        return "".join(name)


def create_argument_parser(pgmname):
    """Create a parser for parsing the command line"""

    # Determine help text width
    help_width = shutil.get_terminal_size(fallback=(80, 24)).columns - 2

    parser = argparse.ArgumentParser(
        prog=pgmname,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=textwrap.fill(
            "A tool to convert a six-character name to a 32-bit integer "
            "and back, using the DEC RADIX 50 character set with an IBM "
            "SQUOZE character encoding scheme",
            help_width,
        ),
        epilog=textwrap.fill("available characters:") + '\n  "' + Squoze.chars + '"',
    )

    parser.add_argument(
        "--unsquoze",
        "-u",
        action="store_true",
        help="convert values to names",
    )

    parser.add_argument(
        "--decimal",
        "-d",
        action="store_true",
        help="print values in decimal instead of hexadecimal",
    )

    parser.add_argument(
        "param",
        nargs="+",
        metavar="PARAM",
        help="name or value to convert",
    )

    return parser


def main():
    """Main program"""

    # Determine program name, for error messages
    pgmname = os.path.split(sys.argv[0])[-1]

    try:
        # Parse command line arguments
        args = create_argument_parser(pgmname).parse_args()

        # Only print values if there are more than one parameter
        one_param = len(args.param) == 1

        # Show value in decimal or hexadecimal
        value_fmt = args.decimal and "{:d}" or "0x{:08x}"

        # Perform command
        for param in args.param:
            if args.unsquoze:
                # Unsquoze: uint32 to text
                try:
                    value = int(param, 0)
                except ValueError:
                    raise ValueError(f"'{param}': invalid value")

                name = Squoze.unsquoze(value)
                value_str = value_fmt.format(value)

                print(one_param and f"{name}" or f"{value_str}: {name}")
            else:
                # Squoze: text to uint32
                name = param.upper()
                value = Squoze.squoze(name)
                value_str = value_fmt.format(value)

                print(one_param and f"{value_str}" or f"{name}: {value_str}")
    except (ValueError, IOError, OSError) as exc:
        sys.stderr.write("%s: %s\n" % (pgmname, exc))
        return 1


# Run main
if __name__ == "__main__":
    sys.exit(main())
