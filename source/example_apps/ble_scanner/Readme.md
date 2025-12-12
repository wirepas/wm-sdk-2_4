# BLE Scanner Application

## Application Scope

This application demonstrates how to receive Bluetooth Beacons, filter them, and send the filtered data to a backend.

## Application Architecture

The "BLE Scanner App" example configures and starts a node.

This application initializes the stack and waits for a message from the backend to begin listening for Beacons.
It also initializes the BLE callbacks to filter and process received Bluetooth Beacons.

**Please note that without filtering, a node may have to process more than 100 beacons per second.** 

The app starts without Beacon reception. 
It must be activated by sending a configuration message from endpoint 13 to endpoint 13.

Commands:

- **SCANNER_CMD_DISABLE**: Stop Bluetooth Beacon reception.
- **SCANNER_CMD_ENABLE**: Start Bluetooth Beacon reception.
- **SCANNER_CMD_ENABLE_PERIODIC**: Start Bluetooth Beacon reception for a specified duration on a time basis.
                                   period (How often in seconds to scan) and scan_len (How long to scan for beacons)
                                   have to be passed as parameters.
                                   By default period is 360s and scan_len is 60s: 
                                       node will listen to Bluetooth beacon for 60s every 360s.

### File List

- `app.c`: Configures the node and the `ble_scanner` library.
- `ble_scanner.c` and `ble_scanner.h`: BLE scanner library.
- `backendscripts/beacon_scanner_config.py`: Enables/disables the Bluetooth scanner feature.
- `backendscripts/beacon_scanner_rx.py`: Displays incoming Bluetooth Beacons.

Once activated, any incoming Beacons will be:
- Filtered by `ble_scanner_filter(...)` in `app.c`
- Processed by the `ble_scanner.c` module
- Handled by `on_beacon_received(...)` in `app.c` with the incoming packet.

## Customization

By default, the app is configured to scan for an Enocean Switch with source
address '0x4C, 0x2E, 0x00, 0x00, 0x15'. This address is stored in
`pattern_enocean` in `ble_scanner_filter(...)` in `app.c`.
It must be adapted to the enocean switch used.

## Testing

A minimal network setup is required, consisting of 
- at least one node running this code 
- one sink (flashed with the dual MCU application) connected to a gateway with access to a WNT backend.

Once the network is fully working:
- Bluetooth Beacon scanning can be activated using `beacon_scanner_config.py`.
- Incoming Bluetooth Beacons can be displayed using `beacon_scanner_rx.py`.

### Script Command Line Format

```
python <script>.py 
    -s <MQTT broker host address> 
    -p <MQTT broker port> 
    -u <username (optional)> 
    -pw <password (optional)> 
    -fu <disable SSL/TLS for an unsecure connection to the MQTT broker (optional)>
    -node <node address>
```

For `beacon_scanner_config.py`, the command syntax is:
```
cmd <disable, enable, periodic>
```

*Note: Full command line interface details can be obtained with `python <script>.py -h`.*
