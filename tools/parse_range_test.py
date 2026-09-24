from enum import Enum
from pathlib import Path
from typing import Final, Optional
import argparse
import logging
import math
import re
import os
import statistics
import textwrap


logging.basicConfig(format="%(levelname)s | %(message)s", level=logging.INFO)
_LOGGER: Final = logging.getLogger()


class ERROR_CODES(Enum):
    FILE_NOT_FOUND = 1
    NO_DATA = 2


class Data:
    """Parse and store data from range test output, line by line"""

    def __init__(self, line: str) -> None:
        # Sanitize data
        line = re.sub("\n|\t| |", "", line)
        _, self.address, self.path_loss, self.tx_power, self.is_connected, self.is_next_hop = (
            line.split("|")
        )

    @property
    def address(self) -> int:
        return self._address

    @address.setter
    def address(self, value: int | str):
        self._address = int(value)

    @property
    def is_connected(self) -> bool:
        return self._is_connected

    @is_connected.setter
    def is_connected(self, value:bool | int | float):
        self._is_connected = True if int(value) == 1 else False

    @property
    def is_next_hop(self) -> bool:
        return self._is_next_hop

    @is_next_hop.setter
    def is_next_hop(self, value: bool | int | float):
        self._is_next_hop = True if int(value) == 1 else False

    @property
    def path_loss(self) -> float:
        return self._path_loss

    @path_loss.setter
    def path_loss(self, value: float | int | str):
        self._path_loss = float(value)

    @property
    def tx_power(self) -> float:
        return self._tx_power

    @tx_power.setter
    def tx_power(self, value: float | int | str):
        self._tx_power = float(value)

    def to_csv(self):
        return f"{self.address};{self.path_loss};{self.tx_power};{self.is_connected};{self.is_next_hop}"


def linear_to_db(value: int | float) -> float:
    """Convert data from linear domain to Decibel"""
    return 10 * math.log10(value)


def pad(value: int | float, decimals: Optional[int] = None):
    """Right align data for cleaner output"""
    if decimals is not None and type(value) is float:
        value = round(value, decimals)

    return str(value).rjust(6)


if __name__ == "__main__":

    # Determine help text width.
    try:
        help_width: Final = int(os.environ["COLUMNS"]) - 2
    except (KeyError, ValueError):
        help_width: Final = 78

    parser = argparse.ArgumentParser(
        prog="Range test data analytics",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=textwrap.fill(
            "Tool to parse UART output from 5G range test and generate "
            "a CSV file, it also displays basic statistics",
            help_width,
        ),
    )

    parser.add_argument(
        "-f",
        "--filename",
        metavar="FILENAME",
        help="Input file to parse",
        required=True,
    )
    parser.add_argument(
        "-g",
        "--gateway_address",
        metavar="GATEWAY_ADDRESS",
        help="Gateway address (decimal)",
        required=True,
    )
    args = parser.parse_args()

    input_file: Final = Path(args.filename)

    try:
        input_file.stat()
    except FileNotFoundError:
        _LOGGER.critical("Input file not found")
        exit(ERROR_CODES.FILE_NOT_FOUND.value)

    counter = 0
    datas: list[Data] = []
    output_file: Final = input_file.parent.joinpath(f"{input_file.stem}.csv")
    path_losses = []

    # Parse the serial output file
    with open(input_file, "r") as file:
        for line in file.readlines():
            if line.startswith("[PRINT_DATA]") and "Address" not in line:
                datas.append(Data(line))

    # Write the CSV file
    with open(output_file, "w") as file:
        file.write("Address;Path loss;Tx Power;Is connected?;Is next hop?\n")
        for data in datas:
            file.write(f"{data.to_csv()}\n")

    # Filter data and compute mean in linear domain
    path_losses_db = [
        data.path_loss for data in datas if data.address == int(args.gateway_address)
    ]
    path_losses_linear = [
        10 ** (data.path_loss / 10)
        for data in datas
        if data.address == int(args.gateway_address)
    ]

    if len(path_losses_linear) == 0:
        addresses: Final = set([data.address for data in datas])
        _LOGGER.warning(
            f'No data found for gateway address "{args.gateway_address}". Found nodes:'
        )
        for address in addresses:
            _LOGGER.warning(f"\t- {address}")
        exit(ERROR_CODES.NO_DATA.value)

    _LOGGER.info(f"Parsed {len(path_losses_db)} samples:")
    _LOGGER.info(
        f"\t- Average path loss:\t{pad(linear_to_db(statistics.mean(path_losses_linear)),2)} dBm"
    )
    _LOGGER.info(f"\t- Min:\t\t\t{pad(min(path_losses_db))} dBm")
    _LOGGER.info(f"\t- Max:\t\t\t{pad(max(path_losses_db))} dBm")
    _LOGGER.info(f"\t- Standard deviation:\t{pad(statistics.stdev(path_losses_db),2)} dBm")
    _LOGGER.info(f'CSV file exported to "{output_file}"')